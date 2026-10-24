#ifndef EE02_WEATHER_VALUES_H
#define EE02_WEATHER_VALUES_H

// Existing weather-only capacities and English date labels. Kept here so active
// acquisition need not import legacy location/time/sleep or graphics constants.
static const int FORECAST_COUNT = 5;
static const int DAILY_FCST_COUNT = 5;
static const char* const FULL_DAY_NAMES[] = {"Sunday", "Monday", "Tuesday", "Wednesday",
                                           "Thursday", "Friday", "Saturday"};

// Shared observation declarations, extracted unchanged from weather.h.
// Acquisition owns these values; renderers borrow them without a second store.
struct WeatherData {
  bool valid;
  char icon[4];
  char description[64];
  char fetchTime[32];
  float temp;
  int humidity;
  float rain;
  float windSpeed;
};

struct ForecastEntry {
  bool valid;
  char timeStr[6];
  char icon[4];
  float tempHigh;
  float tempLow;
};

struct DailyForecastEntry {
  bool valid;
  char dayName[10];
  char icon[4];
  char description[32];
  float tempHigh;
  float tempLow;
};

#endif
