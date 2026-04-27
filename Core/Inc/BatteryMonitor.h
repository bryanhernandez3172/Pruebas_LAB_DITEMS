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

/* Handle and address — must match .ioc */

#define BQ27441_I2C             (&hi2c1)
#define BQ27441_ADDR            (0x55U << 1)    /**< 7-bit 0x55, HAL left-shift */
#define BQ27441_TIMEOUT_MS      50U
#define BQ27441_CFGUPDATE_MS    2000U           /**< Max wait for CFGUPMODE flag */
#define BQ27441_RECOVERY_MS     800U            /**< Lockout after an I2C fault  */

/* ========================  BATTERY PARAMETERS  ============================= */

/* Adjust these values when swapping the battery cell */

#define BAT_DESIGN_CAP_MAH      400U            /**< Design capacity in mAh      */
#define BAT_DESIGN_ENERGY_MWH   1480U           /**< Design energy in mWh        */
#define BAT_TERMINATE_MV        2800U           /**< Terminate voltage in mV     */
#define BAT_TAPER_CURRENT_MA    40U             /**< Taper current in mA         */
#define BAT_TAPER_RATE          ((uint16_t)((BAT_DESIGN_CAP_MAH * 10UL + \
                                  (BAT_TAPER_CURRENT_MA / 2U)) / BAT_TAPER_CURRENT_MA))

/* ========================  MONITOR THRESHOLDS  ============================= */

/* Thresholds used by BatGauge_Update() to classify charge state */

#define BAT_I_THRESH_MA         60U             /**< Current above = charging    */
#define BAT_FULL_V_MV           4180U           /**< Voltage threshold for full  */
#define BAT_FULL_I_MA           20U             /**< Current threshold for full  */

/* ========================  BQ27441 REGISTERS  ============================== */

/* Standard command registers (16-bit read) */

#define BQ27441_REG_CONTROL     0x00U
#define BQ27441_REG_VOLTAGE     0x04U           /**< Voltage in mV               */
#define BQ27441_REG_FLAGS       0x06U           /**< Status flags                */
#define BQ27441_REG_RM          0x0CU           /**< Remaining capacity mAh      */
#define BQ27441_REG_FCC         0x0EU           /**< Full charge capacity mAh    */
#define BQ27441_REG_AVG_CURR    0x10U           /**< Average current mA (signed) */
#define BQ27441_REG_SOC         0x1CU           /**< State of charge %           */
#define BQ27441_REG_SOH         0x20U           /**< State of health %           */

/* Control sub-commands (write 16-bit to REG_CONTROL) */

#define BQ27441_CTRL_DEVICE_TYPE        0x0001U
#define BQ27441_CTRL_BAT_INSERT         0x000CU
#define BQ27441_CTRL_CLEAR_HIBERNATE    0x0012U
#define BQ27441_CTRL_SET_CFGUPDATE      0x0013U
#define BQ27441_CTRL_IT_ENABLE          0x0021U
#define BQ27441_CTRL_SEALED             0x0020U
#define BQ27441_CTRL_SOFT_RESET         0x0042U

/* Extended data commands (CFGUPDATE mode) */

#define BQ27441_EXT_BLOCK_CTRL          0x61U   /**< BlockDataControl            */
#define BQ27441_EXT_DATA_CLASS          0x3EU   /**< DataClass                   */
#define BQ27441_EXT_DATA_BLOCK          0x3FU   /**< DataBlock                   */
#define BQ27441_EXT_BLOCK_DATA          0x40U   /**< BlockData base (32 bytes)   */
#define BQ27441_EXT_CHECKSUM            0x60U   /**< BlockDataChecksum           */

/* State subclass (ID 82) byte offsets */

#define BQ27441_CLASS_STATE             82U
#define BQ27441_STATE_DESIGN_CAP        10U     /**< Design Capacity (mAh)       */
#define BQ27441_STATE_DESIGN_ENE        12U     /**< Design Energy (mWh)         */
#define BQ27441_STATE_TERM_VOLT         16U     /**< Terminate Voltage (mV)      */
#define BQ27441_STATE_TAPER_RATE        27U     /**< Taper Rate                  */

/* FLAGS register bit masks (0x06) */

#define BQ27441_FLAG_CFGUPMODE          (1U << 4)   /**< Config update mode      */
#define BQ27441_FLAG_ITPOR              (1U << 5)   /**< IT power-on reset       */

/* CONTROL_STATUS bit masks */

#define BQ27441_STATUS_SS               (1U << 13)  /**< Sealed state            */

/* Unseal keys (factory default) */

#define BQ27441_UNSEAL_KEY              0x8000U

/* Expected device type ID */

#define BQ27441_DEVICE_TYPE_ID          0x0421U

/* ========================  ENUMERATIONS  =================================== */

/* Battery charge state as determined by BatGauge_Update() */

typedef enum {
    BAT_CHARGE_QUIET        = 0,
    BAT_CHARGE_CHARGING     = 1,
    BAT_CHARGE_DISCHARGING  = 2,
} BatChargeState_t;

/* ============================  STRUCTURES  ================================= */

/* Live data snapshot updated by every successful BatGauge_Update() call */

typedef struct {
    uint16_t         voltage_mV;        /**< Battery voltage in mV           */
    int16_t          avg_current_mA;    /**< Average current in mA (signed)  */
    uint16_t         soc_pct;           /**< State of charge 0–100 %         */
    uint16_t         remaining_mAh;     /**< Remaining capacity in mAh       */
    uint16_t         full_cap_mAh;      /**< Full charge capacity in mAh     */
    uint8_t          soh_pct;           /**< State of health 0–100 %         */
    uint16_t         flags;             /**< Raw FLAGS register (0x06)       */
    BatChargeState_t charge_state;      /**< QUIET / CHARGING / DISCHARGING  */
    uint8_t          is_full;           /**< 1 when battery is full          */
    uint8_t          is_ready;          /**< 1 when last read was valid      */
} BatGauge_Data_t;

/* ================================  API  ==================================== */

/**
 * @brief  Verifies I2C communication and runs the initial configuration.
 * @note   Call once after MX_I2C1_Init(). Always runs BatGauge_Configure()
 *         regardless of NVM state, matching the validated Arduino flow.
 */
HAL_StatusTypeDef BatGauge_Init(void);

/**
 * @brief  State-machine update — call every ~3 s from the main loop.
 * @note   Checks gauge health, re-configures on ITPOR, recovers the I2C bus
 *         on faults, reads all registers, and fills the data structure.
 *         Sets data->is_ready = 0 when the gauge is not reachable or the
 *         read returns an invalid (0xFFFF) value.
 */
void BatGauge_Update(BatGauge_Data_t *data);

/**
 * @brief  Configures NVM parameters (capacity, energy, voltages).
 * @note   Unseals, enters CFGUPDATE, writes the BAT_* macros, exits, and
 *         sends the post-config sequence (CLEAR_HIBERNATE, IT_ENABLE,
 *         BAT_INSERT). Called automatically by Init(); can be re-run
 *         manually if needed.
 */
HAL_StatusTypeDef BatGauge_Configure(void);

#endif /* BATTERYMONITOR_H */
