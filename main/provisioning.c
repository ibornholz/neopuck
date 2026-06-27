// provisioning.c — wifi_provisioning (Transport BLE) + Custom-Endpoints.
// Das iPhone schickt WLAN-Creds über den Standard-Pfad und Agent-Settings über
// den Custom-Endpoint "agent-config" (JSON, siehe config_apply_json()).
#include "provisioning.h"
#include "app_state.h"
#include "config_store.h"

#include <string.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "prov";
static char s_service_name[32];

const char *provisioning_service_name(void) { return s_service_name; }

// --- Custom-Endpoint: Agent-Config -------------------------------------------
static esp_err_t agent_cfg_handler(uint32_t session_id, const uint8_t *inbuf,
                                   ssize_t inlen, uint8_t **outbuf,
                                   ssize_t *outlen, void *priv)
{
    if (inbuf && inlen > 0) {
        config_apply_json((const char *) inbuf, (size_t) inlen);
    }
    const char resp[] = "{\"ok\":true}";
    *outbuf = malloc(sizeof(resp));
    if (!*outbuf) return ESP_ERR_NO_MEM;
    memcpy(*outbuf, resp, sizeof(resp));
    *outlen = sizeof(resp);
    return ESP_OK;
}

// --- Custom-Endpoint: Device-Status (iPhone liest Live-Status) ---------------
static esp_err_t status_handler(uint32_t session_id, const uint8_t *inbuf,
                                ssize_t inlen, uint8_t **outbuf,
                                ssize_t *outlen, void *priv)
{
    const app_config_t *c = config_get();
    char buf[256];
    int n = snprintf(buf, sizeof buf,
        "{\"state\":%d,\"name\":\"%s\",\"agent\":\"%s\",\"provisioned\":%s}",
        (int) app_get_state(), c->device_name, c->agent_url,
        config_is_provisioned() ? "true" : "false");
    *outbuf = malloc(n + 1);
    if (!*outbuf) return ESP_ERR_NO_MEM;
    memcpy(*outbuf, buf, n + 1);
    *outlen = n + 1;
    return ESP_OK;
}

// --- WiFi/Prov-Events --------------------------------------------------------
static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_PROV_EVENT) {
        switch (id) {
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "credentials accepted");
            break;
        case WIFI_PROV_END:
            wifi_prov_mgr_deinit();
            xEventGroupSetBits(g_events, EV_PROV_DONE);
            break;
        default: break;
        }
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        xEventGroupSetBits(g_events, EV_WIFI_DOWN);
        esp_wifi_connect();   // simpler Auto-Reconnect
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "got IP -> wifi up");
        xEventGroupSetBits(g_events, EV_WIFI_UP);
    }
}

static void register_endpoints(void)
{
    wifi_prov_mgr_endpoint_create("agent-config");
    wifi_prov_mgr_endpoint_create("device-status");
}
static void activate_endpoints(void)
{
    wifi_prov_mgr_endpoint_register("agent-config", agent_cfg_handler, NULL);
    wifi_prov_mgr_endpoint_register("device-status", status_handler, NULL);
}

static void start_ble_provisioning(void)
{
    wifi_prov_mgr_config_t cfg = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };
    ESP_ERROR_CHECK(wifi_prov_mgr_init(cfg));

    snprintf(s_service_name, sizeof s_service_name, "PROV_%s",
             config_get()->device_name);

    register_endpoints();
    // POP (proof of possession) als simples Pairing-Secret; im UI als QR zeigen.
    wifi_prov_security_t sec = WIFI_PROV_SECURITY_1;
    ESP_ERROR_CHECK(wifi_prov_mgr_start_provisioning(
        sec, "neopuck-pop", s_service_name, NULL));
    activate_endpoints();

    app_set_state(ST_PROVISION);
    ESP_LOGI(TAG, "BLE provisioning open as '%s'", s_service_name);
}

static void connect_with_saved(void)
{
    const app_config_t *c = config_get();
    wifi_config_t wc = {0};
    strncpy((char *) wc.sta.ssid,     c->wifi_ssid, sizeof wc.sta.ssid - 1);
    strncpy((char *) wc.sta.password, c->wifi_pass, sizeof wc.sta.password - 1);
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));
    ESP_ERROR_CHECK(esp_wifi_start());
    esp_wifi_connect();
    app_set_state(ST_CONNECTING);
    ESP_LOGI(TAG, "connecting to saved SSID '%s'", c->wifi_ssid);
}

void provisioning_start(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t wi = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wi));

    esp_event_handler_register(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, on_event, NULL);

    if (config_is_provisioned()) {
        connect_with_saved();
    } else {
        start_ble_provisioning();
    }
}

void provisioning_reopen(void)
{
    // WLAN stoppen und Provisioning erneut öffnen (vom Settings-Screen).
    esp_wifi_disconnect();
    start_ble_provisioning();
}
