#ifndef EE02_WEATHER_REQUEST_H
#define EE02_WEATHER_REQUEST_H
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
namespace WeatherHttps {
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

}
#endif
