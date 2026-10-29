#ifndef EE02_OBSERVATIONS_TEST_WIFI_H
#define EE02_OBSERVATIONS_TEST_WIFI_H
#include <Arduino.h>
#include <sys/time.h>
#include <vector>
#include <string>
#include <cassert>
enum { WL_CONNECTED=3, WIFI_OFF=0, WIFI_STA=1, WIFI_AP=2 };
#ifndef EE02_TEST_IP_ADDRESS
#define EE02_TEST_IP_ADDRESS
struct IPAddress {
    unsigned value=0;uint8_t octets[4]={};
    IPAddress()=default;IPAddress(uint8_t a,uint8_t b,uint8_t c,uint8_t d):octets{a,b,c,d}{}
    uint8_t operator[](size_t i)const{return octets[i];}
    bool operator==(const IPAddress& other)const{return !memcmp(octets,other.octets,4);}
    bool operator!=(const IPAddress& other)const{return !(*this==other);}
};
#endif
namespace ObservationHost {
struct BootState {
    std::string ssid,password,ntp;
    unsigned apStarts=0,apStops=0;bool apStartOk=true,apStopOk=true,apActive=false;
    IPAddress apAddress;
    bool wifiReady=true,sntpEnabled=false,notify=true,staleTimeValid=true;
    unsigned wifiBegins=0,disconnects=0,off=0,starts=0,stops=0,registrations=0,unregistrations=0;
    bool shutdownOk=true; int wifiMode=WIFI_STA;
    unsigned elapsedMs=0,notifyAfterMs=1000;
    int mode=-1;
    void (*callback)(struct timeval*)=nullptr;
    std::vector<int> order;
};
inline BootState& boot(){static BootState state;return state;}
}
class WiFiClass {
public:
    void begin(const char* ssid,const char* password){auto& b=ObservationHost::boot();++b.wifiBegins;b.ssid=ssid;b.password=password;}
    int status(){return ObservationHost::boot().wifiReady?WL_CONNECTED:0;}
    void persistent(bool enabled){if(enabled)std::abort();}
    void disconnect(bool,bool=false){++ObservationHost::boot().disconnects;}
    bool mode(int mode){auto& b=ObservationHost::boot();if(mode==WIFI_OFF){++b.off;if(!b.shutdownOk)return false;}
        b.wifiMode=mode;return true;}
    bool softAPConfig(IPAddress address,IPAddress gateway,IPAddress mask){assert(address==gateway && mask==IPAddress(255,255,255,0));ObservationHost::boot().apAddress=address;return true;}
    bool softAP(const char* ssid,const char* password,int channel,int hidden,int count){auto& b=ObservationHost::boot();assert(!strcmp(ssid,"EE02-Setup") && !password && channel==1 && !hidden && count==1 && b.wifiMode==WIFI_AP);++b.apStarts;b.apActive=b.apStartOk;return b.apActive;}
    bool softAPdisconnect(bool off){auto& b=ObservationHost::boot();assert(off);++b.apStops;if(b.apStopOk)b.apActive=false;return b.apStopOk;}
    IPAddress softAPIP(){return ObservationHost::boot().apAddress;}
    IPAddress localIP(){return IPAddress(192,0,2,1);}
    int getMode(){return ObservationHost::boot().wifiMode;}
};
static WiFiClass WiFi;
#endif
