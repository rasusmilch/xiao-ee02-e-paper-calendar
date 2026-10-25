#ifndef EE02_DEVICE_CONFIG_H
#define EE02_DEVICE_CONFIG_H
// Build-only provisional inputs. Copy to ignored device_config.h and deliberately
// enable this build. User settings load ONLY from NVS; no import/fallback occurs.
// No usable CA trust is supplied. Certificate rotation remains Open.
namespace DeviceConfig {
static const bool configured=false;
static const char caPem[]="";
static const char weatherCaPem[]="";
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
