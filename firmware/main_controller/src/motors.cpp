#include <Arduino.h>
#include "config.h"
#include "motors.h"

// ---------------------------------------------------------------------------
// Motor driver abstraction.
//
// TB6612FNG / L298N : per motor -> 2 direction pins + 1 PWM (enable) pin.
// DRV8833           : per motor -> 2 PWM-capable IN pins, NO separate enable.
//                     Direction is selected by WHICH input is PWMed:
//                       forward : IN1 = PWM, IN2 = 0
//                       reverse : IN1 = 0,   IN2 = PWM   (fast decay)
//                     PIN_PWMA / PIN_PWMB are unused in DRV8833 mode.
// ---------------------------------------------------------------------------
namespace {
  inline int duty(float s) {
    s = constrain(s, -1.0f, 1.0f);
    return (int)(fabsf(s) * PWM_MAX);
  }

#if defined(MOTOR_DRIVER_DRV8833)
  // One LEDC channel per IN pin (4 total).
  const int CH_A1 = 0, CH_A2 = 1, CH_B1 = 2, CH_B2 = 3;

  void driveDRV(int chFwd, int chRev, float speed) {
    if (speed >= 0.0f) { ledcWrite(chFwd, duty(speed)); ledcWrite(chRev, 0); }
    else               { ledcWrite(chFwd, 0);           ledcWrite(chRev, duty(speed)); }
  }
#else
  const int CH_A = 0;   // LEDC channel, left PWM
  const int CH_B = 1;   // LEDC channel, right PWM

  void driveDir(int ch, int in1, int in2, float speed) {
    bool fwd = speed >= 0.0f;
    digitalWrite(in1, fwd ? HIGH : LOW);
    digitalWrite(in2, fwd ? LOW  : HIGH);
    ledcWrite(ch, duty(speed));
  }
#endif
}

void motors::begin() {
#if defined(MOTOR_DRIVER_DRV8833)
  ledcSetup(CH_A1, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(CH_A2, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(CH_B1, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(CH_B2, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PIN_AIN1, CH_A1);
  ledcAttachPin(PIN_AIN2, CH_A2);
  ledcAttachPin(PIN_BIN1, CH_B1);
  ledcAttachPin(PIN_BIN2, CH_B2);
#else
  pinMode(PIN_AIN1, OUTPUT); pinMode(PIN_AIN2, OUTPUT);
  pinMode(PIN_BIN1, OUTPUT); pinMode(PIN_BIN2, OUTPUT);
  #if defined(MOTOR_DRIVER_TB6612)
  pinMode(PIN_STBY, OUTPUT);
  #endif
  ledcSetup(CH_A, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcSetup(CH_B, PWM_FREQ_HZ, PWM_RES_BITS);
  ledcAttachPin(PIN_PWMA, CH_A);
  ledcAttachPin(PIN_PWMB, CH_B);
#endif
  stop();
  enable(true);
}

void motors::setLeft(float speed) {
#if defined(MOTOR_DRIVER_DRV8833)
  driveDRV(CH_A1, CH_A2, speed);
#else
  driveDir(CH_A, PIN_AIN1, PIN_AIN2, speed);
#endif
}

void motors::setRight(float speed) {
#if defined(MOTOR_DRIVER_DRV8833)
  driveDRV(CH_B1, CH_B2, speed);
#else
  driveDir(CH_B, PIN_BIN1, PIN_BIN2, speed);
#endif
}

void motors::stop() {
#if defined(MOTOR_DRIVER_DRV8833)
  ledcWrite(CH_A1, 0); ledcWrite(CH_A2, 0);
  ledcWrite(CH_B1, 0); ledcWrite(CH_B2, 0);   // both low = coast
#else
  ledcWrite(CH_A, 0); ledcWrite(CH_B, 0);
  digitalWrite(PIN_AIN1, LOW); digitalWrite(PIN_AIN2, LOW);
  digitalWrite(PIN_BIN1, LOW); digitalWrite(PIN_BIN2, LOW);
#endif
}

void motors::enable(bool on) {
#if defined(MOTOR_DRIVER_TB6612)
  digitalWrite(PIN_STBY, on ? HIGH : LOW);
#else
  (void)on;
#endif
}
