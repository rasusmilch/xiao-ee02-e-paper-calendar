#include "../application/five_day_posix_time.h"
#include "../application/psram_workspace.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <string>
#include <vector>

namespace F = FiveDayRenderer;
using Snapshot = FiveDaySnapshot;
namespace {
struct Allocation { uint8_t* raw; uint8_t* data; size_t size; bool live; };
std::vector<Allocation> allocations;
size_t cases=0;
void guards(const Allocation& a) {
    for(size_t i=0;i<16;++i) assert(a.raw[i]==0xa5 && a.data[a.size+i]==0xa5);
}
}
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p) {
    for(auto it=allocations.rbegin();it!=allocations.rend();++it) if(it->data==p && it->live) {
        guards(*it); it->live=false; __real_free(it->raw); return;
    }
    __real_free(p);
}
bool psramFound() { return true; }
void* ps_malloc(size_t n) {
    auto* raw=static_cast<uint8_t*>(std::malloc(n+32)); assert(raw);
    std::memset(raw,0xa5,n+32); allocations.push_back({raw,raw+16,n,true}); return raw+16;
}
namespace {
const char* tz="CET-1CEST,M3.5.0,M10.5.0/3";
FiveDayPosixTime::Context zone={tz};
F::TimeContext context() { return {FiveDayPosixTime::convert,&zone}; }
void configure() { assert(setenv("TZ",tz,1)==0); tzset(); }
Snapshot::Text text(const char* p) { return {p,std::strlen(p)}; }
Snapshot::Window autumn() { return {{1792706400,1792792800,1792879200,1792969200,1793055600,1793142000},20749,0}; }
Snapshot::Window spring() { return {{1774566000,1774652400,1774738800,1774821600,1774908000,1774994400},20539,0}; }
void init(Snapshot& s, Snapshot::Window w=autumn(), size_t sources=1) {
    const Snapshot::Text labels[]={text("Personal"),text("Team"),text("Pending source"),text("Other")};
    assert(s.initialize(w,128,32768,labels,sources)==Snapshot::Result::Ok);
}
void timed(Snapshot& s,int64_t a,int64_t b,const char* title,size_t source=0) {
    Snapshot::Input e={}; e.kind=Snapshot::Kind::Timed; e.range.timed={a,b}; e.title=text(title);
    assert(s.addOccurrence(source,e)==Snapshot::Result::Ok);
}
void allDay(Snapshot& s,int32_t a,int32_t b,const char* title) {
    Snapshot::Input e={}; e.kind=Snapshot::Kind::AllDay; e.range.civil={a,b}; e.title=text(title);
    assert(s.addOccurrence(0,e)==Snapshot::Result::Ok);
}
void complete(Snapshot& s,size_t source=0) { assert(s.completeSource(source)==Snapshot::Result::Ok); }
F::Region area={10,10,1025,1025};
F::Report draw(Seeed_GFX& gfx,const Snapshot& s,F::Region r=area,F::TimeContext c=context(),uint8_t scale=2) {
    gfx.left=r.x; gfx.top=r.y; gfx.right=r.x+r.width; gfx.bottom=r.y+r.height;
    const size_t count=s.occurrenceCount();
    std::vector<Snapshot::Occurrence> records;
    std::vector<std::string> titles;
    for(size_t i=0;i<count;++i) { records.push_back(*s.occurrence(i)); titles.emplace_back(s.title(i).data,s.title(i).size); }
    auto result=F::render(gfx,s,r,c,scale); ++cases;
    assert(count==s.occurrenceCount());
    for(size_t i=0;i<count;++i) {
        assert(!std::memcmp(&records[i],s.occurrence(i),sizeof(records[i])));
        assert(titles[i]==std::string(s.title(i).data,s.title(i).size));
    }
    for(const auto& a:allocations) if(a.live) guards(a);
    return result;
}
std::string lineAt(const Seeed_GFX& gfx,int32_t y,int column=-1) {
    std::vector<Seeed_GFX::Glyph> row;
    for(const auto& g:gfx.glyphs) if(g.y==y && (column<0 || (g.x>=14+column*205 && g.x<14+(column+1)*205))) row.push_back(g);
    std::sort(row.begin(),row.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});
    std::string result; for(const auto& g:row) result+=char(g.code); return result;
}
bool has(const Seeed_GFX& gfx,const std::string& value) {
    for(const auto& g:gfx.glyphs) if(lineAt(gfx,g.y).find(value)!=std::string::npos) return true;
    return false;
}
int32_t yOf(const Seeed_GFX& gfx,const std::string& value,int column=0) {
    for(const auto& g:gfx.glyphs) if(lineAt(gfx,g.y,column).find(value)!=std::string::npos) return g.y;
    return -1;
}
void basics() {
    Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok);
    const auto w=autumn();
    timed(s,w.utcBoundaries[0]+14*3600,w.utcBoundaries[0]+15*3600,"Late");
    timed(s,w.utcBoundaries[0]+9*3600,w.utcBoundaries[0]+10*3600,"Early");
    timed(s,w.utcBoundaries[0]+9*3600,w.utcBoundaries[0]+9*3600,"Point reminder");
    timed(s,w.utcBoundaries[0]+9*3600,w.utcBoundaries[0]+11*3600,"Overlapping");
    for(size_t d=1;d<5;++d) timed(s,w.utcBoundaries[d]+3600,w.utcBoundaries[d]+7200,"Daily");
    complete(s); Seeed_GFX gfx; const auto r=draw(gfx,s);
    assert(r.status==F::Status::Drawn && r.dayEntriesDrawn==8 && !r.dayEntriesOmitted);
    const char* names[]={"Fri","Sat","Sun","Mon","Tue"};
    for(int d=0;d<5;++d) { assert(lineAt(gfx,78,d)==names[d]); assert(lineAt(gfx,94,d)=="2026-10-"+std::to_string(23+d)); }
    assert(yOf(gfx,"Early")<yOf(gfx,"Point reminder") && yOf(gfx,"Point reminder")<yOf(gfx,"Overlapping") && yOf(gfx,"Overlapping")<yOf(gfx,"Late"));
    assert(has(gfx,"09:00 +0200") && has(gfx,"10:00 +0200") && has(gfx,"Point"));
    assert(has(gfx,"all sources complete") && s.source(0)->outcome==Snapshot::Outcome::Complete);
}
void spans() {
    Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok); const auto w=autumn();
    timed(s,w.utcBoundaries[1]-3600,w.utcBoundaries[1]+3600,"Cross midnight");
    timed(s,w.utcBoundaries[0]-3600,w.utcBoundaries[5]+3600,"Window span");
    allDay(s,20749,20750,"One date"); allDay(s,20750,20752,"Two dates"); allDay(s,20747,20750,"Outside start");
    complete(s); Seeed_GFX gfx; const auto r=draw(gfx,s);
    assert(r.status==F::Status::Drawn && r.dayEntriesDrawn==11 && !r.dayEntriesOmitted);
    assert(yOf(gfx,"One date",0)>=0 && yOf(gfx,"One date",1)<0);
    assert(yOf(gfx,"Two dates",1)>=0 && yOf(gfx,"Two dates",2)>=0 && yOf(gfx,"Two dates",3)<0);
    assert(yOf(gfx,"Outside start",0)>=0 && yOf(gfx,"Outside start",1)<0);
    assert(has(gfx,"All day") && has(gfx,"end exclusive") && has(gfx,"Continued"));
    assert(has(gfx,"2026-10-22") && has(gfx,"to 2026-10-28") && has(gfx,"23:00 +0200") && has(gfx,"01:00 +0200"));
}
void outcomes() {
    { Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok); complete(s); Seeed_GFX gfx;
      const auto r=draw(gfx,s); assert(r.status==F::Status::Drawn && r.completeSources==1 && !r.dayEntriesDrawn && has(gfx,"No events - all sources complete")); }
    { Snapshot s; init(s,autumn(),3); assert(s.beginSource(0)==Snapshot::Result::Ok);
      timed(s,1792746000,1792749600,"Available"); complete(s);
      assert(s.beginSource(1)==Snapshot::Result::Ok); assert(s.failSource(1,Snapshot::Reason::ProducerFailure)==Snapshot::Result::Ok);
      Seeed_GFX gfx; const auto r=draw(gfx,s); assert(r.completeSources==1 && r.incompleteSources==1 && r.pendingSources==1);
      assert(has(gfx,"Incomplete schedule") && has(gfx,"Available") && has(gfx,"Team") && has(gfx,"producer failed") && has(gfx,"Pending - await source")); }
    { Snapshot s; init(s,autumn(),4); for(size_t i=0;i<3;++i) { assert(s.beginSource(i)==Snapshot::Result::Ok); assert(s.failSource(i,Snapshot::Reason::Cancelled)==Snapshot::Result::Ok); }
      Seeed_GFX gfx; const auto r=draw(gfx,s); assert(r.incompleteSources==3 && r.pendingSources==1 && !r.dayEntriesDrawn);
      assert(has(gfx,"Calendar unavailable") && has(gfx,"retry") && has(gfx,"+2 other affected sources") && !has(gfx,"No events")); }
    { Snapshot s; init(s,autumn(),2); assert(s.beginSource(0)==Snapshot::Result::Ok); timed(s,1792746000,1792749600,"Source one"); complete(s);
      assert(s.beginSource(1)==Snapshot::Result::Ok); timed(s,1792742400,1792746000,"Source two",1); complete(s,1);
      Seeed_GFX gfx; const auto r=draw(gfx,s); assert(r.completeSources==2 && yOf(gfx,"Source two")<yOf(gfx,"Source one")); }
}
void daylightAndFailures() {
    { Snapshot s; init(s,spring()); assert(s.beginSource(0)==Snapshot::Result::Ok);
      timed(s,1774744200,1774747800,"Spring forward"); complete(s); Seeed_GFX gfx;
      assert(draw(gfx,s).status==F::Status::Drawn); assert(s.window()->utcBoundaries[3]-s.window()->utcBoundaries[2]==23*3600);
      assert(has(gfx,"01:30 +0100") && has(gfx,"03:30 +0200") && !has(gfx,"02:30")); }
    { Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok);
      timed(s,1792891800,1792891800,"Second fold"); timed(s,1792888200,1792888200,"First fold"); complete(s); Seeed_GFX gfx;
      assert(draw(gfx,s).status==F::Status::Drawn); assert(s.window()->utcBoundaries[3]-s.window()->utcBoundaries[2]==25*3600);
      assert(has(gfx,"02:30 +0200") && has(gfx,"02:30 +0100") && yOf(gfx,"First fold",2)<yOf(gfx,"Second fold",2)); }
    Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok); complete(s);
    { Seeed_GFX gfx; assert(draw(gfx,s,area,{nullptr,nullptr}).status==F::Status::InvalidTimeContext && gfx.glyphs.empty() && !gfx.rectangles); }
    { assert(setenv("TZ","UTC0",1)==0); tzset(); Seeed_GFX gfx; assert(draw(gfx,s).status==F::Status::InvalidTimeContext && gfx.glyphs.empty()); configure(); }
    { Snapshot bad; auto w=autumn(); ++w.firstCivilDay; init(bad,w); Seeed_GFX gfx; assert(draw(gfx,bad).status==F::Status::InvalidTimeContext && gfx.glyphs.empty()); }
    { Seeed_GFX gfx; assert(draw(gfx,s,{10,10,400,1025}).status==F::Status::InvalidRegion && gfx.glyphs.empty()); }
    { Seeed_GFX gfx; assert(draw(gfx,s,{1000,10,1025,1025}).status==F::Status::InvalidRegion && gfx.glyphs.empty()); }
    { Seeed_GFX gfx; assert(draw(gfx,s,{10,10,1025,100}).status==F::Status::InvalidRegion && gfx.glyphs.empty()); }
    { assert(unsetenv("TZ")==0); tzset(); Seeed_GFX gfx; assert(draw(gfx,s).status==F::Status::InvalidTimeContext && gfx.glyphs.empty()); configure(); }
    { Snapshot fresh; Seeed_GFX gfx; assert(draw(gfx,fresh).status==F::Status::InvalidSnapshot && gfx.glyphs.empty()); }
}
void textCapacityAndReuse() {
    Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok);
    const std::string longTitle=std::string(28,'L')+"\xc3\xa9\xf0\x9f\x8e\x89"+std::string(100,'Z');
    timed(s,1792746000,1792749600,longTitle.c_str());
    for(int i=0;i<20;++i) timed(s,1792749600+i*60,1792749600+i*60,"Overflow");
    complete(s);
    Seeed_GFX gfx; const auto r=draw(gfx,s,{10,10,1025,264});
    assert(r.status==F::Status::Drawn && r.dayEntriesDrawn==1 && r.dayEntriesOmitted==20 && r.titlesShortened==1 && has(gfx,"..."));
    assert(has(gfx,"+20 more events") && s.source(0)->outcome==Snapshot::Outcome::Complete && s.occurrenceCount()==21);
    { Snapshot unicode; init(unicode); assert(unicode.beginSource(0)==Snapshot::Result::Ok);
      timed(unicode,1792746000,1792749600,"Caf\xc3\xa9 \xf0\x9f\x8e\x89"); complete(unicode); Seeed_GFX canvas;
      const auto out=draw(canvas,unicode); assert(out.glyphsReplaced==2 && has(canvas,"Caf? ?")); }
    Ee02PsramWorkspace workspace; assert(workspace.allocate(2097152).ok());
    const auto poison=[](uint8_t* p,size_t n,void*) -> GfxResult { std::memset(p,0xdd,n); return GfxResult::success(); };
    assert(workspace.withScratch(0,workspace.capacity(),poison).ok()); Seeed_GFX after;
    const auto repeated=draw(after,s,{10,10,1025,264}); assert(repeated.dayEntriesDrawn==r.dayEntriesDrawn && repeated.dayEntriesOmitted==r.dayEntriesOmitted);
    assert(s.title(0).size==longTitle.size() && !std::memcmp(s.title(0).data,longTitle.data(),longTitle.size()));
    assert(workspace.release().ok());
}
void endpointsAndViewport() {
    Snapshot s; init(s); assert(s.beginSource(0)==Snapshot::Result::Ok); const auto w=autumn();
    timed(s,w.utcBoundaries[1]-1800,w.utcBoundaries[1],"Ends at midnight");
    timed(s,w.utcBoundaries[1],w.utcBoundaries[1],"Boundary point");
    timed(s,w.utcBoundaries[0]+9*3600+30,w.utcBoundaries[0]+9*3600+30,"Seconds point");
    allDay(s,w.firstCivilDay-1,w.firstCivilDay+7,"Whole window"); complete(s);
    Seeed_GFX gfx; gfx.setViewport(100,100,400,300,true); gfx.setOrigin(123,456);
    const auto r=draw(gfx,s);
    assert(r.status==F::Status::Drawn && r.dayEntriesDrawn==8);
    assert(yOf(gfx,"Ends at midnight",0)>=0 && yOf(gfx,"Ends at midnight",1)<0);
    assert(yOf(gfx,"Boundary point",0)<0 && yOf(gfx,"Boundary point",1)>=0);
    assert(has(gfx,"09:00:30 +0200") && has(gfx,"to 2026-10-30"));
    assert(gfx.getViewportX()==100 && gfx.getViewportY()==100 && gfx.getViewportWidth()==400 &&
        gfx.getViewportHeight()==300 && gfx.getViewportDatum() && gfx.getOriginX()==123 && gfx.getOriginY()==456);
    { Seeed_GFX small; assert(draw(small,s,{30,20,600,600},context(),1).dayEntriesDrawn==8); }
    { Seeed_GFX large; assert(draw(large,s,{20,30,1500,1100},context(),3).dayEntriesDrawn==8); }
    // A conversion failure only at an endpoint outside the window also fails
    // before any drawing; it cannot be hidden by visible-row capacity.
    Snapshot outside; init(outside); assert(outside.beginSource(0)==Snapshot::Result::Ok);
    timed(outside,w.utcBoundaries[0]-3600,w.utcBoundaries[0]+3600,"Unknown start"); complete(outside);
    const auto limited=[](int64_t utc,F::LocalTime& t,void* p) -> bool {
        return utc>=1792706400 && FiveDayPosixTime::convert(utc,t,p);
    };
    Seeed_GFX bad; assert(draw(bad,outside,area,{limited,&zone}).status==F::Status::InvalidTimeContext && bad.glyphs.empty() && !bad.rectangles);
    { Snapshot exact; init(exact); assert(exact.beginSource(0)==Snapshot::Result::Ok);
      const std::string title=std::string(29,'X')+"\xe2\x82\xacYZ"; // Exactly 32 codepoints, not 32 bytes.
      timed(exact,1792746000,1792749600,title.c_str()); complete(exact); Seeed_GFX canvas;
      const auto out=draw(canvas,exact); assert(!out.titlesShortened && out.glyphsReplaced==1 && has(canvas,"?YZ")); }
}
}
int main() {
    configure(); basics(); spans(); outcomes(); daylightAndFailures(); textCapacityAndReuse(); endpointsAndViewport();
    for(const auto& a:allocations) assert(!a.live);
    std::printf("Five-day renderer: %zu composed render checks; bounded graphics, explicit TZ/DST, source outcomes and scratch reuse passed\n",cases);
}
