// bsp_display.c — CO5300 (QSPI) + CST9217 (I2C) über esp_lvgl_port.
// Init-Sequenz, Panel- und Touch-Konfig sind aus dem offiziellen Waveshare-BSP
// (esp32_s3_touch_amoled_1_75.c) übernommen. LVGL-Anbindung über esp_lvgl_port
// (statt Waveshares esp_lvgl_adapter), wie in board.h/CLAUDE.md vorgegeben.
#include "board.h"
#include "bsp_internal.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_touch_cst9217.h"
#include "esp_lvgl_port.h"

static const char *TAG = "bsp_disp";

// CO5300 verlangt 2px-Alignment: Flush-Bereiche auf gerade/ungerade runden
// (entspricht dem rounder_cb des offiziellen BSP).
static void rounder_event_cb(lv_event_t *e)
{
    lv_area_t *a = (lv_area_t *)lv_event_get_param(e);
    a->x1 = (a->x1 >> 1) << 1;
    a->y1 = (a->y1 >> 1) << 1;
    a->x2 = ((a->x2 >> 1) << 1) + 1;
    a->y2 = ((a->y2 >> 1) << 1) + 1;
}

static esp_lcd_panel_handle_t    s_panel;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_touch_handle_t    s_touch;
static lv_display_t             *s_disp;

// CO5300 Hersteller-Init — wörtlich aus dem offiziellen BSP.
static const co5300_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xFE, (uint8_t[]){0x20}, 1, 0},
    {0x19, (uint8_t[]){0x10}, 1, 0},
    {0x1C, (uint8_t[]){0xA0}, 1, 0},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x35, (uint8_t[]){0x00}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x51, (uint8_t[]){0xFF}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x2A, (uint8_t[]){0x00, 0x06, 0x01, 0xD7}, 4, 0},
    {0x2B, (uint8_t[]){0x00, 0x00, 0x01, 0xD1}, 4, 600},
    {0x11, NULL, 0, 600},
    {0x29, NULL, 0, 0},
};

static esp_err_t panel_init(void)
{
    ESP_LOGI(TAG, "Init QSPI bus + CO5300");
    const spi_bus_config_t buscfg = CO5300_PANEL_BUS_QSPI_CONFIG(
        BSP_LCD_PCLK, BSP_LCD_DATA0, BSP_LCD_DATA1, BSP_LCD_DATA2, BSP_LCD_DATA3,
        BSP_LCD_H_RES * BSP_LCD_V_RES * BSP_LCD_BITS_PER_PIXEL / 8);
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BSP_LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO),
                        TAG, "spi_bus_initialize");

    esp_lcd_panel_io_spi_config_t io_config = CO5300_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    io_config.trans_queue_depth = 10;   // genug Tiefe für partielle Flushes
    co5300_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = { .use_qspi_interface = 1 },
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi(
        (esp_lcd_spi_bus_handle_t)BSP_LCD_SPI_HOST, &io_config, &s_io), TAG, "panel_io");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = BSP_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = BSP_LCD_BITS_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_co5300(s_io, &panel_config, &s_panel), TAG, "new_co5300");

    esp_lcd_panel_set_gap(s_panel, BSP_LCD_COL_GAP, 0);
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_panel, true), TAG, "disp_on");
    return ESP_OK;
}

static esp_err_t touch_init(void)
{
    ESP_RETURN_ON_ERROR(bsp_i2c_init(), TAG, "i2c");

    esp_lcd_panel_io_handle_t tp_io = NULL;
    esp_lcd_panel_io_i2c_config_t tp_io_cfg = ESP_LCD_TOUCH_IO_I2C_CST9217_CONFIG();
    tp_io_cfg.scl_speed_hz = BSP_I2C_CLK_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_i2c(bsp_i2c_get_handle(), &tp_io_cfg, &tp_io),
                        TAG, "touch_io");

    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = BSP_LCD_TOUCH_RST,
        .int_gpio_num = BSP_LCD_TOUCH_INT,
        .levels = { .reset = 0, .interrupt = 0 },
        .flags = { .swap_xy = 0, .mirror_x = 1, .mirror_y = 1 },  // wie offizielles BSP
    };
    return esp_lcd_touch_new_i2c_cst9217(tp_io, &tp_cfg, &s_touch);
}

lv_display_t *bsp_display_start(void)
{
    if (s_disp) return s_disp;

    ESP_ERROR_CHECK(panel_init());
    ESP_ERROR_CHECK(touch_init());

    // Default-Konfig ist stabil; höhere Priorität/Core-Pinning hungert die IDLE-Task
    // aus (Task-Watchdog). Perf kommt aus dem leichteren Orb-Rendering.
    lvgl_port_cfg_t lvgl_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    lvgl_cfg.task_stack = 6144;
    ESP_ERROR_CHECK(lvgl_port_init(&lvgl_cfg));

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io,
        .panel_handle = s_panel,
        // Partielle Buffer (40 Zeilen) im INTERNEN DMA-RAM, double-buffered.
        // Wichtig: Der QSPI-esp_lcd-Color-Pfad braucht DMA-fähigen internen RAM;
        // PSRAM-Buffer schlagen mit "spi transmit color failed" fehl. 40 Zeilen
        // = 2x ~37 KB, passt locker in den internen Heap.
        .buffer_size = BSP_LCD_H_RES * 40,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = { .swap_xy = false, .mirror_x = false, .mirror_y = false },
        .flags = {
            .buff_dma = true,       // interner DMA-RAM (QSPI-Color-Pfad braucht das)
            .swap_bytes = true,     // RGB565 Byte-Swap für QSPI-Panel
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    if (!s_disp) { ESP_LOGE(TAG, "lvgl_port_add_disp failed"); return NULL; }
    lv_display_add_event_cb(s_disp, rounder_event_cb, LV_EVENT_INVALIDATE_AREA, NULL);

    const lvgl_port_touch_cfg_t touch_cfg = {
        .disp = s_disp,
        .handle = s_touch,
    };
    lvgl_port_add_touch(&touch_cfg);

    bsp_display_brightness_set(100);
    ESP_LOGI(TAG, "display + touch up (%dx%d)", BSP_LCD_H_RES, BSP_LCD_V_RES);
    return s_disp;
}

void bsp_display_brightness_set(uint8_t percent)
{
    if (!s_io) return;
    if (percent > 100) percent = 100;
    uint8_t param = (uint8_t)(percent * 255 / 100);
    // CO5300 Write Display Brightness (0x51), QSPI-Kommandoformat wie im BSP.
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    esp_lcd_panel_io_tx_param(s_io, lcd_cmd, &param, 1);
}

bool bsp_display_lock(uint32_t timeout_ms)
{
    return lvgl_port_lock(timeout_ms);
}

void bsp_display_unlock(void)
{
    lvgl_port_unlock();
}
