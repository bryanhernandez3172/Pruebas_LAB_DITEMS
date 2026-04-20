/**
 * @file    Menu_Bluetooth.c
 * @brief   Bluetooth connection screen — conditional view based on bt_connected.
 *
 * @details Disconnected view:
 *          y=0:  [>] Conectar
 *          y=8:  [ ] Salir
 *
 *          Connected view:
 *          y=0:  ID:XXXX           (informational, not selectable)
 *          y=8:  [>] Reconect
 *          y=16: [ ] Salir
 *
 * @date    April 14, 2026
 * @author  César Pérez
 * @version 1.1.0
 */

#include "Menu/Menu_Screens.h"
#include "Display_Oled/Display_Comands.h"
#include "Display_Oled/Display_Fonts.h"
#include <stdio.h>

/* ========================  CONSTANTS  ==================================== */

#define BT_DISC_COUNT   2U
#define BT_CONN_COUNT   2U

/* ========================  DRAW  ========================================= */

void Screen_Bluetooth_Draw(Menu_Handle_t *h)
{
    ssd1306_clearDisplay();
    ssd1306_setTextSize(1U);
    ssd1306_setTextColor(WHITE);

    if (h->sub_state == (uint8_t)BT_ACTION) {
        if (h->bt_connected) {
            ssd1306_printCentered("Reconect..", 12, &Font5x7);
        } else {
            ssd1306_printCentered("Conectando..", 12, &Font5x7);
        }
        ssd1306_display();
        return;
    }

    if (!h->bt_connected) {
        const char *labels[] = { "Conectar", "Salir" };

        for (uint8_t i = 0U; i < BT_DISC_COUNT; i++) {
            int16_t y = (int16_t)(i * MENU_LINE_H);
            ssd1306_setCursor(0, y);
            ssd1306_print((i == h->selected) ? "> " : "  ", &Font5x7);
            ssd1306_print(labels[i], &Font5x7);
        }
    } else {
        char id_str[MENU_MAX_CHARS + 1U];
        snprintf(id_str, sizeof(id_str), "ID:%04X", h->bt_device_id);
        ssd1306_setCursor(0, 0);
        ssd1306_print(id_str, &Font5x7);

        const char *labels[] = { "Reconect", "Salir" };

        for (uint8_t i = 0U; i < BT_CONN_COUNT; i++) {
            int16_t y = (int16_t)((i + 1U) * MENU_LINE_H);
            ssd1306_setCursor(0, y);
            ssd1306_print((i == h->selected) ? "> " : "  ", &Font5x7);
            ssd1306_print(labels[i], &Font5x7);
        }
    }

    ssd1306_display();
}

/* ========================  INPUT  ======================================== */

void Screen_Bluetooth_OnButton(Menu_Handle_t *h, MenuButton_e btn)
{
    if (h->sub_state == (uint8_t)BT_ACTION) {
        h->sub_state    = h->bt_connected ? (uint8_t)BT_CONNECTED : (uint8_t)BT_DISCONNECTED;
        h->needs_redraw = true;
        return;
    }

    uint8_t count = h->bt_connected ? BT_CONN_COUNT : BT_DISC_COUNT;

    if (btn == BTN_NAVIGATE) {
        h->selected++;
        if (h->selected >= count) {
            h->selected = 0U;
        }
        h->needs_redraw = true;

    } else if (btn == BTN_ENTER) {
        if (h->selected == (count - 1U)) {
            Menu_GoTo(h, SCREEN_MAIN_MENU);
        } else {
            h->sub_state    = (uint8_t)BT_ACTION;
            h->needs_redraw = true;
        }
    }
}
