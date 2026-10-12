// Test-local GPIO/time/program-memory stand-ins; no hardware claims.
#ifndef EE02_TEST_ARDUINO_H
#define EE02_TEST_ARDUINO_H
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#define OUTPUT 1
#define INPUT 0
#define INPUT_PULLUP 2
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int digitalRead(int) { return HIGH; }
inline unsigned long millis() { static unsigned long ticks = 0; return ++ticks; }
inline void delay(unsigned long) {}
inline void yield() {}
inline uint8_t pgm_read_byte(const uint8_t* p) { return *p; }
#endif
