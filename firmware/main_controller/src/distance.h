#pragma once
// SHARP GP2Y0A41SK0F IR distance sensor (4-30 cm, analog).
// Returns a smoothed distance in centimetres. Reads ~ SHARP_FAR_CM when nothing
// is within range. NOTE: this sensor only senses STRAIGHT AHEAD and is blind
// closer than ~4 cm (the response curve folds back), so we treat very-close as
// "stop". It is a forward bumper, not a 360° map.
namespace distance {
  void  begin();
  void  update();      // call from the control loop
  float cm();          // latest smoothed distance in cm
  bool  obstacleAhead();   // true if cm() < SHARP_STOP_CM
}
