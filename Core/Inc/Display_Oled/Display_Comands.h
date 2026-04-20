/**
 * @file    Display_Comands.h
 * @brief   SSD1306 command defines and drawing function prototypes.
 *
 * @details Based on the Adafruit SSD1306 + Adafruit GFX library.
 *          Provides pixel, line, rectangle, circle, bitmap, and text
 *          drawing functions that operate on an internal framebuffer.
 *          Call ssd1306_display() to flush the buffer to the screen.
 *
 * @date    March 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef DISPLAY_COMANDS_H
#define DISPLAY_COMANDS_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "Display_Config.h"
#include "Display_Fonts.h"

/* ====================  DEVICE CONSTANTS  ================================== */

/* Pixel color values */

#define BLACK   0
#define WHITE   1
#define INVERSE 2

/* SSD1306 command opcodes */

#define SSD1306_DISPLAYOFF          0xAEU
#define SSD1306_DISPLAYON           0xAFU
#define SSD1306_SETCONTRAST         0x81U
#define SSD1306_DISPLAYALLON_RESUME 0xA4U
#define SSD1306_DISPLAYALLON        0xA5U
#define SSD1306_NORMALDISPLAY       0xA6U
#define SSD1306_INVERTDISPLAY       0xA7U

/* Addressing */

#define SSD1306_MEMORYMODE          0x20U
#define SSD1306_COLUMNADDR          0x21U
#define SSD1306_PAGEADDR            0x22U
#define SSD1306_SETLOWCOLUMN        0x00U
#define SSD1306_SETHIGHCOLUMN       0x10U
#define SSD1306_SETSTARTLINE        0x40U

/* Hardware configuration */

#define SSD1306_SEGREMAP            0xA0U
#define SSD1306_COMSCANINC          0xC0U
#define SSD1306_COMSCANDEC          0xC8U
#define SSD1306_SETCOMPINS          0xDAU
#define SSD1306_SETMULTIPLEX        0xA8U
#define SSD1306_SETDISPLAYOFFSET    0xD3U
#define SSD1306_SETDISPLAYCLOCKDIV  0xD5U

/* Power */

#define SSD1306_CHARGEPUMP          0x8DU
#define SSD1306_SETPRECHARGE        0xD9U
#define SSD1306_SETVCOMDETECT       0xDBU
#define SSD1306_EXTERNALVCC         0x01U
#define SSD1306_SWITCHCAPVCC        0x02U

/* Scroll */

#define SSD1306_DEACTIVATE_SCROLL   0x2EU

/* ================================  API  =================================== */

/* Display control */

/**
 * @brief  Initializes the SSD1306 with the given VCC mode and I2C address.
 * @param  vccstate  SSD1306_EXTERNALVCC or SSD1306_SWITCHCAPVCC.
 * @param  i2caddr   7-bit I2C address (typically 0x3C).
 * @note   Call once after MX_I2Cx_Init().
 */
void ssd1306_begin(uint8_t vccstate, uint8_t i2caddr);

/**
 * @brief  Sends a single command byte to the SSD1306 via I2C.
 * @param  c  Command byte.
 */
void ssd1306_command(uint8_t c);

/**
 * @brief  Flushes the internal framebuffer to the display.
 */
void ssd1306_display(void);

/**
 * @brief  Clears the framebuffer (fills with black).
 */
void ssd1306_clearDisplay(void);

/**
 * @brief  Inverts or restores all pixel colors via the hardware invert command.
 * @param  i  1 to invert, 0 to restore normal.
 */
void ssd1306_invertDisplay(uint8_t i);

/**
 * @brief  Sets display brightness.
 * @param  dim  true = low brightness, false = normal brightness.
 */
void ssd1306_dim(bool dim);

/**
 * @brief  Sets the drawing rotation applied to all pixel operations.
 * @param  r  Rotation index: 0 = 0°, 1 = 90°, 2 = 180°, 3 = 270°.
 */
void ssd1306_setRotation(uint8_t r);

/* Pixel drawing */

/**
 * @brief  Sets a single pixel in the framebuffer.
 * @param  x      X coordinate.
 * @param  y      Y coordinate.
 * @param  color  WHITE, BLACK, or INVERSE.
 */
void ssd1306_drawPixel(int16_t x, int16_t y, uint16_t color);

/**
 * @brief  Fills the entire framebuffer with the given color.
 * @param  color  WHITE or BLACK.
 */
void ssd1306_fillScreen(uint16_t color);

/* Lines */

/**
 * @brief  Draws a line between two points using Bresenham's algorithm.
 * @param  x0     Start X.
 * @param  y0     Start Y.
 * @param  x1     End X.
 * @param  y1     End Y.
 * @param  color  WHITE, BLACK, or INVERSE.
 */
void ssd1306_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color);

/* Rectangles */

/**
 * @brief  Draws a rectangle outline.
 * @param  x      Top-left X.
 * @param  y      Top-left Y.
 * @param  w      Width in pixels.
 * @param  h      Height in pixels.
 * @param  color  WHITE, BLACK, or INVERSE.
 */
void ssd1306_drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/**
 * @brief  Draws a filled rectangle.
 * @param  x      Top-left X.
 * @param  y      Top-left Y.
 * @param  w      Width in pixels.
 * @param  h      Height in pixels.
 * @param  color  WHITE, BLACK, or INVERSE.
 */
void ssd1306_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color);

/* Circles */

/**
 * @brief  Draws a circle outline.
 * @param  x0     Center X.
 * @param  y0     Center Y.
 * @param  r      Radius in pixels.
 * @param  color  WHITE, BLACK, or INVERSE.
 */
void ssd1306_drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/**
 * @brief  Draws a filled circle.
 * @param  x0     Center X.
 * @param  y0     Center Y.
 * @param  r      Radius in pixels.
 * @param  color  WHITE, BLACK, or INVERSE.
 */
void ssd1306_fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color);

/* Bitmaps */

/**
 * @brief  Draws a 1-bit-per-pixel bitmap.
 * @param  x       Top-left X.
 * @param  y       Top-left Y.
 * @param  bitmap  Pointer to the bitmap data array (MSB first, row-major).
 * @param  w       Bitmap width in pixels.
 * @param  h       Bitmap height in pixels.
 * @param  color   WHITE or BLACK.
 */
void ssd1306_drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                        int16_t w, int16_t h, uint16_t color);

/* Text */

/**
 * @brief  Draws a single character at the given position.
 * @param  x      X position.
 * @param  y      Y position.
 * @param  c      Character to draw.
 * @param  color  Foreground color.
 * @param  bg     Background color (same as color = transparent).
 * @param  size   Scale factor (1 = normal).
 * @param  font   Pointer to the Font_t to use.
 */
void ssd1306_drawChar(int16_t x, int16_t y, unsigned char c,
                      uint16_t color, uint16_t bg, uint8_t size,
                      const Font_t *font);

/**
 * @brief  Sets the text scale factor.
 * @param  s  Scale (1 = normal, 2 = double, etc.).
 */
void ssd1306_setTextSize(uint8_t s);

/**
 * @brief  Sets the text foreground color (background matches foreground = transparent).
 * @param  c  Color value.
 */
void ssd1306_setTextColor(uint16_t c);

/**
 * @brief  Sets both text foreground and background colors.
 * @param  c   Foreground color.
 * @param  bg  Background color.
 */
void ssd1306_setTextColorBg(uint16_t c, uint16_t bg);

/**
 * @brief  Positions the text cursor.
 * @param  x  Cursor X.
 * @param  y  Cursor Y.
 */
void ssd1306_setCursor(int16_t x, int16_t y);

/**
 * @brief  Draws one character at the cursor position and advances the cursor.
 * @param  c     Character to write.
 * @param  font  Font to use.
 */
void ssd1306_writeChar(char c, const Font_t *font);

/**
 * @brief  Writes a null-terminated string starting at the current cursor position.
 * @param  str   String to write.
 * @param  font  Font to use.
 */
void ssd1306_print(const char *str, const Font_t *font);

/**
 * @brief  Returns the pixel width of a string with the given font and current scale.
 * @param  str   String to measure.
 * @param  font  Font to use.
 */
uint16_t ssd1306_getStringWidth(const char *str, const Font_t *font);

/**
 * @brief  Writes a string centered horizontally on the given Y row.
 * @param  str   String to print.
 * @param  y     Y coordinate of the text baseline.
 * @param  font  Font to use.
 */
void ssd1306_printCentered(const char *str, int16_t y, const Font_t *font);

/**
 * @brief  Writes a string centered both horizontally and vertically on the display.
 * @param  str   String to print.
 * @param  font  Font to use.
 */
void ssd1306_printCenter(const char *str, const Font_t *font);

#endif /* DISPLAY_COMANDS_H */
