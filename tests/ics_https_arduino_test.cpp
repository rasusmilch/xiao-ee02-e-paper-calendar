// Load the host graphics facade before selecting the adapter platform guard.
#include "../application/ics_https_source.h"
#define ARDUINO_ARCH_ESP32 1
#define ARDUHAL_LOG_LEVEL 0
#include "../application/ics_https_arduino.h"
#include <cstdio>
SecureTrace::State SecureTrace::state;
TestNetwork Network;
int64_t testEspMicroseconds=0;
namespace {
using namespace IcsHttpsSource;
const char host[]="calendar.example.invalid",ca[]="synthetic-test-CA";
void connected() {
    SecureTrace::state={};
    IcsHttpsArduino::Adapter adapter;
    const auto t=adapter.transport();
    assert(t.resolve(t.context,host));
    assert(t.connect(t.context,host,8443,ca,true,{1234,7,100}));
    const auto& s=SecureTrace::state;
    assert(s.dns==1 && s.connects==1 && s.hostname==host && s.port==8443);
    assert(s.ca==ca && s.caSet==1 && s.caOrder<s.connectOrder);
    assert(s.socketMs==1234 && s.handshakeSeconds==7);
    uint8_t bytes[4]={};
    assert(t.write(t.context,bytes,4).bytes==4);
    SecureTrace::state.writeOk=false;
    assert(t.write(t.context,bytes,4).status==IoStatus::Error);
    assert(t.read(t.context,bytes,4).status==IoStatus::WouldBlock);
    SecureTrace::state.available=3; SecureTrace::state.readResult=3;
    const auto r=t.read(t.context,bytes,4);
    assert(r.status==IoStatus::Data && r.bytes==3 && bytes[2]==0x55);
    SecureTrace::state.readResult=-1;
    assert(t.read(t.context,bytes,4).status==IoStatus::Error);
    SecureTrace::state.available=-1;
    assert(t.read(t.context,bytes,4).status==IoStatus::Error);
    SecureTrace::state.available=0; SecureTrace::state.connected=false;
    assert(t.read(t.context,bytes,4).status==IoStatus::Closed);
    t.idle(t.context); t.close(t.context);
    assert(s.stops==1 && !s.ca && !s.hostname);
    // Close revokes the resolved state; reconnect without a new DNS call fails.
    assert(!t.connect(t.context,host,443,ca,true,{1234,7,100}));
    assert(s.connects==1 && s.dns==1);
}
void failures() {
    for(int kind=0;kind<9;++kind) {
        SecureTrace::state={}; IcsHttpsArduino::Adapter adapter; const auto t=adapter.transport();
        if(kind==0) { SecureTrace::state.dnsOk=false; assert(!t.resolve(t.context,host)); }
        else assert(t.resolve(t.context,host));
        Timeouts timeouts={1000,3,50};
        if(kind==1)timeouts.socketMs=0;
        if(kind==2)timeouts.socketMs=UINT32_MAX;
        if(kind==3)timeouts.handshakeSeconds=0;
        if(kind==4)timeouts.handshakeSeconds=UINT32_MAX/1000+1;
        if(kind==8)SecureTrace::state.connectOk=false;
        assert(!t.connect(t.context,host,kind==7?0:443,kind==6?nullptr:ca,kind!=5,timeouts));
        assert(SecureTrace::state.stops && !SecureTrace::state.ca);
        assert(SecureTrace::state.connects==(kind==8?1U:0U));
    }
}
}
int main() {
    connected(); failures();
    testEspMicroseconds=(int64_t(UINT32_MAX)+5000)*1000;
    assert(IcsHttpsArduino::Adapter::monotonicMs(nullptr)==uint64_t(UINT32_MAX)+5000);
    testEspMicroseconds=-1;
    assert(IcsHttpsArduino::Adapter::monotonicMs(nullptr)==UINT64_MAX);
    std::puts("PASS: pinned secure-adapter call ordering, IP+hostname, trust, timeout units, cleanup; stand-ins only");
}
