#ifndef EE02_SETTINGS_PORTAL_ARDUINO_H
#define EE02_SETTINGS_PORTAL_ARDUINO_H
#include "settings_portal.h"
#include <WiFi.h>
#include <NetworkServer.h>
#include <esp_random.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <errno.h>
#if defined(ARDUINO_ARCH_ESP32)
#include <esp_arduino_version.h>
#if ESP_ARDUINO_VERSION_MAJOR != 3 || ESP_ARDUINO_VERSION_MINOR != 3 || ESP_ARDUINO_VERSION_PATCH != 12
#error "Settings portal requires the inspected Arduino-ESP32 3.3.12 APIs"
#endif
#if !defined(ARDUHAL_LOG_LEVEL) || ARDUHAL_LOG_LEVEL != 0
#error "Settings portal requires Core Debug Level None"
#endif
#endif

// Inspected Arduino-ESP32 3.3.12. Single IPv4-bound listener/backlog, one
// synchronous active client. No WebServer buffers, DNS interception or worker.
namespace SettingsPortal {
class ArduinoEndpoint {
    NetworkServer server_;NetworkClient client_;IPAddress address_;bool apOwned_=false;
public:
    explicit ArduinoEndpoint(IPAddress address):server_(address,80,1),address_(address) {}
    ArduinoEndpoint(const ArduinoEndpoint&)=delete;
    ArduinoEndpoint& operator=(const ArduinoEndpoint&)=delete;
    bool start(Mode mode) {
        if(address_==IPAddress() || address_==IPAddress(255,255,255,255)) return false;
        if(mode==Mode::FirstBootAp) {
            apOwned_=true;WiFi.persistent(false);
            if(!WiFi.mode(WIFI_AP) || WiFi.getMode()!=WIFI_AP ||
               !WiFi.softAPConfig(address_,address_,IPAddress(255,255,255,0)) ||
               !WiFi.softAP("EE02-Setup",nullptr,1,0,1) || WiFi.softAPIP()!=address_) return false;
        } else if(WiFi.getMode()!=WIFI_STA || WiFi.status()!=WL_CONNECTED || WiFi.localIP()!=address_) return false;
        server_.begin();return bool(server_);
    }
    bool accept() {
        if(client_.fd()>=0) return true;
        client_=server_.accept();
        if(client_.fd()<0) return false;
        client_.setConnectionTimeout(1000);return true; // milliseconds; no blocking Stream reads
    }
    int receive(uint8_t* p,size_t n) {
        const int available=client_.available();
        if(available>0) return client_.read(p,n<size_t(available)?n:size_t(available));
        return client_.connected()?0:-1;
    }
    int send(const uint8_t* p,size_t n) {
        // NetworkClient::write may restart internal retries after partial progress.
        // Use its public fd with one nonblocking lwIP send, allowing the outer
        // whole-request and session checks on EVERY bounded attempt.
        const int count=::send(client_.fd(),p,n,MSG_DONTWAIT);
        return count<0 && (errno==EAGAIN || errno==EWOULDBLOCK)?0:count>0?count:-1;
    }
    void idle() {delay(5);}
    void random16(uint8_t (&bytes)[16]) {esp_fill_random(bytes,sizeof(bytes));}
    bool closeClient() {client_.flush();client_.stop();return client_.fd()<0 && !client_.connected();}
    bool stop(Mode) {
        bool clean=closeClient();server_.end();clean=!bool(server_) && clean;
        if(apOwned_) {
            const bool stopped=WiFi.softAPdisconnect(true);
            const bool off=WiFi.mode(WIFI_OFF);
            clean=stopped && off && WiFi.getMode()==WIFI_OFF && clean;
        }
        // LAN caller retains its STA connection; no disconnect/mode/persistence mutation.
        return clean;
    }
};
inline uint64_t monotonic(void*) {const int64_t us=esp_timer_get_time();return us<0?UINT64_MAX:uint64_t(us)/1000;}
inline Report firstBoot(DeviceSettings::Service& settings,Work& work) {
    ArduinoEndpoint endpoint(IPAddress(192,168,4,1));
    return run(settings,work,Mode::FirstBootAp,"192.168.4.1",endpoint,monotonic);
}
// Explicit awake invocation only. The caller must ensure no active acquisition,
// scratch/framebuffer or panel operation, and owns restart/sleep decisions on return.
inline Report awakeLan(DeviceSettings::Service& settings,Work& work,IPAddress address) {
    char authority[16];snprintf(authority,sizeof(authority),"%u.%u.%u.%u",
        unsigned(address[0]),unsigned(address[1]),unsigned(address[2]),unsigned(address[3]));
    ArduinoEndpoint endpoint(address);return run(settings,work,Mode::AwakeLan,authority,endpoint,monotonic);
}
}
#endif
