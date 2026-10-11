#include "../calendar_response.h"
#include "../constants.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
CalendarEvent events[CAL_EVENTS_MAX] = {};
unsigned int checks = 0;

void require(bool condition, const char *message) {
  ++checks;
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    std::exit(EXIT_FAILURE);
  }
}

std::string serialize(const JsonDocument &doc) {
  std::string payload;
  serializeJson(doc, payload);
  return payload;
}

std::string fixture(size_t count) {
  JsonDocument doc;
  JsonArray array = doc["events"].to<JsonArray>();
  for (size_t i = 0; i < count; ++i) {
    JsonObject event = array.add<JsonObject>();
    char time[6];
    snprintf(time, sizeof(time), "%02u:00", static_cast<unsigned int>(i % 24));
    event["start"] = time;
    event["end"] = "23:59";
    event["title"] = "Synthetic " + std::to_string(i);
    event["calendar"] = "Test calendar";
  }
  return serialize(doc);
}

CalendarResponseStatus parse(const std::string &payload) {
  return parseCalendarResponse(payload.data(), payload.size(), events);
}

void checkSlots(size_t count) {
  const CalendarEvent empty = {};
  for (size_t i = 0; i < CAL_EVENTS_MAX; ++i) {
    require(events[i].valid == (i < count), "exact valid-slot count");
    if (i >= count) {
      require(memcmp(&events[i], &empty, sizeof(empty)) == 0,
              "unused slots must be completely cleared");
    }
  }
}

void acceptCount(size_t count) {
  require(calendarResponseSucceeded(parse(fixture(count))), "valid response succeeds");
  const size_t retained = count < CAL_EVENTS_MAX ? count : CAL_EVENTS_MAX;
  checkSlots(retained);
  for (size_t i = 0; i < retained; ++i) {
    const std::string title = "Synthetic " + std::to_string(i);
    require(title == events[i].title, "retained event identity");
    require(std::string(events[i].calendar) == "Test calendar", "calendar copied");
  }
}

void reject(const std::string &payload, CalendarResponseStatus expected) {
  require(calendarResponseSucceeded(parse(fixture(6))), "seed prior full snapshot");
  CalendarEvent before[CAL_EVENTS_MAX];
  memcpy(before, events, sizeof(events));
  const CalendarResponseStatus status = parse(payload);
  require(status == expected, "rejection category");
  require(!calendarResponseSucceeded(status), "rejected payload is failure");
  require(memcmp(before, events, sizeof(events)) == 0,
          "rejection must preserve every original event byte");
}

JsonDocument eventDocument() {
  JsonDocument doc;
  require(!deserializeJson(doc, fixture(1)), "fixture must decode");
  return doc;
}

void invalidSchemas() {
  using Status = CalendarResponseStatus;
  reject("{\"error\":\"unauthorized\"}", Status::ApplicationError);
  reject("{\"error\":{\"code\":500},\"events\":[]}", Status::ApplicationError);
  reject("{\"error\":null,\"events\":[]}", Status::ApplicationError);
  reject("{\"error\":false,\"events\":[]}", Status::ApplicationError);
  for (const char *payload : {"{}", "{\"events\":null}", "{\"events\":{}}",
                             "{\"events\":true}", "{\"events\":3}",
                             "{\"events\":\"text\"}", "[]", "null"}) {
    reject(payload, Status::InvalidSchema);
  }
  for (const char *payload : {"", "{", "{\"events\":[", "not JSON",
                             "{\"events\":[]} garbage", "{\"events\":[]}{ }"}) {
    reject(payload, Status::JsonError);
  }
  std::string rawNull = fixture(1);
  rawNull.push_back('\0');
  reject(rawNull, Status::JsonError);
  for (const char *value : {"null", "false", "7", "\"text\"", "[]"}) {
    reject(std::string("{\"events\":[") + value + "]}", Status::InvalidSchema);
  }
  const char *fields[] = {"start", "end", "title", "calendar"};
  for (const char *field : fields) {
    JsonDocument doc = eventDocument();
    doc["events"][0].as<JsonObject>().remove(field);
    reject(serialize(doc), Status::InvalidSchema);
    doc = eventDocument();
    doc["events"][0][field] = nullptr;
    reject(serialize(doc), Status::InvalidSchema);
    doc["events"][0][field] = true;
    reject(serialize(doc), Status::InvalidSchema);
    doc["events"][0][field] = 42;
    reject(serialize(doc), Status::InvalidSchema);
    doc["events"][0][field].to<JsonArray>();
    reject(serialize(doc), Status::InvalidSchema);
  }
}

void timeAndText() {
  for (const char *field : {"start", "end"}) {
    for (const char *time : {"0:00", "00:0", "00:000", "00000", "aa:00",
                            "00:aa", "-1:00", "25:00", "23:60", "99:99",
                            "24:01", " 0:00", "00:0 "}) {
      JsonDocument doc = eventDocument();
      doc["events"][0][field] = time;
      reject(serialize(doc), CalendarResponseStatus::InvalidSchema);
    }
    JsonDocument doc = eventDocument();
    doc["events"][0][field] = JsonString("00:00\0suffix", 12);
    reject(serialize(doc), CalendarResponseStatus::InvalidSchema);
  }
  JsonDocument doc = eventDocument();
  doc["events"][0]["start"] = "24:00";
  reject(serialize(doc), CalendarResponseStatus::InvalidSchema);
  doc["events"][0]["start"] = "00:00";
  doc["events"][0]["end"] = "24:00";
  require(calendarResponseSucceeded(parse(serialize(doc))), "all-day values accepted");
  require(strcmp(events[0].start, "00:00") == 0 &&
              strcmp(events[0].end, "24:00") == 0, "all-day values preserved");
  doc["events"][0]["start"] = "23:59";
  doc["events"][0]["end"] = "00:00";
  require(calendarResponseSucceeded(parse(serialize(doc))), "cross-midnight accepted");

  doc["events"][0]["title"] = std::string(200, 'T');
  doc["events"][0]["calendar"] = std::string(100, 'C');
  require(calendarResponseSucceeded(parse(serialize(doc))), "long strings accepted");
  require(std::string(events[0].title) == std::string(63, 'T') &&
              events[0].title[63] == '\0', "title truncation and termination");
  require(std::string(events[0].calendar) == std::string(31, 'C') &&
              events[0].calendar[31] == '\0', "calendar truncation and termination");
  require(strcmp(events[0].end, "00:00") == 0, "text copying preserves neighboring fields");
  checkSlots(1);
  doc["events"][0]["title"] = "";
  doc["events"][0]["calendar"] = "";
  require(calendarResponseSucceeded(parse(serialize(doc))), "empty text strings accepted");
}

void replacementAndOrdering() {
  acceptCount(6);
  acceptCount(2);
  require(parse(fixture(0)) == CalendarResponseStatus::SuccessEmpty, "empty success status");
  checkSlots(0);
  for (size_t count : {size_t(0), size_t(1), size_t(2), size_t(6)}) {
    acceptCount(count);
    CalendarEvent before[CAL_EVENTS_MAX];
    memcpy(before, events, sizeof(events));
    require(!calendarResponseSucceeded(parse("{\"events\":null}")),
            "successive rejection fails");
    require(memcmp(before, events, sizeof(events)) == 0,
            "successive rejection preserves current empty/short/full snapshot");
    require(!calendarResponseSucceeded(parse("{\"error\":\"unauthorized\"}")),
            "second successive rejection fails");
    require(memcmp(before, events, sizeof(events)) == 0,
            "consecutive failures preserve the current snapshot");
  }
  acceptCount(1);
  reject("{\"events\":null}", CalendarResponseStatus::InvalidSchema);
  acceptCount(2);
  reject("{\"error\":\"unauthorized\"}", CalendarResponseStatus::ApplicationError);
  acceptCount(0);
  acceptCount(6);
  const std::string truncated = fixture(6);
  reject(truncated.substr(0, truncated.size() - 2), CalendarResponseStatus::JsonError);

  // Even records beyond six/eight must validate before any publication.
  for (size_t position : {size_t(3), size_t(6), size_t(7), size_t(9)}) {
    JsonDocument doc;
    require(!deserializeJson(doc, fixture(10)), "overflow fixture decodes");
    doc["events"][position]["title"] = false;
    reject(serialize(doc), CalendarResponseStatus::InvalidSchema);
  }
  JsonDocument doc;
  require(!deserializeJson(doc, fixture(8)), "ordering fixture decodes");
  const char *times[] = {"12:00", "09:30", "00:00", "23:59", "03:00", "06:00",
                        "01:00", "02:00"};
  for (size_t i = 0; i < 8; ++i)
    doc["events"][i]["start"] = times[i];
  require(parse(serialize(doc)) == CalendarResponseStatus::SuccessEvents, "nonempty success status");
  const char *expected[] = {"Synthetic 2", "Synthetic 4", "Synthetic 5",
                            "Synthetic 1", "Synthetic 0", "Synthetic 3"};
  for (size_t i = 0; i < CAL_EVENTS_MAX; ++i)
    require(strcmp(events[i].title, expected[i]) == 0,
            "sort the first six, without selecting a different subset");
  // Do not require qsort stability for equal start times.
}
} // namespace

int main() {
  static_assert(CAL_EVENTS_MAX == 6, "existing firmware capacity");
  static_assert(sizeof(CalendarEvent) == 109, "unchanged event fields and buffer sizes");
  for (size_t count : {size_t(0), size_t(1), size_t(6), size_t(8), size_t(10)})
    acceptCount(count);
  invalidSchemas();
  timeAndText();
  replacementAndOrdering();
  std::cout << "PASS: " << checks << " calendar response assertions; snapshot bytes="
            << sizeof(events) << '\n';
}
