// audio_pipeline.c
#include "audio_pipeline.h"
#include "agent_client.h"
#include "board.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"
#include "esp_log.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "audio";

#define SR            16000
#define FRAME_MS      20
#define FRAME_SAMPLES (SR * FRAME_MS / 1000)   // 320 Samples = 640 Byte
#define PLAY_RB_BYTES (64 * 1024)              // ~2 s TTS-Puffer (PSRAM)

static volatile bool s_capturing;
static volatile uint8_t s_level;
static RingbufHandle_t s_play_rb;

// --- Mic-Capture-Task: liest BSP-Mic, schickt Chunks zum Agent ----------------
static void capture_task(void *arg)
{
    int16_t *frame = malloc(FRAME_SAMPLES * sizeof(int16_t));
    for (;;) {
        if (!s_capturing) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }

        size_t got = bsp_mic_read(frame, FRAME_SAMPLES, 50);
        if (got == 0) continue;

        // einfacher Peak-Pegel für die UI
        int32_t peak = 0;
        for (size_t i = 0; i < got; i++) {
            int32_t a = frame[i] < 0 ? -frame[i] : frame[i];
            if (a > peak) peak = a;
        }
        s_level = (uint8_t) (peak * 100 / 32768);

        agent_send_audio(frame, got);
    }
}

// --- Playback-Task: zieht TTS aus Ringbuffer -> Speaker -----------------------
static void playback_task(void *arg)
{
    for (;;) {
        size_t n = 0;
        int16_t *chunk = xRingbufferReceive(s_play_rb, &n, pdMS_TO_TICKS(100));
        if (chunk && n) {
            bsp_spk_write(chunk, n / 2, 200);
            vRingbufferReturnItem(s_play_rb, chunk);
        }
    }
}

void audio_init(void)
{
    bsp_audio_init(SR);
    s_play_rb = xRingbufferCreate(PLAY_RB_BYTES, RINGBUF_TYPE_BYTEBUF);
    if (!s_play_rb) { ESP_LOGE(TAG, "ringbuf alloc failed"); return; }

    // Capture auf Core 1 (Audio), Playback ebenfalls Core 1 — UI bleibt Core 0.
    xTaskCreatePinnedToCore(capture_task,  "mic_cap", 4096, NULL, 6, NULL, 1);
    xTaskCreatePinnedToCore(playback_task, "spk_play", 4096, NULL, 6, NULL, 1);
    ESP_LOGI(TAG, "audio pipeline up @ %d Hz", SR);
}

void audio_capture_start(void) { agent_begin_input(); s_capturing = true; }

void audio_capture_stop(void)
{
    s_capturing = false;
    s_level = 0;
    agent_end_input();
}

void audio_play_pcm(const int16_t *pcm, size_t samples)
{
    if (s_play_rb)
        xRingbufferSend(s_play_rb, pcm, samples * sizeof(int16_t), pdMS_TO_TICKS(50));
}

uint8_t audio_input_level(void) { return s_level; }
