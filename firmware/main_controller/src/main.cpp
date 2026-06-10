// =====================================================================
//  AI Autonomous Robot — Main Controller (ESP32 DevKit v1)
//  Owns motors, encoders, IMU, audio, and the WebSocket link to the
//  cloud "brain". Runs fast local control + safety reflexes; receives
//  high-level commands from the backend.
// =====================================================================
#include <Arduino.h>
#include <ArduinoJson.h>
#include "config.h"
#include "motors.h"
#include "encoders.h"
#include "imu.h"
#include "audio.h"
#include "comms.h"
#include "pid.h"
#include "distance.h"

// ---- control state ----
Pid pidL(PID_KP, PID_KI, PID_KD);
Pid pidR(PID_KP, PID_KI, PID_KD);

float gMaxSpeed   = MAX_SPEED_DEFAULT;
float gTargetLin  = 0.0f;     // -1..1
float gTargetAng  = 0.0f;     // -1..1
uint32_t gCmdTimeoutMs = CMD_TIMEOUT_MS;
uint32_t gLastCmdMs = 0;
uint32_t gDriveUntilMs = 0;   // 0 = no auto-stop

// turn_to state
bool  gTurning = false;
float gTurnTargetDeg = 0;

bool  gTiltFault = false;
float gLeftRpm = 0, gRightRpm = 0;   // cached from the control loop

// --- collision/stall reflex state ---
uint32_t gStallSinceMs = 0;          // when stall condition started (0 = none)
uint32_t gBackoffUntilMs = 0;        // reversing after a stall until this time
bool  gBlocked = false;              // latched "obstacle ahead" flag for the brain

uint32_t lastCtrlUs = 0;
uint32_t lastTeleMs = 0;

// ---- helpers ----
float angleErr(float target, float current) {
  float e = target - current;
  while (e > 180)  e -= 360;
  while (e < -180) e += 360;
  return e;
}

void applyMotion(float dt) {
  if (gTiltFault) { motors::stop(); return; }

  // Auto-stop if a timed drive expired or command watchdog tripped.
  uint32_t now = millis();
  if (gDriveUntilMs && now > gDriveUntilMs) { gTargetLin = gTargetAng = 0; gDriveUntilMs = 0; }
  if (now - gLastCmdMs > gCmdTimeoutMs)     { gTargetLin = gTargetAng = 0; }

  float lin = constrain(gTargetLin, -gMaxSpeed, gMaxSpeed);
  float ang = constrain(gTargetAng, -gMaxSpeed, gMaxSpeed);

  // ---- Reflex 0: SHARP IR hard-stop (fastest, most reliable) ----
  // If something is within SHARP_STOP_CM straight ahead, forward motion is
  // forbidden immediately. Turning and reversing are still allowed so the brain
  // (or a manual command) can get us out.
  if (distance::obstacleAhead()) {
    if (lin > 0.0f) lin = 0.0f;
    if (!gBlocked) comms::sendEvent("obstacle", true);
    gBlocked = true;
  }
  if (imu::impact()) {
    gTargetLin = gTargetAng = 0;
    gBlocked = true;
    gBackoffUntilMs = now + STALL_BACKOFF_MS;
    comms::sendEvent("impact", true);
  }

  // ---- Reflex 2: stall (commanding forward but wheels not turning) ----
  float measuredRpm = 0.5f * (fabsf(gLeftRpm) + fabsf(gRightRpm));
  bool tryingForward = lin > STALL_CMD_THRESHOLD;
  if (tryingForward && measuredRpm < STALL_RPM) {
    if (gStallSinceMs == 0) gStallSinceMs = now;
    else if (now - gStallSinceMs > STALL_MS && gBackoffUntilMs == 0) {
      gBlocked = true;
      gBackoffUntilMs = now + STALL_BACKOFF_MS;   // reverse pulse to free up
      comms::sendEvent("blocked", true);
    }
  } else {
    gStallSinceMs = 0;
  }

  // While backing off, override forward intent with a short reverse.
  if (now < gBackoffUntilMs) { lin = -0.35f * gMaxSpeed; ang = 0; gTurning = false; }
  else if (gBackoffUntilMs && now >= gBackoffUntilMs) {
    gBackoffUntilMs = 0; gTargetLin = gTargetAng = 0; // stop after backoff, await brain
  }

  if (gTurning) {
    // e > 0 means the target heading is counter-clockwise from us; command
    // positive angular (left/CCW) so yaw increases toward the target.
    float e = angleErr(gTurnTargetDeg, imu::heading());
    if (fabsf(e) < 3.0f) { gTurning = false; lin = ang = 0; }
    else { lin = 0; ang = constrain(e / 90.0f, -gMaxSpeed, gMaxSpeed); }
  }

  // Differential mix -> per-wheel target RPM.
  // Sign convention (matches docs/PROTOCOL.md and the backend):
  //   angular > 0  =>  turn LEFT (counter-clockwise, yaw increases)
  //   => right wheel speeds up, left wheel slows down.
  float leftCmd  = lin - ang;
  float rightCmd = lin + ang;
  float spL, spR;
  encoders::update(dt, spL, spR);     // measured rpm
  gLeftRpm = spL; gRightRpm = spR;
  float tgtL = leftCmd  * TARGET_RPM_AT_FULL;
  float tgtR = rightCmd * TARGET_RPM_AT_FULL;

  float outL = pidL.step(tgtL, spL, dt);   // -1..1
  float outR = pidR.step(tgtR, spR, dt);
  // Feed-forward so PID isn't starting from zero.
  outL += leftCmd; outR += rightCmd;
  motors::setLeft(constrain(outL, -1.0f, 1.0f));
  motors::setRight(constrain(outR, -1.0f, 1.0f));
}

void handleCommand(const String &text) {
  JsonDocument doc;
  if (deserializeJson(doc, text)) return;
  const char *type = doc["type"] | "";
  if (strcmp(type, "cmd") != 0) return;

  const char *action = doc["action"] | "";
  uint32_t cmdId = doc["cmd_id"] | 0;
  gLastCmdMs = millis();

  if (!strcmp(action, "drive")) {
    gTurning = false;
    gBlocked = false; gBackoffUntilMs = 0; gStallSinceMs = 0;  // brain reacted
    gTargetLin = doc["linear"]  | 0.0f;
    gTargetAng = doc["angular"] | 0.0f;
    uint32_t dur = doc["duration_ms"] | 0;
    gDriveUntilMs = dur ? millis() + dur : 0;
  } else if (!strcmp(action, "turn_to")) {
    gTurning = true;
    gTurnTargetDeg = doc["heading_deg"] | imu::heading();
  } else if (!strcmp(action, "stop")) {
    gTurning = false; gTargetLin = gTargetAng = 0; gDriveUntilMs = 0;
    motors::stop();
  } else if (!strcmp(action, "play_audio")) {
    // The backend is about to stream TTS audio (binary frames). Pause motion
    // so the chassis isn't driving while speaking; safety loop keeps running.
    gTurning = false; gTargetLin = gTargetAng = 0; gDriveUntilMs = 0;
  } else if (!strcmp(action, "speak")) {
    // Minimal on-device feedback. Rich TTS audio is streamed by the backend
    // (play_audio path) — see docs/PROTOCOL.md.
    audio::beep(660, 80); audio::beep(990, 120);
  } else if (!strcmp(action, "config")) {
    if (doc["max_speed"].is<float>())      gMaxSpeed = doc["max_speed"];
    if (doc["cmd_timeout_ms"].is<int>())   gCmdTimeoutMs = doc["cmd_timeout_ms"];
  }
  comms::sendAck(cmdId, true);
}

// Incoming binary frames from the backend = TTS audio (PCM16 mono @ AMP_SAMPLE_RATE).
// NON-BLOCKING: bytes are queued to the audio playback task. The control loop,
// safety reflexes and command watchdog keep running while speech plays.
void handleBinary(const uint8_t *data, size_t len) {
  if (!audio::isPlaying()) {
    // First frame of an utterance: pause motion while the robot speaks.
    gTargetLin = gTargetAng = 0; gDriveUntilMs = 0;
  }
  audio::queuePCM16(data, len);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println("\n=== AI Robot main controller boot ===");

  motors::begin();
  encoders::begin();
  distance::begin();
  pidL.setOutputLimit(1.0f);
  pidR.setOutputLimit(1.0f);

  audio::beginOutput();
  audio::beginInput();
  audio::beep(880, 120);          // "I'm alive" chirp

  if (!imu::begin()) Serial.println("[imu] MPU6050 NOT found — check wiring!");
  else Serial.println("[imu] ok");

  comms::onCommand(handleCommand);
  comms::onBinary(handleBinary);
  comms::begin();

  lastCtrlUs = micros();
  gLastCmdMs = millis();
}

void loop() {
  comms::loop();

  uint32_t nowUs = micros();
  float dt = (nowUs - lastCtrlUs) / 1e6f;
  if (dt >= 1.0f / CONTROL_HZ) {
    lastCtrlUs = nowUs;
    imu::update(dt);
    distance::update();
    gTiltFault = imu::tilted();
    if (gTiltFault) { motors::stop(); }
    applyMotion(dt);
  }

  uint32_t nowMs = millis();
  if (nowMs - lastTeleMs >= 1000 / TELEMETRY_HZ) {
    lastTeleMs = nowMs;
    comms::sendTelemetry(imu::heading(), imu::pitch(), imu::roll(),
                         gLeftRpm, gRightRpm, encoders::leftTicks(), encoders::rightTicks(),
                         gTiltFault, gBlocked, distance::cm());
  }

#if ENABLE_MIC_STREAMING
  // Voice-activity-gated mic streaming: when sound is loud enough, stream PCM16
  // up to the backend between "voice_audio"/"voice_end" markers for STT.
  static bool   speaking = false;
  static uint32_t lastVoiceMs = 0;
  static int16_t mic[MIC_CHUNK_SAMPLES];
  size_t n = audio::readMic(mic, MIC_CHUNK_SAMPLES);
  if (n) {
    int32_t peak = 0;
    for (size_t i = 0; i < n; i++) { int32_t a = abs(mic[i]); if (a > peak) peak = a; }
    bool loud = peak > MIC_VAD_THRESHOLD;
    if (loud) {
      if (!speaking) { speaking = true; comms::sendEvent("voice_audio", true); }
      lastVoiceMs = nowMs;
    }
    if (speaking) {
      comms::sendBinary((const uint8_t*)mic, n * sizeof(int16_t));
      if (!loud && nowMs - lastVoiceMs > MIC_VAD_HANGOVER_MS) {
        speaking = false;
        comms::sendEvent("voice_end", true);
      }
    }
  }
#endif
}
