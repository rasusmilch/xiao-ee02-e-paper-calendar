#ifndef EE02_DEVICE_CONFIG_H
#define EE02_DEVICE_CONFIG_H
// Copy to device_config.h at the repository root (Git ignored). Supply your own
// values locally, then explicitly opt in. Never share that file or serial dumps
// of its contents. This template contains no usable credentials or CA trust.
namespace DeviceConfig {
static const bool configured=false;
static const char wifiSsid[]="";
static const char wifiPassword[]="";
static const char posixTimezone[]=""; // Supply an explicit valid POSIX display timezone.
static const char ntpServer[]="";
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
    {false,{"Example personal",16},{reinterpret_cast<const uint8_t*>(firstUrl),sizeof(firstUrl)-1}},
    {false,{"Example shared",14},{reinterpret_cast<const uint8_t*>(secondUrl),sizeof(secondUrl)-1}},
    {false,{"Example disabled",16},{reinterpret_cast<const uint8_t*>(disabledUrl),sizeof(disabledUrl)-1}}
};
// Explicit positive seconds required. Zero intentionally refuses acquisition/sleep.
// This is not a product default or a daily NTP/RTC policy.
static const uint64_t refreshSeconds=0;
// Provisional DEVELOPMENT evaluation bounds, not approved operating capacities.
static const size_t maxConfiguredSources=8;
static const size_t workspaceBytes=2097152;
static const size_t recordCapacity=128;
static const size_t textCapacity=32768;
static const uint64_t wifiWaitMs=30000,ntpWaitMs=30000;
// Existing per-feed evaluation bounds, not selected operating capacities.
static const IcsHttpsSource::Limits calendarLimits={2048,253,2048,4096,512,{10000,15,10000},
    {1048576,983040,16384,64,2048,512,2048,32,2048,8192,8192,0,15000},
    {8192,64,64,128,16,256,8192,16384,512,4}, {512,4096,524288,512,65536},
    {{512,4,256,128,256,512,{512,512,8192},{512,512,8192,256,8192}},8192,65536}};
}
#endif
