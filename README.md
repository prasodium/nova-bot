# AI Autonomous Robot

**An ESP32-based autonomous robot car driven by a cloud vision LLM.**

The robot's *body and senses* run on two ESP32 boards, while its *brain* runs as a
Python backend that consults a vision-capable Large Language Model. The robot
continuously sees (camera), hears (microphone), orients itself (IMU), moves
(encoder motors with closed-loop PID), and speaks (streamed TTS) — and the LLM
decides what to do next.

---

## Table of Contents

1. [Capabilities](#capabilities)
2. [System Overview](#system-overview)
3. [Repository Layout](#repository-layout)
4. [Quick Start](#quick-start)
5. [Configuration Reference](#configuration-reference)
6. [Safety Notes](#safety-notes)
7. [Documentation Index](#documentation-index)
8. [Roadmap](#roadmap)
9. [License](#license)

---

## Capabilities

- **Perception** — captures the scene through a XIAO ESP32-S3 Sense camera node.
- **Reasoning** — a cloud vision LLM (default: Claude Sonnet) interprets each frame
  together with live telemetry and the current mission.
- **Navigation** — closed-loop wheel velocity (quadrature encoders + PID) and
  heading control from the MPU6050 IMU.
- **Obstacle avoidance & recovery** — a forward SHARP IR sensor provides a fast
  on-device hard stop (default 9 cm), backed by vision-based avoidance,
  motor-stall detection, and IMU impact detection.
- **Speech output** — the backend renders TTS offline and streams PCM audio to the
  robot's MAX98357 amplifier. Playback runs in a dedicated task, so safety
  reflexes remain active while the robot talks.
- **Voice commands by name** — address the robot by its wake name (default
  `Nova`): *"Nova, go forward"*, *"Nova, stop"*. Utterances without the name are
  ignored; conversational questions are routed to the LLM.
- **Teleoperation** — a terminal client (`tools/monitor.py`) provides manual WASD
  driving and a live telemetry readout.

> **Sensor limitation.** The SHARP GP2Y0A41SK0F senses *forward only*, in the
> 4–30 cm band. It is a front bumper, not a 360° scanner. Keep speeds modest and
> do not rely on it for side or rear clearance.

---

## System Overview

An ESP32 cannot run a multimodal model, so the system is split into three
cooperating nodes:

```
        SENSES + BODY (on the robot)                    BRAIN (server / cloud)
   ┌───────────────────────────────────────────┐    ┌──────────────────────────────┐
   │  XIAO ESP32-S3 Sense  ──HTTP /capture───► │    │  Python backend (FastAPI)    │
   │  (camera "vision node")                   │    │   • pulls camera frames      │
   │                                           │    │   • queries a vision LLM     │
   │  ESP32 DevKit v1  ◄──WebSocket JSON──►    │◄──►│   • returns motion commands  │
   │  (main controller)                        │    │   • STT (voice in)           │
   │   • 2× N20 motors + encoders (PID)        │    │   • TTS (voice out)          │
   │   • MPU6050 IMU (heading / tilt / impact) │    │   • holds all API keys       │
   │   • SHARP IR forward distance (hard stop) │    └──────────────────────────────┘
   │   • INMP441 mic / MAX98357 + speaker      │
   └───────────────────────────────────────────┘
```

The controller streams telemetry up at ~10 Hz and receives structured JSON
commands back. All AI credentials and heavy computation stay on the backend —
nothing secret is stored on the ESP32s. Two loops run at two speeds: a slow
cloud *think* loop (1–3 s) and a fast on-device *control/reflex* loop (50 Hz)
that never waits on the network.

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for the full design and
[`docs/PROTOCOL.md`](docs/PROTOCOL.md) for the wire protocol.

---

## Repository Layout

```
ai-robot/
├── README.md                  Project overview (this file)
├── CHANGELOG.md               Release notes and bug-fix history
├── LICENSE                    MIT license
├── docs/
│   ├── ARCHITECTURE.md        System design and control loops
│   ├── HARDWARE.md            Bill of materials and power notes
│   ├── WIRING.md              Complete pin map for both boards
│   ├── PROTOCOL.md            WebSocket JSON message schema
│   ├── SETUP.md               Step-by-step bring-up guide
│   └── FLASHING.md            Building and flashing the firmware
├── firmware/
│   ├── main_controller/       ESP32 DevKit v1 — motors, encoders, IMU, audio
│   └── vision_node/           XIAO ESP32-S3 Sense — camera HTTP server
├── backend/                   Python "brain" — FastAPI + LLM + STT/TTS
├── tools/
│   └── monitor.py             Teleop and telemetry CLI
├── examples/                  Sample messages for offline testing
└── .github/workflows/         CI: builds flashable .bin files
```

---

## Quick Start

1. **Obtain flashable binaries.** Push the repository to GitHub; the included
   Actions workflow builds both boards and publishes merged `.bin` files that
   flash at offset `0x0`. Alternatively build locally with PlatformIO. Full
   instructions: [`docs/FLASHING.md`](docs/FLASHING.md).
2. **Flash the vision node** (XIAO ESP32-S3 Sense). It joins Wi-Fi and serves
   JPEG frames at `http://<vision-ip>/capture`.
3. **Flash the main controller** (ESP32 DevKit v1). It joins Wi-Fi and opens a
   WebSocket to the backend.
4. **Run the backend:**

   ```bash
   cd backend
   python -m venv .venv && source .venv/bin/activate
   pip install -r requirements.txt
   cp .env.example .env        # then edit with your API key and node IPs
   python server.py
   ```

5. **Observe or drive manually:**

   ```bash
   python tools/monitor.py
   ```

Detailed bring-up, tuning, and troubleshooting: [`docs/SETUP.md`](docs/SETUP.md).

---

## Configuration Reference

| Setting | Location | Notes |
|---------|----------|-------|
| Wi-Fi SSID / password | `firmware/*/include/config.h` | Both boards; can be injected from CI secrets |
| Backend host / port | `firmware/main_controller/include/config.h` | WebSocket target |
| Vision node URL | `backend/.env` (`VISION_NODE_URL`) | Backend pulls frames from here |
| AI model / API key | `backend/.env` | Default model: `claude-sonnet-4-6` |
| Motor / encoder / I2S pins | `firmware/main_controller/include/config.h` | Verify against `docs/WIRING.md` |
| Motor driver type | `firmware/main_controller/include/config.h` | TB6612FNG (default), DRV8833, L298N |
| PID gains, speed caps, safety thresholds | `firmware/main_controller/include/config.h` | Tune on the bench, wheels raised |
| Robot name, mission, autonomy | `backend/.env` | `ROBOT_NAME`, `ROBOT_GOAL`, `AUTONOMOUS_ON_START` |

---

## Safety Notes

This is a moving machine with safety-relevant behavior. Please observe the
following:

- **First power-up with wheels off the ground.** Verify motor direction, encoder
  polarity, and the steering sign convention before letting it drive.
- **Keep a physical e-stop (power cut) within reach** during early testing.
- The firmware enforces local reflexes that work without the cloud: IR
  hard-stop, tilt cutoff, stall/impact back-off, and a command watchdog that
  stops the motors if the link drops. Audio playback never blocks these loops.
- Do not power the motors from the ESP32's 5 V/3V3 pins — see
  [`docs/HARDWARE.md`](docs/HARDWARE.md).

---

## Documentation Index

| Document | Contents |
|----------|----------|
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | Node responsibilities, control loops, failure handling |
| [`docs/HARDWARE.md`](docs/HARDWARE.md) | Bill of materials, motor-driver options, power design |
| [`docs/WIRING.md`](docs/WIRING.md) | Pin-by-pin wiring tables for both boards |
| [`docs/PROTOCOL.md`](docs/PROTOCOL.md) | Complete WebSocket message reference |
| [`docs/SETUP.md`](docs/SETUP.md) | Bring-up, tuning, troubleshooting |
| [`docs/FLASHING.md`](docs/FLASHING.md) | CI builds and browser-based flashing |
| [`backend/README.md`](backend/README.md) | Backend service and HTTP control API |
| [`CHANGELOG.md`](CHANGELOG.md) | Version history and fixes |

---

## Roadmap

- Wider obstacle coverage (VL53L0X time-of-flight or ultrasonic array).
- On-device wake-word detection so the cloud is only queried when addressed.
- SLAM-lite mapping from camera + encoder odometry.
- Battery monitoring and return-to-base behavior.

Contributions are welcome — please open an issue or pull request.

## License

Released under the MIT License. See [`LICENSE`](LICENSE).
