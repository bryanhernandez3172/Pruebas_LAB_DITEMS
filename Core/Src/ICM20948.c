/**
 * @file    ICM20948.c
 * @brief   Driver implementation for TDK ICM-20948 (9-axis IMU) over I2C.
 *
 * @date    April 22, 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#include "ICM20948.h"
#include <string.h>

/* ========================  EXTERN HAL HANDLES  ============================= */

extern I2C_HandleTypeDef hi2c1;

/* ========================  INTERNAL HELPERS  =============================== */

/**
 * @brief  Selects register bank via REG_BANK_SEL. Skips the write if the
 *         requested bank is already cached in the handle.
 * @param  dev   Driver handle.
 * @param  bank  Target bank (0..3).
 * @note   REG_BANK_SEL bits [5:4] hold the bank number, so write (bank << 4).
 */
static HAL_StatusTypeDef icm_select_bank(ICM20948_t *dev, uint8_t bank)
{
    if (dev->current_bank == bank) {
        return HAL_OK;
    }
    uint8_t v = (uint8_t)(bank << 4);
    HAL_StatusTypeDef st = HAL_I2C_Mem_Write(ICM_I2C, dev->addr,
                                             ICM_REG_BANK_SEL,
                                             I2C_MEMADD_SIZE_8BIT,
                                             &v, 1U, ICM_I2C_TIMEOUT_MS);
    if (st == HAL_OK) {
        dev->current_bank = bank;
    }
    return st;
}

/**
 * @brief  Writes a single byte to a register in the specified bank.
 */
static HAL_StatusTypeDef icm_write_reg(ICM20948_t *dev, uint8_t bank,
                                       uint8_t reg, uint8_t val)
{
    HAL_StatusTypeDef st = icm_select_bank(dev, bank);
    if (st != HAL_OK) return st;

    return HAL_I2C_Mem_Write(ICM_I2C, dev->addr, reg,
                             I2C_MEMADD_SIZE_8BIT,
                             &val, 1U, ICM_I2C_TIMEOUT_MS);
}

/**
 * @brief  Reads a single byte from a register in the specified bank.
 */
static HAL_StatusTypeDef icm_read_reg(ICM20948_t *dev, uint8_t bank,
                                      uint8_t reg, uint8_t *val)
{
    HAL_StatusTypeDef st = icm_select_bank(dev, bank);
    if (st != HAL_OK) return st;

    return HAL_I2C_Mem_Read(ICM_I2C, dev->addr, reg,
                            I2C_MEMADD_SIZE_8BIT,
                            val, 1U, ICM_I2C_TIMEOUT_MS);
}

/**
 * @brief  Reads N consecutive bytes starting at reg in the specified bank.
 */
static HAL_StatusTypeDef icm_read_bytes(ICM20948_t *dev, uint8_t bank,
                                        uint8_t reg, uint8_t *buf, uint16_t n)
{
    HAL_StatusTypeDef st = icm_select_bank(dev, bank);
    if (st != HAL_OK) return st;

    return HAL_I2C_Mem_Read(ICM_I2C, dev->addr, reg,
                            I2C_MEMADD_SIZE_8BIT,
                            buf, n, ICM_I2C_TIMEOUT_MS);
}

/**
 * @brief  Executes a one-shot master transaction on the internal bus via
 *         Slave 4. Used to read/write individual AK09916 registers during
 *         init. Polls I2C_MST_STATUS until SLV4_DONE asserts or timeout.
 * @param  dev         Driver handle.
 * @param  is_read     True to read from AK09916, false to write.
 * @param  slave_reg   Register inside AK09916.
 * @param  data_out    Byte to write (ignored when is_read).
 * @param  data_in     Destination for the read byte (ignored on write).
 * @note   Returns ICM20948_ERR_MAG_NACK if the mag does not acknowledge,
 *         or ICM20948_ERR_TIMEOUT if SLV4_DONE never asserts.
 */
static ICM20948_Status_e icm_slv4_txn(ICM20948_t *dev, bool is_read,
                                      uint8_t slave_reg, uint8_t data_out,
                                      uint8_t *data_in)
{
    uint8_t addr_byte = ICM_MAG_ADDR_7BIT;
    if (is_read) {
        addr_byte |= ICM_SLV_READ_FLAG;
    }

    /* Program Slave 4 registers in bank 3 */
    if (icm_write_reg(dev, 3U, ICM_I2C_SLV4_ADDR, addr_byte) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (icm_write_reg(dev, 3U, ICM_I2C_SLV4_REG, slave_reg) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (!is_read) {
        if (icm_write_reg(dev, 3U, ICM_I2C_SLV4_DO, data_out) != HAL_OK) {
            return ICM20948_ERR_I2C;
        }
    }
    /* Trigger the transaction (EN bit, no interrupts, no regs) */
    if (icm_write_reg(dev, 3U, ICM_I2C_SLV4_CTRL, ICM_SLV_CTRL_EN) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* Poll I2C_MST_STATUS in bank 0 for SLV4_DONE or SLV4_NACK */
    uint32_t t0 = HAL_GetTick();
    uint8_t  status;
    while ((HAL_GetTick() - t0) < 100U) {
        if (icm_read_reg(dev, 0U, ICM_I2C_MST_STATUS, &status) != HAL_OK) {
            return ICM20948_ERR_I2C;
        }
        if (status & ICM_I2C_MST_SLV4_NACK) {
            return ICM20948_ERR_MAG_NACK;
        }
        if (status & ICM_I2C_MST_SLV4_DONE) {
            if (is_read && (data_in != NULL)) {
                if (icm_read_reg(dev, 3U, ICM_I2C_SLV4_DI, data_in) != HAL_OK) {
                    return ICM20948_ERR_I2C;
                }
            }
            return ICM20948_OK;
        }
        HAL_Delay(1U);
    }
    return ICM20948_ERR_TIMEOUT;
}

/**
 * @brief  Assembles a big-endian signed 16-bit value from two bytes
 *         (used for accel, gyro, temp).
 */
static inline int16_t be16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief  Assembles a little-endian signed 16-bit value from two bytes
 *         (used for AK09916 magnetometer data).
 */
static inline int16_t le16(const uint8_t *p)
{
    return (int16_t)(((uint16_t)p[1] << 8) | (uint16_t)p[0]);
}

/* ================================  API  =================================== */

/**
 * @brief  Full initialization sequence:
 *         1. Soft reset via PWR_MGMT_1, wake, auto-clock.
 *         2. Verify WHO_AM_I.
 *         3. Configure gyro DLPF + FSR + sample rate in bank 2.
 *         4. Configure accel DLPF + FSR + sample rate in bank 2.
 *         5. Disable bypass, enable internal I2C master in bank 0.
 *         6. Configure master clock to ~345 kHz in bank 3.
 *         7. Soft-reset AK09916 via SLV4, verify WIA2 = 0x09.
 *         8. Wake AK09916 at 100 Hz continuous via SLV4.
 *         9. Configure SLV0 to auto-read 8 bytes (HXL..ST2) every cycle.
 */
ICM20948_Status_e ICM20948_Init(ICM20948_t *dev)
{
    if (dev == NULL) return ICM20948_ERR_PARAM;

    memset(dev, 0, sizeof(*dev));
    dev->addr         = ICM_ADDR;
    dev->current_bank = 0xFFU;          /* force first icm_select_bank to write */

    /* --- Soft reset and wake --- */
    if (icm_write_reg(dev, 0U, ICM_PWR_MGMT_1,
                      ICM_PWR_MGMT_1_DEVICE_RESET) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    HAL_Delay(100U);
    dev->current_bank = 0xFFU;          /* reset clears bank cache */

    if (icm_write_reg(dev, 0U, ICM_PWR_MGMT_1,
                      ICM_PWR_MGMT_1_CLKSEL_AUTO) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    HAL_Delay(10U);

    /* --- WHO_AM_I --- */
    uint8_t who = 0U;
    if (icm_read_reg(dev, 0U, ICM_WHO_AM_I, &who) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (who != ICM_WHO_AM_I_VALUE) {
        return ICM20948_ERR_ID;
    }

    /* --- Enable all accel and gyro axes --- */
    if (icm_write_reg(dev, 0U, ICM_PWR_MGMT_2, 0x00U) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* --- Gyro config (bank 2): DLPF + FSR + enable DLPF bit --- */
    uint8_t gcfg = (uint8_t)((ICM_DLPF_CFG << 3) | (ICM_GYRO_FSR << 1) | 0x01U);
    if (icm_write_reg(dev, 2U, ICM_GYRO_CONFIG_1, gcfg) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* Gyro sample rate divider: ODR = 1125 / (1 + div) */
    uint8_t gdiv = (uint8_t)((1125U / ICM_ODR_HZ) - 1U);
    if (icm_write_reg(dev, 2U, ICM_GYRO_SMPLRT_DIV, gdiv) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* --- Accel config (bank 2): DLPF + FSR + enable DLPF bit --- */
    uint8_t acfg = (uint8_t)((ICM_DLPF_CFG << 3) | (ICM_ACCEL_FSR << 1) | 0x01U);
    if (icm_write_reg(dev, 2U, ICM_ACCEL_CONFIG, acfg) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* Accel sample rate divider (split across two registers, 12-bit value) */
    uint16_t adiv = (uint16_t)((1125U / ICM_ODR_HZ) - 1U);
    if (icm_write_reg(dev, 2U, ICM_ACCEL_SMPLRT_DIV_1,
                      (uint8_t)((adiv >> 8) & 0x0FU)) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (icm_write_reg(dev, 2U, ICM_ACCEL_SMPLRT_DIV_2,
                      (uint8_t)(adiv & 0xFFU)) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* --- Disable bypass, enable internal I2C master (bank 0) --- */
    if (icm_write_reg(dev, 0U, ICM_INT_PIN_CFG, 0x00U) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (icm_write_reg(dev, 0U, ICM_USER_CTRL,
                      ICM_USER_CTRL_I2C_MST_EN) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* --- Master clock ~345 kHz (bank 3) --- */
    if (icm_write_reg(dev, 3U, ICM_I2C_MST_CTRL,
                      ICM_I2C_MST_CLK_345KHZ) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    HAL_Delay(5U);

    /* --- Soft-reset the AK09916 via SLV4 --- */
    ICM20948_Status_e st = icm_slv4_txn(dev, false, AK09916_CNTL3,
                                        AK09916_SOFT_RESET, NULL);
    if (st != ICM20948_OK) return st;
    HAL_Delay(100U);

    /* --- Verify AK09916 identity (WIA2 == 0x09) --- */
    uint8_t mag_id = 0U;
    st = icm_slv4_txn(dev, true, AK09916_WIA2, 0U, &mag_id);
    if (st != ICM20948_OK) return st;
    if (mag_id != AK09916_WHO_AM_I_VALUE) {
        return ICM20948_ERR_MAG_ID;
    }

    /* --- Wake the AK09916 in continuous 100 Hz mode --- */
    st = icm_slv4_txn(dev, false, AK09916_CNTL2, AK09916_MODE_100HZ, NULL);
    if (st != ICM20948_OK) return st;
    HAL_Delay(10U);

    /* --- Configure SLV0 to auto-read 8 bytes (HXL..ST2) on every cycle --- */
    if (icm_write_reg(dev, 3U, ICM_I2C_SLV0_ADDR,
                      (uint8_t)(ICM_MAG_ADDR_7BIT | ICM_SLV_READ_FLAG)) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (icm_write_reg(dev, 3U, ICM_I2C_SLV0_REG, AK09916_HXL) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    if (icm_write_reg(dev, 3U, ICM_I2C_SLV0_CTRL,
                      (uint8_t)(ICM_SLV_CTRL_EN | 0x08U)) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* Leave driver parked in bank 0 for the data-read path */
    if (icm_select_bank(dev, 0U) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    dev->initialized = true;
    return ICM20948_OK;
}

/**
 * @brief  Thin wrapper around a single-byte read of the WHO_AM_I register.
 */
ICM20948_Status_e ICM20948_WhoAmI(ICM20948_t *dev, uint8_t *id)
{
    if ((dev == NULL) || (id == NULL)) return ICM20948_ERR_PARAM;

    if (icm_read_reg(dev, 0U, ICM_WHO_AM_I, id) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }
    return ICM20948_OK;
}

/**
 * @brief  Reads gyro raw samples for ICM_GYRO_CAL_SAMPLES periods, averages
 *         them and converts to dps. Uses raw values directly so running the
 *         calibration twice does not accumulate bias on bias.
 */
ICM20948_Status_e ICM20948_CalibrateGyroBias(ICM20948_t *dev)
{
    if (dev == NULL) return ICM20948_ERR_PARAM;

    int32_t sum[3] = { 0, 0, 0 };
    ICM20948_Data_t d;

    for (uint16_t i = 0U; i < ICM_GYRO_CAL_SAMPLES; i++) {
        ICM20948_Status_e st = ICM20948_ReadAll(dev, &d);
        if (st != ICM20948_OK) return st;

        sum[0] += d.gyro_raw[0];
        sum[1] += d.gyro_raw[1];
        sum[2] += d.gyro_raw[2];

        HAL_Delay(ICM_GYRO_CAL_DELAY_MS);
    }

    for (uint8_t i = 0U; i < 3U; i++) {
        float avg = (float)sum[i] / (float)ICM_GYRO_CAL_SAMPLES;
        dev->cal.gyro_bias_dps[i] = avg / ICM_GYRO_LSB_PER_DPS;
    }
    dev->gyro_calibrated = true;
    return ICM20948_OK;
}

/**
 * @brief  Reads 22 consecutive bytes starting at ACCEL_XOUT_H:
 *         [0..5]   accel X/Y/Z high+low (big-endian)
 *         [6..11]  gyro  X/Y/Z high+low (big-endian)
 *         [12..13] temperature high+low (big-endian)
 *         [14..21] EXT_SLV_SENS_DATA_00..07 = HXL, HXH, HYL, HYH, HZL, HZH,
 *                  TMPS, ST2 (mag bytes are little-endian)
 *         Applies scaling and calibration in-place.
 */
ICM20948_Status_e ICM20948_ReadAll(ICM20948_t *dev, ICM20948_Data_t *out)
{
    if ((dev == NULL) || (out == NULL)) return ICM20948_ERR_PARAM;

    uint8_t buf[ICM_READALL_LEN];
    if (icm_read_bytes(dev, 0U, ICM_ACCEL_XOUT_H,
                       buf, ICM_READALL_LEN) != HAL_OK) {
        return ICM20948_ERR_I2C;
    }

    /* Raw values */
    out->accel_raw[0] = be16(&buf[0]);
    out->accel_raw[1] = be16(&buf[2]);
    out->accel_raw[2] = be16(&buf[4]);

    out->gyro_raw[0]  = be16(&buf[6]);
    out->gyro_raw[1]  = be16(&buf[8]);
    out->gyro_raw[2]  = be16(&buf[10]);

    out->temp_raw     = be16(&buf[12]);

    out->mag_raw[0]   = le16(&buf[14]);
    out->mag_raw[1]   = le16(&buf[16]);
    out->mag_raw[2]   = le16(&buf[18]);
    /* buf[20] = TMPS, buf[21] = ST2 — read to unlock next mag measurement */

    /* Scaled values */
    for (uint8_t i = 0U; i < 3U; i++) {
        out->accel_g[i]  = (float)out->accel_raw[i] / ICM_ACCEL_LSB_PER_G;
        out->gyro_dps[i] = ((float)out->gyro_raw[i] / ICM_GYRO_LSB_PER_DPS)
                           - dev->cal.gyro_bias_dps[i];
        out->mag_uT[i]   = (float)out->mag_raw[i] * ICM_MAG_UT_PER_LSB;
    }

#ifdef ICM_MAG_CAL_ENABLE
    for (uint8_t i = 0U; i < 3U; i++) {
        out->mag_uT[i] -= dev->cal.mag_hard_iron_uT[i];
    }
#endif

    out->temp_c = ((float)out->temp_raw / ICM_TEMP_LSB_PER_DEG)
                  + ICM_TEMP_ROOM_OFFSET_C;

    return ICM20948_OK;
}

#ifdef ICM_MAG_CAL_ENABLE
/**
 * @brief  Minimal hard-iron calibration: tracks min/max of raw mag over
 *         ICM_MAG_CAL_DURATION_MS while the user rotates the device in a
 *         figure-8 pattern. Hard-iron offset is stored as the midpoint.
 *         Soft-iron matrix is left untouched (identity in the readout).
 */
ICM20948_Status_e ICM20948_CalibrateMag(ICM20948_t *dev)
{
    if (dev == NULL) return ICM20948_ERR_PARAM;

    int16_t mag_min[3] = {  INT16_MAX,  INT16_MAX,  INT16_MAX };
    int16_t mag_max[3] = {  INT16_MIN,  INT16_MIN,  INT16_MIN };
    ICM20948_Data_t d;

    uint32_t t0 = HAL_GetTick();
    while ((HAL_GetTick() - t0) < ICM_MAG_CAL_DURATION_MS) {
        if (ICM20948_ReadAll(dev, &d) == ICM20948_OK) {
            for (uint8_t i = 0U; i < 3U; i++) {
                if (d.mag_raw[i] < mag_min[i]) mag_min[i] = d.mag_raw[i];
                if (d.mag_raw[i] > mag_max[i]) mag_max[i] = d.mag_raw[i];
            }
        }
        HAL_Delay(ICM_MAG_CAL_PERIOD_MS);
    }

    for (uint8_t i = 0U; i < 3U; i++) {
        float mid = ((float)mag_max[i] + (float)mag_min[i]) * 0.5f;
        dev->cal.mag_hard_iron_uT[i] = mid * ICM_MAG_UT_PER_LSB;
    }
    dev->mag_calibrated = true;
    return ICM20948_OK;
}
#endif
