#ifndef EE02_OBSERVATIONS_TEST_HTTP_H
#define EE02_OBSERVATIONS_TEST_HTTP_H
#include <Arduino.h>
#include <WiFiClientSecure.h>
#include <StreamString.h>
#include <cassert>
enum { HTTP_CODE_OK=200 };
enum followRedirects_t {HTTPC_DISABLE_FOLLOW_REDIRECTS,HTTPC_FORCE_FOLLOW_REDIRECTS};
class HTTPClient {
    WiFiClientSecure* client=nullptr;
    std::string host;
    bool redirectsDisabled=false;
public:
    void setFollowRedirects(followRedirects_t policy){assert(policy==HTTPC_DISABLE_FOLLOW_REDIRECTS);
        redirectsDisabled=true;ObservationHost::https().order.push_back(2);}
    bool begin(WiFiClientSecure& secure,const char* url){auto& s=ObservationHost::https();
        assert(s.ca && redirectsDisabled);client=&secure;s.request=url;s.requestHistory.push_back(url);++s.begins;s.order.push_back(3);
        assert(s.request.compare(0,8,"https://")==0);
        const size_t slash=s.request.find('/',8);host=s.request.substr(8,slash-8);return s.beginOk;}
    int GET(){auto& s=ObservationHost::https();assert(client && redirectsDisabled);
        if(!client->connect(host.c_str(),443))return -1;
        ++s.requests;s.order.push_back(6);return s.requests<=s.statuses.size()?s.statuses[s.requests-1]:s.status;}
    int writeToStream(StreamString* output){auto& s=ObservationHost::https();++s.transfers;s.order.push_back(7);
        output->assign(s.payload);return s.transferResult;}
    void end(){auto& s=ObservationHost::https();++s.ends;s.order.push_back(8);}
};
#endif
