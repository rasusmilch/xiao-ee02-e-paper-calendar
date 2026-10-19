#include "../application/five_day_calendar_refresh.h"
#include "../application/five_day_posix_time.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

namespace { bool auditing=false, failAllocation=false; size_t heapCalls=0, cases=0; }
__attribute__((noinline)) void* operator new(size_t n) {
    if(auditing) ++heapCalls;
    void* p=std::malloc(n?n:1); if(!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
namespace {
struct Allocation { uint8_t* raw; uint8_t* data; size_t size; bool live; };
std::vector<Allocation> allocations;
void guards(const Allocation& a) { for(size_t i=0;i<16;++i) assert(a.raw[i]==0xa5 && a.data[a.size+i]==0xa5); }
}
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p) {
    for(auto it=allocations.rbegin();it!=allocations.rend();++it) if(it->live && it->data==p) {
        guards(*it); it->live=false; __real_free(it->raw); return;
    }
    __real_free(p);
}
bool psramFound() { return true; }
void* ps_malloc(size_t n) {
    if(failAllocation) return nullptr;
    auto* p=static_cast<uint8_t*>(std::malloc(n+32)); assert(p);
    std::memset(p,0xa5,n+32); allocations.push_back({p,p+16,n,true}); return p+16;
}
namespace {
namespace F=FiveDayCalendarRefresh; namespace N=IcsHttpsSource; using Snap=FiveDaySnapshot;
const char* tz="CET-1CEST,M3.5.0,M10.5.0/3";
const char ca[]="synthetic CA";
template<size_t N> IcsHttpsSource::View view(const char(&s)[N]) { return {reinterpret_cast<const uint8_t*>(s),N-1}; }
Snap::Text text(const std::string& s) { return {s.data(),s.size()}; }
F::Limits limits() {
    return {64,128,32768,{2048,253,2048,4096,512,{1000,5,20},
        {131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,0,100},
        {4096,32,32,64,8,64,4096,8192,128,8},{128,1024,65536,128,16384},
        {{128,8,64,32,64,128,{128,128,4096},{128,128,4096,64,4096}},4096,4096}}};
}
std::string event(const std::string& uid="same",const std::string& title="Available",const std::string& times="DTSTART:20261023T090000Z\r\nDTEND:20261023T100000Z\r\n",const std::string& extra="") {
    return "BEGIN:VEVENT\r\nUID:"+uid+"\r\nDTSTAMP:20261017T120000Z\r\n"+times+"SUMMARY:"+title+"\r\n"+extra+"END:VEVENT\r\n";
}
std::string calendar(const std::string& events) {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:Synthetic\r\nMETHOD:PUBLISH\r\nX-WR-CALNAME:Example\r\n"+events+"END:VCALENDAR\r\n";
}
std::string response(const std::string& b,bool chunked=false) {
    if(!chunked) return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(b.size())+"\r\n\r\n"+b;
    std::string s="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for(size_t i=0;i<b.size();) { const size_t n=std::min<size_t>(7,b.size()-i); char h[20]; std::snprintf(h,sizeof(h),"%zx",n); s+=std::string(h)+"\r\n"+b.substr(i,n)+"\r\n"; i+=n; }
    return s+"0\r\n\r\n";
}
// Only byte transport is scripted. Every response goes through the actual
// acquisition, framing, parser, stage, source-local and semantic producer.
struct Feed {
    std::string wire,request,host; bool dns=true,tls=true,readError=false;
    explicit Feed(std::string s=response(calendar(event()))) : wire(std::move(s)) { request.reserve(8192);host.reserve(254); }
};
struct Network {
    std::vector<Feed> feeds; size_t resolves=0,connects=0,closes=0,pos=0,polls=0,index=0;
    bool live=false, stall=false; uint64_t now=0,slow=0; size_t fragment=17;
    Ee02PsramWorkspace* workspace;
    explicit Network(Ee02PsramWorkspace& w):workspace(&w) { feeds.reserve(64); }
    static uint64_t clock(void* p) { return static_cast<Network*>(p)->now; }
    static bool resolve(void* p,const char* h) {
        auto& n=*static_cast<Network*>(p); assert(!n.live && n.workspace->state()==Ee02PsramWorkspace::State::ScratchAccess);
        n.index=n.resolves++; assert(n.index<n.feeds.size()); n.pos=n.polls=0;
        n.feeds[n.index].host=h; n.now+=n.slow; return n.feeds[n.index].dns;
    }
    static bool connect(void* p,const char* h,uint16_t,const char* trust,bool time,const N::Timeouts&) {
        auto& n=*static_cast<Network*>(p); assert(!n.live && n.feeds[n.index].host==h && trust && time);
        ++n.connects; n.now+=n.slow; n.live=n.feeds[n.index].tls; return n.live;
    }
    static N::Io write(void* p,const uint8_t* data,size_t n) {
        auto& f=*static_cast<Network*>(p); assert(f.live); n=std::min<size_t>(n,3);
        f.feeds[f.index].request.append(reinterpret_cast<const char*>(data),n); return {N::IoStatus::Data,n};
    }
    static N::Io read(void* p,uint8_t* data,size_t capacity) {
        auto& n=*static_cast<Network*>(p); assert(n.live); ++n.polls; n.now+=2;
        auto& f=n.feeds[n.index]; if(f.readError) return {N::IoStatus::Error,0};
        if(n.stall || n.polls%3==0) return {N::IoStatus::WouldBlock,0};
        if(n.pos==f.wire.size()) return {N::IoStatus::Closed,0};
        const size_t take=std::min(capacity,std::min(n.fragment,f.wire.size()-n.pos));
        std::memcpy(data,f.wire.data()+n.pos,take); n.pos+=take; return {N::IoStatus::Data,take};
    }
    static void idle(void* p) { ++static_cast<Network*>(p)->now; }
    static void close(void* p) { auto& n=*static_cast<Network*>(p); n.live=false; ++n.closes; }
    N::Transport transport() { return {this,resolve,connect,write,read,idle,close}; }
};
struct Observed {
    F::SourceResult values[64]; size_t count=0; Ee02PsramWorkspace* workspace;
    static void callback(const F::SourceResult& r,void* p) {
        auto& o=*static_cast<Observed*>(p); assert(o.workspace->state()==Ee02PsramWorkspace::State::Scratch && o.count<64);
        o.values[o.count++]=r;
    }
};
F::Report run(Network& n,Snap& s,Ee02PsramWorkspace& w,const F::Source* sources,size_t count,F::Limits l=limits(),
    bool trusted=true,const char* timezone=tz,Observed* observed=nullptr,int64_t now=1792746000) {
    const auto transport=n.transport(); const size_t before=heapCalls, psramBefore=allocations.size(); auditing=true;
    const auto r=F::refresh(now,trusted,timezone,w,s,sources,count,l,true,
        {reinterpret_cast<const uint8_t*>(ca),sizeof(ca)},transport,Network::clock,&n,
        observed?Observed::callback:nullptr,observed);
    auditing=false; assert(before==heapCalls); ++cases; assert(!n.live);
    assert(allocations.size()==psramBefore+size_t(r.initialization==Snap::Result::Ok)); // One snapshot owner only.
    assert(w.state()==Ee02PsramWorkspace::State::Scratch || w.state()==Ee02PsramWorkspace::State::Unallocated);
    if(r.acquisitionComplete) assert(!r.pendingSources && r.error==F::Error::None);
    for(const auto& a:allocations) if(a.live) guards(a);
    return r;
}
std::string string(Snap::Text t) { return {t.data,t.size}; }
bool has(const Seeed_GFX& gfx,const std::string& wanted) {
    for(const auto& glyph:gfx.glyphs) {
        std::vector<Seeed_GFX::Glyph> row;
        for(const auto& g:gfx.glyphs) if(g.y==glyph.y) row.push_back(g);
        std::sort(row.begin(),row.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});
        std::string value; for(const auto& g:row) value+=char(g.code);
        if(value.find(wanted)!=std::string::npos) return true;
    }
    return false;
}
FiveDayRenderer::Report draw(Snap& s,Ee02PsramWorkspace& w,Seeed_GFX& gfx,int height=1025) {
    assert(w.withScratch(0,w.capacity(),[](uint8_t* p,size_t n,void*) { std::memset(p,0xdd,n); return GfxResult::success(); }).ok());
    FiveDayPosixTime::Context zone={tz}; gfx.left=10;gfx.top=10;gfx.right=1035;gfx.bottom=10+height;
    const auto r=FiveDayRenderer::render(gfx,s,{10,10,1025,height},{FiveDayPosixTime::convert,&zone});
    assert(r.status==FiveDayRenderer::Status::Drawn); return r;
}
F::Source source(const char* label="Personal",N::View url=view("https://first.example.invalid/one"),bool enabled=true) {
    return {enabled,{label,std::strlen(label)},url};
}
void combined() {
    Ee02PsramWorkspace w; assert(w.allocate(2097152).ok()); Network n(w); Snap s;
    const auto events=event("second","Second fold","DTSTART:20261025T013000Z\r\n")+
        event("first","First fold","DTSTART:20261025T003000Z\r\n")+
        event("cross","Cross midnight","DTSTART:20261023T210000Z\r\nDTEND:20261023T230000Z\r\n")+
        event("day","All day title","DTSTART;VALUE=DATE:20261023\r\n")+
        event("span","Multiple dates","DTSTART;VALUE=DATE:20261024\r\nDTEND;VALUE=DATE:20261027\r\n");
    n.feeds.emplace_back(response(calendar(events),true)); n.feeds.emplace_back(); n.feeds.back().tls=false; n.feeds.emplace_back();
    n.fragment=1; n.slow=1000000;
    std::string label="Personal";
    F::Source sources[]={ {true,text(label),view("https://first.example.invalid/one")},
        {false,{nullptr,SIZE_MAX},{nullptr,SIZE_MAX}},source("Work",view("https://second.example.invalid/two")),
        source("Personal",view("https://first.example.invalid/three")) };
    Observed o={}; o.workspace=&w;
    const auto r=run(n,s,w,sources,4,limits(),true,tz,&o);
    assert(r.status==F::Status::Partial && r.selectedSources==3 && r.attemptedSources==3 && r.completeSources==2 && r.incompleteSources==1);
    assert(r.configuredSources==4 && r.publishedOccurrences==6 && !r.outsideWindowOccurrences && o.count==3);
    assert(o.values[0].configurationIndex==0 && o.values[1].configurationIndex==2 && o.values[2].configurationIndex==3);
    for(size_t i=0;i<3;++i) assert(o.values[i].snapshotIndex==i && o.values[i].acquisitionInvoked);
    assert(o.values[1].acquisition.status==N::Status::TlsFailure && o.values[1].outcome==Snap::Outcome::Incomplete);
    assert(n.resolves==3 && n.connects==3 && n.closes==3 && n.now>6000000);
    assert(n.feeds[0].request.find("GET /one HTTP/1.1")!=std::string::npos && n.feeds[2].request.find("GET /three HTTP/1.1")!=std::string::npos && n.feeds[1].request.empty());
    assert(s.occurrence(0)->range.timed.start==1792891800 && s.occurrence(1)->range.timed.start==1792888200 && s.occurrence(5)->sourceIndex==2);
    const int64_t expected[]={1792706400,1792792800,1792879200,1792969200,1793055600,1793142000};
    for(size_t i=0;i<6;++i) assert(s.window()->utcBoundaries[i]==expected[i]);
    label.assign("changed"); assert(string(s.sourceLabel(0))=="Personal");
    Seeed_GFX gfx; const auto rendered=draw(s,w,gfx);
    assert(rendered.completeSources==2 && rendered.incompleteSources==1 && has(gfx,"Incomplete schedule") && has(gfx,"Work"));
    assert(has(gfx,"02:30 +0200") && has(gfx,"02:30 +0100") && has(gfx,"First fold") && has(gfx,"Second fold"));
    assert(has(gfx,"Cross midnight") && has(gfx,"All day") && has(gfx,"Multiple dates") && has(gfx,"2026-10-27"));
    assert(string(s.title(0))=="Second fold" && string(s.sourceLabel(0))=="Personal");
}
void selection() {
    for(int mode=0;mode<5;++mode) {
        Ee02PsramWorkspace w; assert(w.allocate(2097152).ok()); Network n(w); Snap s; Observed o={};o.workspace=&w;
        F::Source sources[]={source("First"),source("Bad",view("http://example.invalid")),source("Last")};
        size_t count=3;
        if(mode==0) { for(auto& f:sources) f.enabled=false; }
        if(mode==1) count=0;
        if(mode==2) { sources[1].enabled=false; n.feeds.emplace_back(); n.feeds.emplace_back(); }
        if(mode==3) { n.feeds.emplace_back(); n.feeds.emplace_back(); }
        if(mode==4) { sources[0].url={nullptr,1}; sources[2].url=view("broken"); }
        const auto r=run(n,s,w,sources,count,limits(),true,tz,&o);
        Seeed_GFX gfx; draw(s,w,gfx);
        if(mode<2) { assert(r.status==F::Status::NoSelection && !r.attemptedSources && !s.sourceCount() && !n.resolves && !o.count); assert(has(gfx,"No calendars selected") && !has(gfx,"unavailable")); }
        if(mode==2) { assert(r.status==F::Status::AllComplete && r.completeSources==2 && s.occurrenceCount()==2 && n.resolves==2 && o.values[1].configurationIndex==2); assert(has(gfx,"all sources complete")); }
        if(mode==3) { assert(r.status==F::Status::Partial && r.completeSources==2 && r.incompleteSources==1 && n.resolves==2 && o.count==3); assert(o.values[1].acquisition.status==N::Status::InvalidUrl); }
        if(mode==4) { assert(r.status==F::Status::NoneComplete && r.incompleteSources==3 && !n.resolves && !s.occurrenceCount()); assert(o.values[0].acquisition.status==N::Status::InvalidUrl && has(gfx,"Calendar unavailable") && !has(gfx,"No events")); }
    }
    { Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
      for(size_t i=0;i<3;++i) n.feeds.emplace_back();
      F::Source sources[]={source("Same"),source("Same"),source("Same")};
      const auto r=run(n,s,w,sources,3);assert(r.status==F::Status::AllComplete && r.completeSources==3 && r.attemptedSources==3 && r.publishedOccurrences==3);
      Seeed_GFX gfx;draw(s,w,gfx);assert(has(gfx,"all sources complete") && !has(gfx,"Incomplete schedule")); }
    { Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
      for(size_t i=0;i<3;++i) { n.feeds.emplace_back();n.feeds.back().tls=false; }
      F::Source sources[]={source("A"),source("B"),source("C")};
      const auto r=run(n,s,w,sources,3);assert(r.status==F::Status::NoneComplete && r.incompleteSources==3 && r.attemptedSources==3 && !r.publishedOccurrences && n.resolves==3);
      Seeed_GFX gfx;draw(s,w,gfx);assert(has(gfx,"Calendar unavailable") && !has(gfx,"No events")); }
}
void failures() {
    std::vector<std::string> invalid={
        response(calendar(event("x","recur","DTSTART:20261023T090000Z\r\n","RRULE:FREQ=DAILY\r\n"))),
        response(calendar(event("x","zone","DTSTART;TZID=Custom:20261023T090000\r\n"))),
        response(calendar(event("x","cancel","DTSTART:20261023T090000Z\r\n","STATUS:CANCELLED\r\n"))),
        response(calendar(event()+event())),response("invalid\r\n"),"HTTP/1.1 503 Error\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 200 OK\r\nContent-Length: 99999\r\n\r\n"+calendar(event())};
    for(size_t f=0;f<invalid.size()+3;++f) {
        Ee02PsramWorkspace w;assert(w.allocate(2097152).ok()); Network n(w);Snap s;
        n.feeds.emplace_back(); n.feeds.emplace_back(f<invalid.size()?invalid[f]:response(calendar(event()))); n.feeds.emplace_back();
        if(f==invalid.size()) n.feeds[1].dns=false;
        if(f==invalid.size()+1) n.feeds[1].tls=false;
        if(f==invalid.size()+2) n.feeds[1].readError=true;
        F::Source sources[]={source("First"),source("Failed"),source("Final")};
        const auto r=run(n,s,w,sources,3);
        assert(r.status==F::Status::Partial && r.completeSources==2 && r.incompleteSources==1 && r.attemptedSources==3 && s.occurrenceCount()==2);
        assert(s.occurrence(0)->sourceIndex==0 && s.occurrence(1)->sourceIndex==2 && s.source(1)->outcome==Snap::Outcome::Incomplete);
        assert(string(s.title(0))=="Available" && s.release()==Snap::Result::Ok); // No dangling active transaction.
    }
    { Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
      n.feeds.emplace_back(response(calendar(event("x","Outside","DTSTART:20261001T090000Z\r\n"))));
      const auto a=source();const auto r=run(n,s,w,&a,1);Seeed_GFX gfx;draw(s,w,gfx);
      assert(r.status==F::Status::AllComplete && !r.publishedOccurrences && r.outsideWindowOccurrences==1 && has(gfx,"No events - all sources complete")); }
    for(bool textLimit:{false,true}) {
        Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
        n.feeds.emplace_back();n.feeds.emplace_back(response(calendar(event("a","Early tentative")+event("b","Late tentative"))));
        F::Source sources[]={source("A"),source("B")};auto l=limits();if(textLimit) l.textCapacity=22;else l.recordCapacity=2;
        const auto r=run(n,s,w,sources,2,l);assert(r.status==F::Status::Partial && s.occurrenceCount()==1 && s.source(1)->outcome==Snap::Outcome::Incomplete);
        assert(string(s.title(0))=="Available" && s.release()==Snap::Result::Ok);
    }
}
void admission() {
    for(int fault=0;fault<15;++fault) {
        Ee02PsramWorkspace w;if(fault!=8) assert(w.allocate(2097152).ok());Network n(w);Snap s;
        F::Source sources[]={source(),source("Other")};auto l=limits();const char* zone=tz;bool trusted=true;
        if(fault==0) trusted=false;
        if(fault==1) zone="UTC0";
        if(fault==2) zone=nullptr;
        if(fault==3) zone="";
        if(fault==4) l.maxConfiguredSources=1;
        if(fault==5) sources[0].label={"",0};
        if(fault==6) failAllocation=true;
        if(fault==7) l.textCapacity=1;
        if(fault==9) l.acquisition.ingestion.headerFields=0;
        if(fault==10) l.acquisition.stage.poolBytes=2097152;
        if(fault==11) sources[0].label={"\xc0\xaf",2};
        if(fault==12) { assert(!unsetenv("TZ"));tzset(); }
        if(fault==13) l.acquisition.ingestion.processingMs=1;
        if(fault==14) l.acquisition.producer.maxDecodedTextBytes=SIZE_MAX;
        const auto r=run(n,s,w,sources,2,l,trusted,zone);failAllocation=false;
        if(fault==12) { assert(!setenv("TZ",tz,1));tzset(); }
        assert(!n.resolves && !n.connects && !r.acquisitionComplete);
        if(fault==9 || fault==10 || fault==13 || fault==14) { assert(r.status==F::Status::Interrupted && r.attemptedSources==1 && r.pendingSources==2 && r.acquisitionDiagnosticActive); }
        else assert(r.status==F::Status::AdmissionFailure && !s.initialized());
    }
    // A conflicting caller transaction is refused without touching it.
    { Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);n.feeds.emplace_back();Snap s;
      F::Source sources[]={source(),source("Other")};auto r=run(n,s,w,sources,0);assert(r.status==F::Status::NoSelection);
      assert(s.release()==Snap::Result::Ok);FiveDaySnapshot::Window window={};assert(FiveDayWindow::build(1792746000,true,tz,window)==FiveDayWindow::Status::Ok);
      Snap::Text labels[]={{"Active",6}};assert(s.initialize(window,2,100,labels,1)==Snap::Result::Ok && s.beginSource(0)==Snap::Result::Ok);
      r=run(n,s,w,sources,2);assert(r.error==F::Error::InvalidState && !n.resolves && s.source(0)->outcome==Snap::Outcome::Pending);
      assert(s.completeSource(0)==Snap::Result::Ok); }
    // Descriptors/labels may not be borrowed workspace aliases.
    for(bool descriptor:{false,true}) {
        Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;F::Source a=source();uint8_t* escaped=nullptr;
        assert(w.withScratch(0,w.capacity(),[](uint8_t* p,size_t,void* v){*static_cast<uint8_t**>(v)=p;return GfxResult::success();},&escaped).ok());
        if(descriptor) { new (escaped) F::Source(a); }
        else { std::memcpy(escaped,"Label",5);a.label={reinterpret_cast<char*>(escaped),5}; }
        const auto r=run(n,s,w,descriptor?reinterpret_cast<F::Source*>(escaped):&a,1);assert(r.status==F::Status::AdmissionFailure && !n.resolves && !s.initialized());
    }
    { Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
      const auto a=source();auto l=limits();const auto transport=n.transport();
      struct Context { Ee02PsramWorkspace& w; Snap& s; const F::Source& a; const F::Limits& l; const N::Transport& t; Network& n; } c={w,s,a,l,transport,n};
      assert(w.withScratch(0,w.capacity(),[](uint8_t*,size_t,void* p) {
          auto& c=*static_cast<Context*>(p);
          const auto r=F::refresh(1792746000,true,tz,c.w,c.s,&c.a,1,c.l,true,{reinterpret_cast<const uint8_t*>(ca),sizeof(ca)},c.t,Network::clock,&c.n);
          assert(r.error==F::Error::InvalidState && !c.s.initialized() && !c.n.resolves);
          return GfxResult::success(); },&c).ok()); }
}
void prerequisitesAndStall() {
    for(int mode=0;mode<4;++mode) {
        Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;n.feeds.emplace_back();const auto a=source();const auto t=n.transport();const auto l=limits();
        n.stall=mode==3;
        const auto r=F::refresh(1792746000,true,tz,w,s,&a,1,l,mode!=0,
            mode==1?N::View{nullptr,0}:N::View{reinterpret_cast<const uint8_t*>(ca),sizeof(ca)},t,mode==2?nullptr:Network::clock,&n);
        ++cases;
        if(mode==2) { assert(r.status==F::Status::Interrupted && r.pendingSources==1 && !n.resolves); }
        else { assert(r.status==F::Status::NoneComplete && r.incompleteSources==1 && !r.pendingSources && !n.live);
          if(mode==3) assert(n.resolves==1 && n.now>=l.acquisition.ingestion.stalledMs);
          else assert(!n.resolves); }
    }
}
void labelCapacity() {
    // Exact temporary descriptor capacity and the next selected label. These
    // are arithmetic evaluation cases, not suggested calendar operating counts.
    const size_t count=Ee02PsramWorkspace::FrameBufferBytes/sizeof(Snap::Text);
    if(count>size_t(UINT16_MAX)+1) return;
    std::vector<F::Source> sources(count+1,source("A"));
    for(size_t extra=0;extra<2;++extra) {
        Ee02PsramWorkspace w;assert(w.allocate(Ee02PsramWorkspace::FrameBufferBytes).ok());Network n(w);Snap s;
        auto l=limits();l.maxConfiguredSources=count+1;l.recordCapacity=0;l.textCapacity=2*(count+1);
        l.acquisition.ingestion.headerFields=0; // Exact labels accepted, but no network should start.
        const auto r=run(n,s,w,sources.data(),count+extra,l);assert(!n.resolves);
        if(!extra) assert(r.initialization==Snap::Result::Ok && r.status==F::Status::Interrupted && r.pendingSources==count);
        else assert(r.error==F::Error::Workspace && r.status==F::Status::AdmissionFailure && !s.initialized());
    }
    { Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
      alignas(F::Source) uint8_t bytes[sizeof(F::Source)+1]={};
      const auto r=run(n,s,w,reinterpret_cast<const F::Source*>(bytes+1),1);
      assert(r.status==F::Status::AdmissionFailure && !s.initialized() && !n.resolves); }
}
void overflowAndLocalDate() {
    Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Network n(w);Snap s;
    std::string many;for(size_t i=0;i<12;++i) many+=event("e"+std::to_string(i),"Overflow event");
    for(size_t i=0;i<5;++i) { n.feeds.emplace_back(i?"HTTP/1.1 500 Error\r\n\r\n":response(calendar(many))); }
    F::Source sources[]={source("A"),source("B"),source("C"),source("D"),source("E")};
    const auto r=run(n,s,w,sources,5,limits(),true,tz,nullptr,1792708200); // UTC Oct22, local Oct23.
    assert(r.status==F::Status::Partial && s.window()->firstCivilDay==20749);
    Seeed_GFX gfx;const auto out=draw(s,w,gfx,264);assert(out.dayEntriesOmitted && has(gfx,"more events") && has(gfx,"+2 other affected sources"));
}
}
int main() {
    allocations.reserve(512);assert(!setenv("TZ",tz,1));tzset();
    combined();selection();failures();admission();prerequisitesAndStall();labelCapacity();overflowAndLocalDate();
    for(const auto& a:allocations) assert(!a.live);
    std::printf("Calendar refresh: %zu real-pipeline cases; guarded storage, mapping, rollback, rendering and zero audited new allocations passed\n",cases);
}
