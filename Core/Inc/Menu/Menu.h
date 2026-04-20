/**
 * @file    Menu.h
 * @brief   OLED menu system — state machine, types, and public API.
 *
 * @details Two-level state machine for a 64x32 SSD1306 OLED display.
 *          Level 1: which screen is active (MenuScreen_e).
 *          Level 2: internal sub-state managed by each screen's .c file.
 *
 *          Navigation uses two physical buttons (Navigate + Enter).
 *          Button events can come from EXTI ISR or polling.
 *
 * @date    April 13, 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef MENU_H
#define MENU_H

#include "stm32wbxx.h"
#include <stdbool.h>
#include <stdint.h>

/* ========================  BUTTON CONFIGURATION  ========================= */

/**
 * @brief GPIO port and pin for each button. Update to match CubeMX .ioc.
 * @note  Buttons are expected active-LOW (pressed = GPIO_PIN_RESET).
 */
#define BTN_NAVIGATE_PORT       GPIOA
#define BTN_NAVIGATE_PIN        GPIO_PIN_7

#define BTN_ENTER_PORT          GPIOA
#define BTN_ENTER_PIN           GPIO_PIN_8

/** @brief Debounce time in milliseconds for polling mode. */
#define BTN_DEBOUNCE_MS         300U

/* ========================  DISPLAY CONSTANTS  ============================ */

#define MENU_SCREEN_W           64U
#define MENU_SCREEN_H           32U
#define MENU_FONT_W             6U      /**< Font5x7 rendered width (5+1) */
#define MENU_FONT_H             7U      /**< Font5x7 height             */
#define MENU_LINE_H             8U      /**< Line spacing (font + 1px)   */
#define MENU_MAX_CHARS          10U     /**< Max chars per line (64/6)   */

/** @brief Duration in ms for splash/transition screens. */
#define MENU_SPLASH_MS          1200U

/* ========================  PROGRAMMING LOCK  ============================= */

/** @brief 3-digit PIN code required to unlock Programming mode. */
#define MENU_LOCK_CODE_0        1U
#define MENU_LOCK_CODE_1        2U
#define MENU_LOCK_CODE_2        3U
#define MENU_LOCK_DIGITS        3U

/* ========================  ENUMERATIONS  ================================= */

/** @brief Button identifiers. */
typedef enum {
    BTN_NONE     = 0,
    BTN_NAVIGATE = 1,
    BTN_ENTER    = 2
} MenuButton_e;

/** @brief Level-1 screen identifiers. */
typedef enum {
    SCREEN_MAIN_MENU    = 0,
    SCREEN_TEST_HW      = 1,
    SCREEN_BLUETOOTH     = 2,
    SCREEN_PROGRAMMING  = 3,
    SCREEN_EXERCISE     = 4
} MenuScreen_e;

/* ========================  STRUCTURES  =================================== */

/** @brief Central menu handle — passed to every screen function. */
typedef struct {
    /* --- Level 1 state --- */
    MenuScreen_e  screen;           /**< Active screen                      */
    uint8_t       selected;         /**< Cursor / highlighted option        */
    bool          needs_redraw;     /**< True when display must refresh     */

    /* --- Level 2 state --- */
    uint8_t       sub_state;        /**< Internal sub-state per screen      */

    /* --- Global flags --- */
    bool          bt_connected;     /**< BT link established                */
    uint16_t      bt_device_id;     /**< Paired device ID                   */

    /* --- Programming lock --- */
    bool          lock_open;        /**< True after correct PIN entry       */
    uint8_t       code_input[MENU_LOCK_DIGITS]; /**< Digits entered so far */
    uint8_t       code_pos;         /**< Current digit index (0..3)         */

    /* --- Timing --- */
    uint32_t      splash_tick;      /**< HAL_GetTick snapshot for splashes  */

    /* --- Button state (polling) --- */
    uint32_t      last_btn_tick;    /**< Last debounced press timestamp     */

    /* --- Button flags (ISR-safe) --- */
    volatile bool flag_navigate;    /**< Set in ISR, cleared in Update      */
    volatile bool flag_enter;       /**< Set in ISR, cleared in Update      */
} Menu_Handle_t;

/* ================================  API  ================================== */

/**
 * @brief  Initializes the menu handle and draws the main menu.
 * @param  h  Pointer to the menu handle.
 */
void Menu_Init(Menu_Handle_t *h);

/**
 * @brief  Main update loop. Processes pending button flags, dispatches
 *         draw and input handling to the active screen.
 * @param  h  Pointer to the menu handle.
 * @note   Call this every iteration of the main while loop.
 */
void Menu_Update(Menu_Handle_t *h);

/**
 * @brief  Polls the GPIO button pins with debounce and sets button flags.
 * @param  h  Pointer to the menu handle.
 * @note   Use this for testing. Replace with EXTI ISR for production/FreeRTOS.
 */
void Menu_Poll(Menu_Handle_t *h);

/**
 * @brief  Processes a button event. Can be called from ISR or polling.
 * @param  h    Pointer to the menu handle.
 * @param  btn  Which button was pressed.
 */
void Menu_OnButton(Menu_Handle_t *h, MenuButton_e btn);

/**
 * @brief  Navigate to a specific screen, resetting sub-state and cursor.
 * @param  h       Pointer to the menu handle.
 * @param  screen  Target screen.
 */
void Menu_GoTo(Menu_Handle_t *h, MenuScreen_e screen);

#endif /* MENU_H */
