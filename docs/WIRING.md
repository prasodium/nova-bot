# Wiring & Pin Map

> **Important.** These are sensible defaults, not gospel. The ESP32 DevKit v1
> has strapping pins (0, 2, 5, 12, 15) and input-only pins (34–39). Verify
> every pin against your board's silkscreen before powering the motors. All
> pins are defined in `firmware/main_controller/include/config.h` — change them
> there, not in the source files.

## Board 1 — ESP32 DevKit v1 (Main Controller)

### Motor driver (default: TB6612FNG)

| Driver pin | ESP32 GPIO | Note |
|------------|-----------|------|
| PWMA | 25 | LEDC PWM — left motor speed |
| AIN1 | 26 | Left direction |
| AIN2 | 27 | Left direction |
| PWMB | 17 | LEDC PWM — right motor (moved off GPIO 32 to free ADC1 for the IR sensor) |
| BIN1 | 33 | Right direction |
| BIN2 | 14 | Right direction |
| STBY | 13 | HIGH = driver enabled |
| VM | Battery + | Motor supply — **not** from the ESP32 |
| VCC | 3V3 | Logic supply |
| GND | GND | Common ground with the ESP32 |

> **DRV8833 instead?** Set `MOTOR_DRIVER_DRV8833` in `config.h` and wire only
> the four input pins (AIN1/AIN2/BIN1/BIN2 above). The PWMA/PWMB pins are
> unused — a DRV8833 has no separate enable input; the firmware PWMs the IN
> pins directly.

### Encoders (N20 quadrature)

| Signal | ESP32 GPIO | Note |
|--------|-----------|------|
| Left A | 34 | Input-only, interrupt (both edges) |
| Left B | 35 | Input-only, interrupt (both edges) |
| Right A | 36 (VP) | Input-only, interrupt (both edges) |
| Right B | 39 (VN) | Input-only, interrupt (both edges) |

> GPIO 34–39 have **no internal pull-ups**. Add external 10 kΩ pull-ups if
> your encoder outputs are open-collector. The firmware performs true ×4
> quadrature decoding, so `ENC_TICKS_PER_REV` = encoder PPR × gear ratio × 4.

### MPU6050 (I2C)

| MPU pin | ESP32 GPIO |
|---------|-----------|
| SDA | 21 |
| SCL | 22 |
| VCC | 3V3 |
| GND | GND |
| INT | 23 (optional, unused) |

### SH1106 1.3" I2C OLED (128×64) — status display

| OLED pin | ESP32 GPIO | Note |
|----------|-----------|------|
| SDA | 21 | Shared with the MPU6050 — different I2C address, no conflict |
| SCL | 22 | Shared with the MPU6050 |
| VCC | 3V3 | |
| GND | GND | |

> Default I2C address is `0x3C` (`OLED_I2C_ADDR` in `config.h`); some modules
> ship at `0x3D` instead — change it there if the screen stays blank. Not
> required for the robot to function — `display::begin()` just logs a warning
> and everything else keeps working if it isn't detected.

### SHARP GP2Y0A41SK0F IR distance sensor (analog, 4–30 cm)

| Sensor wire | ESP32 | Note |
|-------------|-------|------|
| Vo (signal — usually white/yellow) | GPIO 32 | **ADC1** pin required (ADC2 is dead while Wi-Fi is on) |
| Vcc (red) | 5 V | Module runs on 5 V; Vo stays ≤ ~3.1 V, safe for the ADC |
| GND (black) | GND | Common ground |

> Mount it facing forward, ~5–10 cm off the floor, level. It only sees straight
> ahead and is blind closer than ~4 cm — it is a forward bumper, not a 360°
> scanner. A 10 µF capacitor across Vcc/GND near the sensor steadies its noisy
> output.

### INMP441 microphone (I2S0 — input)

| INMP441 pin | ESP32 GPIO |
|-------------|-----------|
| SCK (BCLK) | 18 |
| WS (LRCL) | 19 |
| SD (DOUT) | 5 |
| L/R | GND (left channel) |
| VDD | 3V3 |
| GND | GND |

### MAX98357A amplifier (I2S1 — output)

| MAX98357 pin | ESP32 GPIO / connection |
|--------------|------------------------|
| BCLK | 4 |
| LRC | 15 |
| DIN | 2 |
| GAIN | Float (9 dB) or tie per datasheet |
| SD | 3V3 (enabled) |
| Vin | 5 V |
| GND | GND |
| OUT +/− | 2 W speaker |

## Board 2 — XIAO ESP32-S3 Sense (Vision Node)

The OV2640 camera uses the **fixed onboard pin map** of the Sense expansion
board — no wiring required. Power it over USB-C or a 5 V rail and connect it to
the same Wi-Fi network. The pin map lives in
`firmware/vision_node/include/config.h`; do not change it unless you are using
a different camera board.

## Ground Rule

**Every board and the motor supply must share a common ground.** Floating
grounds cause encoder glitches, I2C lockups, and audio noise.
