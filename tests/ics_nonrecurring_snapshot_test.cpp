#include "../application/ics_nonrecurring_snapshot.h"
#include "../application/five_day_posix_time.h"
#include "../application/psram_workspace.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace { bool audit = false; size_t allocations = 0, scenarios = 0; }
__attribute__((noinline)) void* operator new(size_t n) {
    if (audit) ++allocations;
    void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
namespace {
namespace N = IcsNonrecurringSnapshot;

namespace SL = IcsSourceLocal;
namespace T = IcsTypedValues;
namespace Z = IcsTimezoneLocal;
using Stage = IcsSourceStage;
using Code = Stage::Code;
using S = N::Status;

struct Audit {
    size_t before;
    Audit() : before(allocations) { assert(!audit); audit = true; }
    ~Audit() { audit = false; assert(before == allocations); }
};
struct Guard {
    std::vector<uint8_t> bytes; size_t capacity;
    explicit Guard(size_t n) : bytes(n + 32, 0xa5), capacity(n) {}
    uint8_t* data() { return bytes.data() + 16; }
    void check() const {
        for (size_t i = 0; i < 16; ++i) assert(bytes[i] == 0xa5);
        for (size_t i = 16 + capacity; i < bytes.size(); ++i) assert(bytes[i] == 0xa5);
    }
};
uint64_t clock(void*) { return 0; }
Stage::Limits storage() { return {128,1024,65536,128,16384}; }
IcsStructure::Limits syntax() { return {4096,32,32,64,8,64,4096,8192,128,8}; }
size_t stageBytes() { size_t n = 0; assert(Stage::requiredBytes(storage(), n) == Stage::Status::InProgress); return n; }
size_t syntaxBytes() { size_t n = 0; assert(IcsStructure::scratchBytes(syntax(), n) == IcsStructure::Status::InProgress); return n; }
struct Session {
    Guard data, parser, wire; Stage stage; IcsStructure structure; IcsIngestion http;
    Session() : data(stageBytes()), parser(syntaxBytes()), wire(6144) {
        Audit a;
        assert(stage.initialize(data.data(), data.capacity, storage()) == Stage::Status::InProgress);
        assert(structure.initialize(parser.data(), parser.capacity, syntax(), Stage::sink, &stage) == IcsStructure::Status::InProgress);
        const IcsIngestion::Limits b = {131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(wire.data(), wire.capacity, b, IcsStructure::lineSink, &structure, clock, nullptr) == IcsIngestion::Status::InProgress);
    }
    ~Session() { Audit a; stage.release(); structure.release(); http.release(); data.check(); parser.check(); wire.check(); }
};
std::string component(const std::string& kind, const std::string& fields) { return "BEGIN:" + kind + "\r\n" + fields + "END:" + kind + "\r\n"; }
std::string event(const std::string& fields = "DTSTART:20261023T090000Z\r\nSUMMARY:Appointment\r\n", const std::string& uid="event") {
    return component("VEVENT", "UID:" + uid + "\r\nDTSTAMP:20261017T120000Z\r\n" + fields);
}
std::string calendar(const std::string& children, const std::string& metadata="") {
    return component("VCALENDAR", "VERSION:2.0\r\nPRODID:synthetic\r\n" + metadata + children);
}
void parse(Session& s, const std::string& body, bool chunked = false, size_t split = SIZE_MAX) {
    std::string raw;
    if (!chunked) raw = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    else {
        raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
        for (size_t i = 0; i < body.size();) {
            const size_t n = std::min<size_t>(7, body.size() - i); char hex[24]; std::snprintf(hex, sizeof(hex), "%zx", n);
            raw += hex; raw += "\r\n"; raw.append(body, i, n); raw += "\r\n"; i += n;
        }
        raw += "0\r\n\r\n";
    }
    { Audit a;
        for (size_t i = 0; i < raw.size();) {
            const size_t n = std::min(split, raw.size() - i);
            const auto fed = s.http.feed(reinterpret_cast<const uint8_t*>(raw.data() + i), n);
            if (fed != IcsIngestion::Status::InProgress) std::fprintf(stderr, "fixture after %zu cases: ingestion %u, structure %u\n", scenarios, unsigned(fed), unsigned(s.structure.status()));
            assert(fed == IcsIngestion::Status::InProgress); i += n;
        }
        assert(s.http.finish() == IcsIngestion::Status::Complete);
        assert(s.structure.finish() == IcsStructure::Status::StructurallyComplete);
        assert(s.stage.finish(s.http, s.structure) == Stage::Status::StagedSuccessfully);
        s.http.release(); s.structure.release();
    }
    std::fill(raw.begin(), raw.end(), '!');
    std::fill(s.parser.data(), s.parser.data() + s.parser.capacity, 0xdd);
    std::fill(s.wire.data(), s.wire.data() + s.wire.capacity, 0xdd);
}
} // namespace
struct Allocation { uint8_t* raw; uint8_t* data; size_t size; bool live; };
static std::vector<Allocation> psram;
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p) {
    for (auto i=psram.rbegin(); i!=psram.rend(); ++i) if (i->data==p && i->live) {
        for(size_t j=0;j<16;++j) assert(i->raw[j]==0xa5 && i->data[i->size+j]==0xa5);
        i->live=false; __real_free(i->raw); return;
    }
    __real_free(p);
}
bool psramFound() { return true; }
void* ps_malloc(size_t n) {
    auto* p=static_cast<uint8_t*>(std::malloc(n+32)); assert(p);
    std::memset(p,0xa5,n+32); psram.push_back({p,p+16,n,true}); return p+16;
}
namespace {
using Snapshot=FiveDaySnapshot;
Snapshot::Window autumn() { return {{1792706400,1792792800,1792879200,1792969200,1793055600,1793142000},20749,0}; }
Snapshot::Window spring() { return {{1774566000,1774652400,1774738800,1774821600,1774908000,1774994400},20539,0}; }
void init(Snapshot& s, size_t records=32, size_t text=4096, Snapshot::Window w=autumn()) {
    const Snapshot::Text labels[]={{"Personal",8},{"Team",4}};
    assert(s.initialize(w,records,text,labels,2)==Snapshot::Result::Ok);
}
N::Limits limits() { return {{128,8,64,32,64,128,{128,128,4096},{128,128,4096,64,4096}},4096,4096}; }
N::Report checked(Session& s, Snapshot& snapshot, size_t source=0, N::Limits b=limits(), size_t scratchBytes=8192) {
    Guard temporary(scratchBytes); const auto original=s.data.bytes; const auto counts=s.stage.counts();
    const auto status=s.stage.status(); N::Report r;
    { Audit a; r=N::publish(s.stage,snapshot,source,b,temporary.data(),temporary.capacity,{s.data.data(),s.data.capacity}); }
    assert(original==s.data.bytes && status==s.stage.status());
    const auto after=s.stage.counts();
    assert(counts.components==after.components && counts.calendars==after.calendars && counts.observedProperties==after.observedProperties &&
        counts.storedProperties==after.storedProperties && counts.poolBytes==after.poolBytes && counts.ignoredOpaqueProperties==after.ignoredOpaqueProperties &&
        counts.reviewItems==after.reviewItems && counts.reviewBytes==after.reviewBytes);
    temporary.check(); s.data.check(); ++scenarios;
    if(r.status!=S::SourceCommitted) assert(!r.publishedOccurrences && !r.outsideWindowOccurrences);
    return r;
}
void expect(const N::Report& r, S status) {
    if(r.status!=status) std::fprintf(stderr,"scenario %zu: got %u expected %u, component %zu property %zu local %u event %u\n",scenarios,unsigned(r.status),unsigned(status),r.componentIndex,r.propertyIndex,unsigned(r.localReport.status),unsigned(r.localReport.eventStatus));
    assert(r.status==status);
}
std::string title(const Snapshot& s,size_t i) { const auto t=s.title(i); return std::string(t.data,t.size); }
void rolledBack(Snapshot& s,size_t source=0,size_t retained=0) {
    assert(s.source(source)->outcome==Snapshot::Outcome::Incomplete && s.occurrenceCount()==retained);
    // A new transaction on the other pending source proves no transaction leaked.
    const size_t other=1-source;
    if(s.source(other)->outcome==Snapshot::Outcome::Pending) {
        assert(s.beginSource(other)==Snapshot::Result::Ok);
        assert(s.failSource(other,Snapshot::Reason::Cancelled)==Snapshot::Result::Ok);
    }
}
std::string replace(std::string s,const std::string& a,const std::string& b) {
    const auto p=s.find(a); assert(p!=std::string::npos); s.replace(p,a.size(),b); return s;
}
std::string rows(const Seeed_GFX& gfx,int32_t y) {
    std::vector<Seeed_GFX::Glyph> g;
    for(const auto& c:gfx.glyphs) if(c.y==y) g.push_back(c);
    std::sort(g.begin(),g.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});
    std::string s; for(const auto& c:g) s+=char(c.code); return s;
}
bool has(const Seeed_GFX& gfx,const std::string& text) {
    for(const auto& g:gfx.glyphs) if(rows(gfx,g.y).find(text)!=std::string::npos) return true;
    return false;
}
void draw(Snapshot& s, const std::vector<std::string>& evidence) {
    const char* tz="CET-1CEST,M3.5.0,M10.5.0/3"; assert(!setenv("TZ",tz,1)); tzset();
    FiveDayPosixTime::Context zone={tz}; Seeed_GFX gfx;
    gfx.left=10; gfx.top=10; gfx.right=1035; gfx.bottom=1035;
    const auto report=FiveDayRenderer::render(gfx,s,{10,10,1025,1025},{FiveDayPosixTime::convert,&zone});
    assert(report.status==FiveDayRenderer::Status::Drawn);
    for(const auto& text:evidence) assert(has(gfx,text));
}
void basics() {
    Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\nDTEND:20261023T100000Z\r\nSUMMARY:Folded \r\n title\r\n")),true,1);
    Snapshot snapshot; init(snapshot); const auto r=checked(s,snapshot); expect(r,S::SourceCommitted);
    assert(r.publishedOccurrences==1 && !r.outsideWindowOccurrences);
    assert(snapshot.source(0)->outcome==Snapshot::Outcome::Complete && title(snapshot,0)=="Folded title");
    assert(snapshot.occurrence(0)->range.timed.start==1792746000 && snapshot.occurrence(0)->range.timed.end==1792749600);
    s.stage.release(); std::memset(s.data.data(),0xdd,s.data.capacity);
    Ee02PsramWorkspace workspace; assert(workspace.allocate(2097152).ok());
    assert(workspace.withScratch(0,workspace.capacity(),[](uint8_t* p,size_t n,void*) { std::memset(p,0xcc,n); return GfxResult::success(); }).ok());
    assert(title(snapshot,0)=="Folded title");
    draw(snapshot,{"Folded title","11:00 +0200","12:00 +0200","Pending - await source"});
    assert(workspace.release().ok());
}
void ranges() {
    const std::string fields[]={
        "DTSTART:20261027T160000Z\r\nSUMMARY:Last\r\n",
        "DTSTART:20261023T210000Z\r\nDTEND:20261023T230000Z\r\nSUMMARY:Midnight\r\n",
        "DTSTART;VALUE=DATE:20261023\r\nSUMMARY:Day\r\n",
        "DTSTART;VALUE=DATE:20261024\r\nDTEND;VALUE=DATE:20261027\r\nSUMMARY:Visit\r\n",
        "DTSTART:20261022T200000Z\r\nDTEND:20261028T010000Z\r\nSUMMARY:Window\r\n",
        "DTSTART;VALUE=DATE:20261020\r\nDTEND;VALUE=DATE:20261024\r\nSUMMARY:Date overlap\r\n"};
    std::string body; for(size_t i=0;i<6;++i) body+=event(fields[i],std::to_string(i));
    Session s; parse(s,calendar(body),false,3); Snapshot snap; init(snap);
    expect(checked(s,snap),S::SourceCommitted); assert(snap.occurrenceCount()==6);
    assert(snap.occurrence(0)->range.timed.start==snap.occurrence(0)->range.timed.end);
    assert(snap.occurrence(1)->range.timed.start==1792789200 && snap.occurrence(1)->range.timed.end==1792796400);
    assert(snap.occurrence(2)->kind==Snapshot::Kind::AllDay && snap.occurrence(2)->range.civil.start==20749 && snap.occurrence(2)->range.civil.end==20750);
    assert(snap.occurrence(3)->range.civil.start==20750 && snap.occurrence(3)->range.civil.end==20753);
    assert(snap.occurrence(4)->range.timed.start==1792699200 && snap.occurrence(4)->range.timed.end==1793149200);
    draw(snap,{"Midnight","All day","end exclusive","Visit","Continued","Window"});
    Session outside; parse(outside,calendar(event("DTSTART:20200101T000000Z\r\n")));
    Snapshot empty; init(empty); const auto r=checked(outside,empty); expect(r,S::SourceCommitted);
    assert(!r.publishedOccurrences && r.outsideWindowOccurrences==1 && empty.source(0)->outcome==Snapshot::Outcome::Complete);
    assert(empty.beginSource(1)==Snapshot::Result::Ok && empty.completeSource(1)==Snapshot::Result::Ok);
    draw(empty,{"No events - all sources complete"});
}
void dst() {
    for(bool fall:{false,true}) {
        const auto w=fall?autumn():spring();
        const std::string date=fall?"20261025":"20260329";
        Session s; parse(s,calendar(event("DTSTART:"+date+"T003000Z\r\nSUMMARY:First\r\n","a")+event("DTSTART:"+date+"T013000Z\r\nSUMMARY:Second\r\n","b")),fall,7);
        Snapshot snap; init(snap,32,4096,w);
        assert(!setenv("TZ","EST5EDT,M3.2.0,M11.1.0",1)); tzset(); // Producer must ignore this.
        expect(checked(s,snap),S::SourceCommitted);
        assert(snap.occurrence(0)->range.timed.start==(fall?1792888200:1774744200));
        assert(snap.occurrence(1)->range.timed.start==snap.occurrence(0)->range.timed.start+3600);
        draw(snap,fall?std::vector<std::string>{"02:30 +0200","02:30 +0100","First","Second"}:std::vector<std::string>{"01:30 +0100","03:30 +0200"});
    }
}
void textAndIdentity() {
    { Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\nSUMMARY:Comma\\, semi\\; slash\\\\ line\\nCafé 🌍\r\n")));
      Snapshot snap; init(snap); expect(checked(s,snap),S::SourceCommitted); assert(title(snap,0)=="Comma, semi; slash\\ line\nCafé 🌍"); }
    for(const std::string& summary:{std::string(""),std::string("SUMMARY:\r\n")}) {
        Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\n"+summary))); Snapshot snap; init(snap);
        expect(checked(s,snap),S::SourceCommitted); assert(snap.title(0).size==0);
    }
    for(const char* date:{"20261023T090000Z","20200101T000000Z"}) {
        Session s; const std::string fields="DTSTART:"+std::string(date)+"\r\n";
        parse(s,calendar(event(fields,"one\\nuid")+event(fields,"one\\Nuid")));
        Snapshot snap; init(snap); const auto r=checked(s,snap); expect(r,S::DuplicateUid);
        assert(r.componentIndex==2 && r.propertyCode==Code::Uid); rolledBack(snap);
    }
    { Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\n","UID")+event("DTSTART:20261023T090000Z\r\n","uid")));
      Snapshot snap; init(snap); expect(checked(s,snap),S::SourceCommitted); }
}
void failures() {
    const auto good=calendar(event());
    const std::vector<std::string> unsupported={
        calendar(event(),"METHOD:PUBLISH\r\n"), calendar(event(),"CALSCALE:JULIAN\r\n"),calendar(event(),"X-CALENDAR:any\r\n"),
        calendar(event(),"NAME:Title\r\n"),replace(good,"PRODID:synthetic","PRODID;LANGUAGE=en:synthetic"),
        replace(good,"SUMMARY:Appointment","SUMMARY;LANGUAGE=en:Appointment"),
        replace(good,"UID:event","UID;VALUE=TEXT:event"),replace(good,"DTSTAMP:","DTSTAMP;VALUE=DATE-TIME:"),
        calendar(event("DTSTART:20261023T090000\r\n")),calendar(event("DTSTART;TZID=Custom:20261023T090000\r\n")),
        good+good,calendar(component("VTODO","SUMMARY:Task\r\n")),
        calendar(event()+component("VTIMEZONE","TZID:X\r\n"+component("STANDARD","DTSTART:20261023T020000\r\nTZOFFSETFROM:+0000\r\nTZOFFSETTO:+0100\r\n"))),
        calendar(component("VEVENT","UID:e\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261023T090000Z\r\n"+component("VALARM","ACTION:DISPLAY\r\nTRIGGER:-PT5M\r\nDESCRIPTION:Alarm\r\n")))};
    for(const auto& body:unsupported) {
        Session s; parse(s,body); Snapshot snap; init(snap); expect(checked(s,snap),S::UnsupportedProfile); rolledBack(snap);
    }
    for(const char* field:{"RRULE:FREQ=DAILY","RDATE:20261024T090000Z","EXDATE:20261023T090000Z","RECURRENCE-ID:20261023T090000Z",
        "STATUS:CANCELLED","SEQUENCE:2","CREATED:20261017T120000Z","LAST-MODIFIED:20261017T120000Z","DESCRIPTION:Info","LOCATION:Here",
        "ORGANIZER:mailto:example.invalid","ATTENDEE:mailto:example.invalid","X-EXTRA:any","DURATION:PT1H"}) {
        Session s; parse(s,calendar(event("DTSTART:20200101T000000Z\r\n"+std::string(field)+"\r\n")));
        Snapshot snap; init(snap); expect(checked(s,snap),S::UnsupportedProfile); rolledBack(snap);
    }
    const std::vector<std::string> invalid={
        replace(good,"UID:event\r\n",""),replace(good,"DTSTAMP:20261017T120000Z\r\n",""),replace(good,"DTSTART:20261023T090000Z\r\n",""),
        replace(good,"20261023T090000Z","20260230T090000Z"),replace(good,"20261023T090000Z","20261023T250000Z"),
        replace(good,"SUMMARY:Appointment","SUMMARY:bad\\q"),
        calendar(event("DTSTART;VALUE=DATE:20261023\r\nDTEND;VALUE=DATE:20261023\r\n")),
        calendar(event("DTSTART:20261023T090000Z\r\nDTEND;VALUE=DATE:20261024\r\n")),
        calendar(event("DTSTART;TZID=X:20261023T090000Z\r\n")),
        replace(good,"SUMMARY:Appointment","SUMMARY:One\r\nSUMMARY:Two")};
    for(const auto& body:invalid) {
        Session s; parse(s,body); Snapshot snap; init(snap); const auto original=SL::inspect(s.stage,limits().source);
        const auto r=checked(s,snap); expect(r,S::LocalValidationFailure);
        assert(r.localReport.status==original.status && r.localReport.eventStatus==original.eventStatus && r.propertyIndex==original.propertyIndex); rolledBack(snap);
    }
    for(const char* field:{"DTSTART:20261023T090060Z\r\n","DTSTART:20261023T090000Z\r\nDTEND:20261023T100060Z\r\n"}) {
        Session s; parse(s,calendar(event(field))); Snapshot snap; init(snap); const auto r=checked(s,snap); expect(r,S::InvalidTemporalValue);
        assert(r.typedStatus==T::Status::NeedsContext); rolledBack(snap);
    }
    { Session s; parse(s,replace(good,"PRODID:synthetic","PRODID:bad\\q")); Snapshot snap; init(snap); expect(checked(s,snap),S::InvalidText); rolledBack(snap); }
    { Session s; parse(s,calendar(event(),"CALSCALE:gregorian\r\n")); Snapshot snap; init(snap); expect(checked(s,snap),S::SourceCommitted); }
    { Session s; parse(s,replace(good,"VERSION:2.0","VERSION:2.0;2.0")); Snapshot snap; init(snap); expect(checked(s,snap),S::SourceCommitted); }
}
void budgetsAndTransactions() {
    const auto body=calendar(event()+event("DTSTART:20261023T100000Z\r\nSUMMARY:Second\r\n","two"));
    for(int kind=0;kind<6;++kind) {
        Session s; parse(s,body); Snapshot snap; init(snap,kind==4?1:32,kind==5?28:4096); auto b=limits();
        if(kind==0) b.maxDecodedTextBytes=8;
        if(kind==1) b.maxUidComparisons=0;
        if(kind==2) b.source.maxEvents=1;
        const auto r=checked(s,snap,0,b,kind==3?8191:8192);
        expect(r,kind==2?S::LocalValidationFailure:S::ResourceLimit); rolledBack(snap);
        if(kind==4) assert(r.snapshotResult==Snapshot::Result::RecordCapacity && snap.source(0)->reason==Snapshot::Reason::RecordCapacity);
        if(kind==5) assert(r.snapshotResult==Snapshot::Result::TextCapacity && snap.source(0)->reason==Snapshot::Reason::TextCapacity);
    }
    { Session s; parse(s,body); Snapshot snap; init(snap); auto b=limits(); b.maxUidComparisons=1; b.maxDecodedTextBytes=11;
      expect(checked(s,snap,0,b,22),S::SourceCommitted); }
    { Session s; parse(s,body); Snapshot snap; init(snap); auto b=limits(); b.maxDecodedTextBytes=SIZE_MAX;
      expect(checked(s,snap,0,b,0),S::ResourceLimit); rolledBack(snap); }
    { Session s; parse(s,calendar(event())); Snapshot snap; init(snap);
      expect(checked(s,snap),S::SourceCommitted);
      Session later; parse(later,body); const auto r=checked(later,snap,1,limits(),1); expect(r,S::ResourceLimit);
      assert(snap.source(0)->outcome==Snapshot::Outcome::Complete && title(snap,0)=="Appointment"); rolledBack(snap,1,1);
      draw(snap,{"Appointment","Incomplete schedule","Team","producer failed"}); }
    { Session first; parse(first,calendar(event())); Snapshot snap; init(snap,2);
      expect(checked(first,snap),S::SourceCommitted);
      Session second; parse(second,body); const auto r=checked(second,snap,1); expect(r,S::ResourceLimit);
      assert(r.snapshotResult==Snapshot::Result::RecordCapacity && snap.source(0)->outcome==Snapshot::Outcome::Complete);
      assert(title(snap,0)=="Appointment"); rolledBack(snap,1,1); }
    { Session s; parse(s,body); Snapshot snap; init(snap);
      assert(snap.beginSource(1)==Snapshot::Result::Ok); expect(checked(s,snap),S::InvalidState);
      assert(snap.source(0)->outcome==Snapshot::Outcome::Pending);
      assert(snap.completeSource(1)==Snapshot::Result::Ok); // Conflicting transaction preserved.
      Guard scratch(8192); N::Report r;
      { Audit a; r=N::publish(s.stage,snap,0,limits(),s.data.data(),8192,{s.data.data(),s.data.capacity}); }
      expect(r,S::InvalidArgument); assert(snap.source(0)->outcome==Snapshot::Outcome::Pending);
      { Audit a; r=N::publish(s.stage,snap,0,limits(),scratch.data(),scratch.capacity,{s.data.data(),1}); }
      expect(r,S::InvalidArgument); rolledBack(snap);
    }
    { Session s; parse(s,body); Snapshot snap; init(snap); expect(checked(s,snap,9),S::InvalidArgument);
      assert(snap.source(0)->outcome==Snapshot::Outcome::Pending); expect(checked(s,snap),S::SourceCommitted);
      expect(checked(s,snap),S::InvalidState); }
}
void admissionAndEdges() {
    { Session s; parse(s,calendar(event())); Snapshot snap;
      expect(checked(s,snap),S::InvalidArgument); }
    { Session s; parse(s,calendar(event())); Snapshot snap; init(snap); Guard text(8192);
      N::Report r;
      { Audit a; r=N::publish(s.stage,snap,0,limits(),reinterpret_cast<uint8_t*>(UINTPTR_MAX-1),8192,{s.data.data(),s.data.capacity}); }
      expect(r,S::InvalidArgument); assert(snap.source(0)->outcome==Snapshot::Outcome::Pending); }
    { Session s; parse(s,calendar(event())); auto* c=const_cast<Stage::Component*>(s.stage.component(1));
      auto* p=const_cast<Stage::Property*>(s.stage.property(c->lastProperty)); p->next=c->firstProperty;
      Snapshot snap; init(snap); const auto r=checked(s,snap); expect(r,S::LocalValidationFailure);
      assert(r.localReport.eventStatus==IcsEventLocal::Status::InvalidAssociation); rolledBack(snap); }
    { Session s; parse(s,calendar(event())); const_cast<Stage::Component*>(s.stage.component(1))->parentOrdinal=0;
      Snapshot snap; init(snap); expect(checked(s,snap),S::LocalValidationFailure); rolledBack(snap); }
    { Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\nSUMMARY;LANGUAGE=en:Title\r\n")));
      auto* c=s.stage.component(1);
      auto* p=const_cast<Stage::Property*>(s.stage.property(c->lastProperty)); assert(p->code==Code::Summary);
      p->parameterCount=0; // Test-only descriptor corruption must not hide encoded parameters.
      Snapshot snap; init(snap); expect(checked(s,snap),S::UnsupportedProfile); rolledBack(snap); }
    { Session s; parse(s,calendar(event())); Snapshot snap; init(snap); auto b=limits(); b.maxDecodedTextBytes=0;
      expect(checked(s,snap,0,b,0),S::ResourceLimit); rolledBack(snap); }
    { Session s; parse(s,replace(calendar(event()),"PRODID:synthetic","PRODID:")); Snapshot snap; init(snap);
      expect(checked(s,snap),S::InvalidText); rolledBack(snap); }
    { Session s; parse(s,replace(calendar(event()),"DTSTAMP:20261017T120000Z","DTSTAMP:20261017T120060Z")); Snapshot snap; init(snap);
      expect(checked(s,snap),S::InvalidTemporalValue); rolledBack(snap); }
    { Session s; parse(s,calendar(event("DTSTART;VALUE=date-time:20261023T090000Z\r\nDTEND;VALUE=DATE-TIME:20261023T100000Z\r\n")));
      Snapshot snap; init(snap); expect(checked(s,snap),S::SourceCommitted); }
    { Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\n","long-identity"))); Snapshot snap; init(snap);
      auto b=limits(); b.maxDecodedTextBytes=11;
      const auto r=checked(s,snap,0,b,22); expect(r,S::ResourceLimit); assert(r.propertyCode==Code::Uid); rolledBack(snap); }
    { Session s; parse(s,calendar(event())); Snapshot snap; init(snap); auto b=limits(); b.maxUidComparisons=0;
      expect(checked(s,snap,0,b),S::SourceCommitted); }
    for(const char* end:{"20261023T090000Z","20261022T090000Z"}) {
      Session s; parse(s,calendar(event("DTSTART:20261023T090000Z\r\nDTEND:"+std::string(end)+"\r\n")));
      Snapshot snap; init(snap); expect(checked(s,snap),S::LocalValidationFailure); rolledBack(snap); }
    // Conversion includes the entire accepted Gregorian representation, not time_t.
    const T::Date epoch={1970,1,1}, previous={1969,12,31}, leap={2000,3,1}, century={1900,3,1}, first={0,1,1}, last={9999,12,31};
    int32_t day=0;
    assert(N::detail::civil(epoch,day) && day==0); assert(N::detail::civil(previous,day) && day==-1);
    assert(N::detail::civil(leap,day) && day==11017); assert(N::detail::civil(century,day) && day==-25508);
    assert(N::detail::civil(first,day) && day==-719528); assert(N::detail::civil(last,day) && day==2932896);
    int64_t utc=0; const T::DateTime pre={previous,23,59,59,T::TimeMode::Utc};
    assert(N::detail::utc(pre,utc) && utc==-1);
}
struct Pipeline { Snapshot* snapshot; IcsStructure::View raw; N::Report report; };
GfxResult pipeline(uint8_t* bytes,size_t capacity,void* context) {
    auto& data=*static_cast<Pipeline*>(context);
    const size_t a=stageBytes(), b=syntaxBytes(), wireBytes=6144, textBytes=8192;
    assert(a<=capacity && b<=capacity-a && wireBytes<=capacity-a-b && textBytes<=capacity-a-b-wireBytes);
    Stage stage; IcsStructure parser; IcsIngestion input;
    assert(stage.initialize(bytes,a,storage())==Stage::Status::InProgress);
    assert(parser.initialize(bytes+a,b,syntax(),Stage::sink,&stage)==IcsStructure::Status::InProgress);
    const IcsIngestion::Limits inputLimits={131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
    assert(input.initialize(bytes+a+b,wireBytes,inputLimits,IcsStructure::lineSink,&parser,clock,nullptr)==IcsIngestion::Status::InProgress);
    for(size_t i=0;i<data.raw.size;++i) assert(input.feed(data.raw.data+i,1)==IcsIngestion::Status::InProgress);
    assert(input.finish()==IcsIngestion::Status::Complete);
    assert(parser.finish()==IcsStructure::Status::StructurallyComplete);
    assert(stage.finish(input,parser)==Stage::Status::StagedSuccessfully);
    { Audit audit; data.report=N::publish(stage,*data.snapshot,0,limits(),bytes+a+b+wireBytes,textBytes,{bytes,a+b+wireBytes}); }
    input.release(); parser.release(); stage.release();
    return GfxResult::success();
}
void sharedWorkspace() {
    const std::string body=calendar(event("DTSTART:20261023T090000Z\r\nSUMMARY:Workspace title\r\n"));
    const auto raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    Snapshot snap; init(snap); Ee02PsramWorkspace workspace; assert(workspace.allocate(2097152).ok());
    Pipeline data={&snap,{reinterpret_cast<const uint8_t*>(raw.data()),raw.size()},N::Report{}};
    assert(workspace.withScratch(0,workspace.capacity(),pipeline,&data).ok());
    expect(data.report,S::SourceCommitted);
    assert(workspace.withScratch(0,workspace.capacity(),[](uint8_t* p,size_t n,void*) { std::memset(p,0xdc,n); return GfxResult::success(); }).ok());
    assert(title(snap,0)=="Workspace title" && data.report.publishedOccurrences==1);
    draw(snap,{"Workspace title","11:00 +0200"}); assert(workspace.release().ok());
}
void unavailable() {
    { Session s; Snapshot snap; init(snap); expect(checked(s,snap),S::Unavailable); rolledBack(snap); }
    { Session s; parse(s,calendar(event())); s.stage.release(); Snapshot snap; init(snap); expect(checked(s,snap),S::Unavailable); rolledBack(snap); }
    { Session s; const auto body=calendar(event("DTSTART:20261023T090000Z\r\nSUMMARY:"+std::string(1,char(0xc0))+"\r\n"));
      const auto raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
      s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size()); s.http.finish(); s.structure.finish();
      assert(s.structure.status()==IcsStructure::Status::InvalidText);
      assert(s.stage.finish(s.http,s.structure)!=Stage::Status::StagedSuccessfully);
      Snapshot snap; init(snap); expect(checked(s,snap),S::Unavailable); rolledBack(snap); }
    { Session s; auto body=calendar(event()); body.resize(body.size()-15);
      const auto raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
      assert(s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size())==IcsIngestion::Status::InProgress);
      assert(s.http.finish()==IcsIngestion::Status::Complete); assert(s.structure.finish()==IcsStructure::Status::IncompleteStructure);
      assert(s.stage.finish(s.http,s.structure)!=Stage::Status::StagedSuccessfully);
      Snapshot snap; init(snap); expect(checked(s,snap),S::Unavailable); rolledBack(snap); }
    // Unchanged structural grammar rejects a childless root, even when HTTP is complete.
    { Session s; const auto body=calendar(""); const auto raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
      s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size()); s.http.finish(); s.structure.finish();
      assert(s.structure.status()!=IcsStructure::Status::StructurallyComplete);
      assert(s.stage.finish(s.http,s.structure)!=Stage::Status::StagedSuccessfully);
      Snapshot snap; init(snap); expect(checked(s,snap),S::Unavailable); rolledBack(snap); }
    { Session s; const auto body=calendar(event()); const auto raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body.substr(0,body.size()-10);
      s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size()); assert(s.http.finish()!=IcsIngestion::Status::Complete);
      s.structure.finish(); assert(s.stage.finish(s.http,s.structure)!=Stage::Status::StagedSuccessfully);
      Snapshot snap; init(snap); expect(checked(s,snap),S::Unavailable); rolledBack(snap); }
}
}
int main() {
    basics(); ranges(); dst(); textAndIdentity(); failures(); budgetsAndTransactions(); admissionAndEdges(); sharedWorkspace(); unavailable();
    for(const auto& a:psram) assert(!a.live);
    std::printf("ICS nonrecurring snapshot: %zu composed scenarios passed\n",scenarios);
}
