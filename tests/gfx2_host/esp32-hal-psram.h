// Link-time test stand-ins. Real target builds use the installed ESP32 core.
#ifndef EE02_TEST_PSRAM_H
#define EE02_TEST_PSRAM_H
#include <cstddef>
bool psramFound();
void* ps_malloc(size_t bytes);
#endif
