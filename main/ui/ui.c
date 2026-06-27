// ui.c — LVGL v9, AMOLED 466x466, true-black. Zentrale Metapher: ein leuchtender
// "Orb", dessen Farbe den Zustand kodiert. Aufbau:
//   oben    : Transkript/Antworttext (mehrzeilig, zentriert, dezenter Weißton)
//   mitte   : Orb = heller Kern + konzentrische Glow-Schichten + dünner Ring
//   darunter: Status-Label (Versalien, gesperrt)
//   darunter: Hint-Zeile (gedimmt)
//   unten   : Logo (Opazität ~80%)
// LVGL ist nicht thread-safe: jeder Zugriff aus Fremd-Tasks lockt via BSP.
#include "ui.h"
#include "board.h"
#include "audio_pipeline.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdio.h>
#include <string.h>

LV_IMG_DECLARE(logo_img);

// --- Layer/Objekte ------------------------------------------------------------
static lv_obj_t *scr;
static lv_obj_t *status_bar, *batt_lbl, *wifi_dot;
static lv_obj_t *orb_layer;                 // Container für die Orb-Ansicht
static lv_obj_t *ring;                      // dünner Ring (lv_arc)
static lv_obj_t *glow_a, *glow_b, *core;    // Glow-Schichten + heller Kern
static lv_obj_t *think_dot;                 // kleiner Punkt für THINKING
static lv_obj_t *transcript;                // oben
static lv_obj_t *status_lbl, *hint_lbl;     // unter dem Orb
static lv_obj_t *logo;
static lv_obj_t *pair_box, *pair_lbl, *qr;  // Provisioning
static lv_obj_t *miniapp_layer;             // Task 4
static lv_anim_t breathe;

static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

// --- sanftes "Atmen" (Idle/Speaking): Glow-Opazität pulsiert ------------------
static void breathe_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)obj, v, 0);
}
static void start_breathe(void)
{
    lv_anim_init(&breathe);
    lv_anim_set_var(&breathe, glow_a);
    lv_anim_set_exec_cb(&breathe, breathe_cb);
    lv_anim_set_values(&breathe, LV_OPA_20, LV_OPA_60);
    lv_anim_set_duration(&breathe, 1500);
    lv_anim_set_playback_duration(&breathe, 1500);
    lv_anim_set_repeat_count(&breathe, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&breathe);
}
static void stop_breathe(void)
{
    lv_anim_delete(glow_a, breathe_cb);
    lv_obj_set_style_bg_opa(glow_a, LV_OPA_40, 0);
}

// Orb + Ring + Status-Label einfärben.
static void set_orb_color(uint32_t glow, uint32_t core_col)
{
    lv_obj_set_style_bg_color(glow_a, hex(glow), 0);
    lv_obj_set_style_bg_color(glow_b, hex(glow), 0);
    lv_obj_set_style_bg_color(core,   hex(core_col), 0);
    lv_obj_set_style_shadow_color(core, hex(glow), 0);
    lv_obj_set_style_arc_color(ring, hex(glow), LV_PART_INDICATOR);
    lv_obj_set_style_text_color(status_lbl, hex(glow), 0);
}

// Tap auf den Orb: im SPEAKING-Zustand = "tap to dismiss" (Wiedergabe abbrechen),
// sonst wie PWR/Talk-Taste.
static void orb_clicked(lv_event_t *e)
{
    (void)e;
    extern EventGroupHandle_t g_events;
    if (app_get_state() == ST_SPEAKING) {
        audio_play_flush();
        app_set_state(ST_IDLE);
    } else {
        xEventGroupSetBits(g_events, EV_TALK_PRESS);
    }
}

// einen runden, randlosen Vollkreis erzeugen
static lv_obj_t *circle(lv_obj_t *parent, int size, uint32_t color, lv_opa_t opa)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_size(o, size, size);
    lv_obj_center(o);
    lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_bg_color(o, hex(color), 0);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

void ui_init(void)
{
    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    // --- Statusleiste (oben, dezent) ---
    status_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, 200, 24);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 30);
    lv_obj_set_flex_flow(status_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(status_bar, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    wifi_dot = lv_obj_create(status_bar);
    lv_obj_remove_style_all(wifi_dot);
    lv_obj_set_size(wifi_dot, 8, 8);
    lv_obj_set_style_radius(wifi_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(wifi_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(wifi_dot, hex(UI_TEXT_DIM), 0);
    batt_lbl = lv_label_create(status_bar);
    lv_obj_set_style_text_color(batt_lbl, hex(UI_TEXT_DIM), 0);
    lv_obj_set_style_pad_left(batt_lbl, 8, 0);
    lv_label_set_text(batt_lbl, "—%");

    // --- Transkript/Antwort (oben) ---
    transcript = lv_label_create(scr);
    lv_label_set_long_mode(transcript, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(transcript, 360);
    lv_obj_set_style_text_align(transcript, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(transcript, hex(UI_TEXT), 0);
    lv_obj_align(transcript, LV_ALIGN_TOP_MID, 0, 78);
    lv_label_set_text(transcript, "");

    // --- Orb-Layer (mitte) ---
    orb_layer = lv_obj_create(scr);
    lv_obj_remove_style_all(orb_layer);
    lv_obj_set_size(orb_layer, 320, 320);
    lv_obj_center(orb_layer);
    lv_obj_clear_flag(orb_layer, LV_OBJ_FLAG_SCROLLABLE);

    // dünner Ring drumherum
    ring = lv_arc_create(orb_layer);
    lv_obj_set_size(ring, 300, 300);
    lv_obj_center(ring);
    lv_arc_set_rotation(ring, 270);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_value(ring, 100);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, hex(UI_ACCENT_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, hex(UI_ACCENT), LV_PART_INDICATOR);

    // konzentrische Glow-Schichten (fallende Opazität) + heller Kern
    glow_a = circle(orb_layer, 240, UI_ACCENT, LV_OPA_40);
    glow_b = circle(orb_layer, 170, UI_ACCENT, LV_OPA_60);
    core   = circle(orb_layer, 96,  UI_ACCENT, LV_OPA_COVER);
    lv_obj_set_style_shadow_color(core, hex(UI_ACCENT), 0);
    lv_obj_set_style_shadow_width(core, 48, 0);
    lv_obj_set_style_shadow_spread(core, 4, 0);
    lv_obj_set_style_shadow_opa(core, LV_OPA_70, 0);
    // Orb ist tippbar (Talk / tap-to-dismiss)
    lv_obj_add_flag(core, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(core, orb_clicked, LV_EVENT_CLICKED, NULL);

    // kleiner Punkt für THINKING (in den Kern gesetzt)
    think_dot = circle(orb_layer, 18, UI_TEXT, LV_OPA_COVER);
    lv_obj_add_flag(think_dot, LV_OBJ_FLAG_HIDDEN);

    // --- Status-Label (Versalien, gesperrt) ---
    status_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(status_lbl, hex(UI_ACCENT), 0);
    lv_obj_set_style_text_letter_space(status_lbl, 4, 0);
    lv_obj_align(status_lbl, LV_ALIGN_CENTER, 0, 120);
    lv_label_set_text(status_lbl, "");

    // --- Hint-Zeile (gedimmt) ---
    hint_lbl = lv_label_create(scr);
    lv_obj_set_style_text_color(hint_lbl, hex(UI_TEXT_DIM), 0);
    lv_obj_align(hint_lbl, LV_ALIGN_CENTER, 0, 148);
    lv_label_set_text(hint_lbl, "");

    // --- Logo (klein, unten) ---
    logo = lv_image_create(scr);
    lv_image_set_src(logo, &logo_img);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_MID, 0, -30);
    lv_obj_set_style_image_opa(logo, LV_OPA_80, 0);

    // --- Pairing-Box (Provisioning, Task 3) ---
    pair_box = lv_obj_create(scr);
    lv_obj_set_size(pair_box, 440, 440);
    lv_obj_center(pair_box);
    lv_obj_set_style_bg_color(pair_box, hex(UI_BG), 0);
    lv_obj_set_style_border_width(pair_box, 0, 0);
    lv_obj_set_flex_flow(pair_box, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(pair_box, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    qr = lv_qrcode_create(pair_box);
    lv_qrcode_set_size(qr, 200);
    lv_qrcode_set_dark_color(qr, hex(0x000000));
    lv_qrcode_set_light_color(qr, hex(0xFFFFFF));
    lv_obj_set_style_border_color(qr, hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(qr, 6, 0);
    pair_lbl = lv_label_create(pair_box);
    lv_label_set_long_mode(pair_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(pair_lbl, 380);
    lv_obj_set_style_pad_top(pair_lbl, 16, 0);
    lv_obj_set_style_text_align(pair_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(pair_lbl, hex(UI_TEXT), 0);
    lv_obj_add_flag(pair_box, LV_OBJ_FLAG_HIDDEN);

    // --- Mini-App-Layer (Task 4) ---
    miniapp_layer = lv_obj_create(scr);
    lv_obj_remove_style_all(miniapp_layer);
    lv_obj_set_size(miniapp_layer, 466, 466);
    lv_obj_center(miniapp_layer);
    lv_obj_set_style_bg_color(miniapp_layer, hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(miniapp_layer, LV_OPA_COVER, 0);
    lv_obj_clear_flag(miniapp_layer, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(miniapp_layer, LV_OBJ_FLAG_HIDDEN);

    ui_show_state(ST_BOOT);
}

// Sichtbarkeit der Haupt-Layer schalten.
static void show_orb(bool on)
{
    if (on) {
        lv_obj_clear_flag(orb_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(hint_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(orb_layer, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(status_lbl, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(hint_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

void ui_show_state(app_state_t st)
{
    if (!bsp_display_lock(100)) return;

    // Default: Orb sichtbar, Provisioning/Miniapp aus, Atmen aus, Punkt aus.
    stop_breathe();
    lv_obj_add_flag(think_dot, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pair_box, LV_OBJ_FLAG_HIDDEN);
    if (st != ST_MINIAPP) lv_obj_add_flag(miniapp_layer, LV_OBJ_FLAG_HIDDEN);
    show_orb(true);
    lv_obj_set_size(core, 96, 96);   // Kern-Default

    switch (st) {
    case ST_PROVISION:
        show_orb(false);
        lv_obj_clear_flag(pair_box, LV_OBJ_FLAG_HIDDEN);
        break;

    case ST_CONNECTING:
        set_orb_color(UI_IDLE, UI_IDLE);
        lv_label_set_text(status_lbl, "VERBINDE");
        lv_label_set_text(hint_lbl, "");
        lv_label_set_text(transcript, "");
        start_breathe();
        break;

    case ST_IDLE:
        set_orb_color(UI_IDLE, UI_IDLE);
        lv_label_set_text(status_lbl, "BEREIT");
        lv_label_set_text(hint_lbl, "");
        lv_label_set_text(transcript, "");
        start_breathe();
        break;

    case ST_LISTENING:
        set_orb_color(UI_LISTEN, UI_LISTEN_CORE);
        lv_label_set_text(status_lbl, "HÖRE ZU");
        lv_label_set_text(hint_lbl, "release to send");
        break;

    case ST_THINKING:
        set_orb_color(UI_THINK, UI_THINK);
        lv_obj_clear_flag(think_dot, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(core, 70, 70);
        lv_label_set_text(status_lbl, "DENKE");
        lv_label_set_text(hint_lbl, "");
        start_breathe();
        break;

    case ST_SPEAKING:
        set_orb_color(UI_SPEAK, UI_SPEAK_CORE);
        lv_label_set_text(status_lbl, "SPRECHE");
        lv_label_set_text(hint_lbl, "tap to dismiss");
        start_breathe();
        break;

    case ST_ERROR:
        set_orb_color(UI_ERROR, UI_ERROR);
        lv_label_set_text(status_lbl, "FEHLER");
        lv_label_set_text(hint_lbl, "Verbindung gestört");
        break;

    case ST_MINIAPP:
        show_orb(false);
        lv_obj_clear_flag(miniapp_layer, LV_OBJ_FLAG_HIDDEN);
        break;

    default:
        lv_label_set_text(status_lbl, "");
        break;
    }
    bsp_display_unlock();
}

void ui_set_subtitle(const char *text)
{
    if (!bsp_display_lock(50)) return;
    lv_label_set_text(transcript, text ? text : "");
    bsp_display_unlock();
}

void ui_set_level(uint8_t level)
{
    if (!bsp_display_lock(20)) return;
    // Pegel -> Kerngröße, damit der Orb beim Sprechen "lebt".
    int sz = 96 + (level * 60 / 100);
    lv_obj_set_size(core, sz, sz);
    bsp_display_unlock();
}

void ui_set_status(uint8_t batt, bool wifi)
{
    if (!bsp_display_lock(50)) return;
    char b[8]; snprintf(b, sizeof b, "%u%%", batt);
    lv_label_set_text(batt_lbl, b);
    lv_obj_set_style_bg_color(wifi_dot, hex(wifi ? UI_ACCENT : UI_TEXT_DIM), 0);
    bsp_display_unlock();
}

void ui_set_pairing_info(const char *name)
{
    if (!bsp_display_lock(50)) return;
    const char *ssid = name ? name : "neopuck-setup";
    // QR encodiert NUR den Join ins offene Setup-WLAN (kein Heim-Passwort).
    char wifi_qr[96];
    snprintf(wifi_qr, sizeof wifi_qr, "WIFI:T:nopass;S:%s;;", ssid);
    lv_qrcode_update(qr, wifi_qr, strlen(wifi_qr));

    char buf[160];
    snprintf(buf, sizeof buf,
        "WLAN \"%s\" scannen\noder verbinden,\ndann  http://192.168.4.1", ssid);
    lv_label_set_text(pair_lbl, buf);
    bsp_display_unlock();
}

// --- Mini-App-Runtime (Task 4) ----------------------------------------------
lv_obj_t *ui_miniapp_open(void)
{
    if (!bsp_display_lock(100)) return NULL;
    lv_obj_clean(miniapp_layer);                    // alte Mini-App-Reste weg
    lv_obj_clear_flag(miniapp_layer, LV_OBJ_FLAG_HIDDEN);
    show_orb(false);
    lv_obj_add_flag(pair_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *parent = miniapp_layer;
    bsp_display_unlock();
    return parent;
}

void ui_miniapp_close(void)
{
    if (!bsp_display_lock(100)) return;
    lv_obj_clean(miniapp_layer);
    lv_obj_add_flag(miniapp_layer, LV_OBJ_FLAG_HIDDEN);
    bsp_display_unlock();
}
