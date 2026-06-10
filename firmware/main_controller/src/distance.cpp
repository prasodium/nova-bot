#include <Arduino.h>
#include "config.h"
#include "distance.h"

namespace {
  float ema = SHARP_FAR_CM;

  float readOnceCm() {
    // analogReadMilliVolts() applies the ESP32 ADC calibration curve, which
    // matters a lot here because the raw ADC is quite non-linear.
    uint32_t mv = analogReadMilliVolts(PIN_SHARP_ADC);
    float v = mv / 1000.0f;
    if (v < 0.05f) return SHARP_FAR_CM;          // basically nothing / disconnected-low
    // Datasheet curve fit for GP2Y0A41SK0F (4-30 cm):
    //   distance_cm ≈ 12.08 * v^(-1.058)
    float cm = 12.08f * powf(v, -1.058f);
    // Below ~4 cm the curve is ambiguous (voltage folds). Treat as "very close".
    if (v > 2.3f) cm = 3.0f;
    return constrain(cm, 3.0f, SHARP_FAR_CM);
  }

  // Median of 5 to reject spikes.
  float readMedian() {
    float s[5];
    for (int i = 0; i < 5; i++) s[i] = readOnceCm();
    for (int i = 0; i < 4; i++)
      for (int j = i + 1; j < 5; j++)
        if (s[j] < s[i]) { float t = s[i]; s[i] = s[j]; s[j] = t; }
    return s[2];
  }
}

void distance::begin() {
#if ENABLE_SHARP
  analogReadResolution(12);
  // 11 dB attenuation -> ~0..3.3 V usable range (covers the Sharp's output).
  analogSetPinAttenuation(PIN_SHARP_ADC, ADC_11db);
  ema = readMedian();
#endif
}

void distance::update() {
#if ENABLE_SHARP
  float m = readMedian();
  ema = SHARP_EMA_ALPHA * m + (1.0f - SHARP_EMA_ALPHA) * ema;
#endif
}

float distance::cm() {
#if ENABLE_SHARP
  return ema;
#else
  return SHARP_FAR_CM;
#endif
}

bool distance::obstacleAhead() { return cm() < SHARP_STOP_CM; }
