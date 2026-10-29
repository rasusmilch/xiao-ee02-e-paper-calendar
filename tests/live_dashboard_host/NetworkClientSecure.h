#ifndef EE02_LIVE_DASHBOARD_SECURE_HOST_H
#define EE02_LIVE_DASHBOARD_SECURE_HOST_H
#include <Arduino.h>
#include <cassert>
#include <vector>
#include <algorithm>
namespace LiveSocket {
struct Feed{std::string wire,request;bool dns,tls;};
struct State{
    std::vector<Feed> feeds;size_t resolves=0,connects=0,pos=0,index=0,stops=0;
    bool live=false;const char* ca=nullptr;std::vector<const char*> caHistory;
    void (*checkPhase)()=nullptr;
};
inline State& state(){static State value;return value;}
}
#ifndef EE02_TEST_IP_ADDRESS
#define EE02_TEST_IP_ADDRESS
struct IPAddress{unsigned value=0;};
#endif
struct TestNetwork{
    int hostByName(const char* host,IPAddress& ip){auto& s=LiveSocket::state();
        assert(host && *host && !s.live);if(s.checkPhase)s.checkPhase();
        s.index=s.resolves++;s.pos=0;assert(s.index<s.feeds.size());ip.value=1234;return s.feeds[s.index].dns?1:0;}
};
extern TestNetwork Network;
class NetworkClientSecure{
public:
    void setCACert(const char* ca){auto& s=LiveSocket::state();s.ca=ca;if(ca)s.caHistory.push_back(ca);}
    void setConnectionTimeout(uint32_t n){assert(n);}
    void setHandshakeTimeout(unsigned long n){assert(n);}
    int connect(IPAddress ip,uint16_t port,const char* host,const char* ca,const char* cert,const char* key){
        auto& s=LiveSocket::state();if(s.checkPhase)s.checkPhase();assert(ip.value==1234 && port && host && ca==s.ca && !cert && !key);
        ++s.connects;s.live=s.feeds[s.index].tls;return s.live?1:0;}
    size_t write(const uint8_t* data,size_t n){auto& s=LiveSocket::state();if(s.checkPhase)s.checkPhase();assert(s.live);
        n=std::min<size_t>(n,3);s.feeds[s.index].request.append(reinterpret_cast<const char*>(data),n);return n;}
    int available(){auto& s=LiveSocket::state();if(s.checkPhase)s.checkPhase();return int(std::min<size_t>(7,s.feeds[s.index].wire.size()-s.pos));}
    int read(uint8_t* data,size_t n){auto& s=LiveSocket::state();if(s.checkPhase)s.checkPhase();assert(s.live);
        n=std::min(n,s.feeds[s.index].wire.size()-s.pos);memcpy(data,s.feeds[s.index].wire.data()+s.pos,n);s.pos+=n;return int(n);}
    uint8_t connected(){auto& s=LiveSocket::state();return s.live && s.pos<s.feeds[s.index].wire.size();}
    void stop(){auto& s=LiveSocket::state();s.live=false;++s.stops;}
};
#endif
