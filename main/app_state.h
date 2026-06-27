// app_state.h — geteilte Zustände, Events und Handles.
#pragma once
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    ST_BOOT = 0,
    ST_PROVISION,   // kein WLAN/Agent konfiguriert -> BLE-Pairing-Screen
    ST_CONNECTING,  // WLAN + Agent-WebSocket wird aufgebaut
    ST_IDLE,        // bereit, wartet auf Talk
    ST_LISTENING,   // Mic offen, streamt zum Agent
    ST_THINKING,    // input.end gesendet, Antwort kommt
    ST_SPEAKING,    // TTS-Audio läuft
    ST_ERROR,
} app_state_t;

// Event-Bits (xEventGroup) — entkoppeln ISR/Callbacks von der State-Machine.
#define EV_TALK_PRESS    (1 << 0)   // PWR/Mic gedrückt
#define EV_TALK_RELEASE  (1 << 1)   // PWR/Mic losgelassen (Push-to-Talk)
#define EV_SETTINGS      (1 << 2)   // BOOT lang
#define EV_WIFI_UP       (1 << 3)
#define EV_WIFI_DOWN     (1 << 4)
#define EV_AGENT_UP      (1 << 5)
#define EV_AGENT_DOWN    (1 << 6)
#define EV_AGENT_THINK   (1 << 7)   // input.end ack / response beginnt
#define EV_AGENT_SPEAK   (1 << 8)   // response.audio.begin
#define EV_AGENT_DONE    (1 << 9)   // response.done
#define EV_AGENT_ERROR   (1 << 10)
#define EV_PROV_DONE     (1 << 11)  // Provisioning abgeschlossen

extern EventGroupHandle_t g_events;

void app_set_state(app_state_t st);     // setzt Zustand + triggert UI-Update
app_state_t app_get_state(void);

#ifdef __cplusplus
}
#endif
