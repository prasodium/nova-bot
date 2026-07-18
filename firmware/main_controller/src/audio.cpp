#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>
#include "config.h"
#include "audio.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/stream_buffer.h"

#define I2S_AMP  I2S_NUM_1
#define I2S_MIC  I2S_NUM_0

// ---------------------------------------------------------------------------
// Playback runs in its OWN FreeRTOS task fed by a stream buffer, so the main
// loop (control + safety reflexes) is NEVER blocked by i2s_write(). Incoming
// WebSocket audio frames are queued with queuePCM16(); the task drains the
// buffer into the I2S DMA at its own pace.
// ---------------------------------------------------------------------------
namespace {
  StreamBufferHandle_t sbuf = nullptr;
  volatile bool playing = false;

  void playbackTask(void *) {
    static int16_t chunk[256];
    for (;;) {
      // Block up to 100 ms waiting for audio data.
      size_t got = xStreamBufferReceive(sbuf, chunk, sizeof(chunk), pdMS_TO_TICKS(100));
      if (got == 0) {
        // The I2S peripheral loops its DMA buffer chain when nothing new is
        // written, so without this the tail of the last clip repeats forever.
        if (playing && xStreamBufferIsEmpty(sbuf)) {
          playing = false;
          i2s_zero_dma_buffer(I2S_AMP);
        }
        continue;
      }
      playing = true;
      size_t written = 0;
      i2s_write(I2S_AMP, chunk, got, &written, portMAX_DELAY);  // blocks THIS task only
    }
  }
}

void audio::beginOutput() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = AMP_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;   // FIX: prevent GPIO0 MCLK conflict
  pins.bck_io_num = PIN_I2S_AMP_BCLK;
  pins.ws_io_num  = PIN_I2S_AMP_LRC;
  pins.data_out_num = PIN_I2S_AMP_DIN;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_AMP, &cfg, 0, NULL);
  i2s_set_pin(I2S_AMP, &pins);
  i2s_zero_dma_buffer(I2S_AMP);

  // Ring buffer + playback task (core 0; loop() runs on core 1).
  sbuf = xStreamBufferCreate(AUDIO_RX_BUF_SAMPLES * sizeof(int16_t), 1);
  xTaskCreatePinnedToCore(playbackTask, "audio_play", 4096, nullptr, 1, nullptr, 0);
}

void audio::beginInput() {
#if ENABLE_MIC_STREAMING
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = MIC_SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;   // INMP441 outputs 24-bit in 32-bit slots
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;   // TEMP: diagnose which slot has signal
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;
  cfg.use_apll = false;

  i2s_pin_config_t pins = {};
  pins.mck_io_num = I2S_PIN_NO_CHANGE;   // FIX: prevent GPIO0 MCLK conflict
  pins.bck_io_num = PIN_I2S_MIC_SCK;
  pins.ws_io_num  = PIN_I2S_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = PIN_I2S_MIC_SD;

  i2s_driver_install(I2S_MIC, &cfg, 0, NULL);
  i2s_set_pin(I2S_MIC, &pins);
#endif
}

size_t audio::queuePCM16(const uint8_t *data, size_t len) {
  if (!sbuf) return 0;
  // Never block the caller (the WebSocket callback). If the buffer is full
  // the oldest unqueued bytes of this frame are dropped — preferable to
  // stalling the control loop.
  return xStreamBufferSend(sbuf, data, len, 0);
}

bool audio::isPlaying() { return playing || (sbuf && !xStreamBufferIsEmpty(sbuf)); }

void audio::beep(int freq_hz, int ms) {
  const int sr = AMP_SAMPLE_RATE;
  const int n = sr * ms / 1000;
  static int16_t buf[256];
  int i = 0;
  while (i < n) {
    int chunk = min((int)256, n - i);
    for (int k = 0; k < chunk; k++) {
      float t = (float)(i + k) / sr;
      buf[k] = (int16_t)(6000.0f * sinf(2.0f * PI * freq_hz * t));
    }
    // Beeps are short; wait briefly for buffer space instead of dropping.
    size_t want = chunk * sizeof(int16_t), sent = 0;
    while (sent < want) {
      sent += xStreamBufferSend(sbuf, ((uint8_t*)buf) + sent, want - sent,
                                pdMS_TO_TICKS(50));
    }
    i += chunk;
  }
}

void audio::dizzySound() {
  const int freqs[] = {600, 350, 700, 300, 550, 250};
  for (int f : freqs) beep(f, 90);
}

void audio::snoreSound() {
  beep(140, 250);   // low inhale
  beep(90, 350);    // lower, longer exhale
}

size_t audio::readMic(int16_t *out, size_t maxSamples) {
#if ENABLE_MIC_STREAMING
  // I2S is configured RIGHT_LEFT (stereo) even though the INMP441 is mono on
  // the left slot: ONLY_LEFT mode misbehaves on the legacy ESP-IDF driver
  // (reads back silence). Read interleaved [L,R,L,R,...] and keep only L.
  static int32_t raw[512];
  size_t total = 0;
  while (total < maxSamples) {
    size_t wantRawWords = min((size_t)256, maxSamples - total) * 2;
    size_t got = 0;
    i2s_read(I2S_MIC, raw, wantRawWords * sizeof(int32_t), &got, portMAX_DELAY);
    size_t rawWords = got / sizeof(int32_t);
    for (size_t k = 0; k + 1 < rawWords && total < maxSamples; k += 2)
      out[total++] = (int16_t)(raw[k] >> 14);
    if (rawWords == 0) break;
  }
  return total;
#else
  (void)out; (void)maxSamples; return 0;
#endif
}
