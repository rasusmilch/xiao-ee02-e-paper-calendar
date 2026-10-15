// Actual workspace + Panel_EPaper/DisplayInstance; narrow host hardware/allocator
// and facade/factory stand-ins. No host result establishes physical quiescence.
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <new>
#include <type_traits>
#include <vector>
#include "../application/psram_workspace.h"
#include "../application/five_day_snapshot.h"
#include "../application/ics_ingestion.h"
#include "../application/ics_structure.h"
#include "../application/ics_source_stage.h"

using Workspace = Ee02PsramWorkspace;
namespace {
constexpr size_t evaluationBytes = 2097152; // Provisional experiment only.
constexpr size_t frameBytes = 960000;
constexpr size_t guard = 16;
enum class Fault { None, Factory, Registration, BoardBegin, BusBegin,
    DriverBegin, InitBusy, Configuration, WakeBusy, RefreshBusy, Transport,
    ShutdownBusy, RefreshSleepBusy, Mirror };
Fault fault = Fault::None;
bool psram = true, allocationFails = false, badAddress = false;
size_t psAttempts = 0, workspaceFrees = 0, panelAllocations = 0, invalidSpanFrees = 0;
int components = 0, facades = 0, factoryCalls = 0, refreshes = 0;
struct Allocation { uint8_t* raw; uint8_t* p; size_t size; bool active; };
std::vector<Allocation> records;
Allocation* latest = nullptr;
Panel_EPaper* activePanel = nullptr;

void checkGuards(const Allocation& a) {
    for (size_t i=0; i<guard; ++i) {
        assert(a.raw[i] == 0xa5);
        assert(a.p[a.size+i] == 0xa5);
    }
}
void expect(GfxResult r, GfxError expected) {
    if (r.error != expected) std::fprintf(stderr,"got %u (%s), expected %u\n",
        static_cast<unsigned>(r.error),r.message,static_cast<unsigned>(expected));
    assert(r.error == expected);
}
}
extern "C" void* __real_malloc(size_t);
extern "C" void __real_free(void*);
extern "C" void* __wrap_malloc(size_t n) {
    ++panelAllocations;
    return __real_malloc(n);
}
extern "C" void __wrap_free(void* p) {
    if (p == reinterpret_cast<void*>(UINTPTR_MAX-12)) { ++invalidSpanFrees; return; }
    // The allocator can reuse an address from an earlier, released cycle.
    for (auto it=records.rbegin(); it!=records.rend(); ++it) {
        auto& a=*it;
        if (p != a.p) continue;
        assert(a.active && components == 0 && facades == 0);
        checkGuards(a); a.active = false; ++workspaceFrees;
        __real_free(a.raw);
        return;
    }
    __real_free(p);
}
bool psramFound() { return psram; }
void* ps_malloc(size_t n) {
    ++psAttempts;
    if (allocationFails) return nullptr;
    if (badAddress) return reinterpret_cast<void*>(UINTPTR_MAX-12);
    auto* raw = static_cast<uint8_t*>(__real_malloc(n+2*guard));
    assert(raw); std::memset(raw,0xa5,n+2*guard);
    records.push_back({raw,raw+guard,n,true}); latest = &records.back();
    return latest->p;
}

Seeed_GFX::Seeed_GFX(IPanel& panel) : _panel(panel) { ++facades; }
Seeed_GFX::~Seeed_GFX() { --facades; }
bool Seeed_GFX::begin() { return _panel.begin(); }

namespace {
struct Bus : IBus {
    Bus() { ++components; }
    ~Bus() override { assert(facades == 0); --components; }
    bool begin() override { return fault != Fault::BusBegin; }
    void end() override {}
    void beginWrite() override {}
    void endWrite() override {}
    void writeCommand(uint8_t) override {}
    void writeData(uint8_t) override {}
    void writeData(const uint8_t*,size_t) override {}
    void beginRead() override {}
    void endRead() override {}
    uint8_t readData() override { return 0; }
    void setFrequency(uint32_t) override {}
    uint32_t frequency() const override { return 10000000; }
};
struct Board : IBoard {
    Board() { ++components; }
    ~Board() override { assert(facades == 0); --components; }
    const char* name() const override { return "test EE02"; }
    bool begin() override { return fault != Fault::BoardBegin; }
    int8_t pinCS() const override { return -1; }
    int8_t pinDC() const override { return -1; }
    int8_t pinRST() const override { return -1; }
    int8_t pinBacklight() const override { return -1; }
    int8_t pinMOSI() const override { return -1; }
    int8_t pinMISO() const override { return -1; }
    int8_t pinSCLK() const override { return -1; }
    IBus* createBus() override { return nullptr; }
};
struct Driver : IDriver {
    IBus* transport = nullptr;
    Driver() { ++components; }
    ~Driver() override { assert(facades == 0); --components; }
    uint16_t width() const override { return 1200; }
    uint16_t height() const override { return 1600; }
    uint8_t colorDepth() const override { return 4; }
    const char* name() const override { return fault == Fault::Registration ? "other" : "T133A01"; }
    bool supportsColorfulEPaper() const override { return true; }
    bool init(IBus& b) override {
        transport = &b;
        if (fault == Fault::InitBusy) setOperationError(DriverOperationError::BusyTimeout);
        return fault != Fault::DriverBegin;
    }
    void setRotation(uint8_t r) override { _rotation = r; }
    uint8_t rotation() const override { return _rotation; }
    void invertDisplay(bool) override {}
    void displayOn() override {}
    void displayOff() override {}
    void setAddrWindow(uint16_t,uint16_t,uint16_t,uint16_t) override {}
    void writePixel(uint16_t) override {}
    void writePixels(const uint16_t*,size_t) override {}
    void writeFill(uint16_t,size_t) override {}
    void sleep() override {
        assert(facades == 1); // Explicit checked end/refresh, before facade destruction.
        if (fault == Fault::ShutdownBusy || fault == Fault::RefreshSleepBusy)
            setOperationError(DriverOperationError::BusyTimeout);
    }
    void wake() override {
        if (fault == Fault::WakeBusy) setOperationError(DriverOperationError::BusyTimeout);
    }
    void update() override {
        ++refreshes;
        if (fault == Fault::RefreshBusy) setOperationError(DriverOperationError::BusyTimeout);
        if (fault == Fault::Transport) setOperationError(DriverOperationError::CommunicationFailed);
    }
    void pushNewColors(const uint8_t* p,size_t n) override {
        assert(latest && p == latest->p && n == frameBytes);
    }
    IBus& bus() override { assert(transport); return *transport; }
};
struct Panel : Panel_EPaper {
    Panel(IDriver& d,IBus& b,IBoard* board) : Panel_EPaper(d,b,board) { ++components; }
    ~Panel() override { assert(facades == 0); --components; activePanel = nullptr; }
    GfxResult configure(PanelMode mode) override {
        if (fault == Fault::Configuration) return GfxResult(GfxError::NotSupported,"injected config failure");
        return Panel_EPaper::configure(mode);
    }
};
}
// Link-time factory substitute; target build uses the actual official catalog.
GfxResult ProductCatalog::create(Seeed_Product::Product product,DisplayInstance& instance) {
    assert(product == Seeed_Product::Seeed_ePaper_13INCH3_C); ++factoryCalls;
    auto* b = new Bus; auto* board = new Board; auto* d = new Driver;
    auto* p = new Panel(*d,*b,board); activePanel = p;
    if (fault == Fault::Mirror) p->setHorizontalMirror(true);
    const GfxResult adopted = instance.adopt(board,b,d,p);
    assert(adopted.ok());
    // Exercise cleanup even if a factory left a partially constructed stack.
    if (fault == Fault::Factory) return GfxResult(GfxError::AllocationFailed,"injected factory failure");
    return adopted;
}

namespace {
GfxResult noScratch(uint8_t*,size_t,void*) { return GfxResult::success(); }
GfxResult noDraw(Seeed_GFX&,void*) { return GfxResult::success(); }
GfxResult fill(uint8_t* p,size_t n,void* context) {
    if (n) std::memset(p,*static_cast<uint8_t*>(context),n);
    return GfxResult::success();
}
GfxResult access(uint8_t* p,size_t n,void* context) {
    auto& w = *static_cast<Workspace*>(context);
    assert(w.state() == Workspace::State::ScratchAccess && n == 16);
    expect(w.withScratch(0,1,noScratch),GfxError::InvalidArgument);
    expect(w.release(),GfxError::InvalidArgument);
    expect(w.allocate(frameBytes),GfxError::InvalidArgument);
    expect(w.render(noDraw).operation,GfxError::InvalidArgument);
    struct Temporary { uint32_t value; }; // Test-only temporary typed storage.
    assert(reinterpret_cast<uintptr_t>(p)%alignof(Temporary) == 0);
    auto* object = new (p) Temporary{42};
    assert(object->value == 42); object->~Temporary();
    return GfxResult(GfxError::NotSupported,"test callback failure");
}
GfxResult draw(Seeed_GFX&,void* context) {
    auto& w = *static_cast<Workspace*>(context);
    assert(w.state() == Workspace::State::Rendering && facades == 1);
    assert(activePanel && activePanel->frameBuffer() == latest->p);
    assert(activePanel->frameBufferSize() == frameBytes);
    assert(std::all_of(latest->p,latest->p+frameBytes,[](uint8_t b){return b==0;}));
    expect(w.withScratch(0,1,noScratch),GfxError::InvalidArgument);
    expect(w.release(),GfxError::InvalidArgument);
    expect(w.allocate(frameBytes),GfxError::InvalidArgument);
    expect(w.render(noDraw).operation,GfxError::InvalidArgument);
    activePanel->setRotation(1);
    activePanel->setAddrWindow(0,0,0,0); activePanel->writePixel(0x0000);
    assert(activePanel->readPixel(0,0) == 0x0000);
    if (fault == Fault::ShutdownBusy)
        return GfxResult(GfxError::NotSupported,"drawing declined, shutdown still required");
    return GfxResult::success();
}
void allocationAndBounds() {
    Workspace w;
    expect(w.release(),GfxError::Ok);
    expect(w.withScratch(0,0,noScratch),GfxError::NotInitialized);
    expect(w.render(noDraw).operation,GfxError::NotInitialized);
    expect(w.allocate(0),GfxError::InvalidArgument);
    expect(w.allocate(frameBytes-1),GfxError::InvalidArgument);
    expect(w.allocate(SIZE_MAX),GfxError::InvalidArgument);
    psram = false; const size_t attempts = psAttempts;
    expect(w.allocate(frameBytes),GfxError::AllocationFailed); assert(psAttempts==attempts);
    psram = true; allocationFails = true;
    expect(w.allocate(frameBytes),GfxError::AllocationFailed);
    assert(w.state()==Workspace::State::Unallocated && w.capacity()==0);
    allocationFails = false; badAddress = true;
    expect(w.allocate(frameBytes),GfxError::InvalidArgument); assert(invalidSpanFrees==1);
    assert(w.state()==Workspace::State::Unallocated && w.capacity()==0);
    badAddress = false;
    expect(w.allocate(frameBytes),GfxError::Ok);
    assert(w.capacity()==frameBytes);
    expect(w.allocate(evaluationBytes),GfxError::InvalidArgument);
    expect(w.withScratch(frameBytes,0,noScratch),GfxError::Ok);
    expect(w.withScratch(frameBytes,1,noScratch),GfxError::InvalidArgument);
    expect(w.withScratch(frameBytes+1,0,noScratch),GfxError::InvalidArgument);
    expect(w.withScratch(SIZE_MAX,1,noScratch),GfxError::InvalidArgument);
    expect(w.withScratch(1,SIZE_MAX,noScratch),GfxError::InvalidArgument);
    expect(w.withScratch(0,1,nullptr),GfxError::InvalidArgument);
    uint8_t value = 0x37;
    expect(w.withScratch(1,1,fill,&value),GfxError::Ok); // Byte offsets need no DMA alignment.
    expect(w.withScratch(frameBytes-1,1,fill,&value),GfxError::Ok);
    assert(latest->p[frameBytes-1]==value); checkGuards(*latest);
    expect(w.withScratch(0,16,access,&w),GfxError::NotSupported);
    assert(w.state()==Workspace::State::Scratch);
    expect(w.render(nullptr).operation,GfxError::InvalidArgument);
    assert(w.render(draw,&w).ok());
    expect(w.release(),GfxError::Ok); expect(w.release(),GfxError::Ok);
    expect(w.allocate(frameBytes),GfxError::Ok); // Healthy repeated setup/cleanup.
    assert(w.render(draw,&w).ok()); // Destructor will free this healthy allocation.
}
void patternsAndRepeatedRendering() {
    Workspace w;
    const size_t before=psAttempts, freed=workspaceFrees;
    expect(w.allocate(evaluationBytes),GfxError::Ok);
    uint8_t prefix=0x5a, tail=0x3c;
    expect(w.withScratch(0,frameBytes,fill,&prefix),GfxError::Ok);
    expect(w.withScratch(frameBytes,evaluationBytes-frameBytes,fill,&tail),GfxError::Ok);
    assert(latest->p[0]==prefix);
    for (int i=0;i<3;++i) {
        const auto result = w.render(draw,&w);
        assert(result.ok() && result.shutdownChecked);
        assert(w.state()==Workspace::State::Scratch && !activePanel && components==0 && facades==0);
        assert(std::all_of(latest->p+frameBytes,latest->p+evaluationBytes,
                          [tail](uint8_t b){return b==tail;}));
        checkGuards(*latest);
    }
    assert(psAttempts==before+1 && workspaceFrees==freed);
    expect(w.release(),GfxError::Ok); assert(workspaceFrees==freed+1);
}
struct SnapshotContext { Workspace* workspace; FiveDaySnapshot* snapshot; };
GfxResult stageFromScratch(uint8_t* p,size_t n,void* context) {
    const char title[]="from scratch"; assert(n>=sizeof(title));
    std::memcpy(p,title,sizeof(title));
    FiveDaySnapshot::Input input={}; input.kind=FiveDaySnapshot::Kind::Timed;
    input.range.timed={1,2}; input.title={reinterpret_cast<const char*>(p),sizeof(title)-1};
    auto& snapshot=*static_cast<FiveDaySnapshot*>(context);
    assert(snapshot.addOccurrence(0,input)==FiveDaySnapshot::Result::Ok);
    return GfxResult::success();
}
void checkSnapshot(const FiveDaySnapshot& snapshot) {
    assert(snapshot.occurrenceCount()==1 && snapshot.occurrence(0)->range.timed.start==1);
    const auto title=snapshot.title(0);
    assert(title.size==12 && !std::memcmp(title.data,"from scratch",13));
    assert(snapshot.source(0)->outcome==FiveDaySnapshot::Outcome::Complete);
}
GfxResult drawWithSnapshot(Seeed_GFX& gfx,void* context) {
    const auto& c=*static_cast<SnapshotContext*>(context);
    checkSnapshot(*c.snapshot); return draw(gfx,c.workspace);
}
void snapshotIndependence() {
    const size_t before=psAttempts, freed=workspaceFrees, panelBefore=panelAllocations;
    FiveDaySnapshot snapshot; Workspace w;
    FiveDaySnapshot::Window window={{0,86400,172800,259200,345600,432000},0,0};
    const FiveDaySnapshot::Text labels[]={{"synthetic",9}};
    assert(snapshot.initialize(window,4,64,labels,1)==FiveDaySnapshot::Result::Ok);
    expect(w.allocate(evaluationBytes),GfxError::Ok);
    assert(snapshot.beginSource(0)==FiveDaySnapshot::Result::Ok);
    expect(w.withScratch(0,64,stageFromScratch,&snapshot),GfxError::Ok);
    assert(snapshot.completeSource(0)==FiveDaySnapshot::Result::Ok);
    uint8_t poison=0xbd;
    expect(w.withScratch(0,evaluationBytes,fill,&poison),GfxError::Ok);
    checkSnapshot(snapshot);
    SnapshotContext c={&w,&snapshot}; assert(w.render(drawWithSnapshot,&c).ok());
    checkSnapshot(snapshot);
    assert(std::all_of(latest->p+frameBytes,latest->p+evaluationBytes,
                      [poison](uint8_t b){return b==poison;}));
    assert(psAttempts==before+2 && panelAllocations==panelBefore);
    expect(w.release(),GfxError::Ok);
    checkSnapshot(snapshot); // Also survives complete workspace destruction/release.
    assert(snapshot.release()==FiveDaySnapshot::Result::Ok);
    assert(workspaceFrees==freed+2);
}
struct IngestionCheck { uint64_t now; size_t lines; };
uint64_t ingestionClock(void* context) { return static_cast<IngestionCheck*>(context)->now; }
bool ingestionLine(const uint8_t* p,size_t n,void* context) {
    auto& check=*static_cast<IngestionCheck*>(context);
    assert(check.lines==0 && n==2 && !std::memcmp(p,"AB",2)); ++check.lines;
    return true; // No callback pointer or view escapes into context.
}
GfxResult ingestScratch(uint8_t* p,size_t n,void*) {
    assert(n==4096);
    const IcsIngestion::Limits limits={4096,4096,1024,16,128,64,512,8,128,256,512,1000,100};
    constexpr size_t parserBytes=128+128+512;
    uint8_t* parser=p+16; uint8_t* receive=parser+parserBytes+16;
    const char wire[]="HTTP/1.1 200 OK\r\nContent-Length: 7\r\n\r\nA\r\n B\r\n";
    IngestionCheck check={0,0};
    {
        IcsIngestion ingestion;
        assert(ingestion.initialize(parser,parserBytes,limits,ingestionLine,&check,
                                   ingestionClock,&check)==IcsIngestion::Status::InProgress);
        // One-byte reads from a disjoint slice of the same owned workspace.
        for(size_t i=0;i<sizeof(wire)-1;++i) {
            receive[0]=static_cast<uint8_t>(wire[i]);
            assert(ingestion.feed(receive,1)==IcsIngestion::Status::InProgress);
        }
        assert(check.lines==0); // Last candidate is hidden until finalization.
        assert(ingestion.finish()==IcsIngestion::Status::Complete && check.lines==1);
        ingestion.release();
        std::memset(parser,0xbd,parserBytes);
        assert(ingestion.finish()==IcsIngestion::Status::Complete && check.lines==1);
    } // Destroy parser before scratch is reassigned.
    for(size_t i=0;i<16;++i) {
        assert(p[i]==0xa5 && parser[parserBytes+i]==0xa5);
    }
    for(size_t i=static_cast<size_t>(receive-p)+1;i<n;++i) assert(p[i]==0xa5);
    return GfxResult::success();
}
void ingestionScratchLifetime() {
    const size_t before=psAttempts, freed=workspaceFrees;
    const int factories=factoryCalls;
    Workspace w; expect(w.allocate(frameBytes),GfxError::Ok);
    for(int cycle=0;cycle<3;++cycle) {
        uint8_t reset=0xa5;
        expect(w.withScratch(0,4096,fill,&reset),GfxError::Ok);
        expect(w.withScratch(0,4096,ingestScratch),GfxError::Ok);
        // Destroyed session has no future access; entire slice is reusable.
        uint8_t poison=0x7f;
        expect(w.withScratch(0,4096,fill,&poison),GfxError::Ok);
        checkGuards(*latest);
    }
    assert(factoryCalls==factories && psAttempts==before+1);
    expect(w.release(),GfxError::Ok); assert(workspaceFrees==freed+1);
}
struct StructureCheck { size_t properties, participants; };
bool structureEvent(const IcsStructure::Event& event,void* context) {
    auto& check=*static_cast<StructureCheck*>(context);
    if(event.type!=IcsStructure::EventType::Property) return true;
    ++check.properties;
    if(IcsStructure::identifier(event.property.name,"UID")) {
        assert(event.component.kind==IcsStructure::Kind::Participant);
        assert(event.component.depth==3 && event.component.parentOrdinal==2);
        ++check.participants;
    }
    return true; // Copy counters only; no borrowed views survive.
}
uint64_t structureClock(void*) { return 0; }
GfxResult structureScratch(uint8_t* p,size_t n,void*) {
    assert(n==4096);
    const IcsIngestion::Limits limits={4096,4096,1024,16,128,64,512,8,128,256,512,1000,100};
    const IcsStructure::Limits structureLimits={512,8,8,16,4,32,64,128,32,2};
    size_t structureBytes=0;
    assert(IcsStructure::scratchBytes(structureLimits,structureBytes)==IcsStructure::Status::InProgress);
    constexpr size_t ingestionBytes=128+128+512;
    uint8_t* ingestionStorage=p+16;
    uint8_t* structureStorage=ingestionStorage+ingestionBytes+16;
    uint8_t* receive=structureStorage+structureBytes+16;
    assert(static_cast<size_t>(receive-p)<n);
    StructureCheck check={0,0};
    const char body[]="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:host\r\nBEGIN:VEVENT\r\n"
        "BEGIN:PARTICIPANT\r\nUID:person\r\nEND:PARTICIPANT\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    char header[96];
    const int headerBytes=std::snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n",sizeof(body)-1);
    assert(headerBytes>0 && static_cast<size_t>(headerBytes)<sizeof(header));
    {
        IcsStructure structure;
        assert(structure.initialize(structureStorage,structureBytes,structureLimits,structureEvent,&check)==IcsStructure::Status::InProgress);
        IcsIngestion ingestion;
        assert(ingestion.initialize(ingestionStorage,ingestionBytes,limits,IcsStructure::lineSink,&structure,
                                   structureClock,nullptr)==IcsIngestion::Status::InProgress);
        for(size_t i=0;i<static_cast<size_t>(headerBytes)+sizeof(body)-1;++i) {
            receive[0]=static_cast<uint8_t>(i<static_cast<size_t>(headerBytes)?header[i]:body[i-headerBytes]);
            assert(ingestion.feed(receive,1)==IcsIngestion::Status::InProgress);
        }
        assert(ingestion.finish()==IcsIngestion::Status::Complete);
        assert(structure.finish()==IcsStructure::Status::StructurallyComplete);
        assert(check.properties==3 && check.participants==1);
        ingestion.release(); structure.release();
        std::memset(ingestionStorage,0xbd,ingestionBytes);
        std::memset(structureStorage,0xbd,structureBytes);
        assert(structure.finish()==IcsStructure::Status::StructurallyComplete);
    }
    for(size_t i=0;i<16;++i) {
        assert(p[i]==0xa5 && ingestionStorage[ingestionBytes+i]==0xa5);
        assert(structureStorage[structureBytes+i]==0xa5);
    }
    for(size_t i=static_cast<size_t>(receive-p)+1;i<n;++i) assert(p[i]==0xa5);
    return GfxResult::success();
}
void structureScratchLifetime() {
    const size_t before=psAttempts, freed=workspaceFrees, panels=panelAllocations;
    const int factories=factoryCalls;
    Workspace w; expect(w.allocate(frameBytes),GfxError::Ok);
    for(int cycle=0;cycle<3;++cycle) {
        uint8_t reset=0xa5;
        expect(w.withScratch(0,4096,fill,&reset),GfxError::Ok);
        expect(w.withScratch(0,4096,structureScratch),GfxError::Ok);
        uint8_t poison=0x7f;
        expect(w.withScratch(0,4096,fill,&poison),GfxError::Ok);
        checkGuards(*latest);
    }
    assert(factoryCalls==factories && panelAllocations==panels && psAttempts==before+1);
    expect(w.release(),GfxError::Ok); assert(workspaceFrees==freed+1);
}
GfxResult sourceStageScratch(uint8_t* storage,size_t capacity,void*) {
    const IcsSourceStage::Limits limits={8,16,1024,4,128};
    const IcsStructure::Limits syntax={512,8,8,16,4,32,64,128,16,2};
    const IcsIngestion::Limits transport={4096,4096,1024,16,128,64,512,8,128,256,512,1000,100};
    size_t stageBytes=0, syntaxBytes=0;
    assert(IcsSourceStage::requiredBytes(limits,stageBytes)==IcsSourceStage::Status::InProgress);
    assert(IcsStructure::scratchBytes(syntax,syntaxBytes)==IcsStructure::Status::InProgress);
    uint8_t* stageStorage=storage+16;
    uint8_t* httpStorage=stageStorage+stageBytes+16;
    uint8_t* syntaxStorage=httpStorage+768+16;
    uint8_t* receive=syntaxStorage+syntaxBytes+16;
    assert(static_cast<size_t>(receive-storage)+1<capacity);
    const char body[]="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\nBEGIN:VEVENT\r\n"
        "UID:independent\r\nSUMMARY:retained\r\nEND:VEVENT\r\nBEGIN:X-CUSTOM\r\nUID:ignored\r\n"
        "X-DATA:ignored payload\r\nEND:X-CUSTOM\r\nEND:VCALENDAR\r\n";
    char header[96];
    const int headerBytes=std::snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Length: %zu\r\n\r\n",sizeof(body)-1);
    assert(headerBytes>0 && static_cast<size_t>(headerBytes)<sizeof(header));
    {
        IcsSourceStage stage; IcsStructure parser; IcsIngestion http;
        assert(stage.initialize(stageStorage,stageBytes,limits)==IcsSourceStage::Status::InProgress);
        assert(parser.initialize(syntaxStorage,syntaxBytes,syntax,IcsSourceStage::sink,&stage)==IcsStructure::Status::InProgress);
        assert(http.initialize(httpStorage,768,transport,IcsStructure::lineSink,&parser,structureClock,nullptr)==IcsIngestion::Status::InProgress);
        for(size_t i=0;i<static_cast<size_t>(headerBytes)+sizeof(body)-1;++i) {
            receive[0]=static_cast<uint8_t>(i<static_cast<size_t>(headerBytes)?header[i]:body[i-headerBytes]);
            assert(http.feed(receive,1)==IcsIngestion::Status::InProgress);
        }
        assert(http.finish()==IcsIngestion::Status::Complete);
        assert(parser.finish()==IcsStructure::Status::StructurallyComplete);
        assert(stage.finish(http,parser)==IcsSourceStage::Status::StagedSuccessfully);
        parser.release(); http.release();
        std::memset(httpStorage,0xee,768); std::memset(syntaxStorage,0xee,syntaxBytes);
        IcsSourceStage::View value;
        assert(stage.value(2,value)==IcsSourceStage::Read::Ok && value.size==11 && !std::memcmp(value.data,"independent",11));
        assert(stage.counts().observedProperties==6 && stage.counts().storedProperties==4 && stage.counts().ignoredOpaqueProperties==2);
        assert(stage.payload(2)==IcsSourceStage::Read::PayloadProjectedOut);
        stage.release(); std::memset(stageStorage,0xee,stageBytes);
        assert(stage.value(2,value)==IcsSourceStage::Read::InvalidState && !value.data);
    }
    for(size_t i=0;i<16;++i) {
        assert(storage[i]==0xa5 && stageStorage[stageBytes+i]==0xa5 && httpStorage[768+i]==0xa5 && syntaxStorage[syntaxBytes+i]==0xa5);
    }
    return GfxResult::success();
}
void sourceStageScratchLifetime() {
    const size_t attempts=psAttempts, freed=workspaceFrees, panels=panelAllocations;
    const int factories=factoryCalls;
    Workspace workspace; expect(workspace.allocate(frameBytes),GfxError::Ok);
    for(int cycle=0;cycle<3;++cycle) {
        uint8_t initial=0xa5, poison=0x7f;
        expect(workspace.withScratch(0,8192,fill,&initial),GfxError::Ok);
        expect(workspace.withScratch(0,8192,sourceStageScratch),GfxError::Ok);
        expect(workspace.withScratch(0,8192,fill,&poison),GfxError::Ok);
        checkGuards(*latest);
    }
    assert(psAttempts==attempts+1 && panelAllocations==panels && factoryCalls==factories);
    expect(workspace.release(),GfxError::Ok); assert(workspaceFrees==freed+1);
}
void failures() {
    const Fault faults[] = {Fault::Factory,Fault::Registration,Fault::BoardBegin,
        Fault::BusBegin,Fault::DriverBegin,Fault::InitBusy,Fault::Configuration,
        Fault::WakeBusy,Fault::RefreshBusy,Fault::Transport,Fault::ShutdownBusy,
        Fault::RefreshSleepBusy,Fault::Mirror};
    for (Fault f : faults) {
        fault=f; const size_t freeBefore=workspaceFrees;
        const bool retained=f==Fault::InitBusy || f==Fault::WakeBusy || f==Fault::RefreshBusy ||
            f==Fault::Transport || f==Fault::ShutdownBusy || f==Fault::RefreshSleepBusy;
        {
            Workspace w; expect(w.allocate(evaluationBytes),GfxError::Ok);
            const auto result=w.render(draw,&w);
            assert(!result.ok() && components==0 && facades==0 && !activePanel);
            const GfxError errors[] = {GfxError::AllocationFailed,GfxError::NotSupported,
                GfxError::BoardInitFailed,GfxError::BusInitFailed,GfxError::DriverInitFailed,
                GfxError::BusyTimeout,GfxError::NotSupported,GfxError::BusyTimeout,
                GfxError::BusyTimeout,GfxError::CommunicationFailed,GfxError::NotSupported,
                GfxError::BusyTimeout,GfxError::NotSupported};
            size_t index=0; while(faults[index]!=f) ++index;
            expect(result.operation,errors[index]);
            const bool attempted=f!=Fault::Factory && f!=Fault::Registration;
            assert(result.shutdownChecked==attempted);
            checkGuards(*latest);
            if (retained) {
                const GfxError error=f==Fault::Transport ? GfxError::CommunicationFailed : GfxError::BusyTimeout;
                expect(result.shutdown,error);
                assert(w.state()==Workspace::State::Retained && w.capacity()==evaluationBytes);
                const int calls=factoryCalls;
                expect(w.withScratch(0,1,noScratch),error); expect(w.release(),error);
                expect(w.allocate(frameBytes),error); expect(w.render(noDraw).operation,error);
                assert(factoryCalls==calls && workspaceFrees==freeBefore);
            } else {
                assert(w.state()==Workspace::State::Scratch);
                if (attempted) expect(result.shutdown,GfxError::Ok);
                // Successful cleanup cannot mask the preceding operation error.
                fault=Fault::None; assert(w.render(draw,&w).ok());
                expect(w.release(),GfxError::Ok);
            }
        }
        assert(workspaceFrees==freeBefore+(retained ? 0 : 1));
        if (retained) assert(latest->active); // Destruction must not free it.
    }
    fault=Fault::None;
}
}
int main() {
    static_assert(!std::is_copy_constructible<Workspace>::value,"single owner");
    static_assert(!std::is_move_constructible<Workspace>::value,"stable owner");
    allocationAndBounds(); patternsAndRepeatedRendering(); snapshotIndependence(); ingestionScratchLifetime(); structureScratchLifetime(); sourceStageScratchLifetime(); failures();
    assert(panelAllocations==0 && components==0 && facades==0 && refreshes>0);
    // Dispose the fake process/PSRAM heap after assertions. This is test teardown,
    // not a workspace force-release method or hardware recovery implementation.
    for (auto& a : records) if (a.active) { checkGuards(a); __real_free(a.raw); }
    std::puts("PASS: PSRAM workspace allocation, scratch, handoff, cleanup, retention, snapshot, ingestion and source stage");
}
