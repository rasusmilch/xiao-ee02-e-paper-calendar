#ifndef EE02_OBSERVATIONS_TEST_STREAMSTRING_H
#define EE02_OBSERVATIONS_TEST_STREAMSTRING_H
#include <Arduino.h>
// Stand-in for the inspected Stream + String dual base; no stream/parser model.
struct StreamString : String {};
#endif
