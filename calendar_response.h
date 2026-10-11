#pragma once

#include <ArduinoJson.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

struct CalendarEvent {
  bool valid;
  char start[6];
  char end[6];
  char title[64];
  char calendar[32];
};

enum class CalendarResponseStatus {
  SuccessEmpty,
  SuccessEvents,
  JsonError,
  ApplicationError,
  InvalidSchema
};

inline bool calendarResponseSucceeded(CalendarResponseStatus status) {
  return status == CalendarResponseStatus::SuccessEmpty ||
         status == CalendarResponseStatus::SuccessEvents;
}

namespace calendarResponseDetail {
// ArduinoJson stops at the closing object. Track consumption without copying the
// payload so a second document or trailing garbage cannot be accepted as success.
struct PayloadReader {
  const char *data;
  size_t size;
  size_t position;

  int read() {
    return position < size ? static_cast<unsigned char>(data[position++]) : -1;
  }
  size_t readBytes(char *buffer, size_t length) {
    const size_t remaining = size - position;
    const size_t count = length < remaining ? length : remaining;
    memcpy(buffer, data + position, count);
    position += count;
    return count;
  }
};

inline bool validTime(JsonString value, bool end) {
  if (value.size() != 5)
    return false;
  const char *s = value.c_str();
  if (s[2] != ':' || s[0] < '0' || s[0] > '9' || s[1] < '0' ||
      s[1] > '9' || s[3] < '0' || s[3] > '9' || s[4] < '0' || s[4] > '9')
    return false;
  const int hour = (s[0] - '0') * 10 + s[1] - '0';
  const int minute = (s[3] - '0') * 10 + s[4] - '0';
  return minute <= 59 && (hour <= 23 || (end && hour == 24 && minute == 0));
}

template <size_t Size>
inline void copyText(char (&destination)[Size], const char *source) {
  strncpy(destination, source, Size - 1);
  destination[Size - 1] = '\0';
}

inline int compareEvents(const void *a, const void *b) {
  return strcmp(static_cast<const CalendarEvent *>(a)->start,
                static_cast<const CalendarEvent *>(b)->start);
}
} // namespace calendarResponseDetail

// Validate every returned record, including those beyond the display capacity.
// Only a completely accepted response replaces the volatile snapshot. The fixed
// temporary array costs Capacity * sizeof(CalendarEvent), not one copy per event.
template <size_t Capacity>
CalendarResponseStatus parseCalendarResponse(
    const char *payload, size_t length, CalendarEvent (&destination)[Capacity]) {
  using namespace calendarResponseDetail;
  if (!payload || memchr(payload, '\0', length))
    return CalendarResponseStatus::JsonError;
  PayloadReader reader = {payload, length, 0};
  JsonDocument doc;
  if (deserializeJson(doc, reader))
    return CalendarResponseStatus::JsonError;
  while (reader.position < length) {
    const int c = reader.read();
    if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
      return CalendarResponseStatus::JsonError;
  }
  if (!doc.is<JsonObjectConst>())
    return CalendarResponseStatus::InvalidSchema;
  const JsonObjectConst root = doc.as<JsonObjectConst>();
  // Presence of the transport's error field is an error, regardless of its type.
  if (!root["error"].isUnbound())
    return CalendarResponseStatus::ApplicationError;
  if (!root["events"].is<JsonArrayConst>())
    return CalendarResponseStatus::InvalidSchema;

  CalendarEvent pending[Capacity] = {};
  size_t count = 0;
  for (JsonVariantConst value : root["events"].as<JsonArrayConst>()) {
    if (!value.is<JsonObjectConst>())
      return CalendarResponseStatus::InvalidSchema;
    const JsonObjectConst event = value.as<JsonObjectConst>();
    if (!event["start"].is<const char *>() ||
        !event["end"].is<const char *>() ||
        !event["title"].is<const char *>() ||
        !event["calendar"].is<const char *>() ||
        !validTime(event["start"].as<JsonString>(), false) ||
        !validTime(event["end"].as<JsonString>(), true))
      return CalendarResponseStatus::InvalidSchema;
    if (count < Capacity) {
      copyText(pending[count].start, event["start"].as<const char *>());
      copyText(pending[count].end, event["end"].as<const char *>());
      copyText(pending[count].title, event["title"].as<const char *>());
      copyText(pending[count].calendar, event["calendar"].as<const char *>());
      pending[count].valid = true;
      ++count;
    }
  }
  // Preserve the existing qsort/HH:MM behavior, including unspecified tie order.
  qsort(pending, count, sizeof(CalendarEvent), compareEvents);
  memcpy(destination, pending, sizeof(pending));
  return count == 0 ? CalendarResponseStatus::SuccessEmpty
                    : CalendarResponseStatus::SuccessEvents;
}
