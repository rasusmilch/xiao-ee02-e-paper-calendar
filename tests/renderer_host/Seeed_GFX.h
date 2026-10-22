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
    struct Shape { int32_t x,y,width,height,vx,vy,vw,vh; uint32_t color; };
    std::vector<Glyph> glyphs;
    std::vector<Shape> shapes;
    int32_t left=0, top=0, right=1600, bottom=1200;
    size_t rectangles=0;
    Seeed_GFX() { glyphs.reserve(20000); shapes.reserve(20000); }
    explicit Seeed_GFX(IPanel& p) : Seeed_GFX() { panel=&p; }
    using Observer=void(*)(const Seeed_GFX&);
    static Observer& observer(){static Observer value=nullptr;return value;}
    ~Seeed_GFX(){if(observer())observer()(*this);}
    void setRotation(uint8_t r){if(panel)panel->setRotation(r);}
    void fillScreen(uint32_t color){fillRect(0,0,width(),height(),color);}
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
    void fillRect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t color) {
        x+=ox; y+=oy;
        bounds(x,y,w,h); ++rectangles;
        assert(shapes.size()<shapes.capacity());shapes.push_back({x,y,w,h,vx,vy,vw,vh,color});
    }
    void drawRect(int32_t x,int32_t y,int32_t w,int32_t h,uint32_t color) { fillRect(x,y,w,h,color); }
    void drawLine(int32_t x,int32_t y,int32_t a,int32_t b,uint32_t color) {
        fillRect(x<a?x:a,y<b?y:b,(x<a?a-x:x-a)+1,(y<b?b-y:y-b)+1,color);
    }
    void drawCircle(int32_t x,int32_t y,int32_t r,uint32_t color) { fillRect(x-r,y-r,2*r+1,2*r+1,color); }
    void fillCircle(int32_t x,int32_t y,int32_t r,uint32_t color) { drawCircle(x,y,r,color); }
    void drawChar(int32_t x,int32_t y,uint16_t c,uint32_t,uint32_t,uint8_t size) {
        x+=ox; y+=oy;
        bounds(x,y,6*size,8*size); assert(c>=32 && c<=126);
        assert(glyphs.size()<glyphs.capacity()); glyphs.push_back({x,y,c,size});
    }
    void bounds(int32_t x,int32_t y,int32_t w,int32_t h) const {
        assert(w>0 && h>0 && x>=left && y>=top && x+w<=right && y+h<=bottom);
        assert(x>=vx && y>=vy && x+w<=vx+vw && y+h<=vy+vh);
    }
private:
    IPanel* panel=nullptr;
    int32_t vx=0,vy=0,vw=1600,vh=1200,ox=0,oy=0;
    bool vd=false;
};
#endif
