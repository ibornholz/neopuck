// config_store.h — persistente Konfiguration in NVS.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>   // size_t

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    PROTO_NEONET = 0,    // JSON+Binär-WS (Default, eigenes/openclaw-Backend)
    PROTO_OPENAI_RT,     // OpenAI-Realtime-Adapter
} agent_proto_t;

typedef struct {
    char     wifi_ssid[33];
    char     wifi_pass[65];
    char     agent_url[160];     // wss://host/path
    char     agent_token[129];   // Bearer/Query-Token
    char     device_name[32];    // z.B. "neopuck-01"
    agent_proto_t proto;
    bool     push_to_talk;       // true=halten, false=toggle
    uint8_t  brightness;         // 0..100
    uint8_t  volume;             // 0..100
} app_config_t;

void config_init(void);                 // NVS auf + Defaults laden
const app_config_t *config_get(void);   // aktueller Snapshot
void config_set(const app_config_t *c); // schreibt + persistiert
bool config_is_provisioned(void);       // SSID + Agent-URL vorhanden?

// einzelne Felder aus dem Provisioning setzen (JSON-Endpoint)
void config_apply_json(const char *json, size_t len);

#ifdef __cplusplus
}
#endif
