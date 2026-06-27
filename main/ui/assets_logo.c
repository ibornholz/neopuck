// assets_logo.c — PLATZHALTER fürs Firmenlogo.
// So ersetzt du es durch dein echtes Logo:
//   1) Logo als PNG (z.B. 96x32, transparent) vorbereiten.
//   2) LVGL Image Converter (https://lvgl.io/tools/imageconverter) -> Format
//      "LVGL v9 / True color with alpha" -> erzeugt ein lv_img_dsc_t.
//   3) Den erzeugten C-Code hier einsetzen und 'logo_img' benennen.
// Alternativ zur Laufzeit von SD laden: lv_image_set_src(logo, "S:/logo.png").
#include "lvgl.h"

// Minimaler 2x2-Platzhalter (transparent), damit der Build steht.
static const uint8_t logo_map[] = {
    0x00,0x00,0x00,0x00,  0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,  0x00,0x00,0x00,0x00,
};

const lv_img_dsc_t logo_img = {
    .header.cf    = LV_COLOR_FORMAT_ARGB8888,
    .header.w     = 2,
    .header.h     = 2,
    .data_size    = sizeof(logo_map),
    .data         = logo_map,
};
