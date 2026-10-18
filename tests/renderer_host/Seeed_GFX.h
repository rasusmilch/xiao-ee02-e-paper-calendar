#ifndef EE02_RENDERER_TEST_GFX_H
#define EE02_RENDERER_TEST_GFX_H
#include <core/Panel.h>
#include <core/Color.h>
#include <cassert>
#include <vector>
// API trace only. GLCD 6x8 cell extents match inspected vendored drawCharGlcd.
// This stand-in verifies application coordinates, not vendor rasterization/hardware.
class Seeed_GFX {
public:
    struct Glyph { int32_t x,y; uint16_t code; uint8_t size; };
    std::vector<Glyph> glyphs;
    int32_t left=0, top=0, right=1600, bottom=1200;
    size_t rectangles=0;
    Seeed_GFX() { glyphs.reserve(20000); }
    explicit Seeed_GFX(IPanel& p) : Seeed_GFX() { panel=&p; }
    bool begin() { return panel && panel->begin(); }
    int16_t width() const { return 1600; }
    int16_t height() const { return 1200; }
    void setFreeFont(const void*) {}
    int32_t getViewportX() const { return vx; }
    int32_t getViewportY() const { return vy; }
    int32_t getViewportWidth() const { return vw; }
    int32_t getViewportHeight() const { return vh; }
    bool getViewportDatum() const { return vd; }
    int32_t getOriginX() const { return ox; }
    int32_t getOriginY() const { return oy; }
    void setOrigin(int32_t x,int32_t y) { ox=x; oy=y; }
    void setViewport(int32_t x,int32_t y,int32_t w,int32_t h,bool datum) {
        vx=x; vy=y; vw=w; vh=h; vd=datum; ox=datum?x:0; oy=datum?y:0;
    }
    void fillRect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t) {
        x+=ox; y+=oy;
        bounds(x,y,w,h); ++rectangles;
    }
    void drawChar(int32_t x,int32_t y,uint16_t c,uint32_t,uint32_t,uint8_t size) {
        x+=ox; y+=oy;
        bounds(x,y,6*size,8*size); assert(c>=32 && c<=126);
        assert(glyphs.size()<glyphs.capacity()); glyphs.push_back({x,y,c,size});
    }
    void bounds(int32_t x,int32_t y,int32_t w,int32_t h) const {
        assert(w>0 && h>0 && x>=left && y>=top && x+w<=right && y+h<=bottom);
    }
private:
    IPanel* panel=nullptr;
    int32_t vx=0,vy=0,vw=1600,vh=1200,ox=0,oy=0;
    bool vd=false;
};
#endif
