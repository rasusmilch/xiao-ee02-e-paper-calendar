// Minimal facade stand-in only; host tests use real Panel_EPaper/DisplayInstance.
// The isolated target compile links the actual vendored graphics implementation.
#ifndef EE02_TEST_GFX_FACADE_H
#define EE02_TEST_GFX_FACADE_H
#include <core/Panel.h>
class Seeed_GFX {
public:
    explicit Seeed_GFX(IPanel& panel);
    ~Seeed_GFX();
    bool begin();
private:
    IPanel& _panel;
};
#endif
