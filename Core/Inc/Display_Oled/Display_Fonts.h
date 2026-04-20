/**
 * @file    Display_Fonts.h
 * @brief   Bitmap font definitions and text state for SSD1306 OLED driver.
 *
 * @details Available fonts (enable with the #define macros below):
 *            FONT_5x7  — Classic Adafruit GFX (10 chars × 4 lines on 64×32)
 *            FONT_4x6  — Ultra-compact (12 chars × 5 lines on 64×32)
 *            FONT_3x5  — Minimal, only readable up close (16 chars × 6 lines on 64×32)
 *
 * @date    March 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef DISPLAY_FONTS_H
#define DISPLAY_FONTS_H

#include <stdint.h>
#include "Display_Config.h"

/* ====================  FONT SELECTION  ==================================== */

/* Comment out any fonts not needed to save flash */

#define FONT_6x8
#define FONT_5x7
#define FONT_4x6
#define FONT_3x5

/* ======================  STRUCTURES  ====================================== */

/**
 * @brief  Holds cursor position, scale, and color for text rendering functions.
 */
typedef struct {
    int16_t  cursor_x;    /**< Current X cursor position                        */
    int16_t  cursor_y;    /**< Current Y cursor position                        */
    uint8_t  size;        /**< Scale factor (1 = normal, 2 = double, etc.)      */
    uint16_t color;       /**< Text foreground color (WHITE, BLACK, or INVERSE) */
    uint16_t bg;          /**< Background color (same as color = transparent)   */
} TextState_t;

/**
 * @brief  Describes a single bitmap font.
 */
typedef struct {
    const uint8_t  width;      /**< Character width in pixels          */
    const uint8_t  height;     /**< Character height in pixels         */
    const uint8_t  first_char; /**< First ASCII character in the table */
    const uint8_t  last_char;  /**< Last ASCII character in the table  */
    const uint8_t *data;       /**< Pointer to the bitmap data array   */
} Font_t;

/* ====================  FONT DECLARATIONS  ================================= */

/* Defined in Display_Fonts.c */

#ifdef FONT_6x8
extern const Font_t Font6x8;
#endif

#ifdef FONT_5x7
extern const Font_t Font5x7;
#endif

#ifdef FONT_4x6
extern const Font_t Font4x6;
#endif

#ifdef FONT_3x5
extern const Font_t Font3x5;
#endif

#endif /* DISPLAY_FONTS_H */
