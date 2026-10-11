#pragma once

struct DataAcquisitionResults {
  bool weather;
  bool forecast;
  bool dailyForecast;
  bool calendar;
};

// Call each synchronous fetch once, in order, on an established connection.
// Callbacks must be non-null. Report outcomes before disconnecting; no callback
// runs when Wi-Fi is unavailable. The return value is current-weather success,
// preserving updateWeather()'s contract rather than aggregating all outcomes.
inline bool acquireDataSources(
    bool wifiConnected, bool (*fetchWeather)(), bool (*fetchForecast)(),
    bool (*fetchDailyForecast)(), bool (*fetchCalendar)(),
    void (*report)(const DataAcquisitionResults &), void (*disconnect)()) {
  if (!wifiConnected) {
    return false;
  }

  DataAcquisitionResults results;
  results.weather = fetchWeather();
  results.forecast = fetchForecast();
  results.dailyForecast = fetchDailyForecast();
  results.calendar = fetchCalendar();
  report(results);
  disconnect();
  return results.weather;
}
