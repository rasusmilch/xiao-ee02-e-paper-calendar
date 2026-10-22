#ifndef EE02_LIVE_DASHBOARD_DECODED_FIXTURE_H
#define EE02_LIVE_DASHBOARD_DECODED_FIXTURE_H
#include <Arduino.h>
// Scripted DECODED VALUES for orchestration branch coverage, not a JSON parser.
// The exact single synthetic payload selects a supplied decoded observation.
// No claims about JSON syntax, schemas, allocation or real ArduinoJson execution.
namespace LiveDecoded {
inline const char* payload(){return "{\"fixture\":\"supplied decoded weather values\"}";}
}
class JsonValue {
    const char* key;
public:
    explicit JsonValue(const char* k=""):key(k){}
    JsonValue operator[](const char* k)const{return JsonValue(k);}
    JsonValue operator[](int)const{return *this;}
    operator const char*()const{
        if(!strcmp(key,"icon"))return "01d";
        if(!strcmp(key,"description"))return "Synthetic clear";
        if(!strcmp(key,"dt_txt"))return "2026-10-25 12:00:00";
        return nullptr;
    }
    operator float()const{
        if(!strcmp(key,"temp") || !strcmp(key,"temp_max"))return 17;
        if(!strcmp(key,"temp_min"))return 9;
        if(!strcmp(key,"speed"))return 2;
        return 0;
    }
    operator int()const{return !strcmp(key,"humidity")?60:0;}
    template<class T> bool is()const{return true;}
    template<class T> T as()const{return static_cast<T>(*this);}
};
class JsonDocument{public:JsonValue operator[](const char* key)const{return JsonValue(key);}};
class JsonArray{public:JsonArray(JsonValue){} size_t size()const{return 1;} JsonValue operator[](int)const{return JsonValue();}};
class DeserializationError{
    bool failed;
public:explicit DeserializationError(bool f):failed(f){} explicit operator bool()const{return failed;}
    const char* c_str()const{return "synthetic fixture rejection";}
};
inline DeserializationError deserializeJson(JsonDocument&,const String& value){return DeserializationError(value!=LiveDecoded::payload());}
#endif
