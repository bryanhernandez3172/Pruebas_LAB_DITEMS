/**
 * @file    ICM20948.h
 * @brief   9-axis IMU driver for TDK ICM-20948 over I2C on STM32F4xx.
 *
 * @details Wraps the accelerometer, gyroscope, temperature sensor and the
 *          embedded AK09916 magnetometer into a single I2C transaction.
 *          The ICM-20948's internal I2C master is configured to auto-read the
 *          AK09916 and place the six mag bytes in EXT_SLV_SENS_DATA_xx, so the
 *          nine axes are delivered in one packet with one timestamp.
 *
 *          Bypass mode is intentionally disabled — the STM32 never talks to
 *          the AK09916 directly. All mag traffic goes through the ICM master
 *          controller.
 *
 *          Register banks are handled internally: the driver caches the
 *          current bank in the handle and writes REG_BANK_SEL only when it
 *          changes.
 *
 *          Calibration model:
 *          - Gyro bias: always applied. Call ICM20948_CalibrateGyroBias()
 *            right after ICM20948_Init() with the device completely still.
 *          - Magnetometer hard-iron: compiled out by default. Define
 *            ICM_MAG_CAL_ENABLE below to compile in the calibration struct
 *            and the figure-8 routine ICM20948_CalibrateMag().
 *
 *          Default configuration targets human-scale motion (like a phone):
 *          accel ±4 g, gyro ±500 dps, DLPF ~50 Hz, ODR 100 Hz.
 *
 *          CubeMX configuration:
 *          - I2C peripheral in Standard or Fast mode (100 – 400 kHz).
 *          - No interrupts required for basic operation.
 *          - AD0 pin tied to GND on the EV-ICM-20948 breakout → address 0x68.
 *            Change ICM_AD0_PIN_STATE if AD0 is wired to VDD.
 *
 * @date    April 22, 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef ICM20948_H
#define ICM20948_H

#include "stm32wbxx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================  CONFIGURATION  ================================= */

/* I2C handle and timeout — update to match CubeMX .ioc */

#define ICM_I2C                     (&hi2c1)
#define ICM_I2C_TIMEOUT_MS          50U

/* AD0 pin wiring: 0 → address 0x68, 1 → address 0x69 */

#define ICM_AD0_PIN_STATE           0U

/* Full-scale range selection (see ICM_ACCEL_FSR_* / ICM_GYRO_FSR_* below) */

#define ICM_ACCEL_FSR               ICM_ACCEL_FSR_4G
#define ICM_GYRO_FSR                ICM_GYRO_FSR_500DPS

/* Digital Low Pass Filter: 0..7. Lower value = wider band, more noise.
 * Higher value = narrower band, smoother output, more group delay.
 *   0 → 197 Hz (gyro) / 246 Hz (accel)
 *   3 →  51 Hz         /  50 Hz
 *   4 →  24 Hz         /  24 Hz
 *   5 →  12 Hz         /  12 Hz    <-- default, matches phone-like motion
 *   6 →   6 Hz         /   6 Hz
 * Most human motion lives below 5 Hz, so value 5 kills noise without
 * smearing the signal. Drop to 4 if you feel the response is too sluggish. */

#define ICM_DLPF_CFG                5U

/* Output data rate in Hz. Internal base is 1125 Hz, divided down */

#define ICM_ODR_HZ                  100U

/* Gyro bias calibration parameters (200 × 5 ms ≈ 1 s still period) */

#define ICM_GYRO_CAL_SAMPLES        200U
#define ICM_GYRO_CAL_DELAY_MS       5U

/* Automatic bus recovery: if ICM20948_ReadAll() gets a HAL I2C error, the
 * driver will reset the I2C peripheral and re-run Init() this many times
 * before giving up on that call. 0 disables the retry. */

#define ICM_RECOVERY_RETRIES        1U

/* Uncomment to compile in magnetometer hard-iron calibration and correction.
 * Requires the user to call ICM20948_CalibrateMag() while slowly rotating
 * the device in all orientations for ICM_MAG_CAL_DURATION_MS.
 * When commented out, mag readings are delivered raw (µT with factory
 * sensitivity applied) — sufficient for testing. */

// #define ICM_MAG_CAL_ENABLE

#ifdef ICM_MAG_CAL_ENABLE
#define ICM_MAG_CAL_DURATION_MS     20000U
#define ICM_MAG_CAL_PERIOD_MS       50U
#endif

/* ====================  DEVICE CONSTANTS  ================================== */

/* I2C addresses */

#define ICM_ADDR_BASE               0x68U
#define ICM_ADDR_7BIT               (ICM_ADDR_BASE + ICM_AD0_PIN_STATE)
#define ICM_ADDR                    (ICM_ADDR_7BIT << 1)    /**< 8-bit HAL form */

#define ICM_MAG_ADDR_7BIT           0x0CU                   /**< AK09916 fixed */

/* Expected WHO_AM_I values */

#define ICM_WHO_AM_I_VALUE          0xEAU
#define AK09916_WHO_AM_I_VALUE      0x09U

/* Full-scale range selectors (used by ICM_ACCEL_FSR / ICM_GYRO_FSR) */

#define ICM_ACCEL_FSR_2G            0U
#define ICM_ACCEL_FSR_4G            1U
#define ICM_ACCEL_FSR_8G            2U
#define ICM_ACCEL_FSR_16G           3U

#define ICM_GYRO_FSR_250DPS         0U
#define ICM_GYRO_FSR_500DPS         1U
#define ICM_GYRO_FSR_1000DPS        2U
#define ICM_GYRO_FSR_2000DPS        3U

/* Scale factors (LSB per physical unit) derived from the selected FSR */

#if   (ICM_ACCEL_FSR == ICM_ACCEL_FSR_2G)
#define ICM_ACCEL_LSB_PER_G         16384.0f
#elif (ICM_ACCEL_FSR == ICM_ACCEL_FSR_4G)
#define ICM_ACCEL_LSB_PER_G         8192.0f
#elif (ICM_ACCEL_FSR == ICM_ACCEL_FSR_8G)
#define ICM_ACCEL_LSB_PER_G         4096.0f
#elif (ICM_ACCEL_FSR == ICM_ACCEL_FSR_16G)
#define ICM_ACCEL_LSB_PER_G         2048.0f
#endif

#if   (ICM_GYRO_FSR == ICM_GYRO_FSR_250DPS)
#define ICM_GYRO_LSB_PER_DPS        131.0f
#elif (ICM_GYRO_FSR == ICM_GYRO_FSR_500DPS)
#define ICM_GYRO_LSB_PER_DPS        65.5f
#elif (ICM_GYRO_FSR == ICM_GYRO_FSR_1000DPS)
#define ICM_GYRO_LSB_PER_DPS        32.8f
#elif (ICM_GYRO_FSR == ICM_GYRO_FSR_2000DPS)
#define ICM_GYRO_LSB_PER_DPS        16.4f
#endif

#define ICM_MAG_UT_PER_LSB          0.15f       /**< AK09916 fixed sensitivity */
#define ICM_TEMP_LSB_PER_DEG        333.87f     /**< Temp sensor sensitivity  */
#define ICM_TEMP_ROOM_OFFSET_C      21.0f       /**< Temp sensor room offset  */

/* Register bank select register (same address in every bank) */

#define ICM_REG_BANK_SEL            0x7FU

/* --- Bank 0 registers --- */

#define ICM_WHO_AM_I                0x00U
#define ICM_USER_CTRL               0x03U
#define ICM_PWR_MGMT_1              0x06U
#define ICM_PWR_MGMT_2              0x07U
#define ICM_INT_PIN_CFG             0x0FU
#define ICM_I2C_MST_STATUS          0x17U
#define ICM_ACCEL_XOUT_H            0x2DU       /**< Start of 22-byte data run */

/* USER_CTRL bits */

#define ICM_USER_CTRL_I2C_MST_EN    (1U << 5)

/* PWR_MGMT_1 bits */

#define ICM_PWR_MGMT_1_DEVICE_RESET (1U << 7)
#define ICM_PWR_MGMT_1_SLEEP        (1U << 6)
#define ICM_PWR_MGMT_1_CLKSEL_AUTO  0x01U

/* INT_PIN_CFG bits */

#define ICM_INT_PIN_CFG_BYPASS_EN   (1U << 1)

/* I2C_MST_STATUS bits */

#define ICM_I2C_MST_SLV4_DONE       (1U << 6)
#define ICM_I2C_MST_SLV4_NACK       (1U << 4)

/* --- Bank 2 registers --- */

#define ICM_GYRO_SMPLRT_DIV         0x00U
#define ICM_GYRO_CONFIG_1           0x01U
#define ICM_ACCEL_SMPLRT_DIV_1      0x10U
#define ICM_ACCEL_SMPLRT_DIV_2      0x11U
#define ICM_ACCEL_CONFIG            0x14U

/* --- Bank 3 registers (I2C master) --- */

#define ICM_I2C_MST_CTRL            0x01U
#define ICM_I2C_SLV0_ADDR           0x03U
#define ICM_I2C_SLV0_REG            0x04U
#define ICM_I2C_SLV0_CTRL           0x05U
#define ICM_I2C_SLV4_ADDR           0x13U
#define ICM_I2C_SLV4_REG            0x14U
#define ICM_I2C_SLV4_CTRL           0x15U
#define ICM_I2C_SLV4_DO             0x16U
#define ICM_I2C_SLV4_DI             0x17U

/* Slave-config bit helpers */

#define ICM_SLV_READ_FLAG           0x80U       /**< OR with addr to read    */
#define ICM_SLV_CTRL_EN             0x80U       /**< Enable bit in SLVx_CTRL */

/* I2C master clock: bits [3:0] = 7 → 345.6 kHz (AK09916-safe) */

#define ICM_I2C_MST_CLK_345KHZ      0x07U

/* --- AK09916 registers (accessed via ICM master) --- */

#define AK09916_WIA2                0x01U
#define AK09916_ST1                 0x10U
#define AK09916_HXL                 0x11U       /**< Start of 6 mag bytes    */
#define AK09916_ST2                 0x18U       /**< Read-to-unlock marker   */
#define AK09916_CNTL2               0x31U
#define AK09916_CNTL3               0x32U

/* AK09916 operating modes (written to CNTL2) */

#define AK09916_MODE_POWERDOWN      0x00U
#define AK09916_MODE_SINGLE         0x01U
#define AK09916_MODE_10HZ           0x02U
#define AK09916_MODE_20HZ           0x04U
#define AK09916_MODE_50HZ           0x06U
#define AK09916_MODE_100HZ          0x08U

/* AK09916 soft reset (CNTL3) */

#define AK09916_SOFT_RESET          0x01U

/* Byte count read from ICM in a single ReadAll transaction:
 * 6 accel + 6 gyro + 2 temp + 8 mag-slave = 22 bytes */

#define ICM_READALL_LEN             22U

/* ========================  ENUMERATIONS  ================================== */

/* Driver return codes */

typedef enum {
    ICM20948_OK             = 0,    /**< Operation successful                */
    ICM20948_ERR_I2C        = 1,    /**< I2C communication error             */
    ICM20948_ERR_ID         = 2,    /**< WHO_AM_I mismatch on ICM-20948      */
    ICM20948_ERR_MAG_ID     = 3,    /**< WIA2 mismatch on AK09916            */
    ICM20948_ERR_MAG_NACK   = 4,    /**< SLV4 transaction NACKed by mag      */
    ICM20948_ERR_TIMEOUT    = 5,    /**< SLV4_DONE never asserted            */
    ICM20948_ERR_PARAM      = 6     /**< NULL pointer or bad argument        */
} ICM20948_Status_e;

/* ============================  STRUCTURES  ================================ */

/* Calibration coefficients applied inside ICM20948_ReadAll() */

typedef struct {
    float gyro_bias_dps[3];                 /**< Subtracted from gyro (dps)  */

#ifdef ICM_MAG_CAL_ENABLE
    float mag_hard_iron_uT[3];              /**< Subtracted from mag (µT)    */
    float mag_soft_iron[3][3];              /**< Reserved — identity in v1   */
#endif
} ICM20948_Cal_t;

/* Runtime handle — one per physical sensor */

typedef struct {
    uint8_t          addr;                  /**< 8-bit HAL address           */
    uint8_t          current_bank;          /**< Cached bank to avoid writes */
    bool             initialized;           /**< True after successful Init  */
    bool             gyro_calibrated;       /**< True after bias calibration */
#ifdef ICM_MAG_CAL_ENABLE
    bool             mag_calibrated;        /**< True after figure-8 routine */
#endif
    uint32_t         consecutive_i2c_errors;/**< Reset to 0 on success       */
    uint32_t         total_recoveries;      /**< Bus resets performed so far */
    ICM20948_Cal_t   cal;
} ICM20948_t;

/* Full sensor snapshot — raw and scaled side by side for easy debugging */

typedef struct {
    /* Raw signed 16-bit values exactly as the chip reports them */
    int16_t accel_raw[3];                   /**< X, Y, Z                     */
    int16_t gyro_raw[3];                    /**< X, Y, Z                     */
    int16_t mag_raw[3];                     /**< X, Y, Z (AK09916 LE)        */
    int16_t temp_raw;

    /* Same values converted to physical units, bias/hard-iron applied */
    float   accel_g[3];                     /**< g                           */
    float   gyro_dps[3];                    /**< degrees per second          */
    float   mag_uT[3];                      /**< micro-Tesla                 */
    float   temp_c;                         /**< degrees Celsius             */
} ICM20948_Data_t;

/* ================================  API  =================================== */

/**
 * @brief  Resets the ICM-20948, configures accel/gyro/master I2C and wakes
 *         the AK09916 for auto-read. Leaves current bank set to 0.
 * @param  dev  Pointer to a zero-initialised handle.
 * @note   Call after MX_I2C1_Init(). Verifies WHO_AM_I on both chips.
 */
ICM20948_Status_e ICM20948_Init(ICM20948_t *dev);

/**
 * @brief  Reads the WHO_AM_I register and returns its byte value.
 * @param  dev  Initialised handle.
 * @param  id   Destination for the read byte (should equal 0xEA).
 */
ICM20948_Status_e ICM20948_WhoAmI(ICM20948_t *dev, uint8_t *id);

/**
 * @brief  Averages the gyro output over a short still period and stores the
 *         result in dev->cal.gyro_bias_dps. Subtracted from every subsequent
 *         ICM20948_ReadAll().
 * @param  dev  Initialised handle. Device must be motionless during the call.
 * @note   Duration is ICM_GYRO_CAL_SAMPLES × ICM_GYRO_CAL_DELAY_MS (≈ 1 s).
 */
ICM20948_Status_e ICM20948_CalibrateGyroBias(ICM20948_t *dev);

/**
 * @brief  Reads accel, gyro, temperature and magnetometer in a single 22-byte
 *         I2C transaction and fills the ICM20948_Data_t struct with raw and
 *         physical values.
 *         On an I2C error the driver automatically resets the bus and retries
 *         up to ICM_RECOVERY_RETRIES times before giving up.
 * @param  dev  Initialised handle.
 * @param  out  Destination snapshot.
 */
ICM20948_Status_e ICM20948_ReadAll(ICM20948_t *dev, ICM20948_Data_t *out);

/**
 * @brief  Forces a full bus recovery: de-initialises the STM32 I2C peripheral,
 *         re-initialises it and re-runs ICM20948_Init(). Call this manually if
 *         the sensor is unplugged and reconnected, or if consecutive_i2c_errors
 *         grows unbounded. Returns the init status.
 * @param  dev  Handle to recover.
 * @note   Gyro/mag calibration data stored in dev->cal is preserved.
 */
ICM20948_Status_e ICM20948_Recover(ICM20948_t *dev);

#ifdef ICM_MAG_CAL_ENABLE
/**
 * @brief  Simple figure-8 magnetometer calibration. Captures min/max per axis
 *         over ICM_MAG_CAL_DURATION_MS and stores the midpoint as the
 *         hard-iron offset. Soft-iron matrix is left at identity.
 * @param  dev  Initialised handle. User must slowly rotate the device in all
 *              orientations during the call.
 */
ICM20948_Status_e ICM20948_CalibrateMag(ICM20948_t *dev);
#endif

#endif /* ICM20948_H */
