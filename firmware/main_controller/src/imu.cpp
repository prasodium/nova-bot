#include <Arduino.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>
#include "config.h"
#include "imu.h"

namespace {
  Adafruit_MPU6050 mpu;
  float yaw = 0, pitchA = 0, rollA = 0;
  float accelMagG = 1.0f;   // total accel magnitude in g (≈1 at rest)
  float gyroZbias = 0;
  bool ok = false;
}

bool imu::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  if (!mpu.begin()) return false;
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);

  // Quick gyro-Z bias estimate (keep robot still at boot).
  float sum = 0; const int N = 200;
  for (int i = 0; i < N; i++) {
    sensors_event_t a, g, t; mpu.getEvent(&a, &g, &t);
    sum += g.gyro.z; delay(3);
  }
  gyroZbias = sum / N;
  ok = true;
  return true;
}

void imu::update(float dt_s) {
  if (!ok) return;
  sensors_event_t a, g, t;
  mpu.getEvent(&a, &g, &t);
  // Total acceleration magnitude in g (9.80665 m/s^2 == 1 g).
  float ax = a.acceleration.x, ay = a.acceleration.y, az = a.acceleration.z;
  accelMagG = sqrtf(ax * ax + ay * ay + az * az) / 9.80665f;
  // Tilt from accelerometer (degrees).
  pitchA = atan2f(a.acceleration.x,
                  sqrtf(a.acceleration.y * a.acceleration.y +
                        a.acceleration.z * a.acceleration.z)) * 57.2958f;
  rollA  = atan2f(a.acceleration.y, a.acceleration.z) * 57.2958f;
  // Heading from gyro-Z integration (no magnetometer -> will drift slowly).
  float wz = (g.gyro.z - gyroZbias) * 57.2958f;   // deg/s
  yaw += wz * dt_s;
  while (yaw < 0)    yaw += 360.0f;
  while (yaw >= 360) yaw -= 360.0f;
}

float imu::heading() { return yaw; }
float imu::pitch()   { return pitchA; }
float imu::roll()    { return rollA; }
bool  imu::tilted()  { return fabsf(pitchA) > TILT_LIMIT_DEG || fabsf(rollA) > TILT_LIMIT_DEG; }
bool  imu::impact()  { return accelMagG > IMPACT_G; }
float imu::accelG()  { return accelMagG; }
void  imu::zeroHeading() { yaw = 0; }
