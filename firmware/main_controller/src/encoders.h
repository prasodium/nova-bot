#pragma once
#include <stdint.h>
// Quadrature encoder reader using GPIO interrupts.
namespace encoders {
  void begin();
  long leftTicks();
  long rightTicks();
  // Call at a fixed rate; returns measured wheel RPM since last call.
  void update(float dt_s, float &leftRpm, float &rightRpm);
}
