// Syntax-only graphics-independence check. No graphics include paths or private
// credentials; parser/network declarations are supplied by narrow host facades.
#include "../application/weather_acquisition.h"
#include "../application/battery_measurement.h"
#include <type_traits>
static_assert(std::is_same<decltype(&fetchWeatherData),bool(*)(const WeatherHttps::Trust&,const WeatherHttps::Request&)>::value,"weather entry point");
static_assert(std::is_same<decltype(&fetchForecastData),bool(*)(const WeatherHttps::Trust&,const WeatherHttps::Request&)>::value,"short forecast entry point");
static_assert(std::is_same<decltype(&fetchDailyForecastData),bool(*)(const WeatherHttps::Trust&,const WeatherHttps::Request&)>::value,"daily entry point");
static_assert(std::is_same<decltype(&readBatteryMillivolts),int(*)()>::value,"measurement entry point");
static_assert(std::is_same<decltype(&voltageToPercent),int(*)(int)>::value,"conversion entry point");
static_assert(sizeof(forecastData)/sizeof(ForecastEntry)==FORECAST_COUNT,"same forecast capacity");
static_assert(sizeof(dailyForecastData)/sizeof(DailyForecastEntry)==DAILY_FCST_COUNT,"same daily capacity");
