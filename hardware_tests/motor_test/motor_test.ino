// =====================================================================
//  MOTOR + ENCODER TEST — ESP32 DevKit V1 + TB6612FNG
//  Pins match firmware/main_controller/include/config.h of nova-bot.
//
//  !! LIFT THE ROBOT SO THE WHEELS ARE OFF THE GROUND BEFORE TESTING !!
//
//  What it does:
//   * Runs an automatic sequence at boot:
//       LEFT fwd -> LEFT rev -> RIGHT fwd -> RIGHT rev -> BOTH fwd -> stop
//   * Prints encoder tick counts so you can verify:
//       - the correct wheel spins        (wiring of AIN/BIN vs motors)
//       - "forward" makes ticks INCREASE (encoder A/B phase order)
//   * After the sequence, a serial menu lets you drive manually:
//       q/a = left  fwd/rev     w/s = right fwd/rev
//       e   = both forward      d   = both reverse
//       x   = stop              1..9 = speed 10%..90%
//
//  Serial monitor: 115200 baud
// =====================================================================

#include <Arduino.h>

// ---------- pins (same as config.h) ----------
#define PIN_PWMA   25   // left speed (PWM)
#define PIN_AIN1   26
#define PIN_AIN2   27
#define PIN_PWMB   17   // right speed (PWM)
#define PIN_BIN1   33
#define PIN_BIN2   14
#define PIN_STBY   13   // TB6612 standby, HIGH = enabled

#define PIN_ENC_LA 34
#define PIN_ENC_LB 35
#define PIN_ENC_RA 36
#define PIN_ENC_RB 39

// ---------- PWM ----------
#define PWM_FREQ_HZ  20000
#define PWM_RES_BITS 10
#define PWM_MAX      ((1 << PWM_RES_BITS) - 1)

// ESP32 Arduino core 3.x changed the LEDC API. Support both.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
  #define LEDC_NEW_API 1
#else
  #define LEDC_NEW_API 0
  const int CH_A = 0, CH_B = 1;
#endif

// ---------- encoder counters (quadrature x2 on channel A edges) ----------
volatile long ticksL = 0, ticksR = 0;

void IRAM_ATTR isrL() {
  bool b = digitalRead(PIN_ENC_LB);
  bool a = digitalRead(PIN_ENC_LA);
  ticksL += (a == b) ? +1 : -1;
}
void IRAM_ATTR isrR() {
  bool b = digitalRead(PIN_ENC_RB);
  bool a = digitalRead(PIN_ENC_RA);
  ticksR += (a == b) ? +1 : -1;
}

// ---------- motor helpers ----------
void pwmWrite(int motor /*0=L 1=R*/, int dutyVal) {
#if LEDC_NEW_API
  ledcWrite(motor == 0 ? PIN_PWMA : PIN_PWMB, dutyVal);
#else
  ledcWrite(motor == 0 ? CH_A : CH_B, dutyVal);
#endif
}

// speed: -1.0 .. +1.0
void setLeft(float s) {
  s = constrain(s, -1.0f, 1.0f);
  bool fwd = s >= 0;
  digitalWrite(PIN_AIN1, fwd ? HIGH : LOW);
  digitalWrite(PIN_AIN2, fwd ? LOW  : HIGH);
  pwmWrite(0, (int)(fabsf(s) * PWM_MAX));
}
void setRight(float s) {
  s = constrain(s, -1.0f, 1.0f);
  bool fwd = s >= 0;
  digitalWrite(PIN_BIN1, fwd ? HIGH : LOW);
  digitalWrite(PIN_BIN2, fwd ? LOW  : HIGH);
  pwmWrite(1, (int)(fabsf(s) * PWM_MAX));
}
void stopAll() { setLeft(0); setRight(0); }

// run a motor for ms milliseconds and report tick delta
void runAndReport(const char *label, float l, float r, uint32_t ms) {
  long l0 = ticksL, r0 = ticksR;
  Serial.printf("\n>> %s  (%.0f%% for %lu ms)\n", label, 100.0f * max(fabsf(l), fabsf(r)), (unsigned long)ms);
  setLeft(l); setRight(r);
  delay(ms);
  stopAll();
  delay(300);   // let wheels stop
  long dl = ticksL - l0, dr = ticksR - r0;
  Serial.printf("   ticks: L=%+ld  R=%+ld\n", dl, dr);

  // simple verdicts
  if (fabsf(l) > 0.01f && labs(dl) < 20)
    Serial.println("   [!] LEFT encoder saw almost nothing -> motor not spinning or encoder wiring/power issue");
  if (fabsf(r) > 0.01f && labs(dr) < 20)
    Serial.println("   [!] RIGHT encoder saw almost nothing -> motor not spinning or encoder wiring/power issue");
  if (l > 0.01f && dl < -20) Serial.println("   [!] LEFT counts NEGATIVE on forward -> swap ENC_LA/ENC_LB (or motor wires)");
  if (r > 0.01f && dr < -20) Serial.println("   [!] RIGHT counts NEGATIVE on forward -> swap ENC_RA/ENC_RB (or motor wires)");
}

float uiSpeed = 0.4f;

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== MOTOR + ENCODER TEST ===");
  Serial.println("!! Wheels OFF the ground !!\n");

  pinMode(PIN_AIN1, OUTPUT); pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT); pinMode(PIN_BIN2, OUTPUT);
  pinMode(PIN_STBY, OUTPUT); digitalWrite(PIN_STBY, HIGH);  // enable driver

#if LEDC_NEW_API
  ledcAttach(PIN_PWMA, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttach(PIN_PWMB, PWM_FREQ_HZ, PWM_RES_BITS);
#else
  ledcSetup(CH_A, PWM_FREQ_HZ, PWM_RES_BITS); ledcAttachPin(PIN_PWMA, CH_A);
  ledcSetup(CH_B, PWM_FREQ_HZ, PWM_RES_BITS); ledcAttachPin(PIN_PWMB, CH_B);
#endif
  stopAll();

  // encoders (34/35/36/39 are input-only, no internal pull-ups!)
  pinMode(PIN_ENC_LA, INPUT); pinMode(PIN_ENC_LB, INPUT);
  pinMode(PIN_ENC_RA, INPUT); pinMode(PIN_ENC_RB, INPUT);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_LA), isrL, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_RA), isrR, CHANGE);

  delay(1500);

  // ---------- automatic sequence ----------
  runAndReport("LEFT  forward", +0.4f, 0, 1500);
  runAndReport("LEFT  reverse", -0.4f, 0, 1500);
  runAndReport("RIGHT forward", 0, +0.4f, 1500);
  runAndReport("RIGHT reverse", 0, -0.4f, 1500);
  runAndReport("BOTH  forward", +0.4f, +0.4f, 1500);

  Serial.println("\n=== sequence done — manual mode ===");
  Serial.println("q/a=L fwd/rev  w/s=R fwd/rev  e=both fwd  d=both rev  x=stop  1-9=speed");
}

uint32_t lastPrint = 0;

void loop() {
  if (Serial.available()) {
    char c = Serial.read();
    if (c >= '1' && c <= '9') { uiSpeed = (c - '0') * 0.1f; Serial.printf("[speed %.0f%%]\n", uiSpeed * 100); }
    else switch (c) {
      case 'q': setLeft(+uiSpeed);  Serial.println("[L fwd]");  break;
      case 'a': setLeft(-uiSpeed);  Serial.println("[L rev]");  break;
      case 'w': setRight(+uiSpeed); Serial.println("[R fwd]");  break;
      case 's': setRight(-uiSpeed); Serial.println("[R rev]");  break;
      case 'e': setLeft(+uiSpeed);  setRight(+uiSpeed); Serial.println("[both fwd]"); break;
      case 'd': setLeft(-uiSpeed);  setRight(-uiSpeed); Serial.println("[both rev]"); break;
      case 'x': stopAll();          Serial.println("[stop]");   break;
      default: break;
    }
  }

  // live tick readout every 500 ms
  if (millis() - lastPrint > 500) {
    lastPrint = millis();
    Serial.printf("ticks L=%ld  R=%ld\n", ticksL, ticksR);
  }
}
