// miniapp_qr.c — zeigt einen QR-Code, den der Agent per app.launch schickt.
// Beispiel-Aufruf vom Agent/Bridge:
//   {"type":"app.launch","app":"qr","params":{"data":"https://…","caption":"Scan"}}
#include "miniapp.h"
#include "ui/ui.h"
#include "cJSON.h"
#include <string.h>

static lv_obj_t *s_qr, *s_cap;

static void qr_launch(lv_obj_t *parent, const char *params_json)
{
    const char *data = "https://neopuck.local";
    const char *caption = NULL;

    cJSON *root = params_json ? cJSON_Parse(params_json) : NULL;
    if (root) {
        const cJSON *d = cJSON_GetObjectItemCaseSensitive(root, "data");
        const cJSON *c = cJSON_GetObjectItemCaseSensitive(root, "caption");
        if (cJSON_IsString(d) && d->valuestring[0]) data = d->valuestring;
        if (cJSON_IsString(c) && c->valuestring[0]) caption = c->valuestring;
    }

    s_qr = lv_qrcode_create(parent);
    lv_qrcode_set_size(s_qr, 260);
    lv_qrcode_set_dark_color(s_qr, lv_color_hex(0x000000));
    lv_qrcode_set_light_color(s_qr, lv_color_hex(0xFFFFFF));
    lv_obj_set_style_border_color(s_qr, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(s_qr, 8, 0);
    lv_qrcode_update(s_qr, data, strlen(data));
    lv_obj_center(s_qr);

    s_cap = lv_label_create(parent);
    lv_obj_set_style_text_color(s_cap, lv_color_hex(UI_TEXT), 0);
    lv_obj_align(s_cap, LV_ALIGN_CENTER, 0, 165);
    lv_label_set_text(s_cap, caption ? caption : "");

    if (root) cJSON_Delete(root);
}

static void qr_exit(void)
{
    s_qr = NULL;   // Objekte gehören dem Container und werden dort abgeräumt
    s_cap = NULL;
}

// kein tick/on_touch nötig — statischer Screen, Exit über den Runtime-Button.
const miniapp_t miniapp_qr = {
    .id = "qr",
    .launch = qr_launch,
    .tick = NULL,
    .on_touch = NULL,
    .exit = qr_exit,
};
