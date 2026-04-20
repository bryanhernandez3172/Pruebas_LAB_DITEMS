/**
 * @file    BatteryMonitor.c
 * @brief   Driver for BQ27441-G1 fuel gauge (I2C) + analog battery level (ADC).
 *
 * @date    April 15, 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#include "BatteryMonitor.h"
#include <stdbool.h>

/* ========================  EXTERN HAL HANDLES  ============================= */

extern I2C_HandleTypeDef hi2c1;
extern ADC_HandleTypeDef hadc1;

/* ========================  INTERNAL HELPERS  ================================ */

/**
 * @brief  Reads a 16-bit value from a standard command register (little-endian).
 */
static HAL_StatusTypeDef read16(uint8_t reg, uint16_t *value)
{
    uint8_t buf[2];
    HAL_StatusTypeDef st;

    st = HAL_I2C_Mem_Read(BQ27441_I2C, BQ27441_ADDR, reg,
                          I2C_MEMADD_SIZE_8BIT, buf, 2U, BQ27441_TIMEOUT_MS);
    if (st == HAL_OK) {
        *value = (uint16_t)(buf[0] | (buf[1] << 8));
    }
    return st;
}

/**
 * @brief  Writes a 16-bit value to a register (little-endian).
 */
static HAL_StatusTypeDef write16(uint8_t reg, uint16_t value)
{
    uint8_t buf[2];
    buf[0] = (uint8_t)(value & 0xFF);
    buf[1] = (uint8_t)(value >> 8);

    return HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, buf, 2U, BQ27441_TIMEOUT_MS);
}

/* ========================  DATA MEMORY HELPERS  ============================ */

/**
 * @brief  Writes a single byte to a register.
 */
static HAL_StatusTypeDef write8(uint8_t reg, uint8_t value)
{
    return HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR, reg,
                             I2C_MEMADD_SIZE_8BIT, &value, 1U, BQ27441_TIMEOUT_MS);
}

/**
 * @brief  Reads the full 32-byte data block into buf.
 */
static HAL_StatusTypeDef readBlock(uint8_t *buf)
{
    return HAL_I2C_Mem_Read(BQ27441_I2C, BQ27441_ADDR, BQ27441_EXT_BLOCK_DATA,
                            I2C_MEMADD_SIZE_8BIT, buf, 32U, BQ27441_TIMEOUT_MS);
}

/**
 * @brief  Computes the BQ27441 block checksum: 255 - (sum of 32 bytes).
 */
static uint8_t blockChecksum(const uint8_t *buf)
{
    uint8_t sum = 0U;
    for (uint8_t i = 0U; i < 32U; i++) {
        sum += buf[i];
    }
    return (uint8_t)(255U - sum);
}

/**
 * @brief  Waits for a specific flag bit to reach a target state.
 */
static HAL_StatusTypeDef waitFlag(uint16_t mask, bool target, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    uint16_t flags;

    while ((HAL_GetTick() - start) < timeout_ms) {
        if (read16(BQ27441_REG_FLAGS, &flags) != HAL_OK) {
            return HAL_ERROR;
        }
        bool state = (flags & mask) != 0U;
        if (state == target) {
            return HAL_OK;
        }
        HAL_Delay(10);
    }
    return HAL_TIMEOUT;
}

/* ========================  BQ27441 API  ==================================== */

HAL_StatusTypeDef BatGauge_Init(void)
{
    uint16_t dev_type = 0U;
    HAL_StatusTypeDef st;

    st = BatGauge_Control(BQ27441_CTRL_DEVICE_TYPE, &dev_type);
    if (st != HAL_OK) {
        return st;
    }

    if (dev_type != BQ27441_DEVICE_TYPE_ID) {
        return HAL_ERROR;
    }

    return HAL_OK;
}

HAL_StatusTypeDef BatGauge_ReadAll(BatGauge_Data_t *data)
{
    HAL_StatusTypeDef st;
    uint16_t raw;

    st = read16(BQ27441_REG_VOLTAGE, &data->voltage_mV);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_AVG_CURR, &raw);
    if (st != HAL_OK) return st;
    data->avg_current_mA = (int16_t)raw;

    st = read16(BQ27441_REG_SOC, &data->soc_pct);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_RM, &data->remaining_mAh);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_FCC, &data->full_cap_mAh);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_SOH, &data->soh_pct);
    if (st != HAL_OK) return st;
    data->soh_pct &= 0x00FFU;  /* Only low byte is SOH % */

    st = read16(BQ27441_REG_TEMP, &raw);
    if (st != HAL_OK) return st;
    /* BQ27441 reports temp in 0.1 K, convert to 0.1 °C */
    data->temp_c10 = (int16_t)(raw - 2731);

    st = read16(BQ27441_REG_FLAGS, &data->flags);
    return st;
}

HAL_StatusTypeDef BatGauge_ReadReg(uint8_t reg, uint16_t *value)
{
    return read16(reg, value);
}

HAL_StatusTypeDef BatGauge_Control(uint16_t subcmd, uint16_t *value)
{
    HAL_StatusTypeDef st;

    /* Write sub-command to control register */
    st = write16(BQ27441_REG_CONTROL, subcmd);
    if (st != HAL_OK) {
        return st;
    }

    /* Read response from control register */
    if (value != NULL) {
        HAL_Delay(5);  /* Small delay for sub-command processing */
        st = read16(BQ27441_REG_CONTROL, value);
    }

    return st;
}

HAL_StatusTypeDef BatGauge_SoftReset(void)
{
    return write16(BQ27441_REG_CONTROL, BQ27441_CTRL_SOFT_RESET);
}

HAL_StatusTypeDef BatGauge_Configure(void)
{
    HAL_StatusTypeDef st;
    uint8_t block[32];

    /* 1. Unseal the device */
    st = write16(BQ27441_REG_CONTROL, BQ27441_UNSEAL_KEY_A);
    if (st != HAL_OK) return st;
    st = write16(BQ27441_REG_CONTROL, BQ27441_UNSEAL_KEY_B);
    if (st != HAL_OK) return st;

    /* 2. Enter CFGUPDATE mode */
    st = write16(BQ27441_REG_CONTROL, BQ27441_CTRL_SET_CFGUPDATE);
    if (st != HAL_OK) return st;

    st = waitFlag(BQ27441_FLAG_CFGUPMODE, true, 1000U);
    if (st != HAL_OK) return st;

    /* 3. Enable block data access */
    st = write8(BQ27441_EXT_BLOCK_CTRL, 0x00U);
    if (st != HAL_OK) return st;

    /* 4. Access State subclass (82), block 0 */
    st = write8(BQ27441_EXT_DATA_CLASS, BQ27441_CLASS_STATE);
    if (st != HAL_OK) return st;
    st = write8(BQ27441_EXT_DATA_BLOCK, 0x00U);
    if (st != HAL_OK) return st;
    HAL_Delay(5);

    /* 5. Read old checksum */
    uint8_t old_cksum;
    st = HAL_I2C_Mem_Read(BQ27441_I2C, BQ27441_ADDR, BQ27441_EXT_CHECKSUM,
                          I2C_MEMADD_SIZE_8BIT, &old_cksum, 1U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) return st;

    /* 6. Read current block data */
    st = readBlock(block);
    if (st != HAL_OK) return st;

    /* Save old bytes for checksum update */
    uint8_t old_dc_msb = block[BQ27441_STATE_DESIGN_CAP];
    uint8_t old_dc_lsb = block[BQ27441_STATE_DESIGN_CAP + 1];
    uint8_t old_de_msb = block[BQ27441_STATE_DESIGN_ENE];
    uint8_t old_de_lsb = block[BQ27441_STATE_DESIGN_ENE + 1];
    uint8_t old_tv_msb = block[BQ27441_STATE_TERM_VOLT];
    uint8_t old_tv_lsb = block[BQ27441_STATE_TERM_VOLT + 1];
    uint8_t old_tr_msb = block[BQ27441_STATE_TAPER_RATE];
    uint8_t old_tr_lsb = block[BQ27441_STATE_TAPER_RATE + 1];

    /* 7. Modify parameters (big-endian in data memory) */
    block[BQ27441_STATE_DESIGN_CAP]     = (uint8_t)(BAT_DESIGN_CAP_MAH >> 8);
    block[BQ27441_STATE_DESIGN_CAP + 1] = (uint8_t)(BAT_DESIGN_CAP_MAH & 0xFF);

    block[BQ27441_STATE_DESIGN_ENE]     = (uint8_t)(BAT_DESIGN_ENERGY_MWH >> 8);
    block[BQ27441_STATE_DESIGN_ENE + 1] = (uint8_t)(BAT_DESIGN_ENERGY_MWH & 0xFF);

    block[BQ27441_STATE_TERM_VOLT]      = (uint8_t)(BAT_TERMINATE_MV >> 8);
    block[BQ27441_STATE_TERM_VOLT + 1]  = (uint8_t)(BAT_TERMINATE_MV & 0xFF);

    block[BQ27441_STATE_TAPER_RATE]     = (uint8_t)(BAT_TAPER_RATE >> 8);
    block[BQ27441_STATE_TAPER_RATE + 1] = (uint8_t)(BAT_TAPER_RATE & 0xFF);

    /* 8. Write only the changed bytes individually */
    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_DESIGN_CAP,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_DESIGN_CAP], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) return st;

    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_DESIGN_ENE,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_DESIGN_ENE], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) return st;

    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_TERM_VOLT,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_TERM_VOLT], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) return st;

    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_TAPER_RATE,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_TAPER_RATE], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) return st;

    /* 9. Compute new checksum using TI's method:
     *    new_cksum = 255 - ((255 - old_cksum - old_bytes + new_bytes) % 256) */
    uint8_t temp = (255U - old_cksum
                    - old_dc_msb - old_dc_lsb
                    - old_de_msb - old_de_lsb
                    - old_tv_msb - old_tv_lsb
                    - old_tr_msb - old_tr_lsb
                    + block[BQ27441_STATE_DESIGN_CAP]
                    + block[BQ27441_STATE_DESIGN_CAP + 1]
                    + block[BQ27441_STATE_DESIGN_ENE]
                    + block[BQ27441_STATE_DESIGN_ENE + 1]
                    + block[BQ27441_STATE_TERM_VOLT]
                    + block[BQ27441_STATE_TERM_VOLT + 1]
                    + block[BQ27441_STATE_TAPER_RATE]
                    + block[BQ27441_STATE_TAPER_RATE + 1]);
    uint8_t new_cksum = 255U - temp;
    st = write8(BQ27441_EXT_CHECKSUM, new_cksum);
    if (st != HAL_OK) return st;
    HAL_Delay(100);  /* Give NVM time to write */

    /* 9. Exit CFGUPDATE — soft reset */
    st = BatGauge_SoftReset();
    if (st != HAL_OK) return st;

    st = waitFlag(BQ27441_FLAG_CFGUPMODE, false, 1000U);
    if (st != HAL_OK) return st;

    /* 10. Seal the device */
    st = write16(BQ27441_REG_CONTROL, BQ27441_CTRL_SEALED);

    return st;
}

/* ========================  ADC POTENTIOMETER  ============================== */

uint16_t BatAdc_ReadRaw(void)
{
    ADC_ChannelConfTypeDef conf = {0};
    conf.Channel = BAT_ADC_CHANNEL;
    conf.Rank    = 1U;
    conf.SamplingTime = ADC_SAMPLETIME_79CYCLES_5;

    if (HAL_ADC_ConfigChannel(BAT_ADC_HANDLE, &conf) != HAL_OK) {
        return 0U;
    }

    HAL_ADC_Start(BAT_ADC_HANDLE);

    if (HAL_ADC_PollForConversion(BAT_ADC_HANDLE, BAT_ADC_TIMEOUT_MS) != HAL_OK) {
        HAL_ADC_Stop(BAT_ADC_HANDLE);
        return 0U;
    }

    uint16_t raw = (uint16_t)HAL_ADC_GetValue(BAT_ADC_HANDLE);
    HAL_ADC_Stop(BAT_ADC_HANDLE);
    return raw;
}

uint32_t BatAdc_ReadVoltage_mV(void)
{
    uint16_t raw = BatAdc_ReadRaw();
    if (raw == 0U) {
        return 0U;
    }

    /* V_bat = (raw / 4095) * VREF * (DIV_NUM / DIV_DEN) */
    uint32_t mv = ((uint32_t)raw * BAT_VREF_MV * BAT_DIV_NUM) /
                  ((uint32_t)BAT_ADC_MAX * BAT_DIV_DEN);
    return mv;
}
