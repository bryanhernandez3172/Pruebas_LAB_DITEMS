/**
 * @file    Menu_TestHW.c
 * @brief   Test Hardware screen — select PCB to diagnose.
 *
 * @details Layout (64x32, Font5x7, list with ">" cursor):
 *          y=0:  [>] Mira
 *          y=8:  [ ] Sensores
 *          y=16: [ ] Salir
 *
 * @date    April 14, 2026
 * @author  César Pérez
 * @version 1.1.0
 */

#include "Menu/Menu_Screens.h"
#include "Display_Oled/Display_Comands.h"
#include "Display_Oled/Display_Fonts.h"

/* ========================  CONSTANTS  ==================================== */

#define TESTHW_OPT_COUNT    3U
#define TESTHW_ACTION       10U    /**< sub_state value for placeholder */

static const char *testhw_labels[TESTHW_OPT_COUNT] = {
    "Mira",
    "Sensores",
    "Salir"
};

static const char *testhw_actions[TESTHW_OPT_COUNT] = {
    "Aux Mira",
    "Aux Sensor",
    NULL
};

/* ========================  DRAW  ========================================= */

void Screen_TestHW_Draw(Menu_Handle_t *h)
{
    ssd1306_clearDisplay();
    ssd1306_setTextSize(1U);
    ssd1306_setTextColor(WHITE);

    if (h->sub_state == TESTHW_ACTION) {
        ssd1306_printCentered(testhw_actions[h->selected], 12, &Font5x7);
    } else {
        for (uint8_t i = 0U; i < TESTHW_OPT_COUNT; i++) {
            int16_t y = (int16_t)(i * MENU_LINE_H);
            ssd1306_setCursor(0, y);
            ssd1306_print((i == h->selected) ? "> " : "  ", &Font5x7);
            ssd1306_print(testhw_labels[i], &Font5x7);
        }
    }

    ssd1306_display();
}

/* ========================  INPUT  ======================================== */

void Screen_TestHW_OnButton(Menu_Handle_t *h, MenuButton_e btn)
{
    if (h->sub_state == TESTHW_ACTION) {
        h->sub_state    = 0U;
        h->needs_redraw = true;
        return;
    }

    if (btn == BTN_NAVIGATE) {
        h->selected++;
        if (h->selected >= TESTHW_OPT_COUNT) {
            h->selected = 0U;
        }
        h->needs_redraw = true;

    } else if (btn == BTN_ENTER) {
        if (h->selected == (TESTHW_OPT_COUNT - 1U)) {
            Menu_GoTo(h, SCREEN_MAIN_MENU);
        } else {
            h->sub_state    = TESTHW_ACTION;
            h->needs_redraw = true;
        }
    }
}
