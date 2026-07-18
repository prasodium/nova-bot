#include <Wire.h>
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SH110X.h>
#include "config.h"
#include "display.h"

// ---------------------------------------------------------------------------
// Two "eyes" drawn procedurally (no bitmaps) around fixed centers on a
// 128x64 panel. Each mood is just a different shape per eye, built from
// Adafruit_GFX primitives. A thin polyline (drawLine repeated at a few y
// offsets) stands in for a stroke since GFX lines are 1px.
// ---------------------------------------------------------------------------
namespace {
  Adafruit_SH1106G oled(OLED_WIDTH, OLED_HEIGHT, &Wire, -1);
  bool ok = false;

  constexpr int16_t EYE_LX = 36, EYE_RX = 92, EYE_Y = 30;

  uint32_t nextBlinkMs = BLINK_MIN_MS;   // first blink shortly after boot
  uint32_t blinkUntilMs = 0;

  void thickLine(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t thickness) {
    for (int16_t o = 0; o < thickness; o++) oled.drawLine(x0, y0 + o, x1, y1 + o, SH110X_WHITE);
  }

  // Upward chevron "^" (happy) or downward "v" (sad), per eye.
  void chevron(int16_t cx, int16_t cy, bool up) {
    int16_t dy = up ? -8 : 8;
    thickLine(cx - 15, cy, cx, cy + dy, 4);
    thickLine(cx, cy + dy, cx + 15, cy, 4);
  }

  void faceNeutral() {
    oled.fillRoundRect(EYE_LX - 15, EYE_Y - 13, 30, 26, 8, SH110X_WHITE);
    oled.fillRoundRect(EYE_RX - 15, EYE_Y - 13, 30, 26, 8, SH110X_WHITE);
  }

  void faceHappy() {
    chevron(EYE_LX, EYE_Y, true);
    chevron(EYE_RX, EYE_Y, true);
  }

  void faceAngry() {
    oled.fillRoundRect(EYE_LX - 15, EYE_Y - 6, 30, 16, 4, SH110X_WHITE);
    oled.fillRoundRect(EYE_RX - 15, EYE_Y - 6, 30, 16, 4, SH110X_WHITE);
    // Eyebrows angled inward-down, like a V over the eyes.
    oled.fillTriangle(EYE_LX - 16, EYE_Y - 20, EYE_LX + 15, EYE_Y - 10, EYE_LX + 15, EYE_Y - 15, SH110X_WHITE);
    oled.fillTriangle(EYE_RX + 16, EYE_Y - 20, EYE_RX - 15, EYE_Y - 10, EYE_RX - 15, EYE_Y - 15, SH110X_WHITE);
  }

  void heart(int16_t cx, int16_t cy) {
    oled.fillCircle(cx - 6, cy - 4, 8, SH110X_WHITE);
    oled.fillCircle(cx + 6, cy - 4, 8, SH110X_WHITE);
    oled.fillTriangle(cx - 13, cy - 2, cx + 13, cy - 2, cx, cy + 14, SH110X_WHITE);
  }

  void faceLove() {
    heart(EYE_LX, EYE_Y);
    heart(EYE_RX, EYE_Y);
  }

  void faceShy() {
    // Small eyes looking down, plus a blush hatch under each.
    oled.fillCircle(EYE_LX, EYE_Y + 6, 7, SH110X_WHITE);
    oled.fillCircle(EYE_RX, EYE_Y + 6, 7, SH110X_WHITE);
    for (int16_t o = -6; o <= 6; o += 4) {
      oled.drawLine(EYE_LX - 14 + o, EYE_Y + 24, EYE_LX - 6 + o, EYE_Y + 18, SH110X_WHITE);
      oled.drawLine(EYE_RX - 14 + o, EYE_Y + 24, EYE_RX - 6 + o, EYE_Y + 18, SH110X_WHITE);
    }
  }

  void faceSad() {
    chevron(EYE_LX, EYE_Y, false);
    chevron(EYE_RX, EYE_Y, false);
    // A single teardrop under the right eye.
    oled.fillCircle(EYE_RX + 10, EYE_Y + 20, 3, SH110X_WHITE);
    oled.fillTriangle(EYE_RX + 7, EYE_Y + 20, EYE_RX + 13, EYE_Y + 20, EYE_RX + 10, EYE_Y + 12, SH110X_WHITE);
  }

  void faceSleepy() {
    oled.fillRoundRect(EYE_LX - 15, EYE_Y - 3, 30, 6, 3, SH110X_WHITE);
    oled.fillRoundRect(EYE_RX - 15, EYE_Y - 3, 30, 6, 3, SH110X_WHITE);
    oled.setTextSize(1);
    oled.setCursor(100, 4);  oled.print("z");
    oled.setCursor(108, 10); oled.print("Z");
  }

  void faceSurprised() {
    oled.drawCircle(EYE_LX, EYE_Y, 15, SH110X_WHITE);
    oled.drawCircle(EYE_LX, EYE_Y, 14, SH110X_WHITE);
    oled.fillCircle(EYE_LX, EYE_Y, 5, SH110X_WHITE);
    oled.drawCircle(EYE_RX, EYE_Y, 15, SH110X_WHITE);
    oled.drawCircle(EYE_RX, EYE_Y, 14, SH110X_WHITE);
    oled.fillCircle(EYE_RX, EYE_Y, 5, SH110X_WHITE);
    thickLine(EYE_LX - 14, EYE_Y - 22, EYE_LX + 10, EYE_Y - 26, 2);
    thickLine(EYE_RX + 14, EYE_Y - 22, EYE_RX - 10, EYE_Y - 26, 2);
  }

  // Quick closed-eye lids, used for the idle "alive" blink — flashed briefly
  // over whatever mood is currently showing, not a mood of its own.
  void faceClosedEyes() {
    oled.fillRoundRect(EYE_LX - 15, EYE_Y - 2, 30, 4, 2, SH110X_WHITE);
    oled.fillRoundRect(EYE_RX - 15, EYE_Y - 2, 30, 4, 2, SH110X_WHITE);
  }

  // Classic cartoon dizzy spiral, per eye — a swirl traced outward in polar
  // coordinates rather than a bitmap.
  void spiral(int16_t cx, int16_t cy) {
    float angle = 0, radius = 1;
    int16_t px = cx, py = cy;
    for (int i = 0; i < 40; i++) {
      angle += 0.9f;
      radius += 0.33f;
      int16_t x = cx + (int16_t)(cosf(angle) * radius);
      int16_t y = cy + (int16_t)(sinf(angle) * radius);
      oled.drawLine(px, py, x, y, SH110X_WHITE);
      px = x; py = y;
    }
  }

  void faceDizzy() {
    spiral(EYE_LX, EYE_Y);
    spiral(EYE_RX, EYE_Y);
  }

  void drawMood(display::Mood m) {
    using M = display::Mood;
    switch (m) {
      case M::HAPPY:     faceHappy();     break;
      case M::ANGRY:     faceAngry();     break;
      case M::LOVE:      faceLove();      break;
      case M::SHY:       faceShy();       break;
      case M::SAD:       faceSad();       break;
      case M::SLEEPY:    faceSleepy();    break;
      case M::SURPRISED: faceSurprised(); break;
      case M::DIZZY:     faceDizzy();     break;
      default:            faceNeutral();  break;
    }
  }

  // Small sound-wave crescents in the bottom-right corner while capturing
  // voice. Centers sit just past the right edge so only a crescent shows —
  // GFX clips off-screen drawing for free.
  void drawListening() {
    oled.drawCircle(OLED_WIDTH + 2, OLED_HEIGHT - 6, 6, SH110X_WHITE);
    oled.drawCircle(OLED_WIDTH + 2, OLED_HEIGHT - 6, 12, SH110X_WHITE);
    oled.drawCircle(OLED_WIDTH + 2, OLED_HEIGHT - 6, 18, SH110X_WHITE);
  }
}

bool display::begin() {
#if ENABLE_OLED
  ok = oled.begin(OLED_I2C_ADDR, true);
  if (!ok) return false;
  oled.setRotation(0);
  oled.clearDisplay();
  oled.setTextColor(SH110X_WHITE);
  oled.cp437(true);
  oled.display();
  return true;
#else
  return false;
#endif
}

void display::showBoot(const char *line1, const char *line2) {
#if ENABLE_OLED
  if (!ok) return;
  oled.clearDisplay();
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println(line1);
  if (line2 && line2[0]) oled.println(line2);
  oled.display();
#else
  (void)line1; (void)line2;
#endif
}

void display::showFace(Mood mood, bool listening, bool wsConnected) {
#if ENABLE_OLED
  if (!ok) return;
  oled.clearDisplay();
  if (!wsConnected) {
    oled.setTextSize(1);
    oled.setCursor(4, 24);
    oled.println(" Waiting for");
    oled.println(" brain link...");
    oled.display();
    return;
  }
  uint32_t now = millis();
  if (now >= blinkUntilMs && now >= nextBlinkMs) {
    blinkUntilMs = now + BLINK_DURATION_MS;
    nextBlinkMs = blinkUntilMs + random(BLINK_MIN_MS, BLINK_MAX_MS);
  }
  if (now < blinkUntilMs) faceClosedEyes();
  else                    drawMood(mood);

  if (listening) drawListening();
  oled.display();
#else
  (void)mood; (void)listening; (void)wsConnected;
#endif
}

display::Mood display::moodFromName(const char *name) {
  if (!name) return Mood::NEUTRAL;
  String s(name); s.toLowerCase();
  if (s == "happy" || s == "smile" || s == "joy")        return Mood::HAPPY;
  if (s == "angry" || s == "mad")                        return Mood::ANGRY;
  if (s == "love" || s == "heart")                       return Mood::LOVE;
  if (s == "shy" || s == "embarrassed")                  return Mood::SHY;
  if (s == "sad" || s == "upset")                        return Mood::SAD;
  if (s == "sleepy" || s == "tired")                     return Mood::SLEEPY;
  if (s == "surprised" || s == "shocked" || s == "wow")  return Mood::SURPRISED;
  if (s == "dizzy" || s == "spin" || s == "spinning")    return Mood::DIZZY;
  return Mood::NEUTRAL;
}
