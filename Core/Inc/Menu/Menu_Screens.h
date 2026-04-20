/**
 * @file    Menu_Screens.h
 * @brief   Internal screen function declarations for the menu system.
 *
 * @details Each screen exposes two functions:
 *          - Draw: renders the screen based on handle state.
 *          - OnButton: reacts to Navigate / Enter presses.
 *
 *          This header is internal — only included by Menu.c and
 *          the individual screen .c files. Do NOT include in main.c.
 *
 * @date    April 13, 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef MENU_SCREENS_H
#define MENU_SCREENS_H

#include "Menu/Menu.h"

/* ========================  MAIN MENU  ==================================== */
/* Implemented in Menu.c */

void Screen_MainMenu_Draw(Menu_Handle_t *h);
void Screen_MainMenu_OnButton(Menu_Handle_t *h, MenuButton_e btn);

/* ========================  TEST HARDWARE  ================================ */

void Screen_TestHW_Draw(Menu_Handle_t *h);
void Screen_TestHW_OnButton(Menu_Handle_t *h, MenuButton_e btn);

/* ========================  BLUETOOTH  ==================================== */

/** @brief Sub-states for the Bluetooth screen. */
typedef enum {
    BT_DISCONNECTED  = 0,   /**< Shows: CONECTAR / VOLVER              */
    BT_CONNECTED     = 1,   /**< Shows: ID:XXXX / RECONECT / VOLVER    */
    BT_ACTION        = 2    /**< Placeholder action text                */
} BtSubState_e;

void Screen_Bluetooth_Draw(Menu_Handle_t *h);
void Screen_Bluetooth_OnButton(Menu_Handle_t *h, MenuButton_e btn);

/* ========================  PROGRAMMING  ================================== */

/** @brief Sub-states for the Programming screen. */
typedef enum {
    PROG_SPLASH      = 0,   /**< Full-screen padlock bitmap (timed)    */
    PROG_CODE_ENTRY  = 1,   /**< PIN digit entry                       */
    PROG_UNLOCKED    = 2,   /**< Shows: MICRO / BT / VOLVER            */
    PROG_ACTION      = 3    /**< Placeholder action text                */
} ProgSubState_e;

void Screen_Programming_Draw(Menu_Handle_t *h);
void Screen_Programming_OnButton(Menu_Handle_t *h, MenuButton_e btn);

/* ========================  EXERCISE  ===================================== */

/** @brief Sub-states for the Exercise screen. */
typedef enum {
    EX_NO_BT         = 0,   /**< No connect + Preview + Salir          */
    EX_PREVIEW       = 1,   /**< Bitmap preview (5 s auto-return)      */
    EX_WAITING       = 2,   /**< Bitmap + "Esperando..."               */
    EX_ACTIVE        = 3    /**< HUD: lives, ammo, battery             */
} ExSubState_e;

void Screen_Exercise_Draw(Menu_Handle_t *h);
void Screen_Exercise_OnButton(Menu_Handle_t *h, MenuButton_e btn);

#endif /* MENU_SCREENS_H */
