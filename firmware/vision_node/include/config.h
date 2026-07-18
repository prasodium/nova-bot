#pragma once
// ---------- WiFi ----------
// Real credentials live in secrets.h (gitignored, not committed).
// Copy secrets.h.example to secrets.h and fill in your own values.
// Can also be overridden at build time with -DWIFI_SSID='"..."' (e.g. CI secrets).
#if __has_include("secrets.h")
#include "secrets.h"
#endif
#ifndef WIFI_SSID
#define WIFI_SSID  "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASS
#define WIFI_PASS  "YOUR_WIFI_PASSWORD"
#endif

// ---------- HTTP server ----------
#define HTTP_PORT  80

// ---------- Camera quality ----------
// FRAMESIZE_QVGA(320x240) is light + fast for the LLM; raise to VGA/SVGA if you
// want more detail at the cost of bandwidth and latency.
#define CAM_FRAMESIZE  FRAMESIZE_VGA   // 640x480
#define CAM_JPEG_QUALITY 12            // 10(best)..63(worst)

// ---------- XIAO ESP32-S3 Sense camera pin map (OV2640) ----------
// Do NOT change unless you have a different camera board.
#define PWDN_GPIO_NUM   -1
#define RESET_GPIO_NUM  -1
#define XCLK_GPIO_NUM   10
#define SIOD_GPIO_NUM   40
#define SIOC_GPIO_NUM   39
#define Y9_GPIO_NUM     48
#define Y8_GPIO_NUM     11
#define Y7_GPIO_NUM     12
#define Y6_GPIO_NUM     14
#define Y5_GPIO_NUM     16
#define Y4_GPIO_NUM     18
#define Y3_GPIO_NUM     17
#define Y2_GPIO_NUM     15
#define VSYNC_GPIO_NUM  38
#define HREF_GPIO_NUM   47
#define PCLK_GPIO_NUM   13
