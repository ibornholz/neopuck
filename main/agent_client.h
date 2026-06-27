// agent_client.h — WebSocket-Verbindung zum Agent + Protokoll.
#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Callbacks Richtung App/UI (laufen im WS-Task -> UI-Zugriff lockt selbst).
typedef struct {
    void (*on_transcript)(const char *role, const char *text, bool final);
    void (*on_response_text)(const char *text);
    void (*on_audio)(const int16_t *pcm, size_t samples);   // TTS -> Speaker
    void (*on_state_event)(uint32_t ev_bit);                // EV_AGENT_* setzen
} agent_callbacks_t;

void agent_client_init(const agent_callbacks_t *cb);
void agent_client_connect(void);     // baut/öffnet WS gemäß config_get()
void agent_client_disconnect(void);
bool agent_client_is_connected(void);

// --- Mini-App-Steuerung vom Agent (Task 4) ----------------------------------
// app.launch übergibt App-ID + params an die State-Machine; EV_APP_LAUNCH wird
// gesetzt, danach holt app_main die Details hier ab.
typedef struct {
    char id[32];
    char params[256];   // roher JSON-String der "params" (oder "{}")
} agent_launch_t;

// Holt eine ausstehende app.launch-Anforderung ab (true = vorhanden).
bool agent_pop_launch(agent_launch_t *out);

// Talk-Flow
void agent_begin_input(void);                          // input.begin
void agent_send_audio(const int16_t *pcm, size_t n);   // Mic-Chunk -> binär
void agent_end_input(void);                            // input.end

#ifdef __cplusplus
}
#endif
