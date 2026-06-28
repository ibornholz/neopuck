// config_store.c
#include "config_store.h"
#include <string.h>
#include "nvs_flash.h"
#include "nvs.h"
#include "cJSON.h"
#include "esp_log.h"

// Optionale lokale Seed-Zugangsdaten (gitignored). Wenn vorhanden, werden sie
// gesetzt, falls das NVS leer ist (z.B. nach einem Flash). Portal überschreibt.
#if defined(__has_include)
#  if __has_include("config_secrets.h")
#    include "config_secrets.h"
#    define HAVE_SEED 1
#  endif
#endif

static const char *TAG = "cfg";
static const char *NS  = "neopuck";

static app_config_t s_cfg;

static void load_defaults(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    strcpy(s_cfg.device_name, "neopuck-01");
    s_cfg.proto        = PROTO_NEONET;
    s_cfg.push_to_talk = false;   // Toggle: 1x tippen = Aufnahme an, nochmal = senden
    s_cfg.brightness   = 80;
    s_cfg.volume       = 70;
}

static void persist(void)
{
    nvs_handle_t h;
    if (nvs_open(NS, NVS_READWRITE, &h) != ESP_OK) return;
    nvs_set_blob(h, "cfg", &s_cfg, sizeof(s_cfg));
    nvs_commit(h);
    nvs_close(h);
}

void config_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    load_defaults();

    nvs_handle_t h;
    if (nvs_open(NS, NVS_READONLY, &h) == ESP_OK) {
        size_t sz = sizeof(s_cfg);
        if (nvs_get_blob(h, "cfg", &s_cfg, &sz) != ESP_OK || sz != sizeof(s_cfg)) {
            load_defaults();   // Layout-Mismatch -> Defaults
        }
        nvs_close(h);
    }
#ifdef HAVE_SEED
    if (!config_is_provisioned()) {   // nur wenn NVS leer -> Re-Provisioning bleibt möglich
        strcpy(s_cfg.wifi_ssid,   SEED_WIFI_SSID);
        strcpy(s_cfg.wifi_pass,   SEED_WIFI_PASS);
        strcpy(s_cfg.agent_url,   SEED_AGENT_URL);
        strcpy(s_cfg.agent_token, SEED_AGENT_TOKEN);
        persist();
        ESP_LOGI(TAG, "NVS leer -> Seed-Creds aus config_secrets.h gesetzt");
    }
#endif

    ESP_LOGI(TAG, "config loaded: provisioned=%d url=%s",
             config_is_provisioned(), s_cfg.agent_url);
}

const app_config_t *config_get(void) { return &s_cfg; }

void config_set(const app_config_t *c)
{
    s_cfg = *c;
    persist();
}

bool config_is_provisioned(void)
{
    return s_cfg.wifi_ssid[0] != '\0' && s_cfg.agent_url[0] != '\0';
}

// kopiert max. dst_sz-1 Zeichen aus einem optionalen JSON-String
static void cpy_str(cJSON *root, const char *key, char *dst, size_t dst_sz)
{
    cJSON *it = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsString(it) && it->valuestring) {
        strncpy(dst, it->valuestring, dst_sz - 1);
        dst[dst_sz - 1] = '\0';
    }
}

void config_apply_json(const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) { ESP_LOGW(TAG, "bad provisioning json"); return; }

    cpy_str(root, "wifi_ssid",   s_cfg.wifi_ssid,   sizeof s_cfg.wifi_ssid);
    cpy_str(root, "wifi_pass",   s_cfg.wifi_pass,   sizeof s_cfg.wifi_pass);
    cpy_str(root, "agent_url",   s_cfg.agent_url,   sizeof s_cfg.agent_url);
    cpy_str(root, "agent_token", s_cfg.agent_token, sizeof s_cfg.agent_token);
    cpy_str(root, "device_name", s_cfg.device_name, sizeof s_cfg.device_name);

    cJSON *proto = cJSON_GetObjectItemCaseSensitive(root, "proto");
    if (cJSON_IsString(proto) && proto->valuestring) {
        s_cfg.proto = strcmp(proto->valuestring, "openai_rt") == 0
                          ? PROTO_OPENAI_RT : PROTO_NEONET;
    }
    cJSON *ptt = cJSON_GetObjectItemCaseSensitive(root, "push_to_talk");
    if (cJSON_IsBool(ptt)) s_cfg.push_to_talk = cJSON_IsTrue(ptt);

    cJSON *br = cJSON_GetObjectItemCaseSensitive(root, "brightness");
    if (cJSON_IsNumber(br)) s_cfg.brightness = (uint8_t) br->valuedouble;
    cJSON *vol = cJSON_GetObjectItemCaseSensitive(root, "volume");
    if (cJSON_IsNumber(vol)) s_cfg.volume = (uint8_t) vol->valuedouble;

    cJSON_Delete(root);
    persist();
    ESP_LOGI(TAG, "config updated via provisioning");
}
