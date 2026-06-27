// board.h — schmales BSP-Interface. Implementiert wird das vom offiziellen
// Waveshare-BSP (CO5300 / CST9217 / ES8311 / ES7210 / AXP2101). Die App ruft
// nur diese Funktionen — kein erfundenes Pin-/Clock-Setup in der App-Schicht.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// --- Display + Touch ---------------------------------------------------------
// Initialisiert QSPI-AMOLED + Touch, startet esp_lvgl_port und liefert den
// Default-Display-Handle. Backlight/Brightness via AXP/Panel.
lv_display_t *bsp_display_start(void);
void          bsp_display_brightness_set(uint8_t percent);   // 0..100

// LVGL ist nicht thread-safe: vor jedem UI-Zugriff aus Fremd-Tasks locken.
bool bsp_display_lock(uint32_t timeout_ms);
void bsp_display_unlock(void);

// --- Audio -------------------------------------------------------------------
// Capture läuft über ES7210 (Dual-Mic + AEC) -> PCM16 16k mono.
// Playback über ES8311 -> Speaker. Beides als blocking read/write auf I2S.
void   bsp_audio_init(uint32_t sample_rate_hz);   // typ. 16000
size_t bsp_mic_read(int16_t *dst, size_t max_samples, uint32_t timeout_ms);
size_t bsp_spk_write(const int16_t *src, size_t samples, uint32_t timeout_ms);
void   bsp_spk_volume_set(uint8_t percent);       // 0..100

// --- Buttons -----------------------------------------------------------------
typedef enum {
    BSP_BTN_PWR  = 0,   // Push-to-Talk
    BSP_BTN_BOOT = 1,   // lang -> Settings
} bsp_button_t;

typedef enum {
    BSP_BTN_DOWN,
    BSP_BTN_UP,
    BSP_BTN_LONG,       // > 800 ms gehalten
} bsp_button_evt_t;

typedef void (*bsp_button_cb_t)(bsp_button_t btn, bsp_button_evt_t evt, void *arg);
void bsp_buttons_init(bsp_button_cb_t cb, void *arg);

// --- Power -------------------------------------------------------------------
uint8_t bsp_battery_percent(void);   // 0..100 via AXP2101
bool    bsp_is_charging(void);

#ifdef __cplusplus
}
#endif
