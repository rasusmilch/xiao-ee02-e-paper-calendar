#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <esp_sntp.h>
#include <atomic>
#include "../constants.h"

// Legacy cold-boot prerequisite only. Caller supplies SSID/PASSWORD declarations.
// This boot attempt exclusively owns the SNTP notification slot; it unregisters
// its callback and stops SNTP before returning. No RTC/persistent trust is added.
namespace LegacyTimeSync {
static std::atomic<bool> observed(false);
inline void synchronized(struct timeval*) {
  observed.store(true, std::memory_order_release);
}
}

inline bool syncTime() {
  bool trusted = false;
  LegacyTimeSync::observed.store(false, std::memory_order_release);
  WiFi.begin(SSID, PASSWORD);
  for (int i = 0; i < 100 && WiFi.status() != WL_CONNECTED; i++) {
    delay(100);
  }
  if (WiFi.status() == WL_CONNECTED) {
    if (esp_sntp_enabled())
      esp_sntp_stop();
    LegacyTimeSync::observed.store(false, std::memory_order_release);
    // Inspected IDF immediate mode updates system UTC before this notification.
    esp_sntp_set_sync_mode(SNTP_SYNC_MODE_IMMED);
    esp_sntp_set_time_sync_notification_cb(LegacyTimeSync::synchronized);
    configTzTime(TIMEZONE, NTP_SERVER);
    for (int retry = 0; retry < 20 &&
         !LegacyTimeSync::observed.load(std::memory_order_acquire); ++retry) {
      delay(500);
    }
    if (esp_sntp_enabled())
      esp_sntp_stop();
    esp_sntp_set_time_sync_notification_cb(nullptr);
    trusted = LegacyTimeSync::observed.load(std::memory_order_acquire);
  }
  LegacyTimeSync::observed.store(false, std::memory_order_release);
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return trusted;
}
