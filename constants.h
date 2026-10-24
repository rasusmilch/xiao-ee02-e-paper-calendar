#pragma once
#include "application/weather_values.h"

// ============================================================
// Central constants shared across all modules
// ============================================================

// Weather location (city, country code) for OpenWeather API
const char *WX_LOCATION = "Lohr am Main,DE";

// Timezone for NTP synchronization (CET/CEST for Germany)
const char *TIMEZONE = "CET-1CEST,M3.5.0,M10.5.0/3";
const char *NTP_SERVER = "pool.ntp.org";

// Month names (English)
const char *MONTH_NAMES[] = {"January",   "February", "March",    "April",
                             "May",       "June",     "July",     "August",
                             "September", "October",  "November", "December"};

// Weekday abbreviations starting with Monday (EU calendar convention)
const char *DAY_NAMES[] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};

// Weather condition name mapping (OWM icon code prefix -> human readable)
struct WeatherCondition {
  const char *iconPrefix;
  const char *label;
};

const WeatherCondition WEATHER_CONDITIONS[] = {
    {"01", "Clear"},        {"02", "Partly Cloudy"}, {"03", "Cloudy"},
    {"04", "Overcast"},     {"09", "Showers"},       {"10", "Rain"},
    {"11", "Thunderstorm"}, {"13", "Snow"},          {"50", "Mist"},
};

const int NUM_CONDITIONS =
    sizeof(WEATHER_CONDITIONS) / sizeof(WEATHER_CONDITIONS[0]);

// ============================================================
// Display layout constants
// ============================================================

// Existing panel dividers; preserve retained geometry during the redesign.
const int LAYOUT_MARGIN = 10;
const int PANEL_DIVIDER_X = 1044;
const int LEFT_PANEL_DIVIDER_Y = 1044;

// Calendar section (lower right: x=1054..1580, y=600..1180)
const int CAL_HEADER_X = 1064;
const int CAL_HEADER_Y = 600;
const int CAL_HEADER_W = 506;
const int CAL_HEADER_H = 60;
const int CAL_GRID_X = 1063;
const int CAL_GRID_Y = 695;
const int CAL_CELL_W = 72;
const int CAL_CELL_H = 45;
const int CAL_COLS = 7;

// Icon scale defines (matching the example's Large=20 / Small=8 convention)
// WX_ICON_SIZE=180 with scale=20 gives ~160px wide icons
#define ICON_LARGE 20
#define ICON_SMALL 8
#define ICON_DAILY 6

// Forecast

// Daily forecast section (lower left)
// Area: x=10..1034, y=1044..1190 (146px tall)
const int DAILY_FCST_X = 10;
const int DAILY_FCST_Y = 1054;
const int DAILY_FCST_H = 130;
const int DAILY_FCST_AREA_W = 1024;
const int DAILY_FCST_CELL_W = DAILY_FCST_AREA_W / DAILY_FCST_COUNT; // ~204

// Weather section (upper right: x=1054..1580, y=10..600)
const int WX_AREA_X = 1054;
const int WX_AREA_W = 526;
const int WX_CENTER_X = WX_AREA_X + WX_AREA_W / 2; // 1317
const int WX_DAY_Y = 50;
const int WX_ICON_Y = 150;
const int WX_ICON_SIZE = 180;
const int WX_DESC_Y = 305;
const int WX_DATA_Y = 360;

// 3-hourly forecast section (bottom of weather panel)
const int FCST_Y = 430;
const int FCST_CELL_W = 95;
const int FCST_ICON_Y = FCST_Y + 31;

// Calendar events section (below monthly grid, lower right)
const int CAL_EVENTS_X = 1063;
const int CAL_EVENTS_W = 504;
const int CAL_EVENTS_Y = 970;
const int CAL_EVENTS_MAX = 6;

// Battery charge indicator position (lower right corner of 1600x1200 display)
const int BAT_ICON_X = 1532; // battery icon x position
const int BAT_ICON_Y = 1160; // battery icon y position
const int BAT_ICON_W = 50;   // battery icon body width
const int BAT_ICON_H = 22;   // battery icon body height
const int BAT_TEXT_GAP = 8;  // gap between icon and percentage text (px)

// Deep sleep duration in hours (0 = disabled, useful for debugging)
const int DEEP_SLEEP_HOURS = 3;
