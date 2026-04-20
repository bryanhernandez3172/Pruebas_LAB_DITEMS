/**
 * @file    GPS.h
 * @brief   Driver for L86-M33 GPS module over UART on STM32F4xx.
 *
 * @details NMEA 0183 sentence parser with MTK command interface.
 *          Parses GPRMC and GPGGA sentences to extract position, altitude,
 *          speed, course, satellite count, fix quality, and UTC date/time.
 *
 *          Reception model:
 *          - ISR accumulates bytes via Gps_StoreByte() until a complete
 *            sentence (terminated by '\n') is detected.
 *          - Main loop calls Gps_Process() to parse the buffered sentence
 *            and update the GPS data structure.
 *
 *          MTK command interface:
 *          - Gps_SendMTK() sends any arbitrary PMTK command with automatic
 *            checksum calculation.  Use directly with the GPS_CMD_* / GPS_OUTPUT_*
 *            / GPS_FIX_* defines below.
 *
 *          CubeMX configuration:
 *          - UART peripheral in Asynchronous mode at 9600 baud (L86 default).
 *          - No hardware flow control required.
 *          - Enable UART global interrupt in NVIC.
 *
 * @date    April 16, 2026
 * @author  César Pérez
 * @version 3.0.0
 */

#ifndef GPS_H
#define GPS_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32wbxx_hal.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================  CONFIGURATION  ================================= */

/* UART handle — update to match CubeMX .ioc */

#define GPS_UART                (&huart1)

/* FORCE_ON pin — keeps the module awake (active HIGH on L86-M33) */

#define GPS_FORCE_ON_PORT       GPIOA
#define GPS_FORCE_ON_PIN        GPIO_PIN_12

/* Timezone offset (hours) applied to the parsed UTC time.
 * Examples:  CST México = -6,  CET Europe = +1,  JST Japan = +9 */

#define GPS_UTC_OFFSET_H        (-6)

/* Uncomment to enable debug diagnostics (counters, raw buffer, last sentence) */
// #define GPS_DEBUG

/* Timeouts */

#define GPS_TX_TIMEOUT_MS       500U    /**< Transmit timeout in ms          */

/* NMEA buffer sizes */

#define GPS_SENTENCE_MAX_LEN    120U    /**< Max NMEA sentence length        */
#define GPS_POSITION_STR_LEN    32U     /**< "lat,lon" string buffer         */

/* Wiring check */

#define GPS_WIRING_TIMEOUT_MS   5000U   /**< Time window for wiring check   */
#define GPS_WIRING_MIN_CHARS    10U     /**< Minimum chars expected          */

/* ========================  MTK OUTPUT PRESETS  ============================= */
/*
 * PMTK314 fields (in order):
 *   GLL, RMC, VTG, GGA, GSA, GSV, (10 reserved), MCHN
 * Value: 0=off, 1=every fix, 2=every other fix, etc.
 */

/** Only RMC + GGA (position + altitude + satellites) */
#define GPS_OUTPUT_RMC_GGA      "PMTK314,0,1,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"

/** Only RMC (minimum recommended: pos + speed + date) */
#define GPS_OUTPUT_RMC_ONLY     "PMTK314,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0"

/** All sentences at 1 Hz */
#define GPS_OUTPUT_ALL          "PMTK314,1,1,1,1,1,1,0,0,0,0,0,0,0,0,0,0,0,0,0"

/** Restore factory default output */
#define GPS_OUTPUT_DEFAULT      "PMTK314,-1"

/* ========================  MTK FIX RATE  ================================== */

#define GPS_FIX_0_5HZ           "PMTK220,2000"  /**< 1 fix every 2 seconds  */
#define GPS_FIX_1HZ             "PMTK220,1000"  /**< 1 fix per second       */
#define GPS_FIX_5HZ             "PMTK220,200"   /**< 5 fixes per second     */
#define GPS_FIX_10HZ            "PMTK220,100"   /**< 10 fixes per second    */

/* ========================  MTK COMMANDS  ================================== */

/* Restart */
#define GPS_CMD_HOT_RESTART     "PMTK101"       /**< Hot restart (keep almanac + ephemeris)   */
#define GPS_CMD_WARM_RESTART    "PMTK102"       /**< Warm restart (keep almanac)              */
#define GPS_CMD_COLD_RESTART    "PMTK103"       /**< Cold restart (clear all)                 */
#define GPS_CMD_FULL_COLD       "PMTK104"       /**< Full cold restart + reset to factory     */

/* Standby */
#define GPS_CMD_STANDBY         "PMTK161,0"     /**< Enter standby mode (wake with any byte)  */

/* Baud rate */
#define GPS_CMD_BAUD_9600       "PMTK251,9600"      /**< Set UART to 9600 baud   */
#define GPS_CMD_BAUD_38400      "PMTK251,38400"     /**< Set UART to 38400 baud  */
#define GPS_CMD_BAUD_57600      "PMTK251,57600"     /**< Set UART to 57600 baud  */
#define GPS_CMD_BAUD_115200     "PMTK251,115200"    /**< Set UART to 115200 baud */

/* AIC — Active Interference Cancellation */
#define GPS_CMD_AIC_ON          "PMTK286,1"     /**< Enable AIC              */
#define GPS_CMD_AIC_OFF         "PMTK286,0"     /**< Disable AIC             */

/* Constellation search mode */
#define GPS_CMD_GPS_ONLY        "PMTK353,1,0,0,0,0" /**< GPS only            */
#define GPS_CMD_GPS_GLONASS     "PMTK353,1,1,0,0,0" /**< GPS + GLONASS       */

/* Query firmware version */
#define GPS_CMD_FW_VERSION      "PMTK605"       /**< Query firmware release  */

/* ========================  ENUMERATIONS  ================================== */

typedef enum {
    GPS_OK              = 0,    /**< Operation successful                    */
    GPS_ERR_PARAM,              /**< Invalid or NULL parameter               */
    GPS_ERR_UART,               /**< UART communication error                */
    GPS_ERR_TIMEOUT,            /**< Operation timed out                     */
    GPS_ERR_NO_FIX,             /**< No valid fix available                  */
    GPS_ERR_CHECKSUM,           /**< NMEA checksum mismatch                  */
    GPS_ERR_PARSE,              /**< Sentence format error                   */
    GPS_ERR_OVERFLOW            /**< Buffer overflow                         */
} GpsStatus_e;

typedef enum {
    GPS_FIX_INVALID     = 0,    /**< No fix                                 */
    GPS_FIX_GPS         = 1,    /**< Standard GPS fix                       */
    GPS_FIX_DGPS        = 2,    /**< Differential GPS fix                   */
} GpsFix_e;

/* ============================  STRUCTURES  ================================ */

/** @brief Parsed GPS data — clean, production-ready fields. */
typedef struct {
    /* Position */
    double   latitude;          /**< Degrees (+ North / - South)             */
    double   longitude;         /**< Degrees (+ East  / - West)              */
    double   altitude;          /**< Meters above mean sea level             */
    bool     position_valid;    /**< true if current fix is reliable         */

    /* Motion */
    double   speed_kmh;         /**< Speed over ground in km/h              */
    double   course;            /**< Course over ground in degrees (0-360)  */

    /* Satellites and fix */
    uint8_t  satellites;        /**< Number of satellites in use             */
    GpsFix_e fix_quality;       /**< Fix type (invalid / GPS / DGPS)        */
    double   hdop;              /**< Horizontal dilution of precision        */

    /* Date and time (LOCAL — UTC + GPS_UTC_OFFSET_H) */
    uint16_t year;              /**< Year  (e.g. 2026)                       */
    uint8_t  month;             /**< Month (1–12)                            */
    uint8_t  day;               /**< Day   (1–31)                            */
    uint8_t  hour;              /**< Hour  (0–23)                            */
    uint8_t  minute;            /**< Minute (0–59)                           */
    uint8_t  second;            /**< Second (0–59)                           */
    bool     datetime_valid;    /**< true if date/time is reliable           */

    /* Formatted string: "lat,lon" ready to send/log */
    char     position_str[GPS_POSITION_STR_LEN];
} GpsData_t;

#ifdef GPS_DEBUG
/** @brief Debug diagnostics — only compiled when GPS_DEBUG is defined. */
typedef struct {
    uint32_t chars_processed;       /**< Total bytes fed to the parser       */
    uint32_t sentences_ok;          /**< Sentences with good checksum        */
    uint32_t sentences_fail;        /**< Sentences with bad checksum         */
    char     last_sentence[GPS_SENTENCE_MAX_LEN]; /**< Last parsed sentence  */
    char     raw[256];              /**< Circular raw byte buffer            */
    uint16_t raw_idx;               /**< Write index into raw[]              */
} GpsDebug_t;
#endif

/** @brief GPS driver control handle. */
typedef struct {
    UART_HandleTypeDef *huart;          /**< CubeMX-generated UART handle    */

    /* ISR sentence accumulation */
    char     sentence[GPS_SENTENCE_MAX_LEN]; /**< Current sentence buffer    */
    uint16_t sentence_idx;              /**< Write index into sentence[]     */
    bool     sentence_ready;            /**< true when '\n' received         */

    /* Parsed output */
    GpsData_t data;                     /**< Latest parsed GPS data          */

#ifdef GPS_DEBUG
    GpsDebug_t debug;                   /**< Debug diagnostics               */
#endif

    /* ISR byte landing zone */
    uint8_t  rx_byte;                   /**< Last byte from UART interrupt   */
} Gps_Handle_t;

/* ================================  API  =================================== */

/* --- Initialization --- */

/**
 * @brief  Initializes the GPS handle, asserts FORCE_ON, arms UART interrupt.
 * @param  h  Pointer to the GPS handle.
 */
GpsStatus_e Gps_Init(Gps_Handle_t *h);

/* --- Main-loop processing --- */

/**
 * @brief  Parses a completed NMEA sentence and updates h->data.
 * @param  h  Pointer to the GPS handle.
 * @note   Call in the main loop. Returns GPS_OK when new data is available,
 *         GPS_ERR_NO_FIX if no sentence was pending.
 */
GpsStatus_e Gps_Process(Gps_Handle_t *h);

/* --- ISR interface --- */

/**
 * @brief  Accumulates one received byte into the sentence buffer.
 * @param  h  Pointer to the GPS handle.
 * @note   Call from HAL_UART_RxCpltCallback. Re-arms the interrupt.
 */
void Gps_StoreByte(Gps_Handle_t *h);

/* --- MTK command interface --- */

/**
 * @brief  Sends an MTK command string with automatic checksum and framing.
 * @param  h    Pointer to the GPS handle.
 * @param  cmd  Command body without '$' or '*XX\r\n'.
 *              Use GPS_CMD_*, GPS_OUTPUT_*, or GPS_FIX_* defines.
 * @note   Builds the full sentence: $cmd*XX\r\n and transmits it.
 */
GpsStatus_e Gps_SendMTK(Gps_Handle_t *h, const char *cmd);

/* --- Utilities --- */

/**
 * @brief  Checks if the GPS module is physically connected (bytes arriving).
 * @param  h  Pointer to the GPS handle.
 */
bool Gps_CheckWiring(Gps_Handle_t *h);

/**
 * @brief  Resets the sentence buffer and parsed data to defaults.
 * @param  h  Pointer to the GPS handle.
 */
void Gps_Reset(Gps_Handle_t *h);

/**
 * @brief  Formats latitude and longitude into h->data.position_str as "lat,lon".
 * @param  h  Pointer to the GPS handle.
 * @note   Called automatically by Gps_Process() when a valid fix is obtained.
 */
void Gps_FormatPosition(Gps_Handle_t *h);

/* --- FORCE_ON pin control --- */

/**
 * @brief  Asserts FORCE_ON pin HIGH — keeps the L86-M33 in full-power mode.
 */
void Gps_ForceOn(void);

/**
 * @brief  De-asserts FORCE_ON pin LOW — allows the module to enter standby.
 */
void Gps_ForceOff(void);

#ifdef __cplusplus
}
#endif

#endif /* GPS_H */
