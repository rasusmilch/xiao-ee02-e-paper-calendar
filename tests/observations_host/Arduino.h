#ifndef EE02_OBSERVATIONS_TEST_ARDUINO_H
#define EE02_OBSERVATIONS_TEST_ARDUINO_H
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
using String=std::string;
enum { A0=1,A5=6,OUTPUT=1,INPUT=0,INPUT_PULLUP=2,HIGH=1,LOW=0 };
namespace ObservationHost {
struct Action { int operation,pin,value; };
extern Action actions[16]; extern size_t actionCount; extern int adc;
inline void action(int operation,int pin,int value) {
    if(actionCount>=16) std::abort();
    actions[actionCount++]={operation,pin,value};
}
struct SerialSink {
    template<class... T> void printf(const char*,T...) {}
    void println(const char*) {}
};
extern SerialSink serial;
}
#define Serial ObservationHost::serial
inline void pinMode(int pin,int mode){ObservationHost::action(0,pin,mode);}
inline void digitalWrite(int pin,int value){ObservationHost::action(1,pin,value);}
namespace ObservationHost {
inline void (*&delayHook())(unsigned long){static void (*hook)(unsigned long)=nullptr;return hook;}
}
inline void delay(unsigned long ms){if(ObservationHost::delayHook())ObservationHost::delayHook()(ms);else ObservationHost::action(2,0,int(ms));}
inline int analogRead(int pin){ObservationHost::action(3,pin,ObservationHost::adc);return ObservationHost::adc;}
inline int digitalRead(int){return HIGH;}
inline unsigned long millis(){static unsigned long ms=0;return ++ms;}
inline void yield(){}
inline uint8_t pgm_read_byte(const uint8_t* p){return *p;}
#endif
