#ifndef EE02_OBSERVATIONS_TEST_HTTP_H
#define EE02_OBSERVATIONS_TEST_HTTP_H
#include <Arduino.h>
#include <WiFiClientSecure.h>
enum { HTTP_CODE_OK=200 };
namespace ObservationHost { extern unsigned begins,requestGets,ends; extern int httpCode; extern std::string request; }
class HTTPClient {
public:
    bool begin(WiFiClientSecure&,const char* url){++ObservationHost::begins;ObservationHost::request=url;return true;}
    int GET(){++ObservationHost::requestGets;return ObservationHost::httpCode;}
    String getString(){return {};}
    void end(){++ObservationHost::ends;}
};
#endif
