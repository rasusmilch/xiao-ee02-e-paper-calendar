#ifndef EE02_PORTAL_RANDOM_H
#define EE02_PORTAL_RANDOM_H
#include <cstddef>
#include <cstdint>
inline void esp_fill_random(void* p,size_t n) { static uint8_t epoch=0;auto* out=static_cast<uint8_t*>(p);for(size_t i=0;i<n;++i)out[i]=uint8_t(++epoch); }
#endif
