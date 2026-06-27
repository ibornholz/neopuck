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

// Orb-Zustandsfarben (Glow/Ring/Label) + helle Kernfarben — themebar.
#define UI_IDLE         0x7C3AED   // Akzent, ruhig atmend
#define UI_LISTEN       0x34D399   // grün
#define UI_LISTEN_CORE  0xEAFFF4
#define UI_THINK        0xF5A623   // amber
#define UI_SPEAK        0x38BDF8   // cyan
#define UI_SPEAK_CORE   0xCBEEFF
#define UI_ERROR        0xFF3B30   // rot

void ui_init(void);                          // baut alle Screens (LVGL gelockt)
void ui_show_state(app_state_t st);          // wechselt sichtbaren Screen
void ui_set_subtitle(const char *text);      // Live-Transkript / Antworttext
void ui_set_level(uint8_t level);            // Mic-Pegel 0..100 -> Orb-Puls
void ui_set_status(uint8_t batt, bool wifi); // Statusleiste oben
void ui_set_pairing_info(const char *name);  // Provisioning-Screen Text/QR-Daten

// --- Mini-App-Runtime (Task 4) ----------------------------------------------
// Liefert einen leeren, bildschirmfüllenden LVGL-Container für eine Mini-App
// (Orb-UI wird ausgeblendet). ui_miniapp_close() räumt ihn wieder ab.
struct _lv_obj_t *ui_miniapp_open(void);
void              ui_miniapp_close(void);

#ifdef __cplusplus
}
#endif
