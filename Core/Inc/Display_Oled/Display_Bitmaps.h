/**
 * @file    Display_Bitmaps.h
 * @brief   Monochromatic bitmap declarations for SSD1306 OLED display.
 *
 * @details Format: 1 bit per pixel, MSB first, row-major order.
 *          Bitmap data is defined in Display_Bitmaps.c.
 *
 * @date    March 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef DISPLAY_BITMAPS_H
#define DISPLAY_BITMAPS_H

#include <stdint.h>

/* ====================  BITMAP DECLARATIONS  =============================== */

/* SIENT logo — 64×32 px */
extern const unsigned char bitmap_Logo_SIENT[];
extern const unsigned char bitmap_Modo_Ejercicio[];
extern const unsigned char bitmap_Candado_Cerrado[];
extern const unsigned char bitmap_Candado_Abierto[];

/* Menu icons — 16×16 px */
extern const unsigned char bitmap_Icon_Wrench[];
extern const unsigned char bitmap_Icon_Bluetooth[];
extern const unsigned char bitmap_Icon_Key[];
extern const unsigned char bitmap_Icon_Crosshair[];

#endif /* DISPLAY_BITMAPS_H */
