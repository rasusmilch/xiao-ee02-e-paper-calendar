#ifndef EE02_SETTINGS_FIXTURE_H
#define EE02_SETTINGS_FIXTURE_H
#include "../application/device_settings_codec.h"
#include <cassert>
#include <cstring>
namespace SettingsFixture {
template<size_t N> void set(DeviceSettings::Text<N>& t,const char* s) { assert(DeviceSettings::assign(t,s,strlen(s))); }
inline DeviceSettings::Values complete() {
 DeviceSettings::Values v={};set(v.ssid,"synthetic-network");set(v.password,"synthetic-password");
 set(v.timezone,"CET-1CEST,M3.5.0,M10.5.0/3");set(v.ntp,"ntp.example.invalid");
 v.weatherEnabled=true;set(v.location,"Example City,XX");set(v.apiKey,"synthetic-key");v.refreshSeconds=42;v.sourceCount=3;
 v.sources[0].enabled=v.sources[2].enabled=true;
 set(v.sources[0].label,"Personal");set(v.sources[0].url,"https://one.example.invalid/one.ics");
 set(v.sources[1].label,"Disabled");
 set(v.sources[2].label,"Shared");set(v.sources[2].url,"https://two.example.invalid/two.ics");return v;
}
}
#endif
