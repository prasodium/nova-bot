# Hardware / Bill of Materials

## Components

| # | Part | Role | Interface |
|---|------|------|-----------|
| 1 | ESP32 DevKit v1 | Main controller: motors, encoders, IMU, audio, link to brain | Wi-Fi |
| 2 | XIAO ESP32-S3 Sense | Vision node: OV2640 camera HTTP server | Wi-Fi |
| 3 | TB6612FNG motor driver | Dual H-bridge for the drive motors (alternatives below) | PWM + GPIO |
| 4 | 2× N20 gear motor with quadrature encoder | Drivetrain with closed-loop speed | PWM / interrupts |
| 5 | MPU6050 | 6-axis IMU: heading, pitch/roll, tilt and impact detection | I2C |
| 6 | SHARP GP2Y0A41SK0F | Forward IR distance, 4–30 cm → on-device hard stop | Analog (ADC1) |
| 7 | INMP441 | I2S MEMS microphone (voice input) | I2S |
| 8 | MAX98357A | I2S Class-D amplifier (voice output) | I2S |
| 9 | 2 W speaker | Audio output element | via MAX98357 |
| 10 | Battery pack (e.g. 2S Li-ion, 6–8 V) | Motor supply | — |

## Motor Driver Options

The firmware supports three common dual H-bridges, selected by exactly one
`MOTOR_DRIVER_*` define in `firmware/main_controller/include/config.h`:

| Driver | Topology | Firmware setting | Notes |
|--------|----------|------------------|-------|
| **TB6612FNG** (default) | 2 direction pins + 1 PWM per motor, plus STBY | `MOTOR_DRIVER_TB6612` | Efficient, 3V3 logic; recommended |
| **DRV8833** | 2 PWM-capable inputs per motor, no enable pin | `MOTOR_DRIVER_DRV8833` | Direction is selected by which input is PWMed; `PIN_PWMA`/`PIN_PWMB` are unused — wire only the four IN pins |
| **L298N** | 2 direction pins + 1 enable per motor | `MOTOR_DRIVER_L298N` | Works, but large, inefficient, ~2 V drop; 5 V logic tolerant |

> A "TB6601" is not a standard part. If your board is marked TB6601, it is most
> likely a TB6612FNG clone (use the default). A **TB6600** is a *stepper*
> driver and is not compatible with the N20 DC motors used here.

## Power Design

- **Never power the motors from the ESP32's 5 V or 3V3 pins.** Use a separate
  motor supply into the driver's VM and share **ground** with the ESP32.
- The MAX98357 can draw 1–2 W peaks; give it a clean 5 V rail with a bulk
  capacitor (≥ 220 µF recommended).
- 3V3 regulator brownout is the most common cause of random ESP32 reboots in
  small robots — budget supply headroom and add decoupling near each board.
- A 10 µF capacitor across the SHARP sensor's Vcc/GND steadies its notoriously
  noisy supply current.

## Forward Distance Sensor

The SHARP GP2Y0A41SK0F (4–30 cm analog IR) is wired to **GPIO 32 (ADC1)**.
ADC1 is mandatory because the ESP32's ADC2 is unavailable while Wi-Fi is
active; the right-motor PWM was moved from GPIO 32 to GPIO 17 to free this pin.
It drives the low-latency forward hard stop (`SHARP_STOP_CM` in `config.h`).

Limitations: forward-only, ~4–30 cm range, blind (response folds back) below
~4 cm. Treat it as a front bumper, not a scanner.

## Recommended Additions

- **VL53L0X ToF** or **HC-SR04 ultrasonic** sensors for wider obstacle coverage.
- **INA219** (or a resistor divider) for battery-voltage telemetry.
- A physical **power switch / e-stop** within easy reach.
