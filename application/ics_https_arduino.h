#ifndef EE02_ICS_HTTPS_ARDUINO_H
#define EE02_ICS_HTTPS_ARDUINO_H
#include "ics_https_source.h"
#include <NetworkClientSecure.h>
#include <esp_arduino_version.h>
#include <esp_timer.h>

#if !defined(ARDUINO_ARCH_ESP32) || ESP_ARDUINO_VERSION_MAJOR != 3 || ESP_ARDUINO_VERSION_MINOR != 3 || ESP_ARDUINO_VERSION_PATCH != 12
#error "ICS secure adapter requires the inspected Arduino-ESP32 3.3.12 API"
#endif
// NetworkManager::hostByName logs a failed hostname at error level. Hostnames
// can themselves be sensitive. No global logging changes or dependency patch.
#if !defined(ARDUHAL_LOG_LEVEL) || ARDUHAL_LOG_LEVEL != 0
#error "ICS secure adapter requires Arduino Core Debug Level None to protect source identities"
#endif

namespace IcsHttpsArduino {
// One exclusive client, no credential store. Synchronous DNS may block for an
// implementation-dependent duration. Never reject late DNS/TLS success. Core
// 3.3.12 uses the connect timeout for socket_timeout and socket SO_*TIMEO.
class Adapter {
public:
    Adapter() = default;
    Adapter(const Adapter&) = delete;
    Adapter& operator=(const Adapter&) = delete;
    ~Adapter() { close(this); }
    IcsHttpsSource::Transport transport() { return {this,resolve,connect,write,read,idle,close}; }
    static uint64_t monotonicMs(void*) {
        const int64_t us=esp_timer_get_time();
        // Negative platform time is an invalid observation, never folded into
        // a wrapping millis(); the core rejects this reserved sentinel.
        return us<0 ? UINT64_MAX : uint64_t(us)/1000;
    }
private:
    NetworkClientSecure client;
    IPAddress address;
    bool resolved=false;
    static bool resolve(void* context,const char* host) {
        auto& a=*static_cast<Adapter*>(context);
        a.resolved=false;
        if(!host || !*host) return false;
        a.resolved=Network.hostByName(host,a.address)==1;
        return a.resolved;
    }
    static bool connect(void* context,const char* host,uint16_t port,const char* ca,bool trustedTime,const IcsHttpsSource::Timeouts& timeouts) {
        auto& a=*static_cast<Adapter*>(context);
        if(!a.resolved || !host || !*host || !port || !ca || !*ca || !trustedTime ||
            !timeouts.socketMs || timeouts.socketMs>INT32_MAX || !timeouts.handshakeSeconds || timeouts.handshakeSeconds>UINT32_MAX/1000) {
            close(context); return false;
        }
        a.client.setCACert(ca); // Enables VERIFY_REQUIRED, never insecure mode.
        a.client.setConnectionTimeout(timeouts.socketMs); // milliseconds
        a.client.setHandshakeTimeout(timeouts.handshakeSeconds); // seconds -> ms internally
        // This exact overload performs no DNS. host is retained for mbedTLS
        // hostname verification/SNI; CA bytes remain caller-owned until close.
        if(!a.client.connect(a.address,port,host,ca,nullptr,nullptr)) { close(context); return false; }
        return true;
    }
    static IcsHttpsSource::Io write(void* context,const uint8_t* bytes,size_t n) {
        auto& a=*static_cast<Adapter*>(context);
        const size_t written=a.client.write(bytes,n);
        return {written?IcsHttpsSource::IoStatus::Data:IcsHttpsSource::IoStatus::Error,written};
    }
    static IcsHttpsSource::Io read(void* context,uint8_t* bytes,size_t capacity) {
        auto& a=*static_cast<Adapter*>(context);
        const int available=a.client.available();
        if(available<0) return {IcsHttpsSource::IoStatus::Error,0};
        if(available>0) {
            const size_t n=capacity<size_t(available)?capacity:size_t(available);
            const int received=a.client.read(bytes,n);
            if(received>0) return {IcsHttpsSource::IoStatus::Data,size_t(received)};
            return {IcsHttpsSource::IoStatus::Error,0};
        }
        // The core can collapse EOF and a TLS available() failure into closed.
        // Do not invent a certificate diagnostic; incomplete framing still fails.
        return {a.client.connected()?IcsHttpsSource::IoStatus::WouldBlock:IcsHttpsSource::IoStatus::Closed,0};
    }
    static void idle(void*) { delay(1); }
    static void close(void* context) {
        auto& a=*static_cast<Adapter*>(context);
        a.client.stop(); a.client.setCACert(nullptr); a.resolved=false;
    }
};
}
#endif
