/**
 * @file    Display_Commands.c
 * @brief   SSD1306 OLED driver implementation for STM32 HAL.
 *
 * @details Based on the Adafruit SSD1306 + Adafruit GFX library.
 *          Manages a static framebuffer; call ssd1306_display() to flush
 *          the buffer to the screen over I2C.
 *
 * @date    March 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#include "Display_Oled/Display_Comands.h"
#include "Display_Oled/Display_Fonts.h"
#include <stdlib.h>

/* ======================  STATIC VARIABLES  ================================ */

static uint8_t    ssd1306_buffer[SSD1306_BUFFER_SIZE];
static TextState_t text_state = {0, 0, 1U, WHITE, WHITE};
static uint8_t    rotation    = 0U;

/* ======================  STATIC FUNCTIONS  ================================ */

/**
 * @brief  Sends a data block to the SSD1306 using I2C memory write (control byte 0x40).
 * @param  data  Pointer to the data buffer.
 * @param  len   Number of bytes to send.
 */
static void ssd1306_data(uint8_t *data, uint16_t len) {
    HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, (SSD1306_I2C_ADDRESS << 1U),
                      0x40U, 1U, data, len, HAL_MAX_DELAY);
}

/**
 * @brief  Draws the octant arcs of a circle outline (Bresenham helper).
 * @param  x0      Center X.
 * @param  y0      Center Y.
 * @param  r       Radius.
 * @param  corner  Bitmask selecting which quadrants to draw (bits 1–4).
 * @param  color   Pixel color.
 */
static void ssd1306_drawCircleHelper(int16_t x0, int16_t y0, int16_t r,
                                     uint8_t corner, uint16_t color) {
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (corner & 0x4U) { ssd1306_drawPixel(x0 + x, y0 + y, color); ssd1306_drawPixel(x0 + y, y0 + x, color); }
        if (corner & 0x2U) { ssd1306_drawPixel(x0 + x, y0 - y, color); ssd1306_drawPixel(x0 + y, y0 - x, color); }
        if (corner & 0x8U) { ssd1306_drawPixel(x0 - y, y0 + x, color); ssd1306_drawPixel(x0 - x, y0 + y, color); }
        if (corner & 0x1U) { ssd1306_drawPixel(x0 - y, y0 - x, color); ssd1306_drawPixel(x0 - x, y0 - y, color); }
    }
}

/**
 * @brief  Fills vertical spans to paint the interior of a circle (used by fillCircle).
 * @param  x0      Center X.
 * @param  y0      Center Y.
 * @param  r       Radius.
 * @param  corners Bitmask: bit 0 = right half, bit 1 = left half.
 * @param  delta   Extra vertical extent added to each span.
 * @param  color   Pixel color.
 */
static void ssd1306_fillCircleHelper(int16_t x0, int16_t y0, int16_t r,
                                     uint8_t corners, int16_t delta,
                                     uint16_t color) {
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;
    int16_t px    = x;
    int16_t py    = y;

    delta++;

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        if (x < (y + 1)) {
            if (corners & 1U) for (int16_t i = y0 - x; i <= y0 + x + delta; i++) ssd1306_drawPixel(x0 + y, i, color);
            if (corners & 2U) for (int16_t i = y0 - x; i <= y0 + x + delta; i++) ssd1306_drawPixel(x0 - y, i, color);
        }
        if (y != py) {
            if (corners & 1U) for (int16_t i = y0 - py; i <= y0 + py + delta; i++) ssd1306_drawPixel(x0 + px, i, color);
            if (corners & 2U) for (int16_t i = y0 - py; i <= y0 + py + delta; i++) ssd1306_drawPixel(x0 - px, i, color);
            py = y;
        }
        px = x;
    }
}

/* ================================  API  =================================== */

/* Display control */

/**
 * @brief  Sends a single command byte to the SSD1306 via I2C (control byte 0x00).
 */
void ssd1306_command(uint8_t c) {
    HAL_I2C_Mem_Write(&SSD1306_I2C_PORT, (SSD1306_I2C_ADDRESS << 1U),
                      0x00U, 1U, &c, 1U, HAL_MAX_DELAY);
}

/**
 * @brief  Initializes the SSD1306 with the given VCC mode, sends the full
 *         init sequence, clears the framebuffer, and flushes to the display.
 */
void ssd1306_begin(uint8_t vccstate, uint8_t i2caddr) {
    (void)i2caddr;
    HAL_Delay(100U);

    ssd1306_command(SSD1306_DISPLAYOFF);
    ssd1306_command(SSD1306_SETDISPLAYCLOCKDIV);
    ssd1306_command(0x80U);
    ssd1306_command(SSD1306_SETMULTIPLEX);
    ssd1306_command(SSD1306_LCDHEIGHT - 1U);
    ssd1306_command(SSD1306_SETDISPLAYOFFSET);
    ssd1306_command(0x00U);
    ssd1306_command(SSD1306_SETSTARTLINE | 0x0U);
    ssd1306_command(SSD1306_CHARGEPUMP);
    ssd1306_command((vccstate == SSD1306_EXTERNALVCC) ? 0x10U : 0x14U);
    ssd1306_command(SSD1306_MEMORYMODE);
    ssd1306_command(0x00U);
    ssd1306_command(SSD1306_SEGREMAP | 0x1U);
    ssd1306_command(SSD1306_COMSCANDEC);

#if defined SSD1306_128_32
    ssd1306_command(SSD1306_SETCOMPINS);  ssd1306_command(0x02U);
    ssd1306_command(SSD1306_SETCONTRAST); ssd1306_command(0x8FU);
#elif defined SSD1306_128_64
    ssd1306_command(SSD1306_SETCOMPINS);  ssd1306_command(0x12U);
    ssd1306_command(SSD1306_SETCONTRAST); ssd1306_command((vccstate == SSD1306_EXTERNALVCC) ? 0x9FU : 0xCFU);
#elif defined SSD1306_96_16
    ssd1306_command(SSD1306_SETCOMPINS);  ssd1306_command(0x02U);
    ssd1306_command(SSD1306_SETCONTRAST); ssd1306_command((vccstate == SSD1306_EXTERNALVCC) ? 0x10U : 0xAFU);
#elif defined SSD1306_64_32
    ssd1306_command(SSD1306_SETCOMPINS);  ssd1306_command(0x12U);
    ssd1306_command(SSD1306_SETCONTRAST); ssd1306_command(0xCFU);
    ssd1306_command(SSD1306_SETPRECHARGE); ssd1306_command((vccstate == SSD1306_EXTERNALVCC) ? 0x22U : 0xF1U);
#endif

    ssd1306_command(SSD1306_SETPRECHARGE);  ssd1306_command((vccstate == SSD1306_EXTERNALVCC) ? 0x22U : 0xF1U);
    ssd1306_command(SSD1306_SETVCOMDETECT); ssd1306_command(0x40U);
    ssd1306_command(SSD1306_DISPLAYALLON_RESUME);
    ssd1306_command(SSD1306_NORMALDISPLAY);
    ssd1306_command(SSD1306_DEACTIVATE_SCROLL);
    ssd1306_command(SSD1306_DISPLAYON);

    ssd1306_clearDisplay();
    ssd1306_display();
}

/**
 * @brief  Sets the column/page addressing window and flushes the framebuffer
 *         to the display in 16-byte chunks.
 */
void ssd1306_display(void) {
    ssd1306_command(SSD1306_COLUMNADDR);
#if defined SSD1306_64_32
    ssd1306_command(0x20U);
    ssd1306_command(0x20U + SSD1306_LCDWIDTH - 1U);
#else
    ssd1306_command(0U);
    ssd1306_command(SSD1306_LCDWIDTH - 1U);
#endif
    ssd1306_command(SSD1306_PAGEADDR);
    ssd1306_command(0U);
    ssd1306_command((SSD1306_LCDHEIGHT / 8U) - 1U);

    for (uint16_t i = 0U; i < SSD1306_BUFFER_SIZE; i += 16U) {
        ssd1306_data(&ssd1306_buffer[i], 16U);
    }
}

/**
 * @brief  Fills the framebuffer with 0x00 (all pixels black).
 */
void ssd1306_clearDisplay(void) {
    memset(ssd1306_buffer, 0x00U, SSD1306_BUFFER_SIZE);
}

/**
 * @brief  Sends the hardware invert or normal-display command.
 */
void ssd1306_invertDisplay(uint8_t i) {
    ssd1306_command(i ? SSD1306_INVERTDISPLAY : SSD1306_NORMALDISPLAY);
}

/**
 * @brief  Sets contrast to near-zero (dim) or 0xCF (normal).
 */
void ssd1306_dim(bool dim) {
    ssd1306_command(SSD1306_SETCONTRAST);
    ssd1306_command(dim ? 0x00U : 0xCFU);
}

/**
 * @brief  Stores the rotation index (0–3) applied by ssd1306_drawPixel().
 */
void ssd1306_setRotation(uint8_t r) {
    rotation = r & 0x03U;
}

/* Pixel drawing */

/**
 * @brief  Applies the current rotation transform, bounds-checks the coordinates,
 *         then sets, clears, or toggles the corresponding bit in the framebuffer.
 */
void ssd1306_drawPixel(int16_t x, int16_t y, uint16_t color) {
    int16_t w = SSD1306_LCDWIDTH;
    int16_t h = SSD1306_LCDHEIGHT;

    /* Apply rotation */
    int16_t t;
    switch (rotation) {
        case 1U: t = x; x = w - 1 - y; y = t;         break;
        case 2U: x = w - 1 - x; y = h - 1 - y;        break;
        case 3U: t = x; x = y;  y = h - 1 - t;        break;
        default: break;
    }

    if (x < 0 || x >= w || y < 0 || y >= h) return;

    switch (color) {
        case WHITE:   ssd1306_buffer[x + (y / 8) * w] |=  (1U << (y & 7U)); break;
        case BLACK:   ssd1306_buffer[x + (y / 8) * w] &= ~(1U << (y & 7U)); break;
        case INVERSE: ssd1306_buffer[x + (y / 8) * w] ^=  (1U << (y & 7U)); break;
        default: break;
    }
}

/**
 * @brief  Delegates to ssd1306_fillRect() covering the entire display.
 */
void ssd1306_fillScreen(uint16_t color) {
    ssd1306_fillRect(0, 0, SSD1306_LCDWIDTH, SSD1306_LCDHEIGHT, color);
}

/* Lines */

/**
 * @brief  Draws a line using Bresenham's algorithm, with swap logic for steep lines.
 */
void ssd1306_drawLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint16_t color) {
    int16_t steep = abs(y1 - y0) > abs(x1 - x0);
    int16_t t;

    if (steep) { t = x0; x0 = y0; y0 = t; t = x1; x1 = y1; y1 = t; }
    if (x0 > x1) { t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    int16_t dx    = x1 - x0;
    int16_t dy    = abs(y1 - y0);
    int16_t err   = dx / 2;
    int16_t ystep = (y0 < y1) ? 1 : -1;

    for (; x0 <= x1; x0++) {
        if (steep) ssd1306_drawPixel(y0, x0, color);
        else       ssd1306_drawPixel(x0, y0, color);
        err -= dy;
        if (err < 0) { y0 += ystep; err += dx; }
    }
}

/* Rectangles */

/**
 * @brief  Draws four lines forming a closed rectangle outline.
 */
void ssd1306_drawRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    ssd1306_drawLine(x,         y,         x + w - 1, y,         color);
    ssd1306_drawLine(x,         y + h - 1, x + w - 1, y + h - 1, color);
    ssd1306_drawLine(x,         y,         x,         y + h - 1, color);
    ssd1306_drawLine(x + w - 1, y,         x + w - 1, y + h - 1, color);
}

/**
 * @brief  Fills a rectangle by calling ssd1306_drawPixel() for every pixel inside.
 */
void ssd1306_fillRect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color) {
    for (int16_t i = x; i < x + w; i++)
        for (int16_t j = y; j < y + h; j++)
            ssd1306_drawPixel(i, j, color);
}

/* Circles */

/**
 * @brief  Draws a circle outline using Bresenham's midpoint algorithm.
 */
void ssd1306_drawCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    int16_t f     = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x     = 0;
    int16_t y     = r;

    ssd1306_drawPixel(x0,     y0 + r, color);
    ssd1306_drawPixel(x0,     y0 - r, color);
    ssd1306_drawPixel(x0 + r, y0,     color);
    ssd1306_drawPixel(x0 - r, y0,     color);

    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        ssd1306_drawPixel(x0 + x, y0 + y, color);
        ssd1306_drawPixel(x0 - x, y0 + y, color);
        ssd1306_drawPixel(x0 + x, y0 - y, color);
        ssd1306_drawPixel(x0 - x, y0 - y, color);
        ssd1306_drawPixel(x0 + y, y0 + x, color);
        ssd1306_drawPixel(x0 - y, y0 + x, color);
        ssd1306_drawPixel(x0 + y, y0 - x, color);
        ssd1306_drawPixel(x0 - y, y0 - x, color);
    }
}

/**
 * @brief  Draws the center vertical line then delegates the fills to
 *         ssd1306_fillCircleHelper() for both halves.
 */
void ssd1306_fillCircle(int16_t x0, int16_t y0, int16_t r, uint16_t color) {
    for (int16_t i = y0 - r; i <= y0 + r; i++)
        ssd1306_drawPixel(x0, i, color);
    ssd1306_fillCircleHelper(x0, y0, r, 3U, 0, color);
}

/* Bitmaps */

/**
 * @brief  Iterates over every pixel of the bitmap row-by-row (MSB first)
 *         and calls ssd1306_drawPixel() for each set bit.
 */
void ssd1306_drawBitmap(int16_t x, int16_t y, const uint8_t bitmap[],
                        int16_t w, int16_t h, uint16_t color) {
    int16_t byteWidth = (w + 7) / 8;
    uint8_t byte      = 0U;

    for (int16_t j = 0; j < h; j++) {
        for (int16_t i = 0; i < w; i++) {
            if (i & 7) byte <<= 1U;
            else       byte = bitmap[j * byteWidth + i / 8];
            if (byte & 0x80U) ssd1306_drawPixel(x + i, y + j, color);
        }
    }
}

/* Text */

/**
 * @brief  Updates cursor_x and cursor_y in text_state.
 */
void ssd1306_setCursor(int16_t x, int16_t y) {
    text_state.cursor_x = x;
    text_state.cursor_y = y;
}

/**
 * @brief  Clamps size to at least 1 and stores it in text_state.
 */
void ssd1306_setTextSize(uint8_t s) {
    text_state.size = (s > 0U) ? s : 1U;
}

/**
 * @brief  Sets both fg and bg to c (transparent background).
 */
void ssd1306_setTextColor(uint16_t c) {
    text_state.color = c;
    text_state.bg    = c;
}

/**
 * @brief  Sets fg to c and bg separately.
 */
void ssd1306_setTextColorBg(uint16_t c, uint16_t bg) {
    text_state.color = c;
    text_state.bg    = bg;
}

/**
 * @brief  Draws each column of the character bitmap scaled by size, filling
 *         the background where fg != bg.
 */
void ssd1306_drawChar(int16_t x, int16_t y, unsigned char c,
                      uint16_t color, uint16_t bg, uint8_t size,
                      const Font_t *font) {
    if (c < font->first_char || c > font->last_char) c = font->first_char;

    uint8_t w = font->width + 1U;
    uint8_t h = font->height;

    if (x + w * size > SSD1306_LCDWIDTH || y + h * size > SSD1306_LCDHEIGHT) return;

    uint16_t offset = (c - font->first_char) * font->width;

    for (uint8_t col = 0U; col < w; col++) {
        uint8_t line = (col < font->width) ? font->data[offset + col] : 0x00U;
        for (uint8_t row = 0U; row < h; row++) {
            if (line & 0x01U) {
                if (size == 1U) ssd1306_drawPixel(x + col, y + row, color);
                else            ssd1306_fillRect(x + col * size, y + row * size, size, size, color);
            } else if (bg != color) {
                if (size == 1U) ssd1306_drawPixel(x + col, y + row, bg);
                else            ssd1306_fillRect(x + col * size, y + row * size, size, size, bg);
            }
            line >>= 1U;
        }
    }
}

/**
 * @brief  Handles newline/carriage-return, wraps at the right edge, then
 *         calls ssd1306_drawChar() and advances cursor_x.
 */
void ssd1306_writeChar(char c, const Font_t *font) {
    uint8_t w = (font->width + 1U) * text_state.size;
    uint8_t h = font->height * text_state.size;

    if (c == '\n') { text_state.cursor_y += h; text_state.cursor_x = 0; return; }
    if (c == '\r') { text_state.cursor_x = 0;                            return; }

    if (text_state.cursor_x + w > SSD1306_LCDWIDTH) {
        text_state.cursor_x = 0;
        text_state.cursor_y += h;
    }

    ssd1306_drawChar(text_state.cursor_x, text_state.cursor_y,
                     c, text_state.color, text_state.bg,
                     text_state.size, font);
    text_state.cursor_x += w;
}

/**
 * @brief  Iterates the string calling ssd1306_writeChar() for each character.
 */
void ssd1306_print(const char *str, const Font_t *font) {
    while (*str) { ssd1306_writeChar(*str, font); str++; }
}

/**
 * @brief  Counts characters and multiplies by (font->width + 1) × size.
 */
uint16_t ssd1306_getStringWidth(const char *str, const Font_t *font) {
    uint16_t len = 0U;
    while (*str) { len++; str++; }
    return len * (font->width + 1U) * text_state.size;
}

/**
 * @brief  Computes the pixel width, derives x so the string is centered, then
 *         sets the cursor and calls ssd1306_print().
 */
void ssd1306_printCentered(const char *str, int16_t y, const Font_t *font) {
    uint16_t w = ssd1306_getStringWidth(str, font);
    int16_t  x = (SSD1306_LCDWIDTH - (int16_t)w) / 2;
    if (x < 0) x = 0;
    ssd1306_setCursor(x, y);
    ssd1306_print(str, font);
}

/**
 * @brief  Computes both x (horizontal center) and y (vertical center), then
 *         sets the cursor and calls ssd1306_print().
 */
void ssd1306_printCenter(const char *str, const Font_t *font) {
    uint16_t w = ssd1306_getStringWidth(str, font);
    uint8_t  h = font->height * text_state.size;
    int16_t  x = (SSD1306_LCDWIDTH  - (int16_t)w) / 2;
    int16_t  y = (SSD1306_LCDHEIGHT - (int16_t)h) / 2;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    ssd1306_setCursor(x, y);
    ssd1306_print(str, font);
}
