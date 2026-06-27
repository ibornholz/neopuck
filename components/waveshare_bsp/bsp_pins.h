// bsp_pins.h — Pinbelegung Waveshare ESP32-S3-Touch-AMOLED-1.75.
// Werte 1:1 aus dem offiziellen Waveshare-BSP-Header übernommen
// (components/esp32_s3_touch_amoled_1_75/include/bsp/...), NICHT geraten.
#pragma once
#include "driver/gpio.h"
#include "driver/i2s_std.h"

// --- I2C (CST9217 Touch, ES8311/ES7210 Codec, AXP2101 PMU teilen den Bus) -----
#define BSP_I2C_NUM           (0)
#define BSP_I2C_SCL           (GPIO_NUM_14)
#define BSP_I2C_SDA           (GPIO_NUM_15)
#define BSP_I2C_CLK_HZ        (400000)

// --- I2S Audio (ES8311 out / ES7210 in) --------------------------------------
#define BSP_I2S_NUM           (0)
#define BSP_I2S_SCLK          (GPIO_NUM_9)
#define BSP_I2S_MCLK          (GPIO_NUM_42)
#define BSP_I2S_LCLK          (GPIO_NUM_45)
#define BSP_I2S_DOUT          (GPIO_NUM_8)
#define BSP_I2S_DSIN          (GPIO_NUM_10)
#define BSP_POWER_AMP_IO      (GPIO_NUM_46)

// --- Display CO5300 (QSPI) ----------------------------------------------------
#define BSP_LCD_SPI_HOST      (SPI2_HOST)
#define BSP_LCD_CS            (GPIO_NUM_12)
#define BSP_LCD_PCLK          (GPIO_NUM_38)
#define BSP_LCD_DATA0         (GPIO_NUM_4)
#define BSP_LCD_DATA1         (GPIO_NUM_5)
#define BSP_LCD_DATA2         (GPIO_NUM_6)
#define BSP_LCD_DATA3         (GPIO_NUM_7)
#define BSP_LCD_RST           (GPIO_NUM_39)
#define BSP_LCD_TOUCH_RST     (GPIO_NUM_40)
#define BSP_LCD_TOUCH_INT     (GPIO_NUM_11)
#define BSP_LCD_H_RES         (466)
#define BSP_LCD_V_RES         (466)
#define BSP_LCD_BITS_PER_PIXEL (16)
#define BSP_LCD_COL_GAP       (0x06)   // CO5300 Spalten-Offset (set_gap)

// --- Buttons ------------------------------------------------------------------
// BOOT-Taste an GPIO0 (verifiziert aus 04_Immersive_block: CALIB_BUTTON_GPIO).
// PWR-Taste hängt am AXP2101-Power-Key (PEKEY), kein eigener GPIO -> bsp_power.
#define BSP_BTN_BOOT_GPIO     (GPIO_NUM_0)

// --- AXP2101 PMU --------------------------------------------------------------
#define BSP_AXP2101_I2C_ADDR  (0x34)
