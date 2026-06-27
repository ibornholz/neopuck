// audio_pipeline.h — Mic-Capture -> Agent, Agent-TTS -> Speaker.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

void audio_init(void);          // BSP-Audio + Tasks starten

// Capture-Fenster steuern (von der State-Machine).
void audio_capture_start(void); // beginnt Mic-Chunks an den Agent zu streamen
void audio_capture_stop(void);

// TTS-Audio vom Agent in die Playback-Queue (aus agent on_audio Callback).
void audio_play_pcm(const int16_t *pcm, size_t samples);

// Playback-Queue sofort leeren (Interrupt: "tap to dismiss" im SPEAKING-State).
void audio_play_flush(void);

// liefert aktuellen Mic-Pegel 0..100 für die UI-Animation.
uint8_t audio_input_level(void);

#ifdef __cplusplus
}
#endif
