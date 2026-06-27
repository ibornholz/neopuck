// agent_client.c — esp_websocket_client + neonet-Protokoll.
// Text-Frames = JSON-Steuerung, Binär-Frames = PCM16LE 16k mono Audio.
// Default-Protokoll PROTO_NEONET; PROTO_OPENAI_RT wird in send/parse umgeleitet
// (Adapter-Stellen markiert mit [OPENAI_RT]).
#include "agent_client.h"
#include "app_state.h"
#include "config_store.h"

#include <string.h>
#include <stdlib.h>
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "cJSON.h"

static const char *TAG = "agent";

static esp_websocket_client_handle_t s_ws;
static agent_callbacks_t s_cb;
static volatile bool s_connected;

// ausstehende app.launch-Anforderung (single-slot)
static agent_launch_t s_launch;
static volatile bool  s_launch_pending;

bool agent_pop_launch(agent_launch_t *out)
{
    if (!s_launch_pending) return false;
    *out = s_launch;
    s_launch_pending = false;
    return true;
}

// ---- ausgehende JSON-Steuernachricht -----------------------------------------
static void ws_send_json(const char *json)
{
    if (s_ws && s_connected) {
        esp_websocket_client_send_text(s_ws, json, strlen(json), portMAX_DELAY);
    }
}

static void send_session_start(void)
{
    const app_config_t *c = config_get();
    char buf[160];
    snprintf(buf, sizeof buf,
        "{\"type\":\"session.start\",\"sr\":16000,\"fmt\":\"pcm16\",\"device\":\"%s\"}",
        c->device_name);
    ws_send_json(buf);
    // [OPENAI_RT] hier stattdessen session.update mit input_audio_format=pcm16 etc.
}

// ---- eingehende JSON-Steuernachricht parsen ----------------------------------
static void handle_json(const char *data, int len)
{
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) return;
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(root, "type");
    if (!cJSON_IsString(t)) { cJSON_Delete(root); return; }
    const char *type = t->valuestring;

    if (strcmp(type, "transcript") == 0) {
        const cJSON *role  = cJSON_GetObjectItemCaseSensitive(root, "role");
        const cJSON *text  = cJSON_GetObjectItemCaseSensitive(root, "text");
        const cJSON *final = cJSON_GetObjectItemCaseSensitive(root, "final");
        if (s_cb.on_transcript && cJSON_IsString(text)) {
            s_cb.on_transcript(cJSON_IsString(role) ? role->valuestring : "user",
                               text->valuestring, cJSON_IsTrue(final));
        }
    } else if (strcmp(type, "response.text") == 0) {
        const cJSON *text = cJSON_GetObjectItemCaseSensitive(root, "text");
        if (s_cb.on_response_text && cJSON_IsString(text))
            s_cb.on_response_text(text->valuestring);
    } else if (strcmp(type, "response.audio.begin") == 0) {
        if (s_cb.on_state_event) s_cb.on_state_event(EV_AGENT_SPEAK);
    } else if (strcmp(type, "response.done") == 0) {
        if (s_cb.on_state_event) s_cb.on_state_event(EV_AGENT_DONE);
    } else if (strcmp(type, "thinking") == 0) {
        if (s_cb.on_state_event) s_cb.on_state_event(EV_AGENT_THINK);
    } else if (strcmp(type, "error") == 0) {
        const cJSON *m = cJSON_GetObjectItemCaseSensitive(root, "message");
        ESP_LOGW(TAG, "agent error: %s", cJSON_IsString(m) ? m->valuestring : "?");
        if (s_cb.on_state_event) s_cb.on_state_event(EV_AGENT_ERROR);
    } else if (strcmp(type, "app.launch") == 0) {
        const cJSON *app = cJSON_GetObjectItemCaseSensitive(root, "app");
        const cJSON *params = cJSON_GetObjectItemCaseSensitive(root, "params");
        if (cJSON_IsString(app)) {
            memset(&s_launch, 0, sizeof s_launch);
            strncpy(s_launch.id, app->valuestring, sizeof s_launch.id - 1);
            if (params) {
                char *ps = cJSON_PrintUnformatted(params);
                if (ps) { strncpy(s_launch.params, ps, sizeof s_launch.params - 1); free(ps); }
            } else {
                strcpy(s_launch.params, "{}");
            }
            s_launch_pending = true;
            if (s_cb.on_state_event) s_cb.on_state_event(EV_APP_LAUNCH);
        }
    } else if (strcmp(type, "app.exit") == 0) {
        if (s_cb.on_state_event) s_cb.on_state_event(EV_APP_EXIT);
    }
    cJSON_Delete(root);
}

// ---- WebSocket-Events --------------------------------------------------------
static void ws_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    esp_websocket_event_data_t *e = (esp_websocket_event_data_t *) data;
    switch (id) {
    case WEBSOCKET_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "ws connected");
        send_session_start();
        if (s_cb.on_state_event) s_cb.on_state_event(EV_AGENT_UP);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (e->op_code == 0x02) {                 // binär = TTS-Audio (PCM16)
            if (s_cb.on_audio && e->data_len > 0)
                s_cb.on_audio((const int16_t *) e->data_ptr, e->data_len / 2);
        } else if (e->op_code == 0x01) {          // text = JSON-Steuerung
            handle_json(e->data_ptr, e->data_len);
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
    case WEBSOCKET_EVENT_ERROR:
        s_connected = false;
        ESP_LOGW(TAG, "ws down");
        if (s_cb.on_state_event) s_cb.on_state_event(EV_AGENT_DOWN);
        break;
    default: break;
    }
}

void agent_client_init(const agent_callbacks_t *cb) { s_cb = *cb; }

void agent_client_connect(void)
{
    const app_config_t *c = config_get();
    if (c->agent_url[0] == '\0') return;

    esp_websocket_client_config_t wcfg = {
        .uri = c->agent_url,
        .buffer_size = 4096,
        .reconnect_timeout_ms = 3000,
        .network_timeout_ms = 8000,
    };
    // Token als Authorization-Header (sauberer als Query-Param).
    char hdr[160];
    if (c->agent_token[0]) {
        snprintf(hdr, sizeof hdr, "Authorization: Bearer %s\r\n", c->agent_token);
        wcfg.headers = hdr;
    }

    s_ws = esp_websocket_client_init(&wcfg);
    esp_websocket_register_events(s_ws, WEBSOCKET_EVENT_ANY, ws_event, NULL);
    esp_websocket_client_start(s_ws);
}

void agent_client_disconnect(void)
{
    if (s_ws) { esp_websocket_client_stop(s_ws); esp_websocket_client_destroy(s_ws); s_ws = NULL; }
    s_connected = false;
}

bool agent_client_is_connected(void) { return s_connected; }

void agent_begin_input(void) { ws_send_json("{\"type\":\"input.begin\"}"); }
void agent_end_input(void)   { ws_send_json("{\"type\":\"input.end\"}"); }

void agent_send_audio(const int16_t *pcm, size_t n)
{
    if (s_ws && s_connected)
        esp_websocket_client_send_bin(s_ws, (const char *) pcm, n * 2, portMAX_DELAY);
}
