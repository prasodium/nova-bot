#pragma once
// Motor driver abstraction. Supports TB6612FNG (default), DRV8833, L298N.
// Selected via the MOTOR_DRIVER_* define in config.h.
namespace motors {
  void begin();
  // speed: -1.0 (full reverse) .. +1.0 (full forward), per wheel.
  void setLeft(float speed);
  void setRight(float speed);
  void stop();
  void enable(bool on);   // STBY for TB6612; no-op otherwise
}
