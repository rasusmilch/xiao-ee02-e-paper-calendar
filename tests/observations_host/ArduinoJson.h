#ifndef EE02_OBSERVATIONS_TEST_JSON_H
#define EE02_OBSERVATIONS_TEST_JSON_H
#include <Arduino.h>
// Declaration-shaped compile facade, NOT a JSON implementation. Successful
// parsing/grouping is checked by exact moved-source comparison, not simulated.
class JsonValue {
public:
    JsonValue operator[](const char*) const{return {};}
    JsonValue operator[](int) const{return {};}
    template<class T> bool is() const{return false;}
    template<class T> T as() const{return T{};}
    template<class T> operator T() const{return T{};}
};
class JsonDocument {public:JsonValue operator[](const char*) const{return {};}};
class JsonArray {
public: size_t size() const{return 0;} JsonValue operator[](int) const{return {};}
};
class DeserializationError {
public: explicit operator bool() const{return true;} const char* c_str() const{return "synthetic parse rejection";}
};
inline DeserializationError deserializeJson(JsonDocument&,const String&){return {};}
#endif
