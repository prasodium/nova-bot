# Hardware Test Sketches (Arduino IDE)

Standalone test programs for the **nova-bot main controller** (ESP32 DevKit V1).

> ⚠️ **These sketches are ONLY for checking that the hardware and wiring work
> properly.** They are NOT part of the robot's real firmware. Use them to verify
> each component one by one (motors, encoders, IMU, microphone, speaker) before
> flashing the main PlatformIO firmware. After testing, re-flash the main
> firmware from `firmware/main_controller/`.

All pin numbers match `firmware/main_controller/include/config.h` — no edits
needed if your wiring follows `docs/WIRING.md`.

---

## Requirements

- **Arduino IDE** with the **ESP32 board package** installed
  (Boards Manager → search "esp32" by Espressif)
- Board: **DOIT ESP32 DEVKIT V1**
- Serial Monitor: **115200 baud**
- **No external libraries needed** — all three sketches use only the built-in
  ESP32 core (raw I2C for the IMU, legacy I2S driver for audio)

Each sketch lives in its own folder (Arduino IDE requires the folder name to
match the `.ino` name):

```
motor_test/motor_test.ino          - motors + encoders
imu_test/imu_test.ino              - MPU6050 accelerometer/gyro
audio_test_v3/audio_test_v3.ino    - INMP441 mic + MAX98357 speaker
```

Flash them **one at a time**, check the results, then move to the next.

---

## 1. Motor + Encoder Test (`motor_test.ino`)

> ⚠️ **LIFT THE ROBOT so the wheels are OFF the ground before flashing this!**
> The motors will spin automatically at boot.
> Motor battery must be connected — USB power alone will not spin the motors.

**Pins tested:** TB6612FNG — PWMA=25, AIN1=26, AIN2=27, PWMB=17, BIN1=33,
BIN2=14, STBY=13 · Encoders — L: 34/35, R: 36/39

**What it does:**
1. Automatic sequence at boot: LEFT fwd → LEFT rev → RIGHT fwd → RIGHT rev →
   BOTH fwd. After each step it prints the encoder tick delta.
2. Prints automatic verdicts, e.g. wheel didn't spin, encoder saw nothing,
   or ticks counted backwards (A/B swapped).
3. Then manual drive mode over the Serial Monitor:
   `q/a` = left fwd/rev · `w/s` = right fwd/rev · `e/d` = both fwd/rev ·
   `x` = stop · `1-9` = speed 10–90%

**Pass criteria:**
- The **correct wheel** spins for each step (left command → left wheel)
- "forward" makes ticks **increase**, reverse makes them **decrease**
- Tick counts for L and R are roughly similar at the same speed

**Common failures:**
| Symptom | Fix |
|---|---|
| Nothing spins | Motor battery off / VM not powered / STBY wiring |
| Wrong wheel spins | Left/right motor wires swapped at the driver |
| Ticks negative on forward | Swap that encoder's A/B wires (or the motor's 2 wires) |
| Motor spins, ticks ~0 | Encoder VCC/GND or A/B wires disconnected |

---

## 2. IMU Test (`imu_test.ino`)

**Pins tested:** MPU6050 — SDA=21, SCL=22 (I2C, address 0x68)

> Keep the robot **completely still** for the first ~2 seconds after boot
> (gyro bias calibration).

**What it does:**
1. I2C bus scan — should find a device at **0x68**
2. WHO_AM_I check — confirms the chip is a real MPU6050
3. Gyro bias calibration
4. Live readout at 10 Hz: pitch, roll, yaw, acceleration magnitude (g),
   gyro-Z rate, temperature

**Pass criteria:**
- Flat on the table: pitch ≈ 0°, roll ≈ 0°, |a| ≈ 1.00 g
- Tilt the robot → pitch/roll follow smoothly
- Rotate it on the spot → yaw changes and returns near start
- Tap the chassis → |a| spikes and prints `<IMPACT!>` (same 1.8 g threshold
  the main firmware uses for impact detection)

**Common failures:**
| Symptom | Fix |
|---|---|
| "NO devices found" in scan | SDA/SCL swapped, no 3.3V/GND, bad jumpers |
| WHO_AM_I ≠ 0x68 | Wrong module or AD0 pin tied high (address 0x69) |
| Values frozen / noisy garbage | Loose wiring, shared GND missing |

---

## 3. Audio Test — Mic + Speaker (`audio_test_v3.ino`)

**Pins tested:**
- INMP441 microphone (I2S0): SCK=18, WS=19, SD=5 — **VDD to 3.3V ONLY**
  (5V destroys it), L/R pin tied to **GND**
- MAX98357 amplifier (I2S1): BCLK=4, LRC=15, DIN=2 — Vin to **5V**,
  speaker: 4 Ω 3 W (or 8 Ω 2–3 W)

**What it does:**
1. Plays a 5-note melody at boot → instant **speaker check**
2. **Automatic mic diagnosis** — listens on the LEFT I2S slot for 3 s, then the
   RIGHT slot for 3 s. **Speak continuously ("test test test…") during the
   whole scan!** It then prints a verdict telling you whether the mic works and
   on which slot.
3. Menu:
   `t` = tone sweep · `m` = mic VU meter (bar jumps when you speak/clap) ·
   `l` = **live loopback** (mic → speaker, hear yourself = full chain proven) ·
   `x` = stop · `+`/`-` = volume · `1`/`2` = force LEFT/RIGHT mic slot

> ⚠️ In loopback mode keep the speaker away from the mic or lower the volume,
> otherwise it will howl (feedback).

**Pass criteria:**
- You hear 5 clean notes at boot
- Mic scan verdict says **"mic WORKS"** (LEFT or RIGHT slot)
- VU meter bar jumps when you talk, `<speech>` appears when loud
- In loopback you hear your own voice through the speaker

**Common failures:**
| Symptom | Fix |
|---|---|
| No melody | MAX98357 Vin must be 5V; check BCLK/LRC/DIN; SD pin must not be grounded |
| "NO I2S DATA AT ALL" | SCK/WS/SD wiring, 3.3V power, or **unsoldered header pins** on the mic board |
| "bus alive but NO VOICE" | SD wire loose, L/R pin floating (tie to GND), or mic killed by 5V |
| Works only on RIGHT slot | Board quirk — note it! The main firmware then needs `I2S_CHANNEL_FMT_ONLY_RIGHT` in `src/audio.cpp` |

**Important note about the main firmware:** this test fixed an I2S bug
(GPIO 0 MCLK conflict). The same one-line fix must be applied to
`firmware/main_controller/src/audio.cpp` — add

```cpp
pins.mck_io_num = I2S_PIN_NO_CHANGE;
```

right after **both** `i2s_pin_config_t pins = {};` lines (in `beginOutput()`
and `beginInput()`), otherwise voice playback / mic streaming can silently
fail there too.

---

## Recommended test order

1. **IMU** (safest, nothing moves)
2. **Audio** (mic + speaker)
3. **Motors** (robot lifted, battery connected)

When all three pass, your wiring is verified — flash the real firmware from
`firmware/main_controller/` with PlatformIO and connect to the backend.
