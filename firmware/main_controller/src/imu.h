#pragma once
namespace imu {
  bool begin();       // detect + configure the sensor only (no calibration)
  // ~600 ms; keep the robot completely still. Sets the gyro-drift bias and
  // the resting accel/pitch/roll baseline that impact/shake/tilt measure
  // against. Split out from begin() so callers can show UI feedback first.
  void calibrate();
  void update(float dt_s);
  float heading();   // integrated yaw, degrees 0..360
  float pitch();     // degrees
  float roll();      // degrees
  bool  tilted();    // true if |pitch| or |roll| beyond TILT_LIMIT_DEG
  bool  impact();    // true on a sudden acceleration spike (collision)
  bool  shaking();   // true for a while after several rapid jolts (being picked up/shaken)
  float accelG();    // total acceleration magnitude in g
  void  zeroHeading();
}
