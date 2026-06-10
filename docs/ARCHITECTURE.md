# Architecture

## Overview

The system is composed of three cooperating nodes: a stateless camera node, a
real-time motor controller, and a Python backend that hosts the AI "brain".
The split exists because an ESP32 cannot run a multimodal model, while a cloud
LLM cannot meet real-time safety deadlines — so each layer does only what it is
good at.

## Nodes

### 1. Vision node — XIAO ESP32-S3 Sense

- Boots, joins Wi-Fi, and starts a minimal HTTP server.
- `GET /capture` returns one JPEG frame; `GET /stream` returns MJPEG for
  browser debugging; `GET /` is a status page.
- Stateless by design: it only produces images. The brain *pulls* a frame
  whenever it wants to look, which keeps the camera node simple and robust.
- Note: `/stream` occupies the single-threaded web server while open, which
  starves `/capture` (and therefore the brain). Use it for debugging only.

### 2. Main controller — ESP32 DevKit v1

Owns all real-time, latency-sensitive hardware:

| Subsystem | Implementation |
|-----------|----------------|
| Motors | LEDC PWM through a TB6612FNG (DRV8833 / L298N selectable) |
| Encoders | True ×4 quadrature decoding via interrupts on both edges of both channels |
| Velocity control | Per-wheel PID with feed-forward, 50 Hz |
| Heading / tilt | MPU6050: gyro-Z yaw integration, accelerometer pitch/roll and impact detection |
| Forward distance | SHARP GP2Y0A41SK0F analog IR on ADC1, median-of-5 + EMA filtering |
| Audio out | MAX98357 over I2S1, fed by a dedicated playback task (never blocks the control loop) |
| Audio in | INMP441 over I2S0 with a simple voice-activity gate (optional) |

It maintains one WebSocket to the backend: telemetry up at ~10 Hz, commands
down, plus binary audio in both directions.

**Local safety reflexes** run on-device and require no cloud:

1. **IR hard-stop** — forward motion is forbidden inside `SHARP_STOP_CM`.
2. **Tilt cutoff** — motors cut immediately beyond `TILT_LIMIT_DEG`.
3. **Stall back-off** — commanded forward + stationary wheels → brief reverse.
4. **Impact back-off** — acceleration spike → stop and reverse pulse.
5. **Command watchdog** — no command within `CMD_TIMEOUT_MS` → motion targets
   zeroed (covers link loss).

### 3. Brain — Python backend (FastAPI)

- Hosts the WebSocket hub the controller connects to.
- Runs the perception → decision loop:
  1. Pull a frame from the vision node.
  2. Send frame + telemetry + mission to a vision LLM.
  3. Parse the strict-JSON reply (`scene`, `action`, `speed`, `say`).
  4. Translate the action into protocol commands and send them.
- Performs a **redundant safety check**: a `forward` decision is converted to
  `turn_around` when telemetry shows `blocked` or `distance_cm` below
  `FRONT_STOP_CM`, so unsafe commands are not even issued.
- Handles STT (robot mic → text, wake-name gated) and TTS (text → PCM streamed
  to the robot, paced at real-time rate).
- Holds all secrets. The robot never sees API keys.

## Control Loops

```
            ┌──────────── every 1–3 s (THINK_INTERVAL_S) ──────────┐
            ▼                                                      │
   pull /capture ──► vision LLM ──► {scene, action, say} ──► cmd ──┘
   (vision node)     (cloud)         (strict JSON)        (controller)

            ┌──────────── every 20 ms (CONTROL_HZ = 50) ───────────┐
            ▼                                                      │
   encoders + IMU + IR ──► reflexes ──► PID ──► PWM                │
   (controller — fully local, never waits on the network) ─────────┘
```

Two loops at two speeds: a slow cloud *think* loop and a fast on-device
*control/reflex* loop. The robot keeps holding speed, watching for obstacles,
and enforcing every reflex even while the brain is mid-thought or while speech
is playing (audio runs in its own task).

## Failure Handling

| Failure | Behavior |
|---------|----------|
| WebSocket drops | Watchdog zeroes motion within `CMD_TIMEOUT_MS`; client auto-reconnects every 2 s |
| Vision node unreachable | Backend skips the think tick, logs, and retries |
| LLM returns malformed JSON | Backend falls back to `stop` and re-asks next tick |
| Tilt beyond limit | Controller cuts motors immediately and ignores drive commands |
| Wi-Fi unavailable at boot | Controller restarts after a 30 s timeout |
| Audio buffer overflow | Excess TTS bytes are dropped; control is never blocked |

## Message Protocol

The exact JSON schema for both directions is specified in
[`PROTOCOL.md`](PROTOCOL.md).
