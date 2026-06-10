#include <Arduino.h>
#include "config.h"
#include "encoders.h"

// ---------------------------------------------------------------------------
// Full x4 quadrature decoding.
//
// ENC_TICKS_PER_REV in config.h is defined as PPR * gear ratio * 4
// (e.g. 7 PPR * 50:1 * 4 = 1400). To actually produce x4 counts we must
// react to BOTH edges of BOTH channels, not just the rising edge of A.
// A state-transition lookup table also rejects invalid (bounce) transitions.
// ---------------------------------------------------------------------------
namespace {
  volatile long lTicks = 0, rTicks = 0;
  volatile uint8_t lState = 0, rState = 0;   // 2-bit (A<<1 | B) previous state
  long lPrev = 0, rPrev = 0;

  // Index = (prevState << 2) | newState. Value = -1, 0, +1.
  // 0 entries are invalid transitions (both bits changed = glitch).
  const int8_t QDEC[16] = {
     0, -1, +1,  0,
    +1,  0,  0, -1,
    -1,  0,  0, +1,
     0, +1, -1,  0
  };

  inline uint8_t readPair(int pinA, int pinB) {
    return (uint8_t)((digitalRead(pinA) << 1) | digitalRead(pinB));
  }

  void IRAM_ATTR isrLeft() {
    uint8_t s = readPair(PIN_ENC_LA, PIN_ENC_LB);
    lTicks += QDEC[(lState << 2) | s];
    lState = s;
  }
  void IRAM_ATTR isrRight() {
    uint8_t s = readPair(PIN_ENC_RA, PIN_ENC_RB);
    rTicks += QDEC[(rState << 2) | s];
    rState = s;
  }
}

void encoders::begin() {
  // GPIO 34-39 are input-only and have NO internal pull-ups; add external
  // 10 k pull-ups if your encoder outputs are open-collector (see WIRING.md).
  pinMode(PIN_ENC_LA, INPUT);
  pinMode(PIN_ENC_LB, INPUT);
  pinMode(PIN_ENC_RA, INPUT);
  pinMode(PIN_ENC_RB, INPUT);
  lState = readPair(PIN_ENC_LA, PIN_ENC_LB);
  rState = readPair(PIN_ENC_RA, PIN_ENC_RB);
  // CHANGE on both channels of each wheel -> true x4 decoding.
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_LA), isrLeft,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_LB), isrLeft,  CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_RA), isrRight, CHANGE);
  attachInterrupt(digitalPinToInterrupt(PIN_ENC_RB), isrRight, CHANGE);
}

long encoders::leftTicks()  { noInterrupts(); long v = lTicks; interrupts(); return v; }
long encoders::rightTicks() { noInterrupts(); long v = rTicks; interrupts(); return v; }

void encoders::update(float dt_s, float &leftRpm, float &rightRpm) {
  long l = leftTicks(), r = rightTicks();
  long dl = l - lPrev, dr = r - rPrev;
  lPrev = l; rPrev = r;
  if (dt_s <= 0) { leftRpm = rightRpm = 0; return; }
  leftRpm  = (dl / ENC_TICKS_PER_REV) / dt_s * 60.0f;
  rightRpm = (dr / ENC_TICKS_PER_REV) / dt_s * 60.0f;
}
