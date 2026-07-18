# Wire Protocol Reference

WebSocket JSON protocol between the **main controller** (ESP32 DevKit v1) and
the **backend**. The controller connects to `ws://<backend>:<port>/ws/robot`.
All structured messages are UTF-8 JSON, one per WebSocket *text* frame; audio
in either direction travels as *binary* frames (see [Audio framing](#audio-framing)).

## Sign Convention (authoritative)

| Field | Positive value means |
|-------|----------------------|
| `linear` | forward |
| `angular` | turn **left** (counter-clockwise; `heading_deg` increases) |
| `heading_deg` | yaw, 0–360°, increasing counter-clockwise |

The firmware's differential mix, the backend planner, and the teleop client all
follow this convention.

---

## Controller → Backend

### `telemetry` (~10 Hz)

```json
{
  "type": "telemetry",
  "ts": 123456,
  "heading_deg": 87.4,
  "pitch_deg": 1.1,
  "roll_deg": -0.3,
  "left_rpm": 38.2,
  "right_rpm": 39.0,
  "left_ticks": 10422,
  "right_ticks": 10510,
  "tilt_fault": false,
  "blocked": false,
  "distance_cm": 23.4,
  "link": "ok"
}
```

| Field | Meaning |
|-------|---------|
| `tilt_fault` | IMU pitch/roll beyond `TILT_LIMIT_DEG`; motors are cut locally |
| `blocked` | Latched obstacle flag (IR hard-stop, stall, or impact); cleared by the next `drive` |
| `distance_cm` | Forward SHARP IR reading (≈4–30 cm; ~35 = nothing in range) |

### `event` (asynchronous)

```json
{ "type": "event", "name": "obstacle",    "value": true }
{ "type": "event", "name": "impact",      "value": true }
{ "type": "event", "name": "blocked",     "value": true }
{ "type": "event", "name": "voice_audio", "value": true }
{ "type": "event", "name": "voice_end",   "value": true }
{ "type": "event", "name": "shaken",      "value": true }
```

`shaken` fires once when the IMU detects several rapid jolts in a short window
(`SHAKE_JOLT_COUNT` within `SHAKE_WINDOW_MS`) — being picked up and shaken, as
opposed to a single sharp `impact`. The firmware reacts locally (dizzy spiral
eyes + a wobble sound, motors stopped) immediately; the backend responds by
speaking a complaint (see `server.py`'s handler for this event).

`voice_audio` marks the start of an utterance; the mic stream then follows as
binary frames until a `voice_end` marker (only with `ENABLE_MIC_STREAMING=1`).

### `ack`

```json
{ "type": "ack", "cmd_id": 42, "ok": true }
```

---

## Backend → Controller

### `cmd: drive` — velocity command (normalized −1..1)

```json
{ "type": "cmd", "cmd_id": 42, "action": "drive",
  "linear": 0.35, "angular": 0.0, "duration_ms": 600 }
```

| Field | Meaning |
|-------|---------|
| `linear` | forward (+) / backward (−), −1..1 |
| `angular` | left (+) / right (−), −1..1 |
| `duration_ms` | auto-stop after this long (safety); `0` = drive until the next command or watchdog timeout |

Issuing a `drive` clears the latched `blocked` flag (the brain has reacted).

### `cmd: turn_to` — closed-loop heading turn (IMU)

```json
{ "type": "cmd", "cmd_id": 43, "action": "turn_to", "heading_deg": 180 }
```

Rotates in place until `heading_deg` is reached within ±3°.

### `cmd: stop`

```json
{ "type": "cmd", "cmd_id": 44, "action": "stop" }
```

### `cmd: speak` — minimal on-device acknowledgement

```json
{ "type": "cmd", "cmd_id": 45, "action": "speak", "text": "I see a doorway." }
```

The firmware plays a short chirp. Rich speech uses `play_audio` instead.

### `cmd: play_audio` — announce a TTS stream

```json
{ "type": "cmd", "cmd_id": 46, "action": "play_audio", "rate": 16000 }
```

Sent immediately before streaming TTS audio. The firmware pauses motion (safety
reflexes keep running) and plays the binary frames that follow.

### `cmd: mood` — set the OLED face expression

```json
{ "type": "cmd", "cmd_id": 47, "action": "mood", "mood": "happy" }
```

`mood` is one of `neutral | happy | angry | love | shy | sad | sleepy | surprised | dizzy`
(unknown values fall back to `neutral`). Commanded moods hold for `MOOD_HOLD_MS`
(default 6 s) before reverting to neutral. Safety states always win over a
commanded mood, highest priority first: being shaken (`dizzy`) > `tilt_fault`
(`surprised`) > `blocked` (`angry`) > whatever was last commanded. No-op if no
SH1106 display is wired.

### `cmd: config` — runtime configuration

```json
{ "type": "cmd", "action": "config", "max_speed": 0.4, "cmd_timeout_ms": 800 }
```

---

## Audio framing

| Direction | Trigger | Format |
|-----------|---------|--------|
| Backend → robot (TTS) | after a `play_audio` command | binary frames of PCM16 LE mono @ `AMP_SAMPLE_RATE` (16 kHz), paced at ~real-time rate; firmware buffers ~1 s |
| Robot → backend (mic) | between `voice_audio` / `voice_end` events | binary frames of PCM16 LE mono @ `MIC_SAMPLE_RATE` (16 kHz) |

## Reliability rules

- **Command watchdog.** If no command arrives within `cmd_timeout_ms`
  (default 800 ms), the controller zeroes its motion targets.
- **Reconnection.** The WebSocket client retries every 2 s and runs a
  15 s heartbeat; on disconnect the watchdog stops the motors.
- Unknown `type` or `action` values are ignored (commands are still ack'd).
