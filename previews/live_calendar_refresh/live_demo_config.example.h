#ifndef EE02_LIVE_DEMO_CONFIG_H
#define EE02_LIVE_DEMO_CONFIG_H
// Copy to live_demo_config.h IN THIS DIRECTORY (Git ignored). Supply your own
// values locally, then explicitly opt in. Never share that file or serial dumps
// of its contents. This template contains no usable credentials or CA trust.
namespace LiveDemoConfig {
static const bool configured=false;
static const char wifiSsid[]="EXAMPLE_WIFI";
static const char wifiPassword[]="EXAMPLE_PASSWORD";
static const char posixTimezone[]="UTC0"; // Example only: choose your display TZ.
static const char ntpServer[]="ntp.example.invalid";
static const char caPem[]=""; // Caller-provided trusted CA PEM, including terminal NUL.
// Independent development weather settings. Empty fields are fine when disabled.
static const bool weatherEnabled=false;
static const char weatherLocation[]="Example City,XX";
static const char weatherApiKey[]="";
static const char weatherCaPem[]=""; // May use different CA material from calendars.
static const char firstUrl[]="https://example.invalid/personal.ics";
static const char secondUrl[]="https://example.invalid/shared.ics";
static const char disabledUrl[]="https://example.invalid/disabled.ics";
static const FiveDayCalendarRefresh::Source sources[]={
    {true,{"Example personal",16},{reinterpret_cast<const uint8_t*>(firstUrl),sizeof(firstUrl)-1}},
    {true,{"Example shared",14},{reinterpret_cast<const uint8_t*>(secondUrl),sizeof(secondUrl)-1}},
    {false,{"Example disabled",16},{reinterpret_cast<const uint8_t*>(disabledUrl),sizeof(disabledUrl)-1}}
};
// Provisional DEVELOPMENT evaluation bounds, not approved operating capacities.
static const size_t maxConfiguredSources=8;
static const size_t workspaceBytes=2097152;
static const size_t recordCapacity=128;
static const size_t textCapacity=32768;
static const uint64_t wifiWaitMs=30000,ntpWaitMs=30000;
}
#endif
