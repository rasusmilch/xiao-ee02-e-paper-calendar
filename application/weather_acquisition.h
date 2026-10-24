#pragma once

#include "weather_values.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <StreamString.h>
#include <WiFiClientSecure.h>
#include <string.h>
#include <time.h>
#include <stdint.h>
#include <utility>

#if defined(ARDUINO_ARCH_ESP32)
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR != 3 || ESP_ARDUINO_VERSION_MINOR != 3 || ESP_ARDUINO_VERSION_PATCH != 12
#error "Weather HTTPS requires the inspected Arduino-ESP32 3.3.12 API"
#endif
#if !defined(ARDUHAL_LOG_LEVEL) || ARDUHAL_LOG_LEVEL != 0
#error "Weather HTTPS requires Core Debug Level None: platform HTTP logs can expose API queries"
#endif
#endif

// Authenticated weather acquisition with explicit borrowed request inputs.
// weather.h supplies the legacy one-argument entry points/defaults.
// No graphics, Wi-Fi orchestration, credentials include or panel.
// Preserve the sketch translation unit's existing internal-linkage observations;
// include this guarded header instead of redeclaring/copying their storage.
namespace WeatherHttps {
// Caller guarantees the entire span is readable and remains alive/unchanged until
// the fetch returns. caBytes includes exactly one terminal NUL. No trust is retained.
struct Trust {
  bool clockTrusted;
  const char* caPem;
  size_t caBytes;
};
inline bool admitted(const Trust& trust) {
  if (!trust.clockTrusted || !trust.caPem || trust.caBytes < 2)
    return false;
  const uintptr_t address = reinterpret_cast<uintptr_t>(trust.caPem);
  // Require a representable end for the whole caller-declared span.
  if (trust.caBytes > UINTPTR_MAX - address)
    return false;
  return trust.caPem[trust.caBytes - 1] == '\0' &&
         memchr(trust.caPem, '\0', trust.caBytes - 1) == nullptr;
}

// Byte lengths exclude any terminal NUL. Caller keeps both spans readable and
// stable through the call. They are transient request inputs, never stored.
struct Request {
  const char* location; size_t locationBytes;
  const char* apiKey; size_t apiKeyBytes;
};
inline size_t boundedLength(const char* value, size_t capacity) {
  if (value) for (size_t n = 0; n < capacity; ++n)
    if (!value[n]) return n;
  return 0; // Missing, empty or over-budget legacy C string.
}
inline bool requestSpan(const char* value, size_t bytes, size_t limit) {
  return value && bytes && bytes <= limit &&
         bytes <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(value);
}
inline bool unreserved(unsigned char c) {
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~';
}
// Existing 128-byte encoded-location and 256-byte URL capacities remain bounds,
// not truncation permissions. Commas retain the original city,country syntax.
inline bool makeUrl(const Request& request, const char* endpoint, const char* suffix, char (&url)[256]) {
  if (!requestSpan(request.location, request.locationBytes, 127) ||
      !requestSpan(request.apiKey, request.apiKeyBytes, 255)) return false;
  char encoded[128]; size_t used = 0;
  const char hex[] = "0123456789ABCDEF";
  for (size_t i = 0; i < request.locationBytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(request.location[i]);
    if (c < 32 || c == 127) return false;
    const size_t width = unreserved(c) || c == ',' ? 1 : 3;
    if (width > sizeof(encoded) - 1 - used) return false;
    if (width == 1) encoded[used++] = char(c);
    else { encoded[used++] = '%'; encoded[used++] = hex[c >> 4]; encoded[used++] = hex[c & 15]; }
  }
  encoded[used] = '\0';
  // OpenWeather keys are opaque unreserved ASCII; never interpret query syntax.
  for (size_t i = 0; i < request.apiKeyBytes; ++i)
    if (!unreserved(static_cast<unsigned char>(request.apiKey[i]))) return false;
  const int n = snprintf(url, sizeof(url),
      "https://api.openweathermap.org/data/2.5/%s?q=%s&appid=%.*s&units=metric%s",
      endpoint, encoded, int(request.apiKeyBytes), request.apiKey,
      suffix);
  return n > 0 && size_t(n) < sizeof(url);
}

// HTTPClient::getString() discards writeToStream's failure result in core 3.3.12.
// Use the same String-backed stream, check transfer completion, then move its
// String storage: one payload buffer, unchanged JSON input/interpretation.
inline bool readPayload(HTTPClient& http, String& payload) {
  StreamString received;
  if (http.writeToStream(&received) < 0)
    return false;
  payload = std::move(static_cast<String&>(received));
  return true;
}
} // namespace WeatherHttps

static WeatherData weatherData = {false, "", "", "", 0.0f, 0, 0.0f, 0.0f};
static ForecastEntry forecastData[FORECAST_COUNT] = {};
static DailyForecastEntry dailyForecastData[DAILY_FCST_COUNT] = {};

bool fetchWeatherData(const WeatherHttps::Trust& trust, const WeatherHttps::Request& request) {
  if (!WeatherHttps::admitted(trust))
    return false;
  char url[256];
  if (!WeatherHttps::makeUrl(request, "weather", "", url))
    return false;
  WiFiClientSecure client;
  client.setCACert(trust.caPem);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    http.end();
    client.stop();
    Serial.println("Weather HTTPS initialization failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    client.stop();
    Serial.printf("Weather API returned %d\n", code);
    return false;
  }
  String payload;
  const bool received = WeatherHttps::readPayload(http, payload);
  http.end();
  client.stop();
  if (!received) {
    Serial.println("Weather HTTPS transfer failed");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("JSON parse error: %s\n", err.c_str());
    return false;
  }

  const char *icon = doc["weather"][0]["icon"];
  const char *desc = doc["weather"][0]["description"];
  float temp = doc["main"]["temp"];
  int humidity = doc["main"]["humidity"];
  float rain = 0.0f;
  if (doc["rain"]["1h"].is<float>())
    rain = doc["rain"]["1h"];
  else if (doc["rain"]["3h"].is<float>())
    rain = doc["rain"]["3h"];

  if (icon)
    strncpy(weatherData.icon, icon, sizeof(weatherData.icon) - 1);
  if (desc)
    strncpy(weatherData.description, desc, sizeof(weatherData.description) - 1);

  time_t now;
  time(&now);
  struct tm *tm_info = localtime(&now);
  strftime(weatherData.fetchTime, sizeof(weatherData.fetchTime),
           "%Y-%m-%d @ %H:%M", tm_info);

  weatherData.temp = temp;
  weatherData.humidity = humidity;
  weatherData.rain = rain;
  weatherData.windSpeed = doc["wind"]["speed"].is<float>()
                              ? doc["wind"]["speed"].as<float>()
                              : 0.0f;
  weatherData.valid = true;

  Serial.printf("Weather: %s, %s, %.1f°C, %d%%, %.1fmm, %.1fm/s wind\n",
                weatherData.icon, weatherData.description, weatherData.temp,
                weatherData.humidity, weatherData.rain, weatherData.windSpeed);
  return true;
}

bool fetchForecastData(const WeatherHttps::Trust& trust, const WeatherHttps::Request& request) {
  if (!WeatherHttps::admitted(trust))
    return false;
  char url[256];
  if (!WeatherHttps::makeUrl(request, "forecast", "&cnt=5", url))
    return false;
  WiFiClientSecure client;
  client.setCACert(trust.caPem);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    http.end();
    client.stop();
    Serial.println("Weather HTTPS initialization failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    client.stop();
    Serial.printf("Forecast API returned %d\n", code);
    return false;
  }
  String payload;
  const bool received = WeatherHttps::readPayload(http, payload);
  http.end();
  client.stop();
  if (!received) {
    Serial.println("Weather HTTPS transfer failed");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("Forecast JSON error: %s\n", err.c_str());
    return false;
  }

  JsonArray list = doc["list"];
  for (int i = 0; i < FORECAST_COUNT && static_cast<size_t>(i) < list.size(); i++) {
    const char *icon = list[i]["weather"][0]["icon"];
    const char *dt_txt = list[i]["dt_txt"];
    float high = list[i]["main"]["temp_max"];
    float low = list[i]["main"]["temp_min"];
    if (icon)
      strncpy(forecastData[i].icon, icon, sizeof(forecastData[i].icon) - 1);
    if (dt_txt && strlen(dt_txt) >= 16) {
      strncpy(forecastData[i].timeStr, dt_txt + 11, 5);
      forecastData[i].timeStr[5] = '\0';
    }
    forecastData[i].tempHigh = high;
    forecastData[i].tempLow = low;
    forecastData[i].valid = true;
    Serial.printf("Forecast[%d]: %s %s %.0f/%.0f°C\n", i,
                  forecastData[i].timeStr, forecastData[i].icon,
                  forecastData[i].tempHigh, forecastData[i].tempLow);
  }
  return true;
}

bool fetchDailyForecastData(const WeatherHttps::Trust& trust, const WeatherHttps::Request& request) {
  if (!WeatherHttps::admitted(trust))
    return false;
  char url[256];
  if (!WeatherHttps::makeUrl(request, "forecast", "&cnt=40", url))
    return false;
  WiFiClientSecure client;
  client.setCACert(trust.caPem);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_DISABLE_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) {
    http.end();
    client.stop();
    Serial.println("Weather HTTPS initialization failed");
    return false;
  }
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    client.stop();
    Serial.printf("Daily forecast API returned %d\n", code);
    return false;
  }
  String payload;
  const bool received = WeatherHttps::readPayload(http, payload);
  http.end();
  client.stop();
  if (!received) {
    Serial.println("Weather HTTPS transfer failed");
    return false;
  }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("Daily forecast JSON error: %s\n", err.c_str());
    return false;
  }

  // Group forecast entries by calendar day
  // Per day: sum of tempHigh/tempLow, count, best icon (midday preferred)
  struct DayBucket {
    int day;   // day of month (1-31)
    int month; // month (1-12)
    int count;
    float sumHigh;
    float sumLow;
    float maxHigh;
    float minLow;
    char middayIcon[4];
    char middayDesc[32];
    int middayHour; // closest hour to midday (12-15)
    char fallbackIcon[4];
    char fallbackDesc[32];
    bool hasMidday;
  };

  DayBucket buckets[DAILY_FCST_COUNT];
  for (int i = 0; i < DAILY_FCST_COUNT; i++) {
    buckets[i].day = 0;
    buckets[i].month = 0;
    buckets[i].count = 0;
    buckets[i].sumHigh = 0;
    buckets[i].sumLow = 0;
    buckets[i].maxHigh = -100;
    buckets[i].minLow = 100;
    buckets[i].middayIcon[0] = '\0';
    buckets[i].middayDesc[0] = '\0';
    buckets[i].middayHour = 99;
    buckets[i].fallbackIcon[0] = '\0';
    buckets[i].fallbackDesc[0] = '\0';
    buckets[i].hasMidday = false;
  }

  JsonArray list = doc["list"];
  int bucketCount = 0;
  int yearValue = 0;

  for (int i = 0; static_cast<size_t>(i) < list.size(); i++) {
    const char *dt_txt = list[i]["dt_txt"];
    const char *icon = list[i]["weather"][0]["icon"];
    const char *desc = list[i]["weather"][0]["description"];
    float high = list[i]["main"]["temp_max"];
    float low = list[i]["main"]["temp_min"];

    if (!dt_txt || strlen(dt_txt) < 10)
      continue;

    // Parse date: "YYYY-MM-DD HH:MM:SS"
    int m, d, hh;
    sscanf(dt_txt, "%d-%d-%d %d:", &yearValue, &m, &d, &hh);

    // Find or create bucket for this day
    int bi = -1;
    for (int j = 0; j < bucketCount; j++) {
      if (buckets[j].day == d && buckets[j].month == m) {
        bi = j;
        break;
      }
    }
    if (bi == -1) {
      if (bucketCount >= DAILY_FCST_COUNT)
        break; // we have enough days
      bi = bucketCount++;
      buckets[bi].day = d;
      buckets[bi].month = m;
    }

    buckets[bi].count++;
    buckets[bi].sumHigh += high;
    buckets[bi].sumLow += low;
    if (high > buckets[bi].maxHigh)
      buckets[bi].maxHigh = high;
    if (low < buckets[bi].minLow)
      buckets[bi].minLow = low;

    // Store fallback icon/desc (first entry of the day)
    if (buckets[bi].fallbackIcon[0] == '\0' && icon) {
      strncpy(buckets[bi].fallbackIcon, icon,
              sizeof(buckets[bi].fallbackIcon) - 1);
      if (desc)
        strncpy(buckets[bi].fallbackDesc, desc,
                sizeof(buckets[bi].fallbackDesc) - 1);
    }

    // Prefer midday icon/desc (12:00-15:00)
    if (icon && hh >= 12 && hh <= 15) {
      int dist = abs(hh - 13); // distance from 13:00
      if (!buckets[bi].hasMidday || dist < buckets[bi].middayHour) {
        buckets[bi].hasMidday = true;
        buckets[bi].middayHour = dist;
        strncpy(buckets[bi].middayIcon, icon,
                sizeof(buckets[bi].middayIcon) - 1);
        if (desc)
          strncpy(buckets[bi].middayDesc, desc,
                  sizeof(buckets[bi].middayDesc) - 1);
      }
    }
  }

  // Populate dailyForecastData from buckets
  for (int i = 0; i < bucketCount && i < DAILY_FCST_COUNT; i++) {
    if (buckets[i].count == 0)
      continue;

    dailyForecastData[i].valid = true;

    // Use midday icon/desc if available, else fallback
    if (buckets[i].hasMidday) {
      strncpy(dailyForecastData[i].icon, buckets[i].middayIcon,
              sizeof(dailyForecastData[i].icon) - 1);
      strncpy(dailyForecastData[i].description, buckets[i].middayDesc,
              sizeof(dailyForecastData[i].description) - 1);
    } else {
      strncpy(dailyForecastData[i].icon, buckets[i].fallbackIcon,
              sizeof(dailyForecastData[i].icon) - 1);
      strncpy(dailyForecastData[i].description, buckets[i].fallbackDesc,
              sizeof(dailyForecastData[i].description) - 1);
    }

    dailyForecastData[i].tempHigh = buckets[i].maxHigh;
    dailyForecastData[i].tempLow = buckets[i].minLow;

    // Get day name from day/month -> weekday (full name)
    struct tm timeinfo = {};
    timeinfo.tm_year = yearValue - 1900;
    timeinfo.tm_mon = buckets[i].month - 1;
    timeinfo.tm_mday = buckets[i].day;
    mktime(&timeinfo);
    const char *fullName = FULL_DAY_NAMES[timeinfo.tm_wday];
    strncpy(dailyForecastData[i].dayName, fullName,
            sizeof(dailyForecastData[i].dayName) - 1);
    dailyForecastData[i].dayName[sizeof(dailyForecastData[i].dayName) - 1] =
        '\0';

    Serial.printf("Daily[%d]: %s %s %.0f/%.0f°C (%s)\n", i,
                  dailyForecastData[i].dayName, dailyForecastData[i].icon,
                  dailyForecastData[i].tempHigh, dailyForecastData[i].tempLow,
                  dailyForecastData[i].description);
  }

  return true;
}
