#ifndef EE02_OBSERVATIONS_TEST_TLS_H
#define EE02_OBSERVATIONS_TEST_TLS_H
#include <Arduino.h>
#include <vector>
namespace ObservationHost {
// Scripted call-order fixture only. No DNS, sockets or cryptographic validation.
struct HttpsState {
    unsigned caCalls=0,begins=0,resolutions=0,connections=0,requests=0,ends=0,stops=0,transfers=0;
    bool beginOk=true,connectOk=true;
    int status=503,transferResult=0;
    const char* ca=nullptr;
    std::string request,host,payload;
    unsigned port=0;
    std::vector<int> order;
};
inline HttpsState& https(){static HttpsState value;return value;}
}
class WiFiClientSecure {
public:
    void setInsecure()=delete;
    void setCACert(const char* ca){auto& s=ObservationHost::https();s.ca=ca;++s.caCalls;s.order.push_back(1);}
    int connect(const char* host,uint16_t port){auto& s=ObservationHost::https();
        s.host=host;s.port=port;++s.resolutions;s.order.push_back(4);
        ++s.connections;s.order.push_back(5);return s.connectOk?1:0;}
    void stop(){auto& s=ObservationHost::https();++s.stops;s.order.push_back(9);}
};
#endif
