/**
 * @file    BatteryMonitor.h
 * @brief   Driver for BQ27441-G1 fuel gauge (I2C) + analog battery level (ADC).
 *
 * @details The BQ27441-G1 is a single-cell Li-Ion fuel gauge that reports
 *          voltage, current, state of charge, capacity, temperature, and
 *          state of health over I2C.
 *
 *          Additionally, an external potentiometer voltage divider provides
 *          a secondary analog battery reading through an ADC channel.
 *
 *          CubeMX configuration:
 *          - I2C: enable the I2C peripheral connected to the BQ27441 (SDA/SCL).
 *          - ADC: enable the ADC channel connected to the potentiometer wiper.
 *            Resolution 12 bits, single conversion, sampling >= 84 cycles.
 *
 * @date    April 15, 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef BATTERYMONITOR_H
#define BATTERYMONITOR_H

#include "stm32wbxx_hal.h"
#include <stdbool.h>

/* ========================  I2C CONFIGURATION  ============================== */

#define BQ27441_I2C             (&hi2c1)
#define BQ27441_ADDR            (0x55U << 1)  /**< 7-bit 0x55, shifted for HAL */
#define BQ27441_TIMEOUT_MS      50U

/* ========================  ADC CONFIGURATION  ============================== */

#define BAT_ADC_HANDLE          (&hadc1)
#define BAT_ADC_CHANNEL         ADC_CHANNEL_0   /**< Update to match .ioc */
#define BAT_ADC_MAX             4095U
#define BAT_ADC_TIMEOUT_MS      10U

/**
 * @brief  Reference voltage in mV and resistor divider ratio.
 * @note   V_bat = (ADC_raw / 4095) * VREF_MV * BAT_DIV_RATIO
 *         Adjust BAT_DIV_RATIO to match your divider (e.g. 2.0 for equal R).
 */
#define BAT_VREF_MV             3300U
#define BAT_DIV_NUM             2U    /**< Numerator of divider ratio   */
#define BAT_DIV_DEN             1U    /**< Denominator of divider ratio */

/* ========================  BATTERY PARAMETERS  ============================= */

/** @brief Change these values if you swap the battery cell. */
#define BAT_DESIGN_CAP_MAH      400U    /**< Design capacity in mAh          */
#define BAT_DESIGN_ENERGY_MWH   1480U   /**< Design energy in mWh            */
#define BAT_TERMINATE_MV        3200U   /**< Terminate voltage in mV         */
#define BAT_TAPER_CURRENT_MA    40U     /**< Taper current in mA             */
#define BAT_TAPER_RATE          ((uint16_t)((BAT_DESIGN_CAP_MAH * 10UL + (BAT_TAPER_CURRENT_MA / 2U)) / BAT_TAPER_CURRENT_MA))

/* ========================  BQ27441 REGISTERS  ============================== */

/** @brief Standard commands (16-bit read at register address). */
#define BQ27441_REG_CONTROL     0x00U
#define BQ27441_REG_TEMP        0x02U  /**< Temperature in 0.1 K          */
#define BQ27441_REG_VOLTAGE     0x04U  /**< Voltage in mV                 */
#define BQ27441_REG_FLAGS       0x06U  /**< Status flags                  */
#define BQ27441_REG_NOM_CAP     0x08U  /**< Nominal available capacity mAh*/
#define BQ27441_REG_FULL_CAP    0x0AU  /**< Full available capacity mAh   */
#define BQ27441_REG_RM          0x0CU  /**< Remaining capacity mAh        */
#define BQ27441_REG_FCC         0x0EU  /**< Full charge capacity mAh      */
#define BQ27441_REG_AVG_CURR    0x10U  /**< Average current mA (signed)   */
#define BQ27441_REG_STDBY_CURR  0x12U  /**< Standby current mA            */
#define BQ27441_REG_MAX_CURR    0x14U  /**< Max load current mA           */
#define BQ27441_REG_AVG_PWR     0x18U  /**< Average power mW (signed)     */
#define BQ27441_REG_SOC         0x1CU  /**< State of charge %             */
#define BQ27441_REG_INT_TEMP    0x1EU  /**< Internal temperature 0.1 K    */
#define BQ27441_REG_SOH         0x20U  /**< State of health %             */

/** @brief Control sub-commands (write 16-bit to REG_CONTROL). */
#define BQ27441_CTRL_STATUS         0x0000U
#define BQ27441_CTRL_DEVICE_TYPE    0x0001U
#define BQ27441_CTRL_FW_VERSION     0x0002U
#define BQ27441_CTRL_BAT_INSERT     0x000CU
#define BQ27441_CTRL_CLEAR_HIBERNATE 0x0012U
#define BQ27441_CTRL_SET_CFGUPDATE  0x0013U
#define BQ27441_CTRL_IT_ENABLE      0x0021U
#define BQ27441_CTRL_SEALED         0x0020U
#define BQ27441_CTRL_SOFT_RESET     0x0042U

/** @brief Extended data commands for CFGUPDATE mode. */
#define BQ27441_EXT_BLOCK_CTRL      0x61U  /**< BlockDataControl             */
#define BQ27441_EXT_DATA_CLASS      0x3EU  /**< DataClass                    */
#define BQ27441_EXT_DATA_BLOCK      0x3FU  /**< DataBlock                    */
#define BQ27441_EXT_BLOCK_DATA      0x40U  /**< BlockData (32 bytes)         */
#define BQ27441_EXT_CHECKSUM        0x60U  /**< BlockDataChecksum            */

/** @brief Data class and offsets for State subclass (82). */
#define BQ27441_CLASS_STATE         82U
#define BQ27441_STATE_DESIGN_CAP    10U    /**< Offset: Design Capacity      */
#define BQ27441_STATE_DESIGN_ENE    12U    /**< Offset: Design Energy        */
#define BQ27441_STATE_TERM_VOLT     16U    /**< Offset: Terminate Voltage    */
#define BQ27441_STATE_TAPER_RATE    27U    /**< Offset: Taper Rate           */

/** @brief FLAGS register bit masks (register 0x06). */
#define BQ27441_FLAG_DSG            (1U << 0)   /**< Discharging                  */
#define BQ27441_FLAG_SOCF           (1U << 1)   /**< State-of-charge final alarm  */
#define BQ27441_FLAG_SOC1           (1U << 2)   /**< State-of-charge threshold 1  */
#define BQ27441_FLAG_BAT_DET        (1U << 3)   /**< Battery detected             */
#define BQ27441_FLAG_CFGUPMODE      (1U << 4)   /**< Config update mode active    */
#define BQ27441_FLAG_ITPOR          (1U << 5)   /**< IT power-on reset            */
#define BQ27441_FLAG_OCVTAKEN       (1U << 7)   /**< OCV measurement taken        */
#define BQ27441_FLAG_CHG            (1U << 8)   /**< Fast charging allowed        */
#define BQ27441_FLAG_FC             (1U << 9)   /**< Full charge                  */
#define BQ27441_FLAG_UT             (1U << 14)  /**< Under-temperature            */
#define BQ27441_FLAG_OT             (1U << 15)  /**< Over-temperature             */

/** @brief CONTROL_STATUS bit masks (response to sub-command 0x0000). */
#define BQ27441_STATUS_VOK          (1U << 1)   /**< Voltage OK for Qmax update   */
#define BQ27441_STATUS_RUP_DIS      (1U << 2)   /**< Ra update disabled           */
#define BQ27441_STATUS_LDMD         (1U << 3)   /**< Load mode                    */
#define BQ27441_STATUS_SLEEP        (1U << 4)   /**< Sleep mode active            */
#define BQ27441_STATUS_HIBERNATE    (1U << 6)   /**< Hibernate mode active        */
#define BQ27441_STATUS_INITCOMP     (1U << 7)   /**< Initialization complete      */
#define BQ27441_STATUS_RES_UP       (1U << 8)   /**< Resistance update            */
#define BQ27441_STATUS_QMAX_UP      (1U << 9)   /**< Qmax update                  */
#define BQ27441_STATUS_BCA          (1U << 10)  /**< Board calibration active     */
#define BQ27441_STATUS_CCA          (1U << 11)  /**< CC calibration active        */
#define BQ27441_STATUS_CALMODE      (1U << 12)  /**< Calibration mode             */
#define BQ27441_STATUS_SS           (1U << 13)  /**< Sealed state                 */
#define BQ27441_STATUS_WDRESET      (1U << 14)  /**< Watchdog reset               */
#define BQ27441_STATUS_SHUTDOWNEN   (1U << 15)  /**< Shutdown enabled             */

/** @brief Unseal keys (default from factory). */
#define BQ27441_UNSEAL_KEY_A        0x8000U
#define BQ27441_UNSEAL_KEY_B        0x8000U

/** @brief Expected device type ID for BQ27441-G1. */
#define BQ27441_DEVICE_TYPE_ID      0x0421U

/* ========================  STRUCTURES  ===================================== */

/** @brief Complete battery status from BQ27441. */
typedef struct {
    uint16_t voltage_mV;       /**< Battery voltage in mV              */
    int16_t  avg_current_mA;   /**< Average current in mA (signed)     */
    uint16_t soc_pct;          /**< State of charge 0-100 %            */
    uint16_t remaining_mAh;    /**< Remaining capacity in mAh          */
    uint16_t full_cap_mAh;     /**< Full charge capacity in mAh        */
    uint16_t soh_pct;          /**< State of health 0-100 %            */
    int16_t  temp_c10;         /**< Temperature in 0.1 °C              */
    uint16_t flags;            /**< Raw FLAGS register (0x06)          */
    uint16_t ctrl_status;      /**< Raw CONTROL_STATUS (sub-cmd 0x0000)*/
} BatGauge_Data_t;

/* ================================  API  ==================================== */

/**
 * @brief  Verifies communication with the BQ27441 by reading device type.
 * @return HAL_OK if device responds and ID matches 0x0421.
 */
HAL_StatusTypeDef BatGauge_Init(void);

/**
 * @brief  Reads all key registers into the data structure.
 * @param  data  Pointer to BatGauge_Data_t to fill.
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef BatGauge_ReadAll(BatGauge_Data_t *data);

/**
 * @brief  Reads a single 16-bit standard command register.
 * @param  reg    Register address.
 * @param  value  Pointer to store the result.
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef BatGauge_ReadReg(uint8_t reg, uint16_t *value);

/**
 * @brief  Sends a control sub-command and reads the 16-bit response.
 * @param  subcmd  Sub-command ID (e.g. BQ27441_CTRL_DEVICE_TYPE).
 * @param  value   Pointer to store the response (NULL to skip read).
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef BatGauge_Control(uint16_t subcmd, uint16_t *value);

/**
 * @brief  Issues a soft reset to the BQ27441.
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef BatGauge_SoftReset(void);

/**
 * @brief  Configures the BQ27441 with the battery parameters defined above.
 * @details Unseals the device, enters CFGUPDATE mode, writes Design Capacity,
 *          Design Energy, Terminate Voltage, and Taper Rate, then seals back.
 *          Only needs to run once (values persist in NVM). Called automatically
 *          by BatGauge_Init() when the ITPOR flag is detected.
 * @param  itpor  Pass true if ITPOR was set; triggers BAT_INSERT + extra reset.
 * @return HAL_OK on success.
 */
HAL_StatusTypeDef BatGauge_Configure(bool itpor);

/**
 * @brief  Reads the battery voltage from the ADC potentiometer divider.
 * @return Estimated battery voltage in mV (0 on ADC error).
 */
uint32_t BatAdc_ReadVoltage_mV(void);

/**
 * @brief  Reads the raw ADC value from the potentiometer.
 * @return Raw 12-bit ADC value (0-4095), 0 on error.
 */
uint16_t BatAdc_ReadRaw(void);

#endif /* BATTERYMONITOR_H */
