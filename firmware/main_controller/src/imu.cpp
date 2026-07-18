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

  // Boot-time calibration baseline: per-unit accelerometer bias and mounting
  // tilt mean "rest" isn't exactly 1.00 g / 0°,0° pitch,roll on every board.
  // impact/shake/tilt all measure relative to this, not an assumed-perfect
  // sensor. Defaults (in case begin() is skipped) match the old fixed behavior.
  float restAccelG = 1.0f, restPitch = 0.0f, restRoll = 0.0f;

  // Shake/pickup detection state.
  int      shakeJolts = 0;
  uint32_t lastJoltMs = 0;
  uint32_t shakeUntilMs = 0;
  bool     aboveShakeG = false;
}

bool imu::begin() {
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  // Must happen before any read transaction (mpu.begin() included) — setting
  // this later, after begin() already ran, does nothing for the reads that
  // already failed. Conservative 100kHz standard mode for marginal wiring.
  Wire.setClock(100000);

  // TEMP diagnostic: is the I2C bus itself healthy, and what's on it?
  Serial.println("[i2c] scanning bus...");
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c] device found at 0x%02X\n", addr);
      found++;
    }
  }
  Serial.printf("[i2c] scan complete, %d device(s) found\n", found);

  // AD0 selects 0x68 (tied LOW/GND) vs 0x69 (tied HIGH/VCC) — try both so it
  // doesn't matter which way AD0 is wired.
  if (!mpu.begin(0x68) && !mpu.begin(0x69)) return false;
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_44_HZ);
  ok = true;
  return true;
}

void imu::calibrate() {
  if (!ok) return;
  // Gyro-Z bias (for heading integration) AND the resting accelerometer
  // magnitude/pitch/roll (per-unit sensor bias + imperfect mounting).
  float sumGz = 0, sumAccelG = 0, sumPitch = 0, sumRoll = 0;
  const int N = 200;
  for (int i = 0; i < N; i++) {
    sensors_event_t a, g, t; mpu.getEvent(&a, &g, &t);
    sumGz += g.gyro.z;
    float ax = a.acceleration.x, ay = a.acceleration.y, az = a.acceleration.z;
    sumAccelG += sqrtf(ax * ax + ay * ay + az * az) / 9.80665f;
    sumPitch  += atan2f(ax, sqrtf(ay * ay + az * az)) * 57.2958f;
    sumRoll   += atan2f(ay, az) * 57.2958f;
    delay(3);
  }
  gyroZbias  = sumGz / N;
  restAccelG = sumAccelG / N;
  restPitch  = sumPitch / N;
  restRoll   = sumRoll / N;
  Serial.printf("[imu] calibrated: restAccelG=%.3f restPitch=%.2f restRoll=%.2f\n",
               restAccelG, restPitch, restRoll);
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

  // Shake detection: count rising-edge jolts (accel crossing SHAKE_G above
  // the calibrated rest reading) rather than a single spike (that's
  // impact()) — several jolts close together means someone picked the robot
  // up and is shaking it, not a one-off bump.
  uint32_t now = millis();
  bool above = (accelMagG - restAccelG) > SHAKE_G;
  if (above && !aboveShakeG) {
    if (now - lastJoltMs > SHAKE_WINDOW_MS) shakeJolts = 0;   // gap too long — fresh burst
    lastJoltMs = now;
    shakeJolts++;
    if (shakeJolts >= SHAKE_JOLT_COUNT) {
      shakeUntilMs = now + SHAKE_HOLD_MS;
      shakeJolts = 0;
    }
  }
  aboveShakeG = above;
}

float imu::heading() { return yaw; }
// Relative to the calibrated rest orientation, so "0" means level as this
// specific robot sits, not a perfectly-mounted sensor.
float imu::pitch()   { return pitchA - restPitch; }
float imu::roll()    { return rollA - restRoll; }
bool  imu::tilted()  { return fabsf(pitch()) > TILT_LIMIT_DEG || fabsf(roll()) > TILT_LIMIT_DEG; }
bool  imu::impact()  { return (accelMagG - restAccelG) > IMPACT_G; }
bool  imu::shaking() { return millis() < shakeUntilMs; }
float imu::accelG()  { return accelMagG; }
void  imu::zeroHeading() { yaw = 0; }
