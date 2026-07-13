// =====================================================================
//  MPU6050 IMU TEST — ESP32 DevKit V1
//  SDA = GPIO 21, SCL = GPIO 22  (same as nova-bot config.h)
//
//  No extra libraries needed — talks to the MPU6050 directly over I2C.
//
//  What it does:
//   1. I2C scan   -> confirms the sensor is visible (expect 0x68)
//   2. WHO_AM_I   -> confirms it's really an MPU6050
//   3. Gyro bias calibration (keep the robot STILL for ~2 s at boot)
//   4. Live readout @10 Hz:
//        pitch / roll (deg, from accelerometer)
//        yaw          (deg, integrated gyro Z — will drift slowly, normal)
//        accel magnitude in g  (~1.00 at rest)
//        temperature
//
//  How to verify it works:
//   * Flat on the table   -> pitch ≈ 0, roll ≈ 0, |a| ≈ 1.00 g
//   * Tilt nose up        -> pitch goes positive (or negative — note sign)
//   * Tilt right side down-> roll changes
//   * Rotate on the spot  -> yaw follows, returns near start value
//   * Tap the chassis     -> |a| spikes above 1 g  (impact detection input)
//
//  Serial monitor: 115200 baud
// =====================================================================

#include <Arduino.h>
#include <Wire.h>

#define PIN_I2C_SDA  21
#define PIN_I2C_SCL  22

#define MPU_ADDR      0x68   // AD0 low (0x69 if AD0 tied high)
#define REG_PWR_MGMT1 0x6B
#define REG_SMPLRT    0x19
#define REG_CONFIG    0x1A
#define REG_GYRO_CFG  0x1B
#define REG_ACCEL_CFG 0x1C
#define REG_WHO_AM_I  0x75
#define REG_DATA      0x3B   // ACCEL_XOUT_H .. GYRO_ZOUT_L (14 bytes)

// scale factors for the ranges we configure below
const float ACC_LSB_PER_G   = 4096.0f;   // ±8 g
const float GYRO_LSB_PER_DPS = 65.5f;    // ±500 °/s

float yaw = 0, gyroZbias = 0;
uint32_t lastUs = 0;

bool writeReg(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg); Wire.write(val);
  return Wire.endTransmission() == 0;
}

uint8_t readReg(uint8_t reg) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.endTransmission(false);
  Wire.requestFrom((int)MPU_ADDR, 1);
  return Wire.available() ? Wire.read() : 0xFF;
}

bool readRaw(int16_t &ax, int16_t &ay, int16_t &az,
             int16_t &tmp,
             int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(REG_DATA);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)MPU_ADDR, 14) != 14) return false;
  ax = (Wire.read() << 8) | Wire.read();
  ay = (Wire.read() << 8) | Wire.read();
  az = (Wire.read() << 8) | Wire.read();
  tmp = (Wire.read() << 8) | Wire.read();
  gx = (Wire.read() << 8) | Wire.read();
  gy = (Wire.read() << 8) | Wire.read();
  gz = (Wire.read() << 8) | Wire.read();
  return true;
}

void i2cScan() {
  Serial.println("[i2c] scanning...");
  int found = 0;
  for (uint8_t a = 1; a < 127; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[i2c] device at 0x%02X%s\n", a, a == 0x68 || a == 0x69 ? "  <- MPU6050?" : "");
      found++;
    }
  }
  if (!found) Serial.println("[i2c] NO devices found -> check SDA=21 SCL=22, VCC(3.3V), GND wiring");
}

void setup() {
  Serial.begin(115200);
  delay(400);
  Serial.println("\n=== MPU6050 IMU TEST ===");

  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Wire.setClock(400000);

  i2cScan();

  uint8_t who = readReg(REG_WHO_AM_I);
  Serial.printf("[imu] WHO_AM_I = 0x%02X %s\n", who,
                who == 0x68 ? "(OK, MPU6050)" : "(unexpected — check wiring/address)");

  // wake up + configure
  writeReg(REG_PWR_MGMT1, 0x00);        // wake, internal clock
  delay(50);
  writeReg(REG_PWR_MGMT1, 0x01);        // clock = gyro X PLL (more stable)
  writeReg(REG_SMPLRT,    0x04);        // 200 Hz sample rate
  writeReg(REG_CONFIG,    0x03);        // DLPF ~44 Hz (matches main firmware)
  writeReg(REG_GYRO_CFG,  0x08);        // ±500 °/s
  writeReg(REG_ACCEL_CFG, 0x10);        // ±8 g

  // gyro-Z bias calibration — keep the robot STILL
  Serial.println("[imu] calibrating gyro bias, keep still...");
  float sum = 0; const int N = 400;
  int16_t ax, ay, az, t, gx, gy, gz;
  for (int i = 0; i < N; i++) {
    if (readRaw(ax, ay, az, t, gx, gy, gz)) sum += gz / GYRO_LSB_PER_DPS;
    delay(4);
  }
  gyroZbias = sum / N;
  Serial.printf("[imu] gyro-Z bias = %.3f deg/s\n", gyroZbias);
  Serial.println("[imu] streaming... tilt / rotate / tap the board and watch the numbers\n");
  lastUs = micros();
}

uint32_t lastPrint = 0;

void loop() {
  int16_t rax, ray, raz, rt, rgx, rgy, rgz;
  if (!readRaw(rax, ray, raz, rt, rgx, rgy, rgz)) {
    Serial.println("[imu] read FAILED — wiring dropped?");
    delay(500);
    return;
  }

  uint32_t now = micros();
  float dt = (now - lastUs) / 1e6f;
  lastUs = now;

  float ax = rax / ACC_LSB_PER_G;
  float ay = ray / ACC_LSB_PER_G;
  float az = raz / ACC_LSB_PER_G;
  float gzdps = rgz / GYRO_LSB_PER_DPS - gyroZbias;

  // same math as the main firmware
  float accMag = sqrtf(ax * ax + ay * ay + az * az);                       // in g
  float pitch  = atan2f(ax, sqrtf(ay * ay + az * az)) * 57.2958f;
  float roll   = atan2f(ay, az) * 57.2958f;
  yaw += gzdps * dt;
  while (yaw < 0)    yaw += 360.0f;
  while (yaw >= 360) yaw -= 360.0f;

  float tempC = rt / 340.0f + 36.53f;

  if (millis() - lastPrint > 100) {   // 10 Hz print
    lastPrint = millis();
    Serial.printf("pitch=%+7.2f  roll=%+7.2f  yaw=%7.2f  |a|=%.2fg  gz=%+7.2f d/s  T=%.1fC%s\n",
                  pitch, roll, yaw, accMag, gzdps, tempC,
                  accMag > 1.8f ? "   <IMPACT!>" : "");
  }
  delay(2);
}
