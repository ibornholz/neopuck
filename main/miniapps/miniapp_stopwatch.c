// miniapp_stopwatch.c — bewusst kleine Beispiel-App: eine Stoppuhr.
// Demonstriert den kompletten Mini-App-Flow (launch/tick/on_touch/exit).
// Tap = zurücksetzen. Beenden über den Exit-Button (stellt das Runtime).
#include "miniapp.h"
#include "ui/ui.h"
#include <stdio.h>

static lv_obj_t *s_time, *s_hint;
static uint32_t  s_elapsed_ms;

static void sw_render(void)
{
    if (!s_time) return;
    uint32_t s = s_elapsed_ms / 1000;
    char buf[16];
    snprintf(buf, sizeof buf, "%02u:%02u.%u",
             (unsigned)(s / 60), (unsigned)(s % 60),
             (unsigned)((s_elapsed_ms % 1000) / 100));
    lv_label_set_text(s_time, buf);
}

static void sw_launch(lv_obj_t *parent, const char *params_json)
{
    (void)params_json;
    s_elapsed_ms = 0;

    s_time = lv_label_create(parent);
    lv_obj_set_style_text_color(s_time, lv_color_hex(UI_SPEAK), 0);
    lv_obj_set_style_text_font(s_time, &lv_font_montserrat_14, 0);
    lv_obj_set_style_transform_scale(s_time, 384, 0);  // ~3x für große Ziffern
    lv_obj_center(s_time);
    sw_render();

    s_hint = lv_label_create(parent);
    lv_obj_set_style_text_color(s_hint, lv_color_hex(UI_TEXT_DIM), 0);
    lv_obj_align(s_hint, LV_ALIGN_CENTER, 0, 80);
    lv_label_set_text(s_hint, "tap = reset");
}

static void sw_tick(uint32_t dt_ms)
{
    s_elapsed_ms += dt_ms;
    sw_render();
}

static void sw_on_touch(lv_point_t p)
{
    (void)p;
    s_elapsed_ms = 0;   // Tap setzt zurück
    sw_render();
}

static void sw_exit(void)
{
    s_time = NULL;      // Objekte gehören dem Container und werden dort abgeräumt
    s_hint = NULL;
}

const miniapp_t miniapp_stopwatch = {
    .id = "stopwatch",
    .launch = sw_launch,
    .tick = sw_tick,
    .on_touch = sw_on_touch,
    .exit = sw_exit,
};
