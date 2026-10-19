#ifndef EE02_WEATHER_VALUES_H
#define EE02_WEATHER_VALUES_H

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
