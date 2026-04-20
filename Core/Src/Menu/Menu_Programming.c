/**
 * @file    Menu_Programming.c
 * @brief   Programming mode screen — select module to program.
 *
 * @details Layout (64x32, Font5x7):
 *          y=0:  [>] Bluetooth
 *          y=8:  [ ] Micro
 *          y=16: [ ] Lora
 *          y=24: [ ] Salir
 *
 * @date    April 15, 2026
 * @author  César Pérez
 * @version 2.0.0
 */

#include "Menu/Menu_Screens.h"
#include "Display_Oled/Display_Comands.h"
#include "Display_Oled/Display_Fonts.h"

/* ========================  CONSTANTS  ==================================== */

#define PROG_OPT_COUNT      4U

static const char *prog_labels[PROG_OPT_COUNT] = {
    "Bluetoot",
    "Micro",
    "Lora",
    "Salir"
};

static const char *prog_actions[PROG_OPT_COUNT] = {
    "Aux BT",
    "Aux Micro",
    "Aux Lora",
    NULL
};

#define PROG_ACTION_STATE   10U

/* ========================  DRAW  ========================================= */

void Screen_Programming_Draw(Menu_Handle_t *h)
{
    ssd1306_clearDisplay();
    ssd1306_setTextSize(1U);
    ssd1306_setTextColor(WHITE);

    if (h->sub_state == PROG_ACTION_STATE) {
        ssd1306_printCentered(prog_actions[h->selected], 12, &Font5x7);
    } else {
        for (uint8_t i = 0U; i < PROG_OPT_COUNT; i++) {
            int16_t y = (int16_t)(i * MENU_LINE_H);
            ssd1306_setCursor(0, y);
            ssd1306_print((i == h->selected) ? "> " : "  ", &Font5x7);
            ssd1306_print(prog_labels[i], &Font5x7);
        }
    }

    ssd1306_display();
}

/* ========================  INPUT  ======================================== */

void Screen_Programming_OnButton(Menu_Handle_t *h, MenuButton_e btn)
{
    if (h->sub_state == PROG_ACTION_STATE) {
        h->sub_state    = 0U;
        h->needs_redraw = true;
        return;
    }

    if (btn == BTN_NAVIGATE) {
        h->selected++;
        if (h->selected >= PROG_OPT_COUNT) {
            h->selected = 0U;
        }
        h->needs_redraw = true;

    } else if (btn == BTN_ENTER) {
        if (h->selected == (PROG_OPT_COUNT - 1U)) {
            Menu_GoTo(h, SCREEN_MAIN_MENU);
        } else {
            h->sub_state    = PROG_ACTION_STATE;
            h->needs_redraw = true;
        }
    }
}
