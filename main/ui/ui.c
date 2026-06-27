// ui.c — LVGL v9. Ein Screen mit dynamisch umgeschalteten Layern, damit der
// Zustandswechsel ohne Neuaufbau flüssig animiert.
#include "ui.h"
#include "board.h"
#include <stdio.h>
#include <string.h>

// vom Build erzeugt: Firmenlogo als LVGL-Image (PNG -> C-Array).
// Platzhalter: in ui/assets_logo.c ein echtes lv_img_dsc_t 'logo_img' ablegen.
LV_IMG_DECLARE(logo_img);

static lv_obj_t *scr;
static lv_obj_t *status_bar, *batt_lbl, *wifi_dot;
static lv_obj_t *ring;            // lv_arc – pulsiert / reagiert auf Pegel
static lv_obj_t *mic_btn, *mic_icon;
static lv_obj_t *spinner;
static lv_obj_t *subtitle;
static lv_obj_t *logo;
static lv_obj_t *pair_box, *pair_lbl;
static lv_anim_t breathe;

static lv_color_t hex(uint32_t c) { return lv_color_hex(c); }

// sanftes "Atmen" des Rings (Idle/Speaking)
static void breathe_cb(void *obj, int32_t v)
{
    lv_obj_set_style_arc_opa((lv_obj_t *) obj, v, LV_PART_INDICATOR);
}
static void start_breathe(void)
{
    lv_anim_init(&breathe);
    lv_anim_set_var(&breathe, ring);
    lv_anim_set_exec_cb(&breathe, breathe_cb);
    lv_anim_set_values(&breathe, LV_OPA_30, LV_OPA_COVER);
    lv_anim_set_time(&breathe, 1400);
    lv_anim_set_playback_time(&breathe, 1400);
    lv_anim_set_repeat_count(&breathe, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&breathe);
}
static void stop_breathe(void)
{
    lv_anim_delete(ring, breathe_cb);
    lv_obj_set_style_arc_opa(ring, LV_OPA_COVER, LV_PART_INDICATOR);
}

// Tap auf den Mic-Button -> wie PWR-Taste
static void mic_clicked(lv_event_t *e)
{
    (void) e;
    extern EventGroupHandle_t g_events;   // aus app_state
    xEventGroupSetBits(g_events, EV_TALK_PRESS);
}

void ui_init(void)
{
    scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, hex(UI_BG), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // --- Statusleiste (oben, dezent) ---
    status_bar = lv_obj_create(scr);
    lv_obj_remove_style_all(status_bar);
    lv_obj_set_size(status_bar, 200, 24);
    lv_obj_align(status_bar, LV_ALIGN_TOP_MID, 0, 40);
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

    // --- zentraler Ring (Pegel/Atmen) ---
    ring = lv_arc_create(scr);
    lv_obj_set_size(ring, 300, 300);
    lv_obj_center(ring);
    lv_arc_set_rotation(ring, 270);
    lv_arc_set_bg_angles(ring, 0, 360);
    lv_arc_set_value(ring, 100);
    lv_obj_remove_style(ring, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_color(ring, hex(UI_ACCENT_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_width(ring, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ring, hex(UI_ACCENT), LV_PART_INDICATOR);

    // --- Mic-Button (groß, mittig) ---
    mic_btn = lv_button_create(scr);
    lv_obj_set_size(mic_btn, 160, 160);
    lv_obj_center(mic_btn);
    lv_obj_set_style_radius(mic_btn, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mic_btn, hex(UI_ACCENT), 0);
    lv_obj_set_style_bg_grad_color(mic_btn, hex(UI_ACCENT_DIM), 0);
    lv_obj_set_style_bg_grad_dir(mic_btn, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_shadow_color(mic_btn, hex(UI_ACCENT), 0);
    lv_obj_set_style_shadow_width(mic_btn, 40, 0);
    lv_obj_set_style_shadow_opa(mic_btn, LV_OPA_40, 0);
    lv_obj_add_event_cb(mic_btn, mic_clicked, LV_EVENT_CLICKED, NULL);

    mic_icon = lv_label_create(mic_btn);
    lv_label_set_text(mic_icon, LV_SYMBOL_AUDIO);
    lv_obj_set_style_text_color(mic_icon, hex(UI_TEXT), 0);
    lv_obj_center(mic_icon);

    // --- Spinner (Thinking) ---
    spinner = lv_spinner_create(scr);
    lv_obj_set_size(spinner, 80, 80);
    lv_obj_center(spinner);
    lv_obj_set_style_arc_color(spinner, hex(UI_ACCENT_DIM), LV_PART_MAIN);
    lv_obj_set_style_arc_color(spinner, hex(UI_ACCENT), LV_PART_INDICATOR);
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);

    // --- Untertitel (Transkript/Antwort) ---
    subtitle = lv_label_create(scr);
    lv_label_set_long_mode(subtitle, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(subtitle, 360);
    lv_obj_set_style_text_align(subtitle, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(subtitle, hex(UI_TEXT), 0);
    lv_obj_align(subtitle, LV_ALIGN_CENTER, 0, 120);
    lv_label_set_text(subtitle, "");

    // --- Logo (klein, unten) ---
    logo = lv_image_create(scr);
    lv_image_set_src(logo, &logo_img);
    lv_obj_align(logo, LV_ALIGN_BOTTOM_MID, 0, -34);
    lv_obj_set_style_image_opa(logo, LV_OPA_80, 0);

    // --- Pairing-Box (Provisioning) ---
    pair_box = lv_obj_create(scr);
    lv_obj_set_size(pair_box, 360, 360);
    lv_obj_center(pair_box);
    lv_obj_set_style_bg_color(pair_box, hex(UI_BG), 0);
    lv_obj_set_style_border_width(pair_box, 0, 0);
    pair_lbl = lv_label_create(pair_box);
    lv_label_set_long_mode(pair_lbl, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(pair_lbl, 320);
    lv_obj_set_style_text_align(pair_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(pair_lbl, hex(UI_TEXT), 0);
    lv_obj_center(pair_lbl);
    lv_obj_add_flag(pair_box, LV_OBJ_FLAG_HIDDEN);

    ui_show_state(ST_BOOT);
}

static void hide_all_dynamic(void)
{
    lv_obj_add_flag(spinner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(pair_box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(mic_btn, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(ring, LV_OBJ_FLAG_HIDDEN);
    stop_breathe();
}

void ui_show_state(app_state_t st)
{
    if (!bsp_display_lock(100)) return;
    hide_all_dynamic();

    switch (st) {
    case ST_PROVISION:
        lv_obj_add_flag(mic_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(pair_box, LV_OBJ_FLAG_HIDDEN);
        break;

    case ST_CONNECTING:
        lv_obj_add_flag(mic_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(subtitle, "verbinde …");
        break;

    case ST_IDLE:
        lv_arc_set_value(ring, 100);
        start_breathe();
        lv_label_set_text(mic_icon, LV_SYMBOL_AUDIO);
        lv_label_set_text(subtitle, "");
        break;

    case ST_LISTENING:
        lv_obj_set_style_arc_color(ring, hex(UI_ACCENT), LV_PART_INDICATOR);
        lv_label_set_text(mic_icon, LV_SYMBOL_STOP);
        break;

    case ST_THINKING:
        lv_obj_add_flag(mic_btn, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ring, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(spinner, LV_OBJ_FLAG_HIDDEN);
        break;

    case ST_SPEAKING:
        lv_obj_add_flag(mic_btn, LV_OBJ_FLAG_HIDDEN);
        start_breathe();
        break;

    case ST_ERROR:
        lv_obj_set_style_arc_color(ring, hex(0xFF3B30), LV_PART_INDICATOR);
        lv_label_set_text(subtitle, "Verbindung gestört");
        break;
    default: break;
    }
    bsp_display_unlock();
}

void ui_set_subtitle(const char *text)
{
    if (!bsp_display_lock(50)) return;
    lv_label_set_text(subtitle, text ? text : "");
    bsp_display_unlock();
}

void ui_set_level(uint8_t level)
{
    if (!bsp_display_lock(20)) return;
    // Pegel -> Ringbreite, damit es "lebt"
    lv_obj_set_style_arc_width(ring, 6 + (level / 10), LV_PART_INDICATOR);
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
    char buf[160];
    snprintf(buf, sizeof buf,
        "Mit dem iPhone koppeln\n\nBLE: %s\nPIN: neopuck-pop\n\n"
        "App: ESP BLE Provisioning", name ? name : "PROV_neopuck");
    lv_label_set_text(pair_lbl, buf);
    bsp_display_unlock();
}
