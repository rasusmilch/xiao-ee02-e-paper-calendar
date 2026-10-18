#ifndef EE02_TEST_SECURE_CLIENT_H
#define EE02_TEST_SECURE_CLIENT_H
// Deliberately only the inspected 3.3.12 API subset. This does not implement TLS.
#include <Arduino.h>
#include <cassert>
namespace SecureTrace {
struct State {
    size_t dns=0, connects=0, stops=0, caSet=0, sequence=0, caOrder=0, connectOrder=0;
    const char* ca=nullptr; const char* hostname=nullptr;
    uint32_t socketMs=0; unsigned long handshakeSeconds=0; uint16_t port=0;
    bool dnsOk=true, connectOk=true, connected=true, writeOk=true;
    int available=0, readResult=1;
};
extern State state;
}
struct IPAddress { uint32_t value=0; };
struct TestNetwork {
    int hostByName(const char* host,IPAddress& ip) {
        assert(host && *host); ++SecureTrace::state.dns; ip.value=1234;
        return SecureTrace::state.dnsOk ? 1 : 0;
    }
};
extern TestNetwork Network;
class NetworkClientSecure {
public:
    void setCACert(const char* ca) {
        auto& s=SecureTrace::state; s.ca=ca;
        if(ca) { ++s.caSet; s.caOrder=++s.sequence; }
    }
    void setConnectionTimeout(uint32_t ms) { SecureTrace::state.socketMs=ms; }
    void setHandshakeTimeout(unsigned long seconds) { SecureTrace::state.handshakeSeconds=seconds; }
    // No hostname-only overload: accidentally selecting it fails host compilation.
    int connect(IPAddress ip,uint16_t port,const char* host,const char* ca,const char* cert,const char* key) {
        auto& s=SecureTrace::state;
        assert(ip.value==1234 && host && ca==s.ca && !cert && !key);
        ++s.connects; s.connectOrder=++s.sequence; s.hostname=host; s.port=port;
        return s.connectOk ? 1 : 0;
    }
    size_t write(const uint8_t*,size_t n) { return SecureTrace::state.writeOk?n:0; }
    int available() { return SecureTrace::state.available; }
    int read(uint8_t* p,size_t n) {
        const int got=SecureTrace::state.readResult;
        if(got>0) { assert(size_t(got)<=n); std::memset(p,0x55,size_t(got)); }
        return got;
    }
    uint8_t connected() { return SecureTrace::state.connected; }
    void stop() { ++SecureTrace::state.stops; SecureTrace::state.hostname=nullptr; }
};
#endif
