#pragma once
// =====================================================================
//  CONFIGURATION — edit everything here, not in the .cpp files.
//  Verify all pins against docs/WIRING.md before powering motors.
// =====================================================================

// ---------- WiFi ----------
// Real credentials live in secrets.h (gitignored, not committed).
// Copy secrets.h.example to secrets.h and fill in your own values.
// Can also be overridden at build time (e.g. from GitHub Actions secrets)
// with -DWIFI_SSID='"..."'.
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID        "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS        "YOUR_WIFI_PASSWORD"
#endif

// ---------- Backend (the Python brain) ----------
#ifndef BACKEND_HOST
#define BACKEND_HOST     "192.168.0.102"   // PC/Pi running backend/server.py
#endif
#define BACKEND_PORT     8000
#define BACKEND_WS_PATH  "/ws/robot"

// ---------- Motor driver selection ----------
// Default is TB6612FNG. Uncomment ONE if you have a different driver.
#define MOTOR_DRIVER_TB6612
// #define MOTOR_DRIVER_DRV8833
// #define MOTOR_DRIVER_L298N

// ---------- Motor driver pins (TB6612FNG default) ----------
#define PIN_PWMA   25   // left speed (PWM)
#define PIN_AIN1   26
#define PIN_AIN2   27
#define PIN_PWMB   17   // right speed (PWM) — moved off 32 to free an ADC1 pin for the Sharp sensor
#define PIN_BIN1   33
#define PIN_BIN2   14
#define PIN_STBY   13   // HIGH = enabled (TB6612 only)

// PWM (LEDC) config
#define PWM_FREQ_HZ   20000   // 20 kHz = inaudible
#define PWM_RES_BITS  10      // 0..1023
#define PWM_MAX       ((1 << PWM_RES_BITS) - 1)
// Hard ceiling on the actual duty sent to the motor driver, out of PWM_MAX —
// a full-scale (-1..1) speed command still only ever reaches this, regardless
// of what the PID/feed-forward math upstream computes.
#define PWM_MAX_DUTY  500

// ---------- Encoders ----------
#define PIN_ENC_LA   34   // input-only, needs ext pull-up if open-collector
#define PIN_ENC_LB   35
#define PIN_ENC_RA   36
#define PIN_ENC_RB   39
// Counts per output-shaft revolution = encoder PPR * gear ratio * 4 (quadrature).
// CHANGE THIS to match your exact N20 (common values: 7 PPR motor * gearbox).
#define ENC_TICKS_PER_REV   1400.0f

// ---------- MPU6050 (I2C) ----------
#define PIN_I2C_SDA  21
#define PIN_I2C_SCL  22
#define TILT_LIMIT_DEG   45.0f   // beyond this -> emergency motor cutoff

// ---------- SH1106 1.3" I2C OLED (128x64) — status display ----------
// Shares the I2C bus above (GPIO21 SDA / GPIO22 SCL) with the MPU6050; a
// different bus address, so no pin conflict. If the screen stays blank,
// try 0x3D — some modules ship at that address instead of 0x3C.
#define ENABLE_OLED       1
#define OLED_I2C_ADDR     0x3C
#define OLED_WIDTH        128
#define OLED_HEIGHT       64
#define OLED_REFRESH_HZ   4      // keep this low; I2C display writes aren't free
#define MOOD_HOLD_MS      6000   // an explicitly commanded mood reverts to auto after this long
#define BLINK_MIN_MS      3000   // idle "alive" blink: randomized interval between blinks
#define BLINK_MAX_MS      6000
#define BLINK_DURATION_MS 300    // must be >= 1000/OLED_REFRESH_HZ or a refresh could miss it entirely
#define SNORE_INTERVAL_MS 3500   // how often to play the snore sound while sleepy

// ---------- SHARP GP2Y0A41SK0F IR distance sensor (4-30 cm, ANALOG) ----------
// MUST use an ADC1 pin (GPIO 32-39) because ADC2 is unusable while WiFi is on.
// GPIO 32 was freed by moving PIN_PWMB to GPIO 17 (see above).
#define ENABLE_SHARP     0     // no sensor wired yet — distance::cm() reports SHARP_FAR_CM (clear)
#define PIN_SHARP_ADC    32        // ADC1_CH4
#define SHARP_STOP_CM    9.0f      // HARD STOP: cut forward motion if closer than this
#define SHARP_SLOW_CM    18.0f     // advisory: brain should slow/avoid within this
#define SHARP_EMA_ALPHA  0.4f      // distance smoothing 0..1 (higher = snappier/noisier)
#define SHARP_FAR_CM     35.0f     // reported when nothing is in range (sensor maxes ~30cm)

// ---------- I2S microphone (INMP441) on I2S_NUM_0 ----------
#define PIN_I2S_MIC_SCK   18
#define PIN_I2S_MIC_WS    19
#define PIN_I2S_MIC_SD    5
#define MIC_SAMPLE_RATE   16000

// ---------- I2S amplifier (MAX98357) on I2S_NUM_1 ----------
#define PIN_I2S_AMP_BCLK  4
#define PIN_I2S_AMP_LRC   15
#define PIN_I2S_AMP_DIN   2
#define AMP_SAMPLE_RATE   16000

// ---------- Control loop ----------
#define CONTROL_HZ        50          // PID/IMU update rate
#define TELEMETRY_HZ      10          // telemetry push rate
#define CMD_TIMEOUT_MS    800         // stop motors if no cmd within this window
#define MAX_SPEED_DEFAULT 0.6f        // 0..1 cap on commanded speed

// ---------- PID (velocity, per wheel). Tune on the bench! ----------
#define PID_KP   0.020f
#define PID_KI   0.090f
#define PID_KD   0.0008f
#define TARGET_RPM_AT_FULL  100.0f    // wheel rpm corresponding to linear = 1.0
#define MAX_WHEEL_RPM       100.0f    // hard ceiling on the PID setpoint, regardless of mixing

// ---------- Features ----------
#define ENABLE_MIC_STREAMING  1       // 1 = stream INMP441 audio to backend (advanced)

// ---------- Safety reflexes (use existing hardware, no extra sensor) ----------
// Stall detection: if we command forward motion but the wheels barely turn,
// something is blocking us. Trips when commanded speed > threshold yet measured
// rpm stays below STALL_RPM for STALL_MS.
#define STALL_CMD_THRESHOLD  0.20f   // |linear| above this counts as "trying to move"
#define STALL_RPM            8.0f    // measured wheel rpm below this = not moving
#define STALL_MS             350     // must persist this long to trip
#define STALL_BACKOFF_MS     400     // reverse pulse after a stall
// Impact detection: a sudden acceleration spike (g) = we hit something.
#define IMPACT_G             1.8f    // total accel magnitude above this (in g) trips it
// Shake/pickup detection: several rapid jolts in a short window (as opposed
// to a single sharp impact) = someone picked me up and is shaking me.
#define SHAKE_G              1.4f    // accel magnitude (g) counted as one "jolt"
#define SHAKE_JOLT_COUNT     4       // this many jolts within SHAKE_WINDOW_MS = "being shaken"
#define SHAKE_WINDOW_MS      1200    // max gap between jolts to still count as the same burst
#define SHAKE_HOLD_MS        4000    // how long the dizzy reaction lasts once triggered

// ---------- Voice audio over WebSocket ----------
// Playback: backend streams PCM16 mono @ AMP_SAMPLE_RATE as binary frames.
// Frames are queued into a ring buffer drained by a dedicated playback task,
// so the control loop never blocks. 16384 samples = 1 s of audio @ 16 kHz.
// The backend paces its frames at real-time rate to avoid overflowing it.
#define AUDIO_RX_BUF_SAMPLES 16384
// Mic capture/streaming voice-activity gate (only used if ENABLE_MIC_STREAMING=1).
#define MIC_VAD_THRESHOLD    1200    // int16 amplitude to count as speech
#define MIC_VAD_HANGOVER_MS  600     // keep streaming this long after speech stops
#define MIC_CHUNK_SAMPLES    256
