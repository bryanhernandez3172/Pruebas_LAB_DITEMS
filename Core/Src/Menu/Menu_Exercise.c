/**
 * @file    Menu_Exercise.c
 * @brief   Exercise screen — game HUD with BT requirement.
 *
 * @details Flow:
 *          - If bt_connected == false → "No connect" + Preview + Salir
 *            Preview shows bitmap for 5 seconds then returns.
 *          - If bt_connected == true  → bitmap + "Esperando..." (awaits LoRa)
 *          - EX_ACTIVE (future)       → HUD with lives, ammo, battery %
 *
 * @date    April 14, 2026
 * @author  César Pérez
 * @version 1.1.0
 */

#include "Menu/Menu_Screens.h"
#include "Display_Oled/Display_Comands.h"
#include "Display_Oled/Display_Fonts.h"
#include "Display_Oled/Display_Bitmaps.h"
#include <stdio.h>

/* ========================  CONSTANTS  ====================================== */

#define EX_NO_BT_OPTS       2U          /**< Selectable options: Preview, Salir */
#define EX_PREVIEW_MS       5000U       /**< Preview duration in ms             */

static const char *ex_opts[EX_NO_BT_OPTS] = {
    "Preview",
    "Salir"
};

/* ========================  PLACEHOLDER GAME DATA  ========================== */

static uint8_t  game_lives   = 3U;
static uint8_t  game_ammo    = 30U;
static uint8_t  game_battery = 85U;

/* ========================  HUD GEOMETRY (64×32 display)  =================== */
/*
 *  Bottom row layout (y measured from top):
 *
 *  |<-- 19 -->|<-- 20 -->|     |<----- 27 ----->|
 *  | Box 1    | Box 2    |     | Box 3           |
 *  | (0,22)   | (19,22)  |     | (37,23)         |
 *  |  h=10    |  h=10    |     |  h=9            |
 *  +----------+----------+-----+-----------------+  y=32
 *
 *  Box 1 & 2: 2-digit number, Font5x7, centered.
 *  Box 3:     4-char word, Font6x8, 1 px above bottom edge.
 */

#define HUD_BOX1_X   0
#define HUD_BOX1_Y   22       /* 32 - 10 */
#define HUD_BOX1_W   19
#define HUD_BOX1_H   10

#define HUD_BOX2_X   18
#define HUD_BOX2_Y   22       /* 32 - 10 */
#define HUD_BOX2_W   20
#define HUD_BOX2_H   10

#define HUD_BOX3_X   36      /* 64 - 28 */
#define HUD_BOX3_Y   23       /* 32 - 9  */
#define HUD_BOX3_W   27
#define HUD_BOX3_H   9

#define HUD_BOX4_X   41       /* from bottom-left: x=41              */
#define HUD_BOX4_Y   10       /* from bottom-left: y=22 → screen 9-5 */
#define HUD_BOX4_W   21
#define HUD_BOX4_H   6

/**
 * @brief  Writes a max-2-digit number centered inside a HUD box using Font5x7.
 * @param  box_x  Top-left X of the box.
 * @param  box_y  Top-left Y of the box.
 * @param  box_w  Width of the box.
 * @param  box_h  Height of the box.
 * @param  value  Number to display (0–99).
 */
static void HUD_DrawNumber(int16_t box_x, int16_t box_y,
                           int16_t box_w, int16_t box_h,
                           uint8_t value)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%u", (value > 99U) ? 99U : value);

    uint16_t tw = ssd1306_getStringWidth(buf, &Font4x6);
    int16_t  x  = box_x + (box_w - (int16_t)tw) / 2;
    int16_t  y  = box_y + (box_h - 7) / 2;        /* 7 = Font5x7 height */

    ssd1306_setCursor(x, y);
    ssd1306_print(buf, &Font4x6);
}

/**
 * @brief  Writes a percentage (0%–100%) centered in Box 4 using Font4x6.
 * @param  pct  Percentage value (0–100).
 */
static void HUD_DrawPercent(uint8_t pct)
{
    char buf[5];
    snprintf(buf, sizeof(buf), "%u%%", (pct > 100U) ? 100U : pct);

    uint16_t tw = ssd1306_getStringWidth(buf, &Font4x6);
    int16_t  x  = HUD_BOX4_X + (HUD_BOX4_W - (int16_t)tw) / 2;
    int16_t  y  = HUD_BOX4_Y + (HUD_BOX4_H - 6) / 2;  /* 6 = Font4x6 height */

    ssd1306_setCursor(x, y);
    ssd1306_print(buf, &Font4x6);
}

/**
 * @brief  Writes a max-4-char word in Box 3 using Font6x8, 1 px above bottom.
 * @param  str  String to display (max 4 characters).
 */
static void HUD_DrawLabel(const char *str)
{
    /* y = bottom of box - font height - 1 px margin */
    int16_t y = (HUD_BOX3_Y + HUD_BOX3_H) - 8 - 1;  /* 8 = Font6x8 height */

    ssd1306_setCursor(HUD_BOX3_X, y);
    ssd1306_print(str, &Font6x8);
}

/* ========================  BATTERY BAR (42,3  17×4)  ====================== */

#define BAT_BAR_X   42
#define BAT_BAR_Y   3
#define BAT_BAR_W   17
#define BAT_BAR_H   4

/**
 * @brief  Debug: blinks the battery bar 5 times (blocking).
 */
static void HUD_BatParpadeo(void)
{
    for (uint8_t i = 0U; i < 5U; i++) {
        ssd1306_fillRect(BAT_BAR_X, BAT_BAR_Y, BAT_BAR_W, BAT_BAR_H, BLACK);
        ssd1306_display();
        HAL_Delay(180U);

        ssd1306_fillRect(BAT_BAR_X, BAT_BAR_Y, BAT_BAR_W, BAT_BAR_H, WHITE);
        ssd1306_display();
        HAL_Delay(180U);
    }
}

/**
 * @brief  Battery discharge + charge animation (blocking).
 *         Discharge: erases one column at a time from right to left (180 ms/step).
 *         Charge:    draws  one column at a time from left to right (180 ms/step).
 *         Ends with 5 blinks.
 */
static void HUD_BatDesvanecimiento(void)
{
    /* Discharge: right → left */
    for (int16_t col = BAT_BAR_W - 1; col >= 0; col--) {
        ssd1306_drawLine(BAT_BAR_X + col, BAT_BAR_Y,
                         BAT_BAR_X + col, BAT_BAR_Y + BAT_BAR_H - 1, BLACK);
        ssd1306_display();
        HAL_Delay(180U);
    }

    /* Charge: left → right */
    for (int16_t col = 0; col < BAT_BAR_W; col++) {
        ssd1306_drawLine(BAT_BAR_X + col, BAT_BAR_Y,
                         BAT_BAR_X + col, BAT_BAR_Y + BAT_BAR_H - 1, WHITE);
        ssd1306_display();
        HAL_Delay(180U);
    }

    /* Blink after full charge */
    HUD_BatParpadeo();
}

/* ========================  DRAW  =========================================== */

void Screen_Exercise_Draw(Menu_Handle_t *h)
{
    ssd1306_clearDisplay();
    ssd1306_setTextSize(1U);
    ssd1306_setTextColor(WHITE);

    switch ((ExSubState_e)h->sub_state) {

    case EX_NO_BT:
        /* Header (not selectable) */
        ssd1306_setCursor(0, 0);
        ssd1306_print("No connect", &Font5x7);

        /* Selectable options */
        for (uint8_t i = 0U; i < EX_NO_BT_OPTS; i++) {
            int16_t y = (int16_t)((i + 1U) * MENU_LINE_H);
            ssd1306_setCursor(0, y);
            ssd1306_print((i == h->selected) ? "> " : "  ", &Font5x7);
            ssd1306_print(ex_opts[i], &Font5x7);
        }
        break;

    case EX_PREVIEW: {
        /* Bitmap + HUD text */
        ssd1306_drawBitmap(0, 0, bitmap_Modo_Ejercicio,
                           MENU_SCREEN_W, MENU_SCREEN_H, WHITE);

        ssd1306_setTextSize(1U);
        ssd1306_setTextColor(WHITE);
        HUD_DrawNumber(HUD_BOX1_X, HUD_BOX1_Y, HUD_BOX1_W, HUD_BOX1_H, 50U);
        HUD_DrawNumber(HUD_BOX2_X, HUD_BOX2_Y, HUD_BOX2_W, HUD_BOX2_H, 10U);
        HUD_DrawLabel("AZUL");
        HUD_DrawPercent(85U);
        ssd1306_display();

        /* Battery bar animation (blocking) */
        HUD_BatDesvanecimiento();

        h->sub_state    = (uint8_t)EX_NO_BT;
        h->selected     = 0U;
        h->needs_redraw = true;
        break;
    }

    case EX_WAITING:
        /* Bitmap + "Esperando..." at bottom */
        ssd1306_drawBitmap(0, 0, bitmap_Modo_Ejercicio,
                           MENU_SCREEN_W, MENU_SCREEN_H, WHITE);
        ssd1306_fillRect(0, 24, MENU_SCREEN_W, MENU_LINE_H, BLACK);
        ssd1306_printCentered("Esperando..", 24, &Font5x7);
        break;

    case EX_ACTIVE: {
        /* Game HUD */
        char line[16U];

        ssd1306_printCentered("En juego", 0, &Font5x7);

        snprintf(line, sizeof(line), "V:%u B:%u", game_lives, game_ammo);
        ssd1306_printCentered(line, 12, &Font5x7);

        snprintf(line, sizeof(line), "Bat: %u%%", game_battery);
        ssd1306_printCentered(line, 24, &Font5x7);
        break;
    }
    }

    ssd1306_display();
}

/* ========================  INPUT  ========================================== */

void Screen_Exercise_OnButton(Menu_Handle_t *h, MenuButton_e btn)
{
    switch ((ExSubState_e)h->sub_state) {

    case EX_NO_BT:
        if (btn == BTN_NAVIGATE) {
            h->selected++;
            if (h->selected >= EX_NO_BT_OPTS) {
                h->selected = 0U;
            }
            h->needs_redraw = true;

        } else if (btn == BTN_ENTER) {
            if (h->selected == 0U) {
                /* Preview */
                h->sub_state   = (uint8_t)EX_PREVIEW;
                h->splash_tick = HAL_GetTick();
                h->needs_redraw = true;
            } else {
                /* Salir */
                Menu_GoTo(h, SCREEN_MAIN_MENU);
            }
        }
        break;

    case EX_PREVIEW:
        /* Any button exits preview early */
        h->sub_state    = (uint8_t)EX_NO_BT;
        h->selected     = 0U;
        h->needs_redraw = true;
        break;

    case EX_WAITING:
        if (btn == BTN_ENTER) {
            /* Testing: Enter simulates LoRa start */
            h->sub_state    = (uint8_t)EX_ACTIVE;
            h->needs_redraw = true;
        } else if (btn == BTN_NAVIGATE) {
            Menu_GoTo(h, SCREEN_MAIN_MENU);
        }
        break;

    case EX_ACTIVE:
        if (btn == BTN_NAVIGATE) {
            Menu_GoTo(h, SCREEN_MAIN_MENU);
        }
        break;
    }
}

