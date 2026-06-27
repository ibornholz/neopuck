// ui.h — LVGL-Oberfläche. Dunkles AMOLED-Theme, runder Layout für 466x466.
#pragma once
#include "app_state.h"

#ifdef __cplusplus
extern "C" {
#endif

// Akzentfarbe (neonet-Purple). Hintergrund bleibt true black.
#define UI_ACCENT       0x7C3AED
#define UI_ACCENT_DIM   0x3B1F73
#define UI_TEXT         0xF2F2F7
#define UI_TEXT_DIM     0x8A8A8E
#define UI_BG           0x000000

void ui_init(void);                          // baut alle Screens (LVGL gelockt)
void ui_show_state(app_state_t st);          // wechselt sichtbaren Screen
void ui_set_subtitle(const char *text);      // Live-Transkript / Antworttext
void ui_set_level(uint8_t level);            // Mic-Pegel 0..100 -> Ring-Animation
void ui_set_status(uint8_t batt, bool wifi); // Statusleiste oben
void ui_set_pairing_info(const char *name);  // Provisioning-Screen Text/QR-Daten

#ifdef __cplusplus
}
#endif
