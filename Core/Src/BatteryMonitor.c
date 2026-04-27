/**
 * @file    BatteryMonitor.c
 * @brief   Driver implementation for BQ27441-G1 fuel gauge.
 *
 * @date    April 24, 2026
 * @author  César Pérez
 * @version 2.0.0
 */

#include "BatteryMonitor.h"

/* ======================  EXTERNAL HAL HANDLES  ============================ */

extern I2C_HandleTypeDef hi2c1;

/* ======================  STATIC VARIABLES  ================================ */

/* Driver state (mirrors Arduino static variables) */

static uint8_t  bq_sealFlag        = 0U;
static uint8_t  bq_userConfigCtrl  = 0U;
static uint8_t  bq_gaugeReady      = 0U;
static uint8_t  bq_waitingRecovery = 0U;
static uint32_t bq_lastFailTick    = 0U;

/* ======================  STATIC FUNCTIONS  ================================ */

/* Low-level I2C primitives */

/**
 * @brief  Writes 'count' bytes to a register.
 */
static void bq_writeBytes(uint8_t reg, uint8_t *src, uint8_t count)
{
    HAL_I2C_Mem_Write(BQ27441_I2C, BQ27441_ADDR, reg,
                      I2C_MEMADD_SIZE_8BIT, src, count, BQ27441_TIMEOUT_MS);
}

/**
 * @brief  Reads 'count' bytes from a register.
 */
static void bq_readBytes(uint8_t reg, uint8_t *dst, uint8_t count)
{
    HAL_I2C_Mem_Read(BQ27441_I2C, BQ27441_ADDR, reg,
                     I2C_MEMADD_SIZE_8BIT, dst, count, BQ27441_TIMEOUT_MS);
}

/**
 * @brief  Reads a 16-bit little-endian word from a standard register.
 */
static uint16_t bq_readWord(uint8_t reg)
{
    uint8_t buf[2] = {0U, 0U};
    bq_readBytes(reg, buf, 2U);
    return (uint16_t)(buf[0] | ((uint16_t)buf[1] << 8));
}

/**
 * @brief  Writes a Control() sub-command and reads back the 16-bit response.
 */
static uint16_t bq_readControlWord(uint16_t function)
{
    uint8_t cmd[2] = { (uint8_t)(function & 0xFFU), (uint8_t)(function >> 8U) };
    bq_writeBytes(BQ27441_REG_CONTROL, cmd, 2U);
    uint8_t rsp[2] = {0U, 0U};
    bq_readBytes(BQ27441_REG_CONTROL, rsp, 2U);
    return (uint16_t)(rsp[0] | ((uint16_t)rsp[1] << 8));
}

/**
 * @brief  Sends a Control() sub-command without reading the response.
 */
static void bq_executeControlWord(uint16_t function)
{
    uint8_t cmd[2] = { (uint8_t)(function & 0xFFU), (uint8_t)(function >> 8U) };
    bq_writeBytes(BQ27441_REG_CONTROL, cmd, 2U);
}

/* Seal / Unseal helpers */

/**
 * @brief  Returns 1 if the gauge is in sealed state.
 */
static uint8_t bq_sealed(void)
{
    return (bq_readControlWord(BQ27441_STATUS_SS) & BQ27441_STATUS_SS) ? 1U : 0U;
}

/**
 * @brief  Seals the gauge.
 */
static void bq_seal(void)
{
    bq_readControlWord(BQ27441_CTRL_SEALED);
}

/**
 * @brief  Unseals the gauge (two identical key writes).
 */
static void bq_unseal(void)
{
    bq_readControlWord(BQ27441_UNSEAL_KEY);
    bq_readControlWord(BQ27441_UNSEAL_KEY);
}

/* Config Update Mode helpers */

/**
 * @brief  Enters CFGUPDATE mode.
 * @param  userControl  1 = caller will call bq_exitConfig() manually.
 */
static void bq_enterConfig(uint8_t userControl)
{
    if (userControl) {
        bq_userConfigCtrl = 1U;
    }

    if (bq_sealed()) {
        bq_sealFlag = 1U;
        bq_unseal();
    }

    bq_executeControlWord(BQ27441_CTRL_SET_CFGUPDATE);

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < BQ27441_CFGUPDATE_MS) {
        if (bq_readWord(BQ27441_REG_FLAGS) & BQ27441_FLAG_CFGUPMODE) {
            break;
        }
        HAL_Delay(1U);
    }
}

/**
 * @brief  Exits CFGUPDATE mode via soft reset, then re-seals if needed.
 */
static void bq_exitConfig(void)
{
    bq_executeControlWord(BQ27441_CTRL_SOFT_RESET);

    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < BQ27441_CFGUPDATE_MS) {
        if (!(bq_readWord(BQ27441_REG_FLAGS) & BQ27441_FLAG_CFGUPMODE)) {
            break;
        }
        HAL_Delay(1U);
    }

    if (bq_sealFlag) {
        bq_seal();
        bq_sealFlag = 0U;
    }
}

/* Extended data block helpers */

/**
 * @brief  Enables block data access.
 */
static void bq_blockDataControl(void)
{
    uint8_t en = 0x00U;
    bq_writeBytes(BQ27441_EXT_BLOCK_CTRL, &en, 1U);
}

/**
 * @brief  Selects data subclass by ID.
 */
static void bq_blockDataClass(uint8_t id)
{
    bq_writeBytes(BQ27441_EXT_DATA_CLASS, &id, 1U);
}

/**
 * @brief  Selects block offset (offset / 32).
 */
static void bq_blockDataOffset(uint8_t offset)
{
    bq_writeBytes(BQ27441_EXT_DATA_BLOCK, &offset, 1U);
}

/**
 * @brief  Writes one byte into the block data at the given position.
 */
static void bq_writeBlockData(uint8_t pos, uint8_t value)
{
    bq_writeBytes(BQ27441_EXT_BLOCK_DATA + pos, &value, 1U);
}

/**
 * @brief  Reads the full 32-byte block and computes checksum (255 - sum).
 */
static uint8_t bq_computeBlockChecksum(void)
{
    uint8_t block[32];
    bq_readBytes(BQ27441_EXT_BLOCK_DATA, block, 32U);
    uint8_t csum = 0U;
    for (uint8_t i = 0U; i < 32U; i++) {
        csum += block[i];
    }
    return (uint8_t)(255U - csum);
}

/**
 * @brief  Writes the checksum byte to commit the block.
 */
static void bq_writeBlockChecksum(uint8_t csum)
{
    bq_writeBytes(BQ27441_EXT_CHECKSUM, &csum, 1U);
}

/**
 * @brief  Writes 'len' bytes into extended data memory at classID:offset.
 * @note   If bq_userConfigCtrl is 0 it auto-enters and auto-exits config mode.
 */
static void bq_writeExtendedData(uint8_t classID, uint8_t offset,
                                  uint8_t *data, uint8_t len)
{
    if (!bq_userConfigCtrl) {
        bq_enterConfig(0U);
    }

    bq_blockDataControl();
    bq_blockDataClass(classID);
    bq_blockDataOffset(offset / 32U);
    bq_computeBlockChecksum();

    for (uint8_t i = 0U; i < len; i++) {
        bq_writeBlockData((offset % 32U) + i, data[i]);
    }

    bq_writeBlockChecksum(bq_computeBlockChecksum());

    if (!bq_userConfigCtrl) {
        bq_exitConfig();
    }
}

/* Parameter write helpers (big-endian, as BQ27441 expects) */

/**
 * @brief  Writes Design Capacity into NVM (offset 10 of State subclass).
 */
static void bq_setCapacity(uint16_t capacity)
{
    uint8_t d[2] = { (uint8_t)(capacity >> 8), (uint8_t)(capacity & 0xFFU) };
    bq_writeExtendedData(BQ27441_CLASS_STATE, BQ27441_STATE_DESIGN_CAP, d, 2U);
}

/**
 * @brief  Writes Design Energy into NVM (offset 12 of State subclass).
 */
static void bq_setDesignEnergy(uint16_t energy)
{
    uint8_t d[2] = { (uint8_t)(energy >> 8), (uint8_t)(energy & 0xFFU) };
    bq_writeExtendedData(BQ27441_CLASS_STATE, BQ27441_STATE_DESIGN_ENE, d, 2U);
}

/**
 * @brief  Writes Terminate Voltage into NVM (offset 16). Clamped 2500–3700 mV.
 */
static void bq_setTerminateVoltage(uint16_t voltage)
{
    if (voltage < 2500U) { voltage = 2500U; }
    if (voltage > 3700U) { voltage = 3700U; }
    uint8_t d[2] = { (uint8_t)(voltage >> 8), (uint8_t)(voltage & 0xFFU) };
    bq_writeExtendedData(BQ27441_CLASS_STATE, BQ27441_STATE_TERM_VOLT, d, 2U);
}

/**
 * @brief  Writes Taper Rate into NVM (offset 27). Clamped to 2000.
 */
static void bq_setTaperRate(uint16_t rate)
{
    if (rate > 2000U) { rate = 2000U; }
    uint8_t d[2] = { (uint8_t)(rate >> 8), (uint8_t)(rate & 0xFFU) };
    bq_writeExtendedData(BQ27441_CLASS_STATE, BQ27441_STATE_TAPER_RATE, d, 2U);
}

/* Device check helpers */

/**
 * @brief  Returns 1 if the gauge responds on the I2C bus.
 */
static uint8_t bq_isAlive(void)
{
    return (HAL_I2C_IsDeviceReady(BQ27441_I2C, BQ27441_ADDR,
                                  1U, BQ27441_TIMEOUT_MS) == HAL_OK) ? 1U : 0U;
}

/**
 * @brief  Checks device type ID; returns 1 on success.
 */
static uint8_t bq_begin(void)
{
    uint8_t cmd[2] = { (uint8_t)(BQ27441_CTRL_DEVICE_TYPE & 0xFFU),
                       (uint8_t)(BQ27441_CTRL_DEVICE_TYPE >> 8U) };
    uint8_t rsp[2] = {0U, 0U};
    bq_writeBytes(BQ27441_REG_CONTROL, cmd, 2U);
    bq_readBytes(BQ27441_REG_CONTROL, rsp, 2U);
    uint16_t id = (uint16_t)(rsp[0] | ((uint16_t)rsp[1] << 8));
    return (id == BQ27441_DEVICE_TYPE_ID) ? 1U : 0U;
}

/**
 * @brief  Recovers the I2C bus by de-initing and re-initing the peripheral.
 */
static void bq_recoverI2C(void)
{
    HAL_I2C_DeInit(BQ27441_I2C);
    HAL_Delay(20U);
    HAL_I2C_Init(BQ27441_I2C);
    HAL_Delay(20U);
    bq_begin();
}

/* Application-level state helpers */

/**
 * @brief  Checks gauge health; re-configures on ITPOR, recovers on I2C fault.
 * @note   Updates bq_gaugeReady, bq_waitingRecovery, bq_lastFailTick.
 */
static void bq_checkStatus(void)
{
    if (bq_waitingRecovery) {
        if ((HAL_GetTick() - bq_lastFailTick) < BQ27441_RECOVERY_MS) {
            return;
        }
        bq_waitingRecovery = 0U;
    }

    if (!bq_isAlive()) {
        bq_gaugeReady = 0U;
        return;
    }

    uint16_t f = bq_readWord(BQ27441_REG_FLAGS);

    if (f == 0xFFFFU) {
        bq_waitingRecovery = 1U;
        bq_lastFailTick    = HAL_GetTick();
        bq_recoverI2C();
        bq_gaugeReady = 0U;
        return;
    }

    if (f & BQ27441_FLAG_ITPOR) {
        BatGauge_Configure();
        return;
    }

    bq_gaugeReady = 1U;
}

/**
 * @brief  Reads all registers and fills the data structure.
 * @note   Sets data->is_ready = 0 on communication error or invalid value.
 */
static void bq_readAndFill(BatGauge_Data_t *data)
{
    if (!bq_gaugeReady) {
        uint8_t *p = (uint8_t *)data;
        for (uint8_t i = 0U; i < sizeof(BatGauge_Data_t); i++) { p[i] = 0U; }
        return;
    }

    uint16_t soc   = bq_readWord(BQ27441_REG_SOC);
    uint16_t volts = bq_readWord(BQ27441_REG_VOLTAGE);
    int16_t  curr  = (int16_t)bq_readWord(BQ27441_REG_AVG_CURR);
    uint8_t  soh   = (uint8_t)(bq_readWord(BQ27441_REG_SOH) & 0x00FFU);
    uint16_t rm    = bq_readWord(BQ27441_REG_RM);
    uint16_t fcc   = bq_readWord(BQ27441_REG_FCC);
    uint16_t flags = bq_readWord(BQ27441_REG_FLAGS);

    if (soc == 0xFFFFU || volts == 0xFFFFU) {
        bq_gaugeReady = 0U;
        uint8_t *p = (uint8_t *)data;
        for (uint8_t i = 0U; i < sizeof(BatGauge_Data_t); i++) { p[i] = 0U; }
        return;
    }

    data->soc_pct       = soc;
    data->voltage_mV    = volts;
    data->avg_current_mA = curr;
    data->soh_pct       = soh;
    data->remaining_mAh = rm;
    data->full_cap_mAh  = fcc;
    data->flags         = flags;

    int16_t abs_curr = (curr < 0) ? -curr : curr;

    if (curr > (int16_t)BAT_I_THRESH_MA) {
        data->charge_state = BAT_CHARGE_CHARGING;
    } else if (curr < -(int16_t)BAT_I_THRESH_MA) {
        data->charge_state = BAT_CHARGE_DISCHARGING;
    } else {
        data->charge_state = BAT_CHARGE_QUIET;
    }

    data->is_full  = (soc >= 99U) ||
                     (volts >= BAT_FULL_V_MV && (uint16_t)abs_curr <= BAT_FULL_I_MA);
    data->is_ready = 1U;
}

/* ================================  API  ==================================== */

HAL_StatusTypeDef BatGauge_Init(void)
{
    if (!bq_begin()) {
        return HAL_ERROR;
    }

    BatGauge_Configure();
    bq_gaugeReady = 1U;

    return HAL_OK;
}

void BatGauge_Update(BatGauge_Data_t *data)
{
    if (data == NULL) {
        return;
    }

    bq_checkStatus();
    bq_readAndFill(data);
}

HAL_StatusTypeDef BatGauge_Configure(void)
{
    bq_enterConfig(1U);
    bq_setCapacity(BAT_DESIGN_CAP_MAH);
    bq_setDesignEnergy(BAT_DESIGN_ENERGY_MWH);
    bq_setTerminateVoltage(BAT_TERMINATE_MV);
    bq_setTaperRate(BAT_TAPER_RATE);
    bq_exitConfig();
    bq_userConfigCtrl = 0U;

    HAL_Delay(500U);

    bq_executeControlWord(BQ27441_CTRL_CLEAR_HIBERNATE);
    HAL_Delay(10U);
    bq_executeControlWord(BQ27441_CTRL_IT_ENABLE);
    HAL_Delay(10U);
    bq_executeControlWord(BQ27441_CTRL_BAT_INSERT);
    HAL_Delay(10U);

    bq_gaugeReady = 1U;

    return HAL_OK;
}
