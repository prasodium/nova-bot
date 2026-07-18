#pragma once
#include <Arduino.h>
// SH1106 1.3" I2C OLED — animated eyes / mood face. Mirrors the IMU's
// pattern: begin() returns false if the screen isn't detected, and every
// other call becomes a safe no-op — fine to compile/flash before it's wired.
namespace display {
  enum class Mood : uint8_t { NEUTRAL, HAPPY, ANGRY, LOVE, SHY, SAD, SLEEPY, SURPRISED, DIZZY };

  bool begin();
  void showBoot(const char *line1, const char *line2 = "");
  // Full-screen mood face. While the backend link is down, shows a small
  // status message instead (nothing useful to emote about yet).
  void showFace(Mood mood, bool listening, bool wsConnected);
  // Maps a free-text mood word (voice command / LLM field) to Mood; unknown
  // or empty input returns NEUTRAL.
  Mood moodFromName(const char *name);
}
