#ifndef EE02_PORTAL_SERVER_H
#define EE02_PORTAL_SERVER_H
#include <Arduino.h>
#include <WiFi.h>
#include <cassert>
#include <algorithm>
#include <vector>
#include <string>
namespace PortalHost {
struct Exchange {std::string request,response;size_t position=0;bool closed=false;explicit Exchange(std::string q):request(q){}};
struct State {
 std::vector<Exchange> exchanges;size_t index=0,starts=0,stops=0;bool listening=false,failStart=false,failStop=false,failClientStop=false;
 void (*advance)(State&)=nullptr;void (*prepare)(State&)=nullptr;bool prepared=false;int readSlice=128;
};
inline State& state(){static State s;return s;}
}
class NetworkClient {
 bool active_=false;
public:
 NetworkClient()=default;explicit NetworkClient(bool active):active_(active){}
 int fd()const{return active_?55:-1;}
 void setConnectionTimeout(uint32_t n){assert(n==1000);}
 int available(){auto& s=PortalHost::state();return active_?int(s.exchanges[s.index].request.size()-s.exchanges[s.index].position):0;}
 int read(uint8_t* p,size_t n){auto& s=PortalHost::state();auto& e=s.exchanges[s.index];n=std::min(n,size_t(available()));n=std::min(n,size_t(s.readSlice));memcpy(p,e.request.data()+e.position,n);e.position+=n;return int(n);}
 uint8_t connected(){return active_;}
 void flush(){}
 void stop(){auto& s=PortalHost::state();if(active_ && !s.failClientStop){s.exchanges[s.index].closed=true;active_=false;++s.index;}}
};
class NetworkServer {
 IPAddress address_;bool live_=false;
public:
 NetworkServer(const IPAddress& address,uint16_t port,uint8_t count):address_(address){assert(port==80 && count==1);}
 void begin(){auto& s=PortalHost::state();++s.starts;live_=!s.failStart;s.listening=live_;assert(address_==IPAddress(192,168,4,1) || address_==IPAddress(192,0,2,1));}
 explicit operator bool(){return live_;}
 NetworkClient accept(){auto& s=PortalHost::state();if(s.advance)s.advance(s);if(s.prepare && !s.prepared){s.prepared=true;s.prepare(s);}return NetworkClient(live_ && s.index<s.exchanges.size());}
 void end(){auto& s=PortalHost::state();++s.stops;if(!s.failStop){live_=false;s.listening=false;}}
};
#endif
