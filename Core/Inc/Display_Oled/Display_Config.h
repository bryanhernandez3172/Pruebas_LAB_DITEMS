/**
 * @file    Display_Config.h
 * @brief   Screen size and I2C peripheral configuration for SSD1306 OLED driver.
 *
 * @details Uncomment exactly one SSD1306_xxx_yyy define to select the screen
 *          resolution. The corresponding width and height macros are derived
 *          automatically. Also update SSD1306_I2C_PORT to match the I2C
 *          peripheral assigned in CubeMX.
 *
 * @date    March 2026
 * @author  César Pérez
 * @version 1.0.0
 */

#ifndef DISPLAY_CONFIG_H
#define DISPLAY_CONFIG_H

#include "stm32wbxx.h"

/* ========================  CONFIGURATION  ================================= */

/* Screen size selection — uncomment exactly one */

//#define SSD1306_128_64
//#define SSD1306_128_32
//#define SSD1306_96_16
#define SSD1306_64_32

/* Derived resolution from selection */

#if defined SSD1306_128_64
  #define SSD1306_LCDWIDTH    128
  #define SSD1306_LCDHEIGHT   64
#elif defined SSD1306_128_32
  #define SSD1306_LCDWIDTH    128
  #define SSD1306_LCDHEIGHT   32
#elif defined SSD1306_96_16
  #define SSD1306_LCDWIDTH    96
  #define SSD1306_LCDHEIGHT   16
#elif defined SSD1306_64_32
  #define SSD1306_LCDWIDTH    64
  #define SSD1306_LCDHEIGHT   32
#else
  #error "Select a screen size in Display_Config.h"
#endif

/* I2C peripheral handle — update to match CubeMX .ioc */

#define SSD1306_I2C_PORT     hi2c1
extern I2C_HandleTypeDef SSD1306_I2C_PORT;

/* I2C device address (7-bit) */

#define SSD1306_I2C_ADDRESS  0x3CU

/* Framebuffer size in bytes */

#define SSD1306_BUFFER_SIZE  (SSD1306_LCDWIDTH * SSD1306_LCDHEIGHT / 8)

#endif /* DISPLAY_CONFIG_H */
