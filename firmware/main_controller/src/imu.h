#pragma once
namespace imu {
  bool begin();
  void update(float dt_s);
  float heading();   // integrated yaw, degrees 0..360
  float pitch();     // degrees
  float roll();      // degrees
  bool  tilted();    // true if |pitch| or |roll| beyond TILT_LIMIT_DEG
  bool  impact();    // true on a sudden acceleration spike (collision)
  float accelG();    // total acceleration magnitude in g
  void  zeroHeading();
}
