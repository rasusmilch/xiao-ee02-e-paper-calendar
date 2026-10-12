// Exercises the real Panel_EPaper and DisplayInstance with small hardware fakes.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <vector>
#include "../third_party/Seeed_GFX2/src/panel/Panel_EPaper.h"
#include "../third_party/Seeed_GFX2/src/runtime/DisplayInstance.h"

namespace {
constexpr size_t bytes = 960000;
size_t allocations = 0, frees = 0;
bool failAllocation = false;
const void* callerStorage = nullptr;
}
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t n) {
    ++allocations;
    return failAllocation ? nullptr : __real_malloc(n);
}
extern "C" void __wrap_free(void* p) {
    assert(!p || p != callerStorage);
    if (p) ++frees;
    __real_free(p);
}

struct Bus : IBus {
    bool ready = true;
    int starts = 0, stops = 0;
    bool begin() override { ++starts; return ready; }
    void end() override { ++stops; }
    void beginWrite() override {}
    void endWrite() override {}
    void writeCommand(uint8_t) override {}
    void writeData(uint8_t) override {}
    void writeData(const uint8_t*, size_t) override {}
    void beginRead() override {}
    void endRead() override {}
    uint8_t readData() override { return 0; }
    void setFrequency(uint32_t) override {}
    uint32_t frequency() const override { return 10000000; }
};
struct Board : IBoard {
    bool ready = true;
    int off = 0;
    const char* name() const override { return "test"; }
    bool begin() override { return ready; }
    int8_t pinCS() const override { return -1; }
    int8_t pinDC() const override { return -1; }
    int8_t pinRST() const override { return -1; }
    int8_t pinBacklight() const override { return -1; }
    int8_t pinMOSI() const override { return -1; }
    int8_t pinMISO() const override { return -1; }
    int8_t pinSCLK() const override { return -1; }
    IBus* createBus() override { return nullptr; }
    void powerOff() override { ++off; }
};
struct Driver : IDriver {
    Bus* transport = nullptr;
    bool ready = true, changeGeometry = false;
    uint16_t w = 1200, h = 1600;
    uint8_t depth = 4;
    const char* id = "T133A01";
    DriverOperationError initError = DriverOperationError::None;
    DriverOperationError updateError = DriverOperationError::None;
    DriverOperationError sleepError = DriverOperationError::None;
    DriverOperationError wakeError = DriverOperationError::None;
    int inits = 0, wakes = 0, sleeps = 0, updates = 0, pushes = 0;
    const uint8_t* sent = nullptr;
    size_t sentBytes = 0;
    uint16_t width() const override { return w; }
    uint16_t height() const override { return h; }
    uint8_t colorDepth() const override { return depth; }
    const char* name() const override { return id; }
    bool supportsColorfulEPaper() const override { return true; }
    bool supportsGrayRefresh(uint8_t) const override { return true; }
    bool supportsBWRYEPaper() const override { return true; }
    bool init(IBus& b) override {
        transport = static_cast<Bus*>(&b);
        ++inits;
        if (changeGeometry) depth = 1;
        setOperationError(initError);
        return ready;
    }
    void setRotation(uint8_t r) override { _rotation = r; }
    uint8_t rotation() const override { return _rotation; }
    void invertDisplay(bool) override {}
    void displayOn() override {}
    void displayOff() override {}
    void setAddrWindow(uint16_t, uint16_t, uint16_t, uint16_t) override {}
    void writePixel(uint16_t) override {}
    void writePixels(const uint16_t*, size_t) override {}
    void writeFill(uint16_t, size_t) override {}
    void sleep() override { ++sleeps; setOperationError(sleepError); }
    void wake() override { ++wakes; setOperationError(wakeError); }
    void update() override { ++updates; setOperationError(updateError); }
    void pushNewColors(const uint8_t* p, size_t n) override {
        ++pushes; sent = p; sentBytes = n;
    }
    IBus& bus() override { assert(transport); return *transport; }
};
struct Arena {
    // Byte-offset storage proves no unjustified word/DMA alignment requirement.
    std::vector<uint8_t> data;
    uint8_t* p;
    explicit Arena(size_t tail = 64) : data(bytes + tail + 2, 0xa5), p(data.data() + 1) {
        callerStorage = p;
    }
    ~Arena() { callerStorage = nullptr; }
    void guards() const {
        assert(data.front() == 0xa5);
        assert(std::all_of(data.begin() + bytes + 1, data.end(),
                          [](uint8_t v) { return v == 0xa5; }));
    }
    void untouched() const {
        assert(std::all_of(data.begin(), data.end(),
                          [](uint8_t v) { return v == 0xa5; }));
    }
};
static void expect(GfxResult r, GfxError e) {
    if (r.error != e) std::fprintf(stderr, "got %u (%s), expected %u\n",
        static_cast<unsigned>(r.error), r.message, static_cast<unsigned>(e));
    assert(r.error == e);
}
static void registration() {
    Arena a;
    Bus b; Driver d; Panel_EPaper p(d, b);
    expect(p.setBorrowedFrameBuffer(nullptr, bytes), GfxError::InvalidArgument);
    expect(p.setBorrowedFrameBuffer(a.p, bytes - 1), GfxError::InvalidArgument);
    expect(p.setBorrowedFrameBuffer(reinterpret_cast<uint8_t*>(UINTPTR_MAX - 100), bytes),
           GfxError::InvalidArgument);
    expect(p.setBorrowedFrameBuffer(a.p, SIZE_MAX), GfxError::InvalidArgument);
    d.depth = 1;
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::NotSupported);
    d.depth = 4; d.w = 1199;
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::NotSupported);
    d.w = 1200; d.id = "other";
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::NotSupported);
    d.id = "T133A01"; d.setRotation(1);
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::NotSupported);
    d.setRotation(0);
    expect(p.configureVisibleArea(1199, 1600), GfxError::Ok);
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::NotSupported);
    expect(p.configureVisibleArea(1200, 1600), GfxError::Ok);
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::Ok);
    expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::InvalidArgument);
    expect(p.configureVisibleArea(1199, 1600), GfxError::NotSupported);
    a.untouched();
    expect(p.end(), GfxError::Ok); // Pending borrow also terminates here.
    assert(!p.frameBuffer());
    assert(!p.begin()); // Must not silently fall back to allocation.
    expect(p.setBorrowedFrameBuffer(a.p, bytes + 64), GfxError::Ok);
}
static void lifecycle() {
    Arena a;
    Bus b; Board board; Driver d;
    const size_t allocBefore = allocations, freeBefore = frees;
    {
        Panel_EPaper p(d, b, &board);
        expect(p.setBorrowedFrameBuffer(a.p, bytes + 64), GfxError::Ok);
        expect(p.configure(PanelMode::Colorful), GfxError::NotInitialized);
        assert(p.begin());
        assert(p.frameBuffer() == a.p && p.frameBufferSize() == bytes);
        assert(std::all_of(a.p, a.p + bytes, [](uint8_t v) { return v == 0; }));
        a.guards();
        expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::InvalidArgument);
        assert(p.refresh().error == GfxError::NotSupported); // Must select colorful.
        assert(d.pushes == 0 && d.wakes == 0 && d.updates == 0);
        expect(p.configure(PanelMode::Colorful), GfxError::Ok);
        p.setAddrWindow(0,0,p.width()-1,p.height()-1);
        p.writeFill(0x0000, static_cast<size_t>(p.width()) * p.height() + 8);
        a.guards();
        p.drawBufferPixel(-1,-1,0xff,4);
        p.drawBufferPixel(1200,1600,0xff,4);
        a.guards();
        for (PanelMode mode : {PanelMode::Native, PanelMode::Gray4,
                               PanelMode::Gray16, PanelMode::BWRY})
            expect(p.configure(mode), GfxError::NotSupported);
        assert(!p.initGrayMode(4)); assert(!p.deinitGrayMode()); assert(!p.initBWRYMode());
        expect(p.configure(PanelMode::Colorful), GfxError::Ok);
        for (uint8_t r = 0; r != 4; ++r) {
            p.setRotation(r);
            assert(p.frameBufferSize() == bytes && p.frameBuffer() == a.p);
            p.setAddrWindow(0, 0, 0, 0); p.writePixel(0x0000);
            assert(p.readPixel(0, 0) == 0x0000);
            p.setAddrWindow(p.width()-1, p.height()-1, p.width()-1, p.height()-1);
            p.writePixel(0xf800);
            assert(p.readPixel(p.width()-1, p.height()-1) == 0xf800);
            a.guards();
            assert(p.begin()); // Does not refill a live framebuffer.
            assert(p.readPixel(0, 0) == 0x0000);
        }
        assert(p.initColorfulMode()); // Reconfiguration preserves pixels.
        assert(p.readPixel(0, 0) == 0x0000);
        p.setDisplayHorizontalMirror(true);
        expect(p.refresh(), GfxError::NotSupported);
        p.setDisplayHorizontalMirror(false); p.setDisplayVerticalMirror(true);
        expect(p.refresh(), GfxError::NotSupported);
        p.setDisplayVerticalMirror(false);
        p.setHorizontalMirror(true);
        expect(p.refresh(), GfxError::NotSupported);
        p.setHorizontalMirror(false); p.setVerticalMirror(true);
        expect(p.refresh(), GfxError::NotSupported);
        p.setVerticalMirror(false);
        assert(d.updates == 0 && d.pushes == 0 && d.wakes == 0);
        expect(p.refreshFast(), GfxError::NotSupported);
        expect(p.refreshPartial(0,0,8,8), GfxError::NotSupported);
        expect(p.refresh(), GfxError::Ok);
        assert(d.sent == a.p && d.sentBytes == bytes && d.updates == 1);
        expect(p.end(), GfxError::Ok);
        assert(!p.frameBuffer());
        expect(p.end(), GfxError::Ok);
        assert(!p.begin());
        expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::Ok);
        assert(p.begin());
        assert(std::all_of(a.p, a.p + bytes, [](uint8_t v) { return v == 0; }));
        expect(p.configure(PanelMode::Colorful), GfxError::Ok);
        expect(p.refresh(), GfxError::Ok);
        // Destructor ends the active borrow without freeing it.
    }
    assert(allocations == allocBefore && frees == freeBefore);
    a.guards();
}
static void initializationFailures() {
    for (int stage = 0; stage != 5; ++stage) {
        Arena a; Bus b; Board board; Driver d; Panel_EPaper p(d,b,&board);
        expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::Ok);
        if (stage == 0) board.ready = false;
        if (stage == 1) b.ready = false;
        if (stage == 2) d.ready = false;
        if (stage == 3) d.initError = DriverOperationError::BusyTimeout;
        if (stage == 4) d.changeGeometry = true;
        const size_t before = allocations;
        assert(!p.begin());
        const GfxError errors[] = {GfxError::BoardInitFailed, GfxError::BusInitFailed,
            GfxError::DriverInitFailed, GfxError::BusyTimeout, GfxError::NotSupported};
        expect(p.lastResult(), errors[stage]);
        a.untouched(); assert(!p.frameBuffer()); assert(allocations == before);
        if (stage == 3) {
            expect(p.end(), GfxError::BusyTimeout);
            expect(p.configureVisibleArea(1200,1600), GfxError::BusyTimeout);
        }
        board.ready = b.ready = d.ready = true;
        d.initError = DriverOperationError::None; d.changeGeometry = false; d.depth = 4;
        assert(p.begin()); // Explicit retry retains pending registration.
        expect(p.configure(PanelMode::Colorful), GfxError::Ok);
        expect(p.end(), GfxError::Ok);
        a.guards();
    }
}
static void uncertainHardware() {
    for (int stage = 0; stage != 4; ++stage) {
        Arena a; Bus b; Board board; Driver d;
        const size_t before = frees;
        {
            Panel_EPaper p(d,b,&board);
            expect(p.setBorrowedFrameBuffer(a.p, bytes), GfxError::Ok);
            assert(p.begin()); expect(p.configure(PanelMode::Colorful), GfxError::Ok);
            const GfxError error = stage == 3 ? GfxError::CommunicationFailed : GfxError::BusyTimeout;
            if (stage == 0) { d.updateError = DriverOperationError::BusyTimeout; expect(p.refresh(), error); }
            if (stage == 1) { d.sleepError = DriverOperationError::BusyTimeout; expect(p.end(), error); }
            if (stage == 2) {
                p.sleep(); d.wakeError = DriverOperationError::BusyTimeout; p.wake(); expect(p.lastResult(), error);
            }
            if (stage == 3) { d.updateError = DriverOperationError::CommunicationFailed; expect(p.refresh(), error); }
            const int io = d.wakes + d.sleeps + d.updates;
            expect(p.end(), error); expect(p.refresh(), error);
            assert(!p.begin()); expect(p.lastResult(), error);
            expect(p.configure(PanelMode::Colorful), error);
            expect(p.configureVisibleArea(1200,1600), error);
            assert(!p.initColorfulMode()); expect(p.lastResult(), error);
            p.sleep(); p.wake(); expect(p.lastResult(), error);
            expect(p.setBorrowedFrameBuffer(a.p, bytes), error);
            assert(p.frameBuffer() == a.p); // Unsuccessful end doesn't detach.
            assert(d.wakes + d.sleeps + d.updates == io);
            assert(board.off == 0 && b.stops == 0);
        }
        assert(frees == before); a.guards();
    }
}
static void ownedRegression() {
    Bus b; Driver d;
    const size_t beforeA = allocations, beforeF = frees;
    {
        Panel_EPaper p(d,b);
        failAllocation = true;
        assert(!p.begin()); expect(p.lastResult(), GfxError::AllocationFailed);
        failAllocation = false;
        assert(p.begin()); assert(p.frameBufferSize() == bytes);
        std::vector<uint8_t> alternative(bytes);
        expect(p.setBorrowedFrameBuffer(alternative.data(), bytes), GfxError::InvalidArgument);
        expect(p.configure(PanelMode::Colorful), GfxError::Ok);
        p.setHorizontalMirror(true); expect(p.refresh(), GfxError::Ok);
        p.setHorizontalMirror(false); p.setVerticalMirror(true); expect(p.refresh(), GfxError::Ok);
        p.setVerticalMirror(false);
        expect(p.configure(PanelMode::Native), GfxError::Ok);
        assert(p.frameBufferSize() == 240000);
        assert(std::all_of(p.frameBuffer(), p.frameBuffer()+240000, [](uint8_t v){return v==255;}));
        expect(p.refresh(), GfxError::Ok); // Ordinary previous-frame allocation.
        expect(p.configure(PanelMode::Colorful), GfxError::Ok);
        assert(p.frameBufferSize() == bytes);
        expect(p.end(), GfxError::Ok);
        expect(p.setBorrowedFrameBuffer(alternative.data(), bytes), GfxError::InvalidArgument);
        assert(p.begin()); // Ordinary owned end retains storage, as upstream.
    }
    assert(allocations > beforeA && allocations - beforeA == frees - beforeF + 1); // Failed allocation.
}
static void instanceReset() {
    for (bool fail : {false, true}) {
        Arena a; DisplayInstance instance;
        auto* board = new Board; auto* b = new Bus; auto* d = new Driver;
        auto* p = new Panel_EPaper(*d,*b,board);
        const size_t beforeA = allocations, beforeF = frees;
        expect(instance.adopt(board,b,d,p), GfxError::Ok);
        expect(p->setBorrowedFrameBuffer(a.p,bytes), GfxError::Ok);
        expect(instance.begin(), GfxError::Ok);
        expect(p->configure(PanelMode::Colorful), GfxError::Ok);
        if (fail) { d->updateError = DriverOperationError::BusyTimeout; expect(p->refresh(), GfxError::BusyTimeout); }
        instance.reset(); instance.reset();
        assert(!instance.hasStack());
        assert(allocations == beforeA && frees == beforeF); a.guards();
    }
}
int main() {
    registration(); lifecycle(); initializationFailures(); uncertainHardware();
    ownedRegression(); instanceReset();
    std::puts("GFX2 framebuffer ownership/lifecycle tests passed");
}
