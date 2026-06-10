# Setup Guide

End-to-end bring-up: flash both boards, start the backend, then drive.

## 0. Prerequisites

- [PlatformIO](https://platformio.org/) (VS Code extension or
  `pip install platformio`) to build and flash the firmware. To flash prebuilt
  binaries from a browser instead, see [`FLASHING.md`](FLASHING.md).
- Python 3.10+ for the backend.
- All three nodes on the **same Wi-Fi network**.
- An API key for your LLM provider (default: Anthropic).

## 1. Flash the vision node (XIAO ESP32-S3 Sense)

```bash
cd firmware/vision_node
# edit include/config.h: set WIFI_SSID / WIFI_PASS
pio run -t upload
pio device monitor          # note the printed IP, e.g. 192.168.1.50
```

Verify in a browser: `http://<vision-ip>/capture` should return a photo.

## 2. Flash the main controller (ESP32 DevKit v1)

```bash
cd firmware/main_controller
# edit include/config.h:
#   - WIFI_SSID / WIFI_PASS
#   - BACKEND_HOST / BACKEND_PORT  (the machine that will run the brain)
#   - verify every pin define against docs/WIRING.md
#   - select your motor driver (MOTOR_DRIVER_TB6612 is the default)
pio run -t upload
pio device monitor
```

**Keep the wheels off the ground on first boot.** Confirm in the serial monitor
that it joins Wi-Fi and connects to the backend WebSocket.

## 3. Run the backend brain

```bash
cd backend
python -m venv .venv
source .venv/bin/activate            # Windows: .venv\Scripts\activate
pip install -r requirements.txt
cp .env.example .env
# edit .env:
#   ANTHROPIC_API_KEY=sk-ant-...
#   VISION_NODE_URL=http://<vision-ip>
#   AI_MODEL=claude-sonnet-4-6
python server.py
```

The server prints its WebSocket URL and starts the think loop once the
controller connects. Autonomy is **off** by default
(`AUTONOMOUS_ON_START=false`); enable it from the monitor or via the HTTP API.

## 4. Drive and observe

```bash
python tools/monitor.py              # live telemetry + manual WASD teleop
```

Keys: `W/A/S/D` drive · `SPACE` stop · `E` toggle autonomy · `Q` quit.

## 5. Verify motion conventions (wheels raised)

Before letting the robot loose, with wheels off the ground:

1. `W` (forward) — both wheels must spin in the forward direction. If not, swap
   that motor's two wires (or its IN pins).
2. `A` (left) — the **right** wheel must speed up relative to the left, and
   `heading_deg` in the telemetry line must **increase**. If heading moves the
   wrong way, the IMU is mounted upside-down — flip it or negate the gyro sign.
3. Spin a wheel by hand — its `*_rpm` must read a plausible positive value in
   the forward direction. If RPM looks ~4× off, recheck `ENC_TICKS_PER_REV`
   (PPR × gear ratio × 4).

## 6. Tune

| Parameter | Where | Guidance |
|-----------|-------|----------|
| PID gains | `config.h` | Start with P only, add I to remove steady-state error, a little D for overshoot; bench-test with wheels raised |
| `THINK_INTERVAL_S` | `.env` | Lower = snappier but more API calls; 1.5–3 s is a good start |
| `MAX_SPEED` | `.env` | Keep low until navigation is reliable |
| `SHARP_STOP_CM` / `FRONT_STOP_CM` | `config.h` / `.env` | Firmware hard-stop and the backend's redundant check |

## Troubleshooting

| Symptom | Likely cause |
|---------|--------------|
| ESP32 reboots randomly under motor load | Brownout — separate motor supply, add bulk capacitors |
| Encoders count erratically | Missing pull-ups on GPIO 34–39; shared-ground issue |
| Wheel RPM reads about 4× low/high | `ENC_TICKS_PER_REV` does not match PPR × gear ratio × 4 |
| Robot turns the wrong way on "left"/"right" | Motor wiring swapped, or IMU mounted inverted — see step 5 |
| MPU6050 not found on I2C | SDA/SCL swapped, or 5 V applied to a 3V3-only module |
| No audio / noise | I2S pin clash with strapping pins; check GAIN/SD on the MAX98357 |
| Robot won't move but link is "ok" | `tilt_fault` latched — level the robot, check the IMU mount |
| Backend logs "vision node unreachable" | Wrong `VISION_NODE_URL` or different subnet |
| Speech cuts off mid-sentence | Backend not pacing frames (update server.py) or Wi-Fi congestion |
