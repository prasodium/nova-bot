#pragma once
#include <Arduino.h>
#include <functional>
// WiFi + WebSocket client to the backend brain.
namespace comms {
  // Callback receives a parsed command type and the raw JSON document.
  void begin();
  void loop();
  bool connected();
  String localIP();   // valid once begin() returns (it blocks until WiFi joins)
  void sendTelemetry(float heading, float pitch, float roll,
                     float lrpm, float rrpm, long lt, long rt, bool tiltFault, bool blocked, float distanceCm);
  void sendAck(uint32_t cmdId, bool ok);
  void sendEvent(const char *name, bool value);
  void sendBinary(const uint8_t *data, size_t len);   // e.g. mic audio
  // Register handler for incoming command JSON text.
  void onCommand(std::function<void(const String&)> cb);
  // Register handler for incoming binary frames (e.g. TTS audio PCM16).
  void onBinary(std::function<void(const uint8_t*, size_t)> cb);
}
