#pragma once

#define BOARD_SCREEN_COMBO 510
#define USE_XIAO_EPAPER_DISPLAY_BOARD_EE02
#include "TFT_eSPI.h"
#include "constants.h"
#include "credentials.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <stdlib.h>
#include <time.h>

// ============================================================
// Calendar utility functions
// ============================================================

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
}

int daysInMonth(int year, int month) {
  int days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year))
    return 29;
  return days[month - 1];
}

// Returns day of week: 0=Monday, 1=Tuesday, ..., 6=Sunday
int dayOfWeek(int year, int month, int day) {
  tm timeinfo = {};
  timeinfo.tm_year = year - 1900;
  timeinfo.tm_mon = month - 1;
  timeinfo.tm_mday = day;
  mktime(&timeinfo);
  return (timeinfo.tm_wday + 6) % 7; // convert Sunday=0 to Monday=0
}

// Legacy boot time attempt, separated for a focused fresh-notification test.
#include "application/legacy_time_sync.h"

// Draw the monthly calendar on the e-paper display
void drawCalendar(EPaper &epaper) {
  struct tm t;
  if (!getLocalTime(&t))
    return;

  int year = t.tm_year + 1900;
  int month = t.tm_mon + 1;
  int today = t.tm_mday;

  char buf[32];

  // --- Month & Year header (white text on black background) ---
  epaper.setTextColor(TFT_BLACK);
  epaper.setFreeFont(&FreeSans18pt7b);
  sprintf(buf, "%s %d", MONTH_NAMES[month - 1], year);
  int tw = epaper.textWidth(buf);
  int th = epaper.fontHeight();
  epaper.drawString(buf, CAL_HEADER_X + (CAL_HEADER_W - tw) / 2,
                    CAL_HEADER_Y + (CAL_HEADER_H - th + 10) / 2);

  // --- Weekday headers ---
  epaper.setFreeFont(&FreeSansBold9pt7b);
  int dayLabelY = CAL_HEADER_Y + CAL_HEADER_H + 10;
  for (int i = 0; i < CAL_COLS; i++) {
    int cx = CAL_GRID_X + i * CAL_CELL_W;
    int dw = epaper.textWidth(DAY_NAMES[i]);
    epaper.setTextColor((i == 5 || i == 6) ? TFT_RED : TFT_BLACK);
    epaper.drawString(DAY_NAMES[i], cx + (CAL_CELL_W - dw) / 2, dayLabelY);
  }

  // --- Day grid ---
  int days = daysInMonth(year, month);
  int firstDay = dayOfWeek(year, month, 1); // 0=Monday

  int x = CAL_GRID_X + firstDay * CAL_CELL_W;
  int y = CAL_GRID_Y;

  epaper.setFreeFont(&FreeSans12pt7b);
  int dayFontH = epaper.fontHeight();
  for (int d = 1; d <= days; d++) {
    sprintf(buf, "%d", d);
    int dw = epaper.textWidth(buf);
    int cx = x + (CAL_CELL_W - dw) / 2;
    int cy = y + (CAL_CELL_H - dayFontH + 5) / 2;

    if (d == today) {
      epaper.fillRect(x + 2, y, CAL_CELL_W - 4, CAL_CELL_H, TFT_BLACK);
      epaper.setTextColor(TFT_WHITE);
    } else {
      int wday = (firstDay + d - 1) % 7;
      epaper.setTextColor((wday == 5 || wday == 6) ? TFT_RED : TFT_BLACK);
    }
    epaper.drawString(buf, cx, cy);

    x += CAL_CELL_W;
    if ((firstDay + d) % 7 == 0) {
      y += CAL_CELL_H + 4;
      x = CAL_GRID_X;
    }
  }

  epaper.setTextColor(TFT_BLACK);
}

// ============================================================
// Google Calendar events
// ============================================================

struct CalendarEvent {
  bool valid;
  char start[6];
  char end[6];
  char title[64];
  char calendar[32];
};

static CalendarEvent calEvents[CAL_EVENTS_MAX] = {};

// Comparison function for sorting events by start time (HH:MM string)
static int cmpEvents(const void *a, const void *b) {
  return strcmp(((CalendarEvent *)a)->start, ((CalendarEvent *)b)->start);
}

// Fetch today's events from the Google Apps Script endpoint
bool fetchCalendarEvents() {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  char url[256];
  snprintf(url, sizeof(url), "%s?key=%s", GOOGLE_CALENDAR_URL,
           CALENDAR_API_KEY);
  http.begin(client, url);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.setTimeout(15000); // 15s timeout — calendar runs last, WiFi may be slow

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("Calendar API returned %d, retrying...\n", code);
    delay(1000);
    http.begin(client, url);
    http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
    http.setTimeout(15000);
    code = http.GET();
  }
  if (code != HTTP_CODE_OK) {
    http.end();
    Serial.printf("Calendar API retry returned %d\n", code);
    return false;
  }
  String payload = http.getString();
  http.end();

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, payload);
  if (err) {
    Serial.printf("Calendar JSON error: %s\n", err.c_str());
    return false;
  }

  JsonArray events = doc["events"];
  int count = 0;
  for (JsonObject ev : events) {
    if (count >= CAL_EVENTS_MAX)
      break;
    const char *s = ev["start"];
    const char *e = ev["end"];
    const char *t = ev["title"];
    const char *c = ev["calendar"];
    if (s)
      strncpy(calEvents[count].start, s, sizeof(calEvents[count].start) - 1);
    if (e)
      strncpy(calEvents[count].end, e, sizeof(calEvents[count].end) - 1);
    if (t)
      strncpy(calEvents[count].title, t, sizeof(calEvents[count].title) - 1);
    if (c)
      strncpy(calEvents[count].calendar, c,
              sizeof(calEvents[count].calendar) - 1);
    calEvents[count].valid = true;
    Serial.printf("Event[%d]: %s - %s  %s  [%s]\n", count,
                  calEvents[count].start, calEvents[count].end,
                  calEvents[count].title, calEvents[count].calendar);
    count++;
  }

  // Sort events by start time so earlier events appear first
  qsort(calEvents, count, sizeof(CalendarEvent), cmpEvents);

  return true;
}

// Draw calendar events below the monthly grid
void drawCalendarEvents(EPaper &epaper) {
  epaper.setFreeFont(&FreeSans12pt7b);
  epaper.setTextColor(TFT_BLACK);

  int fontH = epaper.fontHeight();
  int lineH = fontH + 6;
  int areaW = CAL_EVENTS_W;

  // Centered divider line above event list
  int divLen = 200;
  int divX = CAL_EVENTS_X + (CAL_EVENTS_W - divLen) / 2 - 15;
  int y = CAL_EVENTS_Y;
  epaper.drawLine(divX, y, divX + divLen, y, TFT_BLACK);
  y += 30;

  for (int i = 0; i < CAL_EVENTS_MAX; i++) {
    if (!calEvents[i].valid)
      continue;

    // Priority: red (!) > blue (birthday) > green (holiday) > black (default)
    uint16_t color = TFT_BLACK;
    if (strchr(calEvents[i].title, '!') != NULL) {
      color = TFT_RED;
    } else {
      // Convert title and calendar to lowercase for case-insensitive matching
      char titleLower[64];
      char calLower[32];
      strncpy(titleLower, calEvents[i].title, sizeof(titleLower) - 1);
      strncpy(calLower, calEvents[i].calendar, sizeof(calLower) - 1);
      for (char *p = titleLower; *p; p++)
        *p = tolower(*p);
      for (char *p = calLower; *p; p++)
        *p = tolower(*p);

      if (strstr(titleLower, "birthday") != NULL ||
          strstr(titleLower, "geburtstag") != NULL) {
        color = TFT_BLUE;
      } else if (strstr(calLower, "holiday") != NULL ||
                 strstr(calLower, "feiertag") != NULL) {
        color = TFT_GREEN;
      }
    }

    char line[96];
    snprintf(line, sizeof(line), "%s - %s  %s", calEvents[i].start,
             calEvents[i].end, calEvents[i].title);

    // Truncate with "..." if text is too wide for the area
    int tw = epaper.textWidth(line);
    if (tw > areaW) {
      int len = strlen(line);
      while (len > 3 && tw > areaW) {
        len--;
        char trunc[96];
        strncpy(trunc, line, len);
        trunc[len] = '\0';
        strcat(trunc, "...");
        tw = epaper.textWidth(trunc);
        if (tw <= areaW) {
          strcpy(line, trunc);
          break;
        }
      }
    }

    epaper.setTextColor(color);
    epaper.drawString(line, CAL_EVENTS_X, y);
    y += lineH;
  }

  epaper.setTextColor(TFT_BLACK);
}