#pragma once
// Minimal velocity PID with output clamping and integral anti-windup.
class Pid {
public:
  Pid(float kp, float ki, float kd) : kp_(kp), ki_(ki), kd_(kd) {}
  void reset() { integral_ = 0; prevErr_ = 0; }
  float step(float setpoint, float measured, float dt) {
    float err = setpoint - measured;
    integral_ += err * dt;
    integral_ = constrain(integral_, -outMax_, outMax_);  // anti-windup
    float deriv = (dt > 0) ? (err - prevErr_) / dt : 0.0f;
    prevErr_ = err;
    float out = kp_ * err + ki_ * integral_ + kd_ * deriv;
    return constrain(out, -outMax_, outMax_);
  }
  void setOutputLimit(float m) { outMax_ = m; }
private:
  float kp_, ki_, kd_;
  float integral_ = 0, prevErr_ = 0, outMax_ = 1.0f;
};
