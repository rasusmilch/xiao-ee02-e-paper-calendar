#include "../application/five_day_window.h"
#ifndef EE02_WINDOW_TIME_ONLY
#include "../application/five_day_posix_time.h"
#include "../application/ics_nonrecurring_snapshot.h"
#endif
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <new>
#include <string>
#include <vector>
namespace { bool tracking=false; size_t allocations=0,cases=0,psramCalls=0; }
__attribute__((noinline)) void* operator new(size_t n) {
    if(tracking)++allocations;
    void* p=std::malloc(n?n:1); if(!p)throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
extern "C" void __real_free(void*);
#ifdef EE02_WINDOW_FAULTS
extern "C" void* __real_malloc(size_t);
extern "C" void* __real_calloc(size_t,size_t);
extern "C" void* __real_realloc(void*,size_t);
extern "C" void* __wrap_malloc(size_t n) { if(tracking)++allocations;return __real_malloc(n); }
extern "C" void* __wrap_calloc(size_t a,size_t b) { if(tracking)++allocations;return __real_calloc(a,b); }
extern "C" void* __wrap_realloc(void* p,size_t n) { if(tracking)++allocations;return __real_realloc(p,n); }
namespace { enum Fault { None, LocalFailure, BadPredecessor, ChangedZone }; Fault fault=None; }
extern "C" struct tm* __real_localtime_r(const time_t*,struct tm*);
extern "C" time_t __real_mktime(struct tm*);
extern "C" struct tm* __wrap_localtime_r(const time_t* stamp,struct tm* out) {
    if(fault==LocalFailure)return nullptr;
    auto* p=__real_localtime_r(stamp,out);
    if(p && fault==BadPredecessor && *stamp==86399)++out->tm_mday;
    return p;
}
extern "C" time_t __wrap_mktime(struct tm* t) {
    const auto result=__real_mktime(t);
    if(fault==ChangedZone) {
        // Fixture mutation, excluded from the production allocation audit.
        const bool previous=tracking;tracking=false;
        assert(!setenv("TZ","JST-9",1));tzset();tracking=previous;
    }
    return result;
}
#endif
namespace {
using Window=FiveDaySnapshot::Window; using S=FiveDayWindow::Status;
struct Allocation { uint8_t* raw;uint8_t* data;size_t size;bool live; };
std::vector<Allocation> psram;
struct Audit { size_t before,ps;
    Audit():before(allocations),ps(psramCalls) { assert(!tracking);tracking=true; }
    ~Audit() { tracking=false;assert(before==allocations && ps==psramCalls); }
};
void configure(const char* zone) { assert(!setenv("TZ",zone,1));tzset(); }
S build(int64_t now,bool trusted,const char* zone,Window& out) {
    Audit a;++cases;return FiveDayWindow::build(now,trusted,zone,out);
}
struct Fixture { const char* zone;int64_t now;int32_t first;int64_t boundaries[6]; };
// Independent literal UTC expectations: civil calendar dates, specified offsets
// and known transition dates; never calculated with the builder or mktime.
const Fixture fixtures[]={
    {"UTC0",1792929600,20751,{1792886400,1792972800,1793059200,1793145600,1793232000,1793318400}}, // 2026-10-25
    {"UTC0",1792972800,20752,{1792972800,1793059200,1793145600,1793232000,1793318400,1793404800}}, // 2026-10-26
    {"JST-9",1792944000,20752,{1792940400,1793026800,1793113200,1793199600,1793286000,1793372400}}, // 2026-10-26
    {"PST8",1792980000,20751,{1792915200,1793001600,1793088000,1793174400,1793260800,1793347200}}, // 2026-10-25
    {"IST-5:30",1792958400,20752,{1792953000,1793039400,1793125800,1793212200,1793298600,1793385000}}, // 2026-10-26
    {"UTC0",1798711200,20818,{1798675200,1798761600,1798848000,1798934400,1799020800,1799107200}}, // 2026-12-31
    {"UTC0",1709121600,19781,{1709078400,1709164800,1709251200,1709337600,1709424000,1709510400}}, // 2024-02-28
    {"UTC0",1772193600,20511,{1772150400,1772236800,1772323200,1772409600,1772496000,1772582400}}, // 2026-02-27
    {"CET-1CEST,M3.5.0,M10.5.0/3",1792746000,20749,{1792706400,1792792800,1792879200,1792969200,1793055600,1793142000}}, // 2026-10-23
    {"CET-1CEST,M3.5.0,M10.5.0/3",1774612800,20539,{1774566000,1774652400,1774738800,1774821600,1774908000,1774994400}}, // 2026-03-27
    {"CST6CDT,M3.2.0/2,M11.1.0/2",1772884800,20519,{1772863200,1772949600,1773032400,1773118800,1773205200,1773291600}}, // 2026-03-07
    {"CST6CDT,M3.2.0/2,M11.1.0/2",1793448000,20757,{1793422800,1793509200,1793599200,1793685600,1793772000,1793858400}}, // 2026-10-31
    {"AEST-10AEDT-11,M10.1.0,M4.1.0/3",1798592400,20817,{1798549200,1798635600,1798722000,1798808400,1798894800,1798981200}}, // 2026-12-30
    {"LHST-10:30LHDT-11,M10.1.0/2,M4.1.0/2",1775264400,20547,{1775221200,1775307600,1775395800,1775482200,1775568600,1775655000}}, // 2026-04-04
    {"LHST-10:30LHDT-11,M10.1.0/2,M4.1.0/2",1790989200,20729,{1790947800,1791034200,1791118800,1791205200,1791291600,1791378000}}, // 2026-10-03
    {"STD0DST,M3.2.0/0,M11.1.0/1",1793493000,20758,{1793487600,1793577600,1793664000,1793750400,1793836800,1793923200}}, // 2026-11-01
    {"UTC0",1772366400,20513,{1772323200,1772409600,1772496000,1772582400,1772668800,1772755200}}, // 2026-03-01
};
void check(const Fixture& f,int64_t now) {
    configure(f.zone);const std::string original=std::getenv("TZ");Window out={};
    const auto status=build(now,true,f.zone,out);
    if(status!=S::Ok)std::fprintf(stderr,"fixture %zu status=%u\n",cases,unsigned(status));
    assert(status==S::Ok && out.firstCivilDay==f.first && !out.reserved);
    assert(std::equal(out.utcBoundaries,out.utcBoundaries+6,f.boundaries));
    assert(now>=out.utcBoundaries[0] && now<out.utcBoundaries[1]);
    assert(original==std::getenv("TZ"));
    Window again={};assert(build(now,true,f.zone,again)==S::Ok && !std::memcmp(&out,&again,sizeof(out)));
}
void ordinary() {
    for(const auto& f:fixtures) {
        check(f,f.now);check(f,f.boundaries[0]);check(f,f.boundaries[0]+1);check(f,f.boundaries[1]-1);
    }
    if(std::numeric_limits<time_t>::is_signed)
        check({"UTC0",-1,-1,{-86400,0,86400,172800,259200,345600}},-1);
    // Actual -1 POSIX instant is midnight here, not a mktime failure sentinel.
    if(std::numeric_limits<time_t>::is_signed)
        check({"LMT-0:00:01",0,0,{-1,86399,172799,259199,345599,431999}},0);
    assert(fixtures[8].boundaries[3]-fixtures[8].boundaries[2]==90000);
    assert(fixtures[9].boundaries[3]-fixtures[9].boundaries[2]==82800);
    assert(fixtures[13].boundaries[2]-fixtures[13].boundaries[1]==88200);
    assert(fixtures[14].boundaries[2]-fixtures[14].boundaries[1]==84600);
}
void failure(int64_t now,bool trusted,const char* zone,S expected) {
    Window out;std::memset(&out,0xa5,sizeof(out));const Window before=out;
    const auto status=build(now,trusted,zone,out);
    if(status!=expected)std::fprintf(stderr,"failure case %zu got=%u expected=%u\n",cases,unsigned(status),unsigned(expected));
    assert(status==expected && !std::memcmp(&out,&before,sizeof(out)));
}
void errors() {
    configure("UTC0");failure(0,false,"UTC0",S::UntrustedTime);
    failure(0,true,nullptr,S::MissingTimezone);failure(0,true,"",S::MissingTimezone);
    assert(!unsetenv("TZ"));tzset();failure(0,true,"UTC0",S::MissingTimezone);
    failure(0,false,nullptr,S::UntrustedTime);
    configure("JST-9");failure(0,true,"UTC0",S::TimezoneMismatch);
    configure("");failure(0,true,"UTC0",S::TimezoneMismatch);
    configure("UTC0");failure(INT64_MAX,true,"UTC0",S::ConversionFailure);
    failure(INT64_MIN,true,"UTC0",S::ConversionFailure);
    if(std::numeric_limits<time_t>::digits>=48) {
        failure((int64_t(INT32_MAX)+1)*86400,true,"UTC0",S::InvalidWindow);
        failure((int64_t(INT32_MAX)-4)*86400,true,"UTC0",S::InvalidWindow);
    }
    const char* skip="STD0DST,M3.2.0/0,M11.1.0/1";
    configure(skip);
    failure(1772971200,true,skip,S::UnsupportedLocalMidnight); // Mar 8, nonexistent 00:00.
    failure(1772798400,true,skip,S::UnsupportedLocalMidnight); // Mar 6; missing Day 2 midnight.
#ifdef EE02_WINDOW_FAULTS
    configure("UTC0");fault=LocalFailure;failure(0,true,"UTC0",S::ConversionFailure);fault=None;
    configure("UTC0");fault=BadPredecessor;failure(0,true,"UTC0",S::InvalidWindow);fault=None;
    configure("UTC0");fault=ChangedZone;failure(0,true,"UTC0",S::TimezoneMismatch);fault=None;
#endif
}
#ifndef EE02_WINDOW_TIME_ONLY
FiveDaySnapshot::Text text(const char* s) { return {s,std::strlen(s)}; }
void initialize(FiveDaySnapshot& s,const Window& w) {
    const auto label=text("Example");assert(s.initialize(w,16,4096,&label,1)==FiveDaySnapshot::Result::Ok);
    assert(s.beginSource(0)==FiveDaySnapshot::Result::Ok);
}
void occurrence(FiveDaySnapshot& s,int64_t a,int64_t b,const char* title,FiveDaySnapshot::Result expected=FiveDaySnapshot::Result::Ok) {
    FiveDaySnapshot::Input e={};e.kind=FiveDaySnapshot::Kind::Timed;e.range.timed={a,b};e.title=text(title);
    assert(s.addOccurrence(0,e)==expected);
}
std::string line(const Seeed_GFX& gfx,int32_t y) {
    std::vector<Seeed_GFX::Glyph> row;
    for(const auto& g:gfx.glyphs)if(g.y==y)row.push_back(g);
    std::sort(row.begin(),row.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});
    std::string out;for(const auto& g:row)out+=char(g.code);return out;
}
bool has(const Seeed_GFX& gfx,const char* wanted) { for(const auto& g:gfx.glyphs)if(line(gfx,g.y).find(wanted)!=std::string::npos)return true;return false; }
void draw(const FiveDaySnapshot& s,const char* zone,const char* a,const char* b) {
    Seeed_GFX gfx;gfx.left=10;gfx.top=10;gfx.right=1035;gfx.bottom=1035;
    FiveDayPosixTime::Context context={zone};
    const auto record=*s.occurrence(0);const auto title=s.title(0);const std::string before(title.data,title.size);
    assert(FiveDayRenderer::render(gfx,s,{10,10,1025,1025},{FiveDayPosixTime::convert,&context}).status==FiveDayRenderer::Status::Drawn);
    assert(!std::memcmp(&record,s.occurrence(0),sizeof(record)) && before==std::string(s.title(0).data,s.title(0).size));
    if(a)assert(has(gfx,a));
    if(b)assert(has(gfx,b));
}
void composed() {
    const auto& f=fixtures[8];configure(f.zone);Window w={};assert(build(f.now,true,f.zone,w)==S::Ok);
    FiveDaySnapshot s;initialize(s,w);
    occurrence(s,w.utcBoundaries[0]+60,w.utcBoundaries[0]+120,"First date");
    occurrence(s,w.utcBoundaries[4]+60,w.utcBoundaries[4]+120,"Fifth date");
    occurrence(s,w.utcBoundaries[0]-60,w.utcBoundaries[0]+60,"Overlap");
    occurrence(s,w.utcBoundaries[0]-120,w.utcBoundaries[0],"Before",FiveDaySnapshot::Result::OutsideWindow);
    occurrence(s,w.utcBoundaries[5],w.utcBoundaries[5],"After",FiveDaySnapshot::Result::OutsideWindow);
    occurrence(s,w.utcBoundaries[1],w.utcBoundaries[1],"Boundary point");
    FiveDaySnapshot::Input all={};all.kind=FiveDaySnapshot::Kind::AllDay;all.range.civil={20748,20752};all.title=text("Multiday");
    assert(s.addOccurrence(0,all)==FiveDaySnapshot::Result::Ok);
    all.range.civil={20748,20749};assert(s.addOccurrence(0,all)==FiveDaySnapshot::Result::OutsideWindow);
    occurrence(s,1792888200,1792889100,"First repeated");occurrence(s,1792891800,1792892700,"Second repeated");
    assert(s.completeSource(0)==FiveDaySnapshot::Result::Ok && s.occurrenceCount()==7);
    assert(s.occurrence(2)->range.timed.start==w.utcBoundaries[0]-60 && s.occurrence(4)->range.civil.end==20752);
    draw(s,f.zone,"02:30 +0200","02:30 +0100");
    Seeed_GFX gfx;FiveDayPosixTime::Context ctx={f.zone};
    assert(FiveDayRenderer::render(gfx,s,{10,10,1025,1025},{FiveDayPosixTime::convert,&ctx}).status==FiveDayRenderer::Status::Drawn);
    for(const char* h:{"2026-10-23","2026-10-24","2026-10-25","2026-10-26","2026-10-27","Fri","Sun","Tue"})assert(has(gfx,h));
    const auto& spring=fixtures[9];configure(spring.zone);assert(build(spring.now,true,spring.zone,w)==S::Ok);
    FiveDaySnapshot sp;initialize(sp,w);occurrence(sp,1774744200,1774747800,"Spring gap");assert(sp.completeSource(0)==FiveDaySnapshot::Result::Ok);
    draw(sp,spring.zone,"01:30 +0100","03:30 +0200");
}
uint64_t clock(void*) { return 0; }
void normalized() {
    const auto& f=fixtures[8];configure(f.zone);Window w={};assert(build(f.now,true,f.zone,w)==S::Ok);
    FiveDaySnapshot snapshot;const auto label=text("Published");assert(snapshot.initialize(w,8,4096,&label,1)==FiveDaySnapshot::Result::Ok);
    const std::string body="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\nBEGIN:VEVENT\r\nUID:one\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261023T090000Z\r\nDTEND:20261023T100000Z\r\nSUMMARY:Normalized title\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    const std::string raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    uint8_t live[24576]={},temporary[2048]={};size_t stageBytes=0,parserBytes=0;
    const IcsSourceStage::Limits st={16,64,8192,16,2048};
    const IcsStructure::Limits syntax={4096,32,32,64,8,64,128,512,16,1};
    assert(IcsSourceStage::requiredBytes(st,stageBytes)==IcsSourceStage::Status::InProgress);
    assert(IcsStructure::scratchBytes(syntax,parserBytes)==IcsStructure::Status::InProgress);
    assert(stageBytes+parserBytes+6144<=sizeof(live));
    {
        IcsSourceStage stage;IcsStructure parser;IcsIngestion http;
        assert(stage.initialize(live,stageBytes,st)==IcsSourceStage::Status::InProgress);
        assert(parser.initialize(live+stageBytes,parserBytes,syntax,IcsSourceStage::sink,&stage)==IcsStructure::Status::InProgress);
        const IcsIngestion::Limits input={32768,16384,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(live+stageBytes+parserBytes,6144,input,IcsStructure::lineSink,&parser,clock,nullptr)==IcsIngestion::Status::InProgress);
        for(size_t i=0;i<raw.size();++i)assert(http.feed(reinterpret_cast<const uint8_t*>(raw.data()+i),1)==IcsIngestion::Status::InProgress);
        assert(http.finish()==IcsIngestion::Status::Complete && parser.finish()==IcsStructure::Status::StructurallyComplete);
        assert(stage.finish(http,parser)==IcsSourceStage::Status::StagedSuccessfully);
        const IcsNonrecurringSnapshot::Limits limits={{32,1,16,0,0,8,{8,32,4096},{8,8,4096,32,4096}},1024,120};
        const auto result=IcsNonrecurringSnapshot::publish(stage,snapshot,0,limits,temporary,sizeof(temporary),{live,sizeof(live)});
        assert(result.status==IcsNonrecurringSnapshot::Status::SourceCommitted && result.publishedOccurrences==1);
    }
    std::memset(live,0xdd,sizeof(live));std::memset(temporary,0xea,sizeof(temporary));
    assert(snapshot.source(0)->outcome==FiveDaySnapshot::Outcome::Complete && snapshot.occurrence(0)->range.timed.start==1792746000);
    draw(snapshot,f.zone,"Normalized title","11:00 +0200");
}
#endif
}
extern "C" void __wrap_free(void* p) {
    for(auto it=psram.rbegin();it!=psram.rend();++it)if(it->live && it->data==p) {
        for(size_t j=0;j<16;++j)assert(it->raw[j]==0xa5 && it->data[it->size+j]==0xa5);
        it->live=false;__real_free(it->raw);return;
    }
    __real_free(p);
}
bool psramFound() { assert(!tracking);return true; }
void* ps_malloc(size_t n) {
    ++psramCalls;assert(!tracking);
    auto* p=static_cast<uint8_t*>(std::malloc(n+32));assert(p);std::memset(p,0xa5,n+32);psram.push_back({p,p+16,n,true});return p+16;
}
int main() {
    ordinary();errors();
#ifndef EE02_WINDOW_TIME_ONLY
    composed();normalized();
#endif
    for(const auto& a:psram)assert(!a.live);
    std::printf("PASS: %zu window builds, atomic failures, explicit TZ, DST/midnights and zero audited builder allocations\n",cases);
}
