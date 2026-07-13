// =====================================================================
//  MIC + SPEAKER TEST v3 — ESP32 DevKit V1  (MCLK GPIO0 conflict FIXED)
//  INMP441 mic + MAX98357 amp, pins match nova-bot config.h
//
//  NEW IN v2: automatic mic diagnosis at boot. It tests the mic in
//  BOTH channel modes (LEFT and RIGHT) for 3 seconds each and tells
//  you which one carries your voice — no code editing needed.
//  >>> SPEAK CONTINUOUSLY ("test test test...") during the mic scan! <<<
//
//  Wiring:
//    INMP441 (3.3V ONLY!):  VDD->3.3V  GND->GND  SCK->18  WS->19  SD->5
//                           L/R -> GND
//    MAX98357:              Vin->5V  GND->GND  BCLK->4  LRC->15  DIN->2
//
//  Menu after the scan:
//    t = speaker tone sweep       m = mic VU meter
//    l = live loopback mic->spk   x = stop     +/- = volume
//    1 = use LEFT channel         2 = use RIGHT channel
//
//  Serial monitor: 115200 baud
// =====================================================================

#include <Arduino.h>
#include <driver/i2s.h>
#include <math.h>

#define PIN_I2S_MIC_SCK   18
#define PIN_I2S_MIC_WS    19
#define PIN_I2S_MIC_SD    5

#define PIN_I2S_AMP_BCLK  4
#define PIN_I2S_AMP_LRC   15
#define PIN_I2S_AMP_DIN   2

#define SAMPLE_RATE       16000
#define I2S_MIC           I2S_NUM_0
#define I2S_AMP           I2S_NUM_1

enum Mode { IDLE, METER, LOOPBACK };
Mode mode = IDLE;
float volume = 0.25f;
bool micUp = false;

// =====================================================================
//  Amp (speaker)
// =====================================================================
void ampBegin() {
  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;   // FIX: prevent GPIO0 MCLK conflict
  pins.bck_io_num   = PIN_I2S_AMP_BCLK;
  pins.ws_io_num    = PIN_I2S_AMP_LRC;
  pins.data_out_num = PIN_I2S_AMP_DIN;
  pins.data_in_num  = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_AMP, &cfg, 0, NULL);
  i2s_set_pin(I2S_AMP, &pins);
  i2s_zero_dma_buffer(I2S_AMP);
}

void playTone(int freq, int ms, float vol) {
  const int n = SAMPLE_RATE * ms / 1000;
  static int16_t buf[256];
  int i = 0;
  float amp = 12000.0f * vol;
  while (i < n) {
    int chunk = min(256, n - i);
    for (int k = 0; k < chunk; k++) {
      int idx = i + k;
      float env = 1.0f;
      if (idx < 160) env = idx / 160.0f;
      if (n - idx < 160) env = (n - idx) / 160.0f;
      buf[k] = (int16_t)(amp * env * sinf(2.0f * PI * freq * (float)idx / SAMPLE_RATE));
    }
    size_t w;
    i2s_write(I2S_AMP, buf, chunk * sizeof(int16_t), &w, portMAX_DELAY);
    i += chunk;
  }
}

void speakerTest() {
  Serial.println("[amp] tone sweep — you should hear 5 notes");
  const int notes[] = {262, 330, 392, 523, 784};
  for (int f : notes) { playTone(f, 250, volume); delay(40); }
  i2s_zero_dma_buffer(I2S_AMP);
  Serial.println("[amp] done. Silent? -> MAX98357 Vin must be 5V; check BCLK=4 LRC=15 DIN=2, speaker wires, SD pin not grounded.");
}

// =====================================================================
//  Mic — installable in LEFT or RIGHT slot mode
// =====================================================================
void micBegin(i2s_channel_fmt_t chfmt) {
  if (micUp) { i2s_driver_uninstall(I2S_MIC); micUp = false; }

  i2s_config_t cfg = {};
  cfg.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX);
  cfg.sample_rate = SAMPLE_RATE;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;   // INMP441: 24-bit in 32-bit slots
  cfg.channel_format = chfmt;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 8;
  cfg.dma_buf_len = 256;

  i2s_pin_config_t pins = {};
  pins.mck_io_num   = I2S_PIN_NO_CHANGE;   // FIX: prevent GPIO0 MCLK conflict
  pins.bck_io_num   = PIN_I2S_MIC_SCK;
  pins.ws_io_num    = PIN_I2S_MIC_WS;
  pins.data_out_num = I2S_PIN_NO_CHANGE;
  pins.data_in_num  = PIN_I2S_MIC_SD;

  i2s_driver_install(I2S_MIC, &cfg, 0, NULL);
  i2s_set_pin(I2S_MIC, &pins);
  micUp = true;
}

int micRead(int16_t *out, int maxSamples) {
  static int32_t raw[256];
  int want = min(maxSamples, 256);
  size_t br = 0;
  i2s_read(I2S_MIC, raw, want * sizeof(int32_t), &br, pdMS_TO_TICKS(100));
  int n = br / sizeof(int32_t);
  for (int i = 0; i < n; i++) out[i] = (int16_t)(raw[i] >> 14);
  return n;
}

// sample for `seconds`, return average absolute amplitude (and peak via ref)
int micProbe(float seconds, int &peakOut) {
  static int16_t s[256];
  long total = 0; long count = 0; int peak = 0;
  uint32_t t0 = millis();
  // flush stale DMA
  for (int i = 0; i < 8; i++) micRead(s, 256);
  while (millis() - t0 < (uint32_t)(seconds * 1000)) {
    int n = micRead(s, 256);
    for (int i = 0; i < n; i++) {
      int v = abs((int)s[i]);
      total += v; count++;
      if (v > peak) peak = v;
    }
  }
  peakOut = peak;
  return count ? (int)(total / count) : -1;   // -1 = no samples at all
}

// =====================================================================
//  Boot-time mic diagnosis
// =====================================================================
void micDiagnose() {
  Serial.println("\n===== MIC DIAGNOSIS =====");
  Serial.println(">>> SPEAK NOW, keep talking until the scan finishes <<<\n");
  delay(800);

  int peakL = 0, peakR = 0;
  micBegin(I2S_CHANNEL_FMT_ONLY_LEFT);
  Serial.println("[scan] LEFT slot, 3 s...  keep talking");
  int avgL = micProbe(3.0f, peakL);
  Serial.printf("[scan]   LEFT : avg=%d  peak=%d\n", avgL, peakL);

  micBegin(I2S_CHANNEL_FMT_ONLY_RIGHT);
  Serial.println("[scan] RIGHT slot, 3 s... keep talking");
  int avgR = micProbe(3.0f, peakR);
  Serial.printf("[scan]   RIGHT: avg=%d  peak=%d\n\n", avgR, peakR);

  // verdict
  bool leftDead  = (avgL < 0), rightDead = (avgR < 0);
  if (leftDead && rightDead) {
    Serial.println("VERDICT: NO I2S DATA AT ALL.");
    Serial.println(" -> SCK(18)/WS(19)/SD(5) wiring, 3.3V power, or unsoldered header pins.");
    micBegin(I2S_CHANNEL_FMT_ONLY_LEFT);
    return;
  }
  int bestPeak = max(peakL, peakR);
  if (bestPeak < 300) {
    Serial.println("VERDICT: bus alive but NO VOICE detected on either slot.");
    Serial.println(" -> Most likely SD wire loose/wrong pin, or header NOT SOLDERED,");
    Serial.println("    or L/R pin floating (must be tied to GND), or mic damaged by 5V.");
    micBegin(I2S_CHANNEL_FMT_ONLY_LEFT);
    return;
  }
  if (peakL >= peakR) {
    Serial.printf("VERDICT: mic WORKS on the LEFT slot (peak %d). Using LEFT.\n", peakL);
    micBegin(I2S_CHANNEL_FMT_ONLY_LEFT);
  } else {
    Serial.printf("VERDICT: mic WORKS on the RIGHT slot (peak %d). Using RIGHT.\n", peakR);
    Serial.println(" NOTE: main firmware uses LEFT — either tie the mic's L/R pin to GND,");
    Serial.println("       or change channel_format to ONLY_RIGHT in audio.cpp later.");
    micBegin(I2S_CHANNEL_FMT_ONLY_RIGHT);
  }
}

// =====================================================================
//  Live modes
// =====================================================================
void meterStep() {
  static int16_t s[256];
  int n = micRead(s, 256);
  if (n == 0) { Serial.println("[mic] no data!"); delay(400); return; }
  long sum = 0; int peak = 0;
  for (int i = 0; i < n; i++) { int v = abs((int)s[i]); sum += v; if (v > peak) peak = v; }
  int avg = sum / n;
  char bar[41];
  int len = constrain(avg / 400, 0, 40);
  for (int i = 0; i < 40; i++) bar[i] = i < len ? '#' : ' ';
  bar[40] = 0;
  Serial.printf("[%s] avg=%5d peak=%5d %s\n", bar, avg, peak, peak > 1200 ? "<speech>" : "");
  delay(30);
}

void loopbackStep() {
  static int16_t s[256];
  int n = micRead(s, 256);
  if (n == 0) return;
  for (int i = 0; i < n; i++)
    s[i] = (int16_t)constrain((int)(s[i] * 2.0f * volume), -32767, 32767);
  size_t w;
  i2s_write(I2S_AMP, s, n * sizeof(int16_t), &w, 0);
}

void printMenu() {
  Serial.println("\n--- menu ---");
  Serial.println(" t = speaker tones   m = mic VU meter   l = loopback   x = stop");
  Serial.println(" 1 = mic LEFT slot   2 = mic RIGHT slot");
  Serial.printf (" +/- = volume (now %.0f%%)\n", volume * 100);
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== MIC + SPEAKER TEST v3 ===");

  ampBegin();
  speakerTest();      // speaker check first
  micDiagnose();      // then auto-diagnose the mic
  printMenu();
}

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 't': mode = IDLE; speakerTest(); printMenu(); break;
      case 'm': mode = METER;    Serial.println("[mic] VU meter — speak or clap"); break;
      case 'l': mode = LOOPBACK; Serial.println("[loop] LIVE mic->speaker (x=stop, keep spk away from mic!)"); break;
      case 'x': mode = IDLE; i2s_zero_dma_buffer(I2S_AMP); Serial.println("[stop]"); printMenu(); break;
      case '1': micBegin(I2S_CHANNEL_FMT_ONLY_LEFT);  Serial.println("[mic] LEFT slot");  break;
      case '2': micBegin(I2S_CHANNEL_FMT_ONLY_RIGHT); Serial.println("[mic] RIGHT slot"); break;
      case '+': volume = min(1.0f, volume + 0.05f); Serial.printf("[vol %.0f%%]\n", volume * 100); break;
      case '-': volume = max(0.0f, volume - 0.05f); Serial.printf("[vol %.0f%%]\n", volume * 100); break;
      default: break;
    }
  }
  if (mode == METER)    meterStep();
  if (mode == LOOPBACK) loopbackStep();
}
