// Load the existing graphics/model host facade before selecting ESP32 adapter guards,
// matching the actual-root fixture; this is not a target GFX compilation.
#include "../application/settings_portal.h"
#define ARDUINO_ARCH_ESP32 1
#define ARDUHAL_LOG_LEVEL 0
#include "../application/settings_portal_arduino.h"
#include <cassert>
#include <cstdio>
namespace ObservationHost {Action actions[16];size_t actionCount=0;int adc=0;SerialSink serial;}
int64_t testEspMicroseconds=0;
void reset(){ObservationHost::boot()={};PortalHost::state()={};testEspMicroseconds=1000;}
int main(){
 using namespace SettingsPortal;
 reset();ArduinoEndpoint ap(IPAddress(192,168,4,1));assert(ap.start(Mode::FirstBootAp));
 assert(ObservationHost::boot().apStarts==1 && ObservationHost::boot().wifiBegins==0 && WiFi.getMode()==WIFI_AP);
 assert(ObservationHost::boot().apAddress==IPAddress(192,168,4,1));uint8_t first[16],second[16];ap.random16(first);ap.random16(second);assert(memcmp(first,second,16));
 PortalHost::state().exchanges.emplace_back("hello");assert(ap.accept());uint8_t bytes[8];assert(ap.receive(bytes,8)==5 && !memcmp(bytes,"hello",5));
 assert(ap.send(bytes,5)==5 && PortalHost::state().exchanges[0].response=="hello");assert(ap.closeClient());
 assert(ap.stop(Mode::FirstBootAp) && !PortalHost::state().listening && WiFi.getMode()==WIFI_OFF && !ObservationHost::boot().apActive);
 reset();ArduinoEndpoint lan(IPAddress(192,0,2,1));assert(lan.start(Mode::AwakeLan));assert(lan.stop(Mode::AwakeLan));
 assert(!ObservationHost::boot().apStarts && !ObservationHost::boot().apStops && !ObservationHost::boot().off && !ObservationHost::boot().disconnects && WiFi.getMode()==WIFI_STA);
 reset();ArduinoEndpoint wildcard(IPAddress{});assert(!wildcard.start(Mode::AwakeLan) && wildcard.stop(Mode::AwakeLan));assert(!ObservationHost::boot().off);
 reset();PortalHost::state().failStart=true;ArduinoEndpoint failed(IPAddress(192,168,4,1));assert(!failed.start(Mode::FirstBootAp) && failed.stop(Mode::FirstBootAp));
 reset();PortalHost::state().failStop=true;ArduinoEndpoint server(IPAddress(192,168,4,1));assert(server.start(Mode::FirstBootAp));assert(!server.stop(Mode::FirstBootAp));
 reset();ObservationHost::boot().apStopOk=false;ArduinoEndpoint badAp(IPAddress(192,168,4,1));assert(badAp.start(Mode::FirstBootAp));assert(!badAp.stop(Mode::FirstBootAp));
 reset();ObservationHost::boot().shutdownOk=false;ArduinoEndpoint badOff(IPAddress(192,168,4,1));assert(badOff.start(Mode::FirstBootAp));assert(!badOff.stop(Mode::FirstBootAp));
 assert(monotonic(nullptr)==1);testEspMicroseconds=-1;assert(monotonic(nullptr)==UINT64_MAX);
 std::puts("Portal Arduino seam: open AP/address/bind, byte I/O, nonce call, shutdown failures, LAN station ownership and clock stand-ins passed (not hardware proof)");
}
