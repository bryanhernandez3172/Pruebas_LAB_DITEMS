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

/* ========================  DEBUG INSTRUMENTATION  ========================== */

/**
 * @brief  Tracks the last step reached inside BatGauge_Configure().
 *         Watch it in the debugger to locate where CFGUPDATE fails.
 *
 *   0  = not started
 *   1  = unseal key A sent
 *   2  = unseal key B sent
 *   3  = SET_CFGUPDATE sent
 *   4  = CFGUPMODE flag became 1
 *   5  = BlockDataControl enabled
 *   6  = DataClass + DataBlock selected
 *   7  = old checksum read
 *   8  = 32-byte block read
 *   9  = new bytes written
 *   10 = new checksum written (State class done)
 *   11 = Registers subclass selected for OpConfig edit
 *   12 = OpConfig read from block
 *   13 = OpConfig written with BIE=0 (or skipped if already 0)
 *   14 = soft reset sent
 *   15 = CFGUPMODE flag became 0
 *   16 = IT_ENABLE subcommand sent
 *   17 = ITPOR flag cleared (IT algorithm running)
 *   18 = SEALED command sent (success)
 */
volatile uint8_t bat_cfg_step = 0U;
volatile HAL_StatusTypeDef bat_cfg_err = HAL_OK;

/** @brief  Flags register read right after IT_ENABLE completes.
 *          If bit 5 (ITPOR) is 0, the Impedance Track algorithm has
 *          started successfully. */
volatile uint16_t bat_cfg_flags_after = 0U;

/** @brief  OpConfig read during Init. If bit 13 (BIE) is 1, the gauge uses
 *          the BI pin for battery detection. If your BI pin is floating,
 *          BAT_DET stays 0 and SOC/SOH/RM/FCC never leave 0. In that case
 *          Configure() will clear BIE so BAT_INSERT can be used. */
volatile uint16_t bat_opconfig_dbg = 0U;

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
    uint16_t flags    = 0U;
    HAL_StatusTypeDef st;

    /* 1. Verify device presence and identity */
    st = BatGauge_Control(BQ27441_CTRL_DEVICE_TYPE, &dev_type);
    if (st != HAL_OK) return st;

    if (dev_type != BQ27441_DEVICE_TYPE_ID) {
        return HAL_ERROR;
    }

    /* 2. Read OpConfig for diagnostics (BIE bit tells us whether BAT_DET
     *    is controlled by the BI pin or by host subcommands). */
    (void)read16(BQ27441_REG_OPCONFIG, (uint16_t *)&bat_opconfig_dbg);

    /* 3. Check ITPOR — if set, the Impedance Track algorithm has not
     *    been initialized. Auto-run Configure so SOC/SOH/RM/FCC start
     *    producing valid values. Configure also clears BIE so that the
     *    BAT_INSERT subcommand below can assert BAT_DET. */
    st = read16(BQ27441_REG_FLAGS, &flags);
    if (st != HAL_OK) return st;

    if ((flags & BQ27441_FLAG_ITPOR) != 0U) {
        st = BatGauge_Configure();
        if (st != HAL_OK) return st;
    }

    /* 4. If BAT_DET is still 0, the chip does not see a battery connected.
     *    Force it high via the BAT_INSERT subcommand. This only works when
     *    OpConfig.BIE = 0 — Configure() takes care of clearing BIE. */
    st = read16(BQ27441_REG_FLAGS, &flags);
    if (st != HAL_OK) return st;

    if ((flags & BQ27441_FLAG_BAT_DET) == 0U) {
        (void)BatGauge_Control(BQ27441_CTRL_BAT_INSERT, NULL);
    }

    return HAL_OK;
}

/**
 * @brief  Computes ETA minutes based on current direction.
 *         TTE when discharging, TTF when charging, INVALID otherwise.
 */
static uint16_t compute_eta(uint16_t rm, uint16_t fcc, int16_t current_mA)
{
    int32_t abs_i = (current_mA < 0) ? -(int32_t)current_mA : (int32_t)current_mA;

    if (abs_i < (int32_t)BAT_IDLE_CURRENT_MA) {
        return BAT_ETA_INVALID;
    }

    if (current_mA < 0) {
        /* Discharging → Time to Empty */
        return (uint16_t)(((uint32_t)rm * 60U) / (uint32_t)abs_i);
    }

    /* Charging → Time to Full */
    if (rm >= fcc) return 0U;
    return (uint16_t)(((uint32_t)(fcc - rm) * 60U) / (uint32_t)abs_i);
}

HAL_StatusTypeDef BatGauge_ReadAll(BatGauge_Data_t *data)
{
    HAL_StatusTypeDef st;
    uint16_t raw;

    data->ready   = false;
    data->full    = false;
    data->state   = BAT_STATE_IDLE;
    data->eta_min = BAT_ETA_INVALID;

    st = read16(BQ27441_REG_VOLTAGE, &data->voltage_mV);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_AVG_CURR, &raw);
    if (st != HAL_OK) return st;
    data->current_mA = (int16_t)raw;

    st = read16(BQ27441_REG_AVG_PWR, &raw);
    if (st != HAL_OK) return st;
    data->power_mW = (int16_t)raw;

    st = read16(BQ27441_REG_SOC, &data->soc_pct);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_RM, &data->rm_mAh);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_FCC, &data->fcc_mAh);
    if (st != HAL_OK) return st;

    st = read16(BQ27441_REG_SOH, &data->soh_pct);
    if (st != HAL_OK) return st;
    data->soh_pct &= 0x00FFU;  /* Only low byte is SOH % */

    st = read16(BQ27441_REG_TEMP, &raw);
    if (st != HAL_OK) return st;
    /* BQ27441 reports temp in 0.1 K, convert to 0.1 °C */
    data->temp_c10 = (int16_t)(raw - 2731);

    st = read16(BQ27441_REG_FLAGS, &data->flags);
    if (st != HAL_OK) return st;

    /* ---- Derive state ---- */
    int16_t abs_i = (data->current_mA < 0) ? -data->current_mA : data->current_mA;
    if (abs_i < (int16_t)BAT_IDLE_CURRENT_MA) {
        data->state = BAT_STATE_IDLE;
    } else if (data->current_mA > 0) {
        data->state = BAT_STATE_CHARGING;
    } else {
        data->state = BAT_STATE_DISCHARGING;
    }

    /* ---- Full flag ----
     * Physically impossible to be full while clearly discharging, and the
     * FC bit is meaningless while ITPOR=1 (IT algorithm not initialised),
     * so only trust it when those conditions are false.
     */
    bool fc_trusted = ((data->flags & BQ27441_FLAG_FC) != 0U) &&
                      ((data->flags & BQ27441_FLAG_ITPOR) == 0U) &&
                      (data->state != BAT_STATE_DISCHARGING);
    data->full = (data->voltage_mV >= BAT_FULL_VOLTAGE_MV) || fc_trusted;

    /* ---- Ready: voltage sane + BAT_DET set + IT initialised ---- */
    data->ready = (data->voltage_mV >= BAT_MIN_VOLTAGE_MV) &&
                  (data->voltage_mV <= BAT_MAX_VOLTAGE_MV) &&
                  ((data->flags & BQ27441_FLAG_BAT_DET) != 0U) &&
                  ((data->flags & BQ27441_FLAG_ITPOR)   == 0U);

    /* ---- ETA ---- */
    if (data->full || data->state == BAT_STATE_IDLE) {
        data->eta_min = BAT_ETA_INVALID;
    } else {
        data->eta_min = compute_eta(data->rm_mAh, data->fcc_mAh, data->current_mA);
    }

    return HAL_OK;
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

    bat_cfg_step = 0U;
    bat_cfg_err  = HAL_OK;

    /* 1. Unseal the device */
    st = write16(BQ27441_REG_CONTROL, BQ27441_UNSEAL_KEY_A);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 1U;

    st = write16(BQ27441_REG_CONTROL, BQ27441_UNSEAL_KEY_B);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 2U;

    /* 2. Enter CFGUPDATE mode */
    st = write16(BQ27441_REG_CONTROL, BQ27441_CTRL_SET_CFGUPDATE);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 3U;

    st = waitFlag(BQ27441_FLAG_CFGUPMODE, true, 1000U);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 4U;

    /* 3. Enable block data access */
    st = write8(BQ27441_EXT_BLOCK_CTRL, 0x00U);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 5U;

    /* 4. Access State subclass (82), block 0 */
    st = write8(BQ27441_EXT_DATA_CLASS, BQ27441_CLASS_STATE);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    st = write8(BQ27441_EXT_DATA_BLOCK, 0x00U);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    HAL_Delay(5);
    bat_cfg_step = 6U;

    /* 5. Read old checksum */
    uint8_t old_cksum;
    st = HAL_I2C_Mem_Read(BQ27441_I2C, BQ27441_ADDR, BQ27441_EXT_CHECKSUM,
                          I2C_MEMADD_SIZE_8BIT, &old_cksum, 1U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 7U;

    /* 6. Read current block data */
    st = readBlock(block);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 8U;

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
    if (st != HAL_OK) { bat_cfg_err = st; return st; }

    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_DESIGN_ENE,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_DESIGN_ENE], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }

    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_TERM_VOLT,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_TERM_VOLT], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }

    st = HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR,
                           BQ27441_EXT_BLOCK_DATA + BQ27441_STATE_TAPER_RATE,
                           I2C_MEMADD_SIZE_8BIT,
                           &block[BQ27441_STATE_TAPER_RATE], 2U, BQ27441_TIMEOUT_MS);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 9U;

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
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    HAL_Delay(100);  /* Give NVM time to write */
    bat_cfg_step = 10U;

    /* 9. Exit CFGUPDATE — soft reset */
    st = BatGauge_SoftReset();
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 11U;

    st = waitFlag(BQ27441_FLAG_CFGUPMODE, false, 1000U);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 12U;

    /* 10. Start the Impedance Track algorithm.
     *     Without this, SOC / SOH / RM / FCC stay at zero and ITPOR never
     *     clears. Must be issued while the device is UNSEALED. */
    st = write16(BQ27441_REG_CONTROL, BQ27441_CTRL_IT_ENABLE);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 13U;

    /* 11. Wait for ITPOR to clear as confirmation that IT is running */
    st = waitFlag(BQ27441_FLAG_ITPOR, false, 2000U);
    (void)read16(BQ27441_REG_FLAGS, (uint16_t *)&bat_cfg_flags_after);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 14U;

    /* 12. Seal the device */
    st = write16(BQ27441_REG_CONTROL, BQ27441_CTRL_SEALED);
    if (st != HAL_OK) { bat_cfg_err = st; return st; }
    bat_cfg_step = 15U;

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
