#include <WiFi.h>
#include <WebSocketsClient.h>
#include <ArduinoJson.h>
#include "config.h"
#include "comms.h"

namespace {
  WebSocketsClient ws;
  bool wsConnected = false;
  std::function<void(const String&)> cmdCb;
  std::function<void(const uint8_t*, size_t)> binCb;

  void onWsEvent(WStype_t type, uint8_t *payload, size_t len) {
    switch (type) {
      case WStype_CONNECTED:
        wsConnected = true;
        Serial.println("[ws] connected to brain");
        break;
      case WStype_DISCONNECTED:
        wsConnected = false;
        Serial.println("[ws] disconnected");
        break;
      case WStype_TEXT:
        if (cmdCb) cmdCb(String((char*)payload, len));
        break;
      case WStype_BIN:
        if (binCb) binCb(payload, len);
        break;
      default: break;
    }
  }
}

void comms::begin() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("[wifi] connecting");
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
    if (millis() - t0 > 30000) {            // don't hang forever on bad WiFi
      Serial.println("\n[wifi] connect timeout (30 s) — restarting");
      ESP.restart();
    }
  }
  Serial.printf("\n[wifi] ip=%s\n", WiFi.localIP().toString().c_str());

  ws.begin(BACKEND_HOST, BACKEND_PORT, BACKEND_WS_PATH);
  ws.onEvent(onWsEvent);
  ws.setReconnectInterval(2000);
  ws.enableHeartbeat(15000, 3000, 2);
}

void comms::loop() { ws.loop(); }
bool comms::connected() { return wsConnected; }
String comms::localIP() { return WiFi.localIP().toString(); }
void comms::onCommand(std::function<void(const String&)> cb) { cmdCb = cb; }
void comms::onBinary(std::function<void(const uint8_t*, size_t)> cb) { binCb = cb; }
void comms::sendBinary(const uint8_t *data, size_t len) {
  if (wsConnected) ws.sendBIN((uint8_t*)data, len);
}

void comms::sendTelemetry(float heading, float pitch, float roll,
                          float lrpm, float rrpm, long lt, long rt, bool tiltFault, bool blocked, float distanceCm) {
  if (!wsConnected) return;
  JsonDocument doc;
  doc["type"] = "telemetry";
  doc["ts"] = millis();
  doc["heading_deg"] = heading;
  doc["pitch_deg"] = pitch;
  doc["roll_deg"] = roll;
  doc["left_rpm"] = lrpm;
  doc["right_rpm"] = rrpm;
  doc["left_ticks"] = lt;
  doc["right_ticks"] = rt;
  doc["tilt_fault"] = tiltFault;
  doc["blocked"] = blocked;
  doc["distance_cm"] = distanceCm;
  doc["link"] = "ok";
  String out; serializeJson(doc, out);
  ws.sendTXT(out);
}

void comms::sendAck(uint32_t cmdId, bool ok) {
  if (!wsConnected) return;
  JsonDocument doc;
  doc["type"] = "ack"; doc["cmd_id"] = cmdId; doc["ok"] = ok;
  String out; serializeJson(doc, out);
  ws.sendTXT(out);
}

void comms::sendEvent(const char *name, bool value) {
  if (!wsConnected) return;
  JsonDocument doc;
  doc["type"] = "event"; doc["name"] = name; doc["value"] = value;
  String out; serializeJson(doc, out);
  ws.sendTXT(out);
}
