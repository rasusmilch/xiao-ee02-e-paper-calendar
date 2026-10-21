#ifndef EE02_OBSERVATIONS_TEST_SNTP_H
#define EE02_OBSERVATIONS_TEST_SNTP_H
#include <WiFi.h>
#include <cassert>
enum {SNTP_SYNC_MODE_IMMED=0};
inline bool esp_sntp_enabled(){return ObservationHost::boot().sntpEnabled;}
inline void esp_sntp_stop(){auto& b=ObservationHost::boot();b.sntpEnabled=false;++b.stops;b.order.push_back(0);}
inline void esp_sntp_set_sync_mode(int mode){auto& b=ObservationHost::boot();b.mode=mode;b.order.push_back(1);}
inline void esp_sntp_set_time_sync_notification_cb(void (*callback)(struct timeval*)){
    auto& b=ObservationHost::boot();b.callback=callback;
    if(callback){++b.registrations;b.order.push_back(2);}else{++b.unregistrations;b.order.push_back(4);}}
inline void configTzTime(const char* tz,const char* server){auto& b=ObservationHost::boot();
    assert(tz && server && b.callback && b.mode==SNTP_SYNC_MODE_IMMED);
    ++b.starts;b.sntpEnabled=true;b.order.push_back(3);}
#endif
