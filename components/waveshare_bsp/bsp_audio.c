// bsp_audio.c — ES8311 (Playback) + ES7210 (Mic) über esp_codec_dev.
// I2S-/Codec-Setup aus dem offiziellen Waveshare-BSP, hier fest auf 16 kHz mono
// PCM16 gemäß board.h-Vertrag (bsp_mic_read / bsp_spk_write).
#include "board.h"
#include "bsp_internal.h"
#include "bsp_pins.h"

#include "esp_log.h"
#include "esp_check.h"
#include "driver/i2s_std.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"

static const char *TAG = "bsp_audio";

static i2s_chan_handle_t s_tx, s_rx;
static const audio_codec_data_if_t *s_data_if;
static esp_codec_dev_handle_t s_spk, s_mic;
static uint8_t s_volume = 70;

static esp_err_t i2s_init(uint32_t sr)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx, &s_rx), TAG, "i2s_new_channel");

    const i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(sr),
        .slot_cfg = I2S_STD_PHILIP_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_SCLK,
            .ws   = BSP_I2S_LCLK,
            .dout = BSP_I2S_DOUT,
            .din  = BSP_I2S_DSIN,
            .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false },
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx, &std_cfg), TAG, "i2s tx cfg");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx, &std_cfg), TAG, "i2s rx cfg");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx), TAG, "i2s tx en");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_rx), TAG, "i2s rx en");

    const audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_I2S_NUM,
        .rx_handle = s_rx,
        .tx_handle = s_tx,
    };
    s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    return s_data_if ? ESP_OK : ESP_FAIL;
}

static esp_codec_dev_handle_t speaker_init(void)
{
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM, .addr = ES8311_CODEC_DEFAULT_ADDR, .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl) return NULL;

    const esp_codec_dev_hw_gain_t gain = { .pa_voltage = 5.0, .codec_dac_voltage = 3.3 };
    const es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_POWER_AMP_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311 = es8311_codec_new(&es_cfg);
    if (!es8311) return NULL;

    const esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT, .codec_if = es8311, .data_if = s_data_if,
    };
    return esp_codec_dev_new(&dev_cfg);
}

static esp_codec_dev_handle_t mic_init(void)
{
    const audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM, .addr = ES7210_CODEC_DEFAULT_ADDR, .bus_handle = bsp_i2c_get_handle(),
    };
    const audio_codec_ctrl_if_t *ctrl = audio_codec_new_i2c_ctrl(&i2c_cfg);
    if (!ctrl) return NULL;

    const es7210_codec_cfg_t es_cfg = { .ctrl_if = ctrl };
    const audio_codec_if_t *es7210 = es7210_codec_new(&es_cfg);
    if (!es7210) return NULL;

    const esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_IN, .codec_if = es7210, .data_if = s_data_if,
    };
    return esp_codec_dev_new(&dev_cfg);
}

void bsp_audio_init(uint32_t sample_rate_hz)
{
    if (s_spk && s_mic) return;
    ESP_ERROR_CHECK(bsp_i2c_init());
    ESP_ERROR_CHECK(i2s_init(sample_rate_hz));

    s_spk = speaker_init();
    s_mic = mic_init();
    if (!s_spk || !s_mic) { ESP_LOGE(TAG, "codec init failed (spk=%p mic=%p)", s_spk, s_mic); return; }

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = sample_rate_hz,
        .channel = 1,
        .bits_per_sample = 16,
    };
    ESP_ERROR_CHECK(esp_codec_dev_open(s_spk, &fs));
    ESP_ERROR_CHECK(esp_codec_dev_open(s_mic, &fs));
    esp_codec_dev_set_out_vol(s_spk, s_volume);
    // Mic-Gain für ES7210 (dezent, gegen Clipping).
    esp_codec_dev_set_in_gain(s_mic, 30.0);

    ESP_LOGI(TAG, "audio codecs up @ %u Hz mono", (unsigned)sample_rate_hz);
}

size_t bsp_mic_read(int16_t *dst, size_t max_samples, uint32_t timeout_ms)
{
    (void)timeout_ms;   // esp_codec_dev_read blockiert bis Puffer voll
    if (!s_mic || !dst || max_samples == 0) return 0;
    int bytes = (int)(max_samples * sizeof(int16_t));
    if (esp_codec_dev_read(s_mic, dst, bytes) != ESP_OK) return 0;
    return max_samples;
}

size_t bsp_spk_write(const int16_t *src, size_t samples, uint32_t timeout_ms)
{
    (void)timeout_ms;
    if (!s_spk || !src || samples == 0) return 0;
    int bytes = (int)(samples * sizeof(int16_t));
    if (esp_codec_dev_write(s_spk, (void *)src, bytes) != ESP_OK) return 0;
    return samples;
}

void bsp_spk_volume_set(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_volume = percent;
    if (s_spk) esp_codec_dev_set_out_vol(s_spk, (int)percent);
}
