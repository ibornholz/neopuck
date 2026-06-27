// app_main.c — bringt alles zusammen: Board, Config, Provisioning, Audio,
// Agent-WebSocket, UI. Die State-Machine läuft als Event-getriebene Schleife.
#include "app_state.h"
#include "config_store.h"
#include "provisioning.h"
#include "agent_client.h"
#include "audio_pipeline.h"
#include "board.h"
#include "ui/ui.h"
#include "miniapps/miniapp.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "app";

EventGroupHandle_t g_events;
static volatile app_state_t s_state = ST_BOOT;

app_state_t app_get_state(void) { return s_state; }
void app_set_state(app_state_t st)
{
    s_state = st;
    ui_show_state(st);
    ESP_LOGI(TAG, "state -> %d", st);
}

// --- Buttons (BSP) -> Events --------------------------------------------------
static void on_button(bsp_button_t btn, bsp_button_evt_t evt, void *arg)
{
    static bool boot_long;
    if (btn == BSP_BTN_PWR) {
        if (evt == BSP_BTN_DOWN) xEventGroupSetBits(g_events, EV_TALK_PRESS);
        if (evt == BSP_BTN_UP)   xEventGroupSetBits(g_events, EV_TALK_RELEASE);
        if (evt == BSP_BTN_LONG) xEventGroupSetBits(g_events, EV_SLEEP);   // PWR lang -> Schlafen/Wecken
    } else if (btn == BSP_BTN_BOOT) {
        if (evt == BSP_BTN_DOWN) boot_long = false;
        else if (evt == BSP_BTN_LONG) { boot_long = true; xEventGroupSetBits(g_events, EV_SETTINGS); }
        else if (evt == BSP_BTN_UP && !boot_long) xEventGroupSetBits(g_events, EV_HOME); // BOOT kurz -> Bereit
    }
}

// --- Agent-Callbacks ----------------------------------------------------------
static void cb_transcript(const char *role, const char *text, bool final)
{
    ui_set_subtitle(text);
}
static void cb_response_text(const char *text) { ui_set_subtitle(text); }
static void cb_audio(const int16_t *pcm, size_t n) { audio_play_pcm(pcm, n); }
static void cb_state_event(uint32_t bit) { xEventGroupSetBits(g_events, bit); }

// --- UI-Status-Ticker (Akku/WLAN/Pegel) ---------------------------------------
static void status_task(void *arg)
{
    for (;;) {
        ui_set_status(bsp_battery_percent(), agent_client_is_connected());
        if (s_state == ST_LISTENING) ui_set_level(audio_input_level());
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

// --- zentrale Event-Loop ------------------------------------------------------
static void app_loop(void)
{
    const app_config_t *c = config_get();
    const EventBits_t ANY =
        EV_TALK_PRESS | EV_TALK_RELEASE | EV_SETTINGS |
        EV_WIFI_UP | EV_WIFI_DOWN | EV_AGENT_UP | EV_AGENT_DOWN |
        EV_AGENT_THINK | EV_AGENT_SPEAK | EV_AGENT_DONE | EV_AGENT_ERROR |
        EV_PROV_DONE | EV_APP_LAUNCH | EV_APP_EXIT | EV_HOME | EV_SLEEP;

    for (;;) {
        EventBits_t b = xEventGroupWaitBits(g_events, ANY, pdTRUE, pdFALSE,
                                            portMAX_DELAY);

        if (b & EV_PROV_DONE)  provisioning_start();            // neu verbinden
        if (b & EV_SETTINGS) {
            if (miniapp_active()) miniapp_stop();
            agent_client_disconnect();   // Agent-Retries stoppen, sonst überschreibt
                                         // EV_AGENT_DOWN gleich wieder das Portal mit ST_ERROR
            provisioning_reopen();
            continue;
        }

        // --- Mini-App-Runtime (Task 4) ---
        if (b & EV_APP_LAUNCH) {
            agent_launch_t l;
            if (agent_pop_launch(&l)) miniapp_start(l.id, l.params);  // -> ST_MINIAPP
        }
        if (b & EV_APP_EXIT) {
            if (miniapp_active()) miniapp_stop();
            app_set_state(ST_IDLE);
        }
        // Während eine Mini-App läuft, sind die Voice-States pausiert.
        if (s_state == ST_MINIAPP) continue;

        // Im Provisioning-/Portal-Modus dürfen WLAN-/Agent-Events den QR-Screen
        // NICHT überschreiben (sonst springt das Portal sofort auf ST_ERROR).
        if (s_state == ST_PROVISION) continue;

        // --- Schlafen / Aufwecken (PWR lang) ---
        if (b & EV_SLEEP) {
            if (s_state == ST_SLEEP) {                 // aufwecken
                bsp_display_brightness_set(c->brightness);
                app_set_state(agent_client_is_connected() ? ST_IDLE : ST_CONNECTING);
            } else {                                   // schlafen legen
                audio_capture_stop();
                audio_play_flush();
                bsp_display_brightness_set(0);
                app_set_state(ST_SLEEP);
            }
            continue;
        }
        if (s_state == ST_SLEEP) {
            if (b & (EV_TALK_PRESS | EV_TALK_RELEASE | EV_HOME)) {  // jeder Druck weckt
                bsp_display_brightness_set(c->brightness);
                app_set_state(agent_client_is_connected() ? ST_IDLE : ST_CONNECTING);
            }
            continue;
        }

        // --- BOOT kurz: zurück auf "Bereit" (enthängt z.B. THINKING) ---
        if (b & EV_HOME) {
            audio_capture_stop();
            audio_play_flush();
            if (miniapp_active()) miniapp_stop();
            app_set_state(agent_client_is_connected() ? ST_IDLE : ST_CONNECTING);
            continue;
        }

        if (b & EV_WIFI_UP)   agent_client_connect();
        if (b & EV_WIFI_DOWN) app_set_state(ST_CONNECTING);

        if (b & EV_AGENT_UP)   app_set_state(ST_IDLE);
        if (b & EV_AGENT_DOWN) app_set_state(ST_ERROR);
        if (b & EV_AGENT_ERROR) app_set_state(ST_ERROR);

        // Talk-Start: nur aus IDLE
        if ((b & EV_TALK_PRESS) && s_state == ST_IDLE) {
            audio_capture_start();
            app_set_state(ST_LISTENING);
        }
        // Push-to-Talk: Loslassen beendet die Aufnahme.
        // Toggle-Modus: zweiter Press beendet (zweiter PRESS während LISTENING).
        bool stop_listen =
            (c->push_to_talk && (b & EV_TALK_RELEASE) && s_state == ST_LISTENING) ||
            (!c->push_to_talk && (b & EV_TALK_PRESS)   && s_state == ST_LISTENING);
        if (stop_listen) {
            audio_capture_stop();
            app_set_state(ST_THINKING);
        }

        if (b & EV_AGENT_THINK) app_set_state(ST_THINKING);
        if (b & EV_AGENT_SPEAK) app_set_state(ST_SPEAKING);
        if (b & EV_AGENT_DONE)  app_set_state(ST_IDLE);
    }
}

void app_main(void)
{
    g_events = xEventGroupCreate();

    config_init();
    bsp_display_start();        // QSPI-AMOLED + Touch + LVGL (Waveshare-BSP)
    bsp_display_brightness_set(config_get()->brightness);

    ui_init();
    ui_set_pairing_info(NULL);  // wird nach Prov-Start mit echtem Namen gesetzt

    audio_init();
    bsp_spk_volume_set(config_get()->volume);

    agent_callbacks_t cb = {
        .on_transcript    = cb_transcript,
        .on_response_text = cb_response_text,
        .on_audio         = cb_audio,
        .on_state_event   = cb_state_event,
    };
    agent_client_init(&cb);

    bsp_buttons_init(on_button, NULL);

    xTaskCreatePinnedToCore(status_task, "status", 3072, NULL, 3, NULL, 0);

    provisioning_start();       // verbindet oder öffnet BLE-Provisioning
    ui_set_pairing_info(provisioning_service_name());

    app_loop();                 // läuft auf dem main task (Core 0)
}
