# Changelog

All notable changes to this project are documented in this file.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.1.0] — 2026-06-10

Code-review release. Fixes four functional bugs, one safety-level design flaw,
and several robustness issues found during a full audit of the firmware,
backend, and CI pipeline.

### Fixed — Firmware (main controller)

- **Steering sign convention inverted.** `docs/PROTOCOL.md`, the backend, and
  the teleop client all define `angular > 0` as *turn left* (counter-clockwise),
  but the differential mix in `main.cpp` sped up the **left** wheel for positive
  angular, turning the robot **right**. Voice and planner "left"/"right"
  commands were therefore swapped, and `turn_to` exhibited positive feedback
  (the heading error grew instead of shrinking) with a z-up IMU mount. The mix
  is now `left = linear − angular`, `right = linear + angular`, consistent with
  the protocol and the gyro-Z convention, and `turn_to` converges.
- **Encoder calibration off by 4×.** `ENC_TICKS_PER_REV` (1400) is documented as
  PPR × gear ratio × 4 (quadrature), but the ISR counted only the rising edge of
  channel A (×1 decoding ≈ 350 counts/rev). Measured RPM read one quarter of
  reality, driving the PID to roughly 4× the commanded speed. The encoder reader
  now performs true ×4 quadrature decoding (interrupts on both edges of both
  channels) with a transition lookup table that also rejects glitches.
- **TTS playback froze all safety reflexes (safety flaw).** Incoming audio
  frames were played with a blocking `i2s_write(..., portMAX_DELAY)` directly
  inside the WebSocket callback, stalling the entire main loop — IR hard-stop,
  tilt cutoff, stall/impact detection, and the command watchdog — for the full
  duration of speech, with motors still energized. Playback now runs in a
  dedicated FreeRTOS task fed by a 1-second ring buffer; the control loop is
  never blocked. Motion is paused when an utterance starts.
- **DRV8833 motor-driver option could not reverse.** The DRV8833 branch applied
  PWM to the (unconnected) enable pin for both directions. The driver is now
  implemented correctly: each H-bridge input has its own LEDC channel, with
  direction selected by which input receives PWM.
- **`play_audio` command implemented.** The backend announces incoming TTS with
  a `play_audio` command; the firmware now handles it (pauses motion) and the
  command is documented in `docs/PROTOCOL.md`.
- **Wi-Fi connect no longer hangs forever.** The controller restarts after a
  30-second connection timeout instead of blocking indefinitely at boot.

### Fixed — Backend

- **Speech-to-text never worked.** `ai/stt.py` passed a `sampling_rate` keyword
  that `faster-whisper`'s `transcribe()` does not accept; the resulting
  `TypeError` was silently swallowed and every utterance transcribed to an empty
  string. The argument is removed and audio is resampled to 16 kHz when needed.
- **TTS frames are now paced at real-time rate** so the firmware's ring buffer
  cannot overflow on long sentences (previously the entire clip was blasted at
  once).
- **Wake-name matching uses word boundaries.** "Nova" no longer triggers inside
  words such as "innovation".
- **Migrated from deprecated `@app.on_event` hooks to the FastAPI lifespan
  API.**

### Fixed — CI / Build

- **`esptool` pinned to 4.x** in the GitHub Actions workflow. esptool v5 renamed
  the underscore commands (`merge_bin` → `merge-bin`) and changed flag syntax,
  which would break the merge step.

### Changed — Documentation

- All Markdown documents reorganized and rewritten in a consistent professional
  format with tables of contents where appropriate.
- `docs/PROTOCOL.md` now documents the `play_audio` command, binary audio
  framing in both directions, and the authoritative steering sign convention.
- `docs/WIRING.md`, `docs/HARDWARE.md`, `docs/SETUP.md`, and
  `docs/ARCHITECTURE.md` updated to reflect the corrected encoder decoding and
  the non-blocking audio pipeline.

## [1.0.0] — 2026-06-08

Initial release: two-board ESP32 robot (main controller + camera vision node),
Python FastAPI backend with vision-LLM planner, voice in/out, teleop CLI, and a
CI workflow producing flashable merged binaries.
