// miniapp.c — Registry + Runtime für Mini-Apps.
// Hält die laufende App, einen Tick-Timer und leitet Touch/Exit weiter.
#include "miniapp.h"
#include "app_state.h"
#include "ui/ui.h"
#include "board.h"

#include <string.h>
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

static const char *TAG = "miniapp";

// Registrierte Apps (per id gesucht). Beispiel-App siehe miniapp_stopwatch.c.
extern const miniapp_t miniapp_stopwatch;
static const miniapp_t *const s_registry[] = {
    &miniapp_stopwatch,
};

static const miniapp_t *s_cur;
static esp_timer_handle_t s_tick;
static int64_t            s_last_us;
static lv_obj_t          *s_exit_btn;

static const miniapp_t *find(const char *id)
{
    for (size_t i = 0; i < sizeof s_registry / sizeof s_registry[0]; i++)
        if (strcmp(s_registry[i]->id, id) == 0) return s_registry[i];
    return NULL;
}

// Tick-Timer (esp_timer-Task): LVGL locken, App ticken lassen.
static void tick_cb(void *arg)
{
    (void)arg;
    if (!s_cur || !s_cur->tick) return;
    int64_t now = esp_timer_get_time();
    uint32_t dt = (uint32_t)((now - s_last_us) / 1000);
    s_last_us = now;
    if (bsp_display_lock(20)) {
        s_cur->tick(dt);
        bsp_display_unlock();
    }
}

// Touch im Mini-App-Container -> Koordinaten an die App.
static void touch_cb(lv_event_t *e)
{
    if (!s_cur || !s_cur->on_touch) return;
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    s_cur->on_touch(p);
}

// Exit-Button (vom Runtime gestellt) -> app.exit-Pfad.
static void exit_cb(lv_event_t *e)
{
    (void)e;
    xEventGroupSetBits(g_events, EV_APP_EXIT);
}

bool miniapp_active(void) { return s_cur != NULL; }

void miniapp_start(const char *id, const char *params_json)
{
    if (s_cur) miniapp_stop();
    const miniapp_t *app = find(id ? id : "");
    if (!app) { ESP_LOGW(TAG, "unbekannte app '%s'", id ? id : "?"); return; }

    lv_obj_t *parent = ui_miniapp_open();   // leerer Vollbild-Container (Orb aus)
    if (!parent) return;
    s_cur = app;

    if (!bsp_display_lock(100)) { s_cur = NULL; return; }
    app->launch(parent, params_json);

    // Touch-Weiterleitung + Exit-Button stellt das Runtime, nicht die App.
    lv_obj_add_flag(parent, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(parent, touch_cb, LV_EVENT_PRESSING, NULL);

    s_exit_btn = lv_button_create(parent);
    lv_obj_set_size(s_exit_btn, 44, 44);
    lv_obj_align(s_exit_btn, LV_ALIGN_TOP_RIGHT, -16, 16);
    lv_obj_set_style_radius(s_exit_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_exit_btn, lv_color_hex(0x1c1c1e), 0);
    lv_obj_set_style_bg_opa(s_exit_btn, LV_OPA_70, 0);
    lv_obj_t *x = lv_label_create(s_exit_btn);
    lv_label_set_text(x, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(x, lv_color_hex(UI_TEXT), 0);
    lv_obj_center(x);
    lv_obj_add_event_cb(s_exit_btn, exit_cb, LV_EVENT_CLICKED, NULL);
    bsp_display_unlock();

    app_set_state(ST_MINIAPP);

    if (app->tick) {
        s_last_us = esp_timer_get_time();
        const esp_timer_create_args_t ta = { .callback = tick_cb, .name = "miniapp_tick" };
        esp_timer_create(&ta, &s_tick);
        esp_timer_start_periodic(s_tick, 33000);   // ~30 fps
    }
    ESP_LOGI(TAG, "gestartet: %s", app->id);
}

void miniapp_stop(void)
{
    if (!s_cur) return;
    if (s_tick) { esp_timer_stop(s_tick); esp_timer_delete(s_tick); s_tick = NULL; }
    if (s_cur->exit) {
        if (bsp_display_lock(100)) { s_cur->exit(); bsp_display_unlock(); }
    }
    const char *id = s_cur->id;
    s_cur = NULL;
    s_exit_btn = NULL;
    ui_miniapp_close();      // räumt Container (inkl. Exit-Button) ab
    ESP_LOGI(TAG, "beendet: %s", id);
}
