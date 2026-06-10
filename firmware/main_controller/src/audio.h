#pragma once
#include <stdint.h>
#include <stddef.h>
// I2S audio: MAX98357 output (amp) + INMP441 input (mic).
// Uses the legacy ESP-IDF I2S driver (driver/i2s.h), which works on
// Arduino-ESP32 2.x. On 3.x you may need the ESP_I2S wrapper — see notes.
//
// Playback is handled by a dedicated FreeRTOS task fed via queuePCM16(),
// so the main control loop is never blocked while speech is playing.
namespace audio {
  void beginOutput();
  void beginInput();

  // Play a short beep so you can confirm the amp works (queued, non-blocking
  // apart from brief waits for buffer space).
  void beep(int freq_hz = 880, int ms = 150);

  // Queue raw signed-16-bit mono PCM bytes at AMP_SAMPLE_RATE for playback.
  // NON-BLOCKING: returns the number of bytes accepted (excess is dropped
  // if the ring buffer is full). Safe to call from the WebSocket callback.
  size_t queuePCM16(const uint8_t *data, size_t len);

  // True while queued audio is still being played.
  bool isPlaying();

  // Read up to `maxSamples` mono int16 samples from the mic.
  // Returns number of samples actually read. Only if ENABLE_MIC_STREAMING.
  size_t readMic(int16_t *out, size_t maxSamples);
}
