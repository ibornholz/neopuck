// bsp_buttons.c — BOOT-Taste (GPIO0) und PWR-Taste (AXP2101-PEKEY).
// BOOT liefert echtes DOWN/UP/LONG per Polling; PWR kommt als PEKEY-IRQ vom PMU:
// kurzer Druck -> DOWN+UP (Tap), langer Druck -> LONG.
#include "board.h"
#include "bsp_internal.h"
#include "bsp_pins.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

static const char *TAG = "bsp_btn";

#define POLL_MS        20
#define LONG_PRESS_MS  800
#define DEBOUNCE_MS    30

static bsp_button_cb_t s_cb;
static void           *s_arg;

static void emit(bsp_button_t btn, bsp_button_evt_t evt)
{
    ESP_LOGD(TAG, "btn=%d evt=%d", btn, evt);
    if (s_cb) s_cb(btn, evt, s_arg);
}

static void buttons_task(void *arg)
{
    (void)arg;
    bool     boot_down = false;
    bool     boot_long_sent = false;
    int64_t  boot_t0 = 0;
    int64_t  last_change = 0;

    for (;;) {
        int64_t now = esp_timer_get_time() / 1000;   // ms

        // --- BOOT (GPIO0, active-low) ---
        bool pressed = (gpio_get_level(BSP_BTN_BOOT_GPIO) == 0);
        if (pressed != boot_down && (now - last_change) > DEBOUNCE_MS) {
            last_change = now;
            boot_down = pressed;
            if (pressed) {
                boot_t0 = now;
                boot_long_sent = false;
                emit(BSP_BTN_BOOT, BSP_BTN_DOWN);
            } else {
                emit(BSP_BTN_BOOT, BSP_BTN_UP);
            }
        }
        if (boot_down && !boot_long_sent && (now - boot_t0) >= LONG_PRESS_MS) {
            boot_long_sent = true;
            emit(BSP_BTN_BOOT, BSP_BTN_LONG);
        }

        // --- PWR (AXP2101 PEKEY) ---
        switch (bsp_power_poll_pekey()) {
        case BSP_PEKEY_SHORT:
            emit(BSP_BTN_PWR, BSP_BTN_DOWN);
            emit(BSP_BTN_PWR, BSP_BTN_UP);
            break;
        case BSP_PEKEY_LONG:
            emit(BSP_BTN_PWR, BSP_BTN_LONG);
            break;
        default: break;
        }

        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}

void bsp_buttons_init(bsp_button_cb_t cb, void *arg)
{
    s_cb = cb;
    s_arg = arg;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << BSP_BTN_BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    bsp_power_init();   // PMU für PEKEY-Polling sicherstellen

    xTaskCreatePinnedToCore(buttons_task, "bsp_btn", 3072, NULL, 4, NULL, 0);
    ESP_LOGI(TAG, "buttons up (BOOT=GPIO%d, PWR=AXP PEKEY)", BSP_BTN_BOOT_GPIO);
}
