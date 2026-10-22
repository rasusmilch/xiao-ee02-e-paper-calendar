#ifndef EE02_OBSERVATIONS_TEST_WIFI_H
#define EE02_OBSERVATIONS_TEST_WIFI_H
#include <Arduino.h>
#include <sys/time.h>
#include <vector>
enum { WL_CONNECTED=3, WIFI_OFF=0, WIFI_STA=1 };
namespace ObservationHost {
struct BootState {
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
    void begin(const char*,const char*){++ObservationHost::boot().wifiBegins;}
    int status(){return ObservationHost::boot().wifiReady?WL_CONNECTED:0;}
    void persistent(bool enabled){if(enabled)std::abort();}
    void disconnect(bool,bool=false){++ObservationHost::boot().disconnects;}
    bool mode(int mode){auto& b=ObservationHost::boot();if(mode==WIFI_OFF){++b.off;if(!b.shutdownOk)return false;}
        b.wifiMode=mode;return true;}
    int getMode(){return ObservationHost::boot().wifiMode;}
};
static WiFiClass WiFi;
#endif
