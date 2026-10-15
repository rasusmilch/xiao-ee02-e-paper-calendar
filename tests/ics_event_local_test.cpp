#include "../application/ics_event_local.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
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
namespace E = IcsEventLocal;
using S = E::Status;
using Stage = IcsSourceStage;
using C = Stage::Code;
struct Audit {
    size_t before;
    Audit() : before(allocations) { assert(!audit); audit = true; }
    ~Audit() { audit = false; assert(before == allocations); }
};
struct Guard {
    std::vector<uint8_t> bytes; size_t capacity;
    explicit Guard(size_t n) : bytes(n+32,0xa5),capacity(n) {}
    uint8_t* data() { return bytes.data()+16; }
    void check() const {
        for (size_t i=0;i<16;++i) assert(bytes[i]==0xa5);
        for (size_t i=16+capacity;i<bytes.size();++i) assert(bytes[i]==0xa5);
    }
};
uint64_t clock(void*) { return 0; }
size_t stageBytes() { size_t n=0; assert(Stage::requiredBytes({128,1024,65536,128,16384},n)==Stage::Status::InProgress); return n; }
IcsStructure::Limits syntax() { return {4096,32,32,64,8,64,4096,8192,128,8}; }
size_t structureBytes() { size_t n=0; assert(IcsStructure::scratchBytes(syntax(),n)==IcsStructure::Status::InProgress); return n; }
struct Session {
    Guard data,syntaxData,httpData;
    Stage stage; IcsStructure structure; IcsIngestion http;
    Session() : data(stageBytes()),syntaxData(structureBytes()),httpData(6144) {
        Audit a;
        assert(stage.initialize(data.data(),data.capacity,{128,1024,65536,128,16384})==Stage::Status::InProgress);
        assert(structure.initialize(syntaxData.data(),syntaxData.capacity,syntax(),Stage::sink,&stage)==IcsStructure::Status::InProgress);
        const IcsIngestion::Limits limits={131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(httpData.data(),httpData.capacity,limits,IcsStructure::lineSink,&structure,clock,nullptr)==IcsIngestion::Status::InProgress);
    }
    ~Session() { Audit a; stage.release(); structure.release(); http.release(); data.check(); syntaxData.check(); httpData.check(); }
};
const std::string uid="UID:event\r\n", stamp="DTSTAMP:20261012T080000Z\r\n", start="DTSTART:20261012T090000Z\r\n";
const std::string required=uid+stamp+start;
std::string event(const std::string& p) { return "BEGIN:VEVENT\r\n"+p+"END:VEVENT\r\n"; }
std::string calendar(const std::string& p,const std::string& metadata="") {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\n"+metadata+p+"END:VCALENDAR\r\n";
}
std::string response(const std::string& body,bool chunked) {
    if (!chunked) return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    std::string r="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for (size_t i=0;i<body.size();) {
        const size_t n=std::min<size_t>(7,body.size()-i); char hex[24]; std::snprintf(hex,sizeof(hex),"%zx",n);
        r+=hex; r+="\r\n"; r.append(body,i,n); r+="\r\n"; i+=n;
    }
    return r+"0\r\n\r\n";
}
void parse(Session& s,const std::string& body,bool chunked=false,size_t step=SIZE_MAX) {
    std::string raw=response(body,chunked); Audit a;
    for (size_t i=0;i<raw.size();) {
        const size_t n=std::min(step,raw.size()-i);
        assert(s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()+i),n)==IcsIngestion::Status::InProgress); i+=n;
    }
    assert(s.http.finish()==IcsIngestion::Status::Complete);
    assert(s.structure.finish()==IcsStructure::Status::StructurallyComplete);
    assert(s.stage.finish(s.http,s.structure)==Stage::Status::StagedSuccessfully);
    s.http.release(); s.structure.release();
    std::memset(s.httpData.data(),0xdd,s.httpData.capacity); std::memset(s.syntaxData.data(),0xdd,s.syntaxData.capacity);
    std::fill(raw.begin(),raw.end(),'!');
}
E::Report checked(Session& s,size_t cal=0,size_t evt=1,E::Limits limits={128,1024,4096}) {
    const auto original=s.data.bytes;
    E::Report r;
    { Audit a; r=E::validate(s.stage,cal,evt,limits); }
    assert(original==s.data.bytes); s.data.check(); ++scenarios; return r;
}
void expect(E::Report r,S status,C code=C::Unknown,size_t index=Stage::None) {
    if (r.status!=status || r.propertyCode!=code || (index!=SIZE_MAX && r.propertyIndex!=index))
        std::fprintf(stderr,"scenario %zu expected %u/%u/%zu got %u/%u/%zu\n",scenarios,
            unsigned(status),unsigned(code),index,unsigned(r.status),unsigned(r.propertyCode),r.propertyIndex);
    assert(r.status==status && r.propertyCode==code);
    if (index!=SIZE_MAX) assert(r.propertyIndex==index);
    if (status==S::LocalChecksPassed)
        assert((r.deferred & (E::SourceValidationRequired|E::IdentityReconciliationPending))==
            (E::SourceValidationRequired|E::IdentityReconciliationPending));
    else assert(r.deferred==0);
}
size_t find(const Stage& stage,C code,size_t nth=0,size_t owner=1) {
    for (size_t i=0;i<stage.counts().storedProperties;++i) {
        const auto* p=stage.property(i);
        if (p->owner==owner && p->code==code) { if (!nth) return i; --nth; }
    }
    return Stage::None;
}
E::Report one(const std::string& properties,S expected=S::LocalChecksPassed,
              C code=C::Unknown,const std::string& metadata="") {
    Session s; parse(s,calendar(event(properties),metadata)); E::Report r=checked(s);
    expect(r,expected,code,expected==S::LocalChecksPassed || expected==S::MissingRequired ? size_t(Stage::None) : SIZE_MAX);
    return r;
}
void requiredAndPrecedence() {
    auto r=one(required); assert(r.deferred==(E::SourceValidationRequired|E::IdentityReconciliationPending));
    one("",S::MissingRequired,C::Uid);
    one(stamp+start,S::MissingRequired,C::Uid);
    one(uid,S::MissingRequired,C::Dtstamp);
    one(uid+start,S::MissingRequired,C::Dtstamp);
    one(uid+stamp,S::MissingRequired,C::Dtstart);
    one("UID:\r\n"+stamp+start,S::MalformedValue,C::Uid);
    one("UID:bad\\q\r\n"+stamp+start,S::MalformedValue,C::Uid);
    one("UID:"+std::string(300,'u')+"\xc3\xa9\r\n"+stamp+start);
    for (const auto& d : std::vector<std::pair<C,std::string>>{{C::Uid,uid},{C::Dtstamp,stamp},{C::Dtstart,start},
        {C::Summary,"SUMMARY:x\r\n"},{C::Status,"STATUS:CONFIRMED\r\n"},{C::Sequence,"SEQUENCE:1\r\n"},
        {C::RecurrenceId,"RECURRENCE-ID:20261012T090000Z\r\n"},{C::Created,"CREATED:20261012T080000Z\r\n"},
        {C::LastModified,"LAST-MODIFIED:20261012T080000Z\r\n"},{C::Description,"DESCRIPTION:x\r\n"},
        {C::Location,"LOCATION:x\r\n"},{C::Dtend,"DTEND:20261012T100000Z\r\n"},{C::Duration,"DURATION:PT1H\r\n"},
        {C::Class,"CLASS:PRIVATE\r\n"},{C::Geo,"GEO:1;2\r\n"},{C::Organizer,"ORGANIZER:mailto:a@example.invalid\r\n"},
        {C::Priority,"PRIORITY:1\r\n"},{C::Transp,"TRANSP:OPAQUE\r\n"},{C::Url,"URL:https://example.invalid\r\n"}}) {
        Session s; const bool inRequired=d.first==C::Uid || d.first==C::Dtstamp || d.first==C::Dtstart;
        parse(s,calendar(event(required+d.second+(inRequired ? "" : d.second))));
        expect(checked(s),S::DuplicateProperty,d.first,find(s.stage,d.first,1));
    }
    // B beats C/E, C beats D, D beats E, and E beats F.
    one("SUMMARY:x\r\nSUMMARY:y\r\n",S::DuplicateProperty,C::Summary);
    one(uid+"DTSTAMP:bad\r\nDTEND:bad\r\nDURATION:bad\r\n",S::MissingRequired,C::Dtstart);
    one("UID:bad\\q\r\n"+stamp+start+"DTEND:bad\r\nDURATION:bad\r\n",S::ConflictingProperties,C::Duration);
    one(required+"DTEND:20261012T080000Z\r\nSUMMARY:bad\\q\r\n",S::MalformedValue,C::Summary);
    Session s; parse(s,calendar(event(required+uid)));
    auto* p=const_cast<Stage::Property*>(s.stage.property(find(s.stage,C::Uid,1)));
    p->next=0;
    expect(checked(s),S::InvalidAssociation,C::Uid,find(s.stage,C::Uid,1));
}
void relationships() {
    one(required+"DTEND:20261012T100000Z\r\n");
    one(required+"DURATION:PT1H\r\n");
    one(required+"DTEND:20261012T100000Z\r\nDURATION:PT1H\r\n",S::ConflictingProperties,C::Duration);
    one(required+"DURATION:PT1H\r\nDTEND:20261012T100000Z\r\n",S::ConflictingProperties,C::Dtend);
    const std::string date=uid+stamp+"DTSTART;VALUE=DATE:20261012\r\n";
    one(date+"DTEND;VALUE=DATE:20261013\r\n");
    for (const char* end : {"20261012","20261011"})
        one(date+"DTEND;VALUE=DATE:"+end+"\r\n",S::InvalidRelationship,C::Dtend);
    one(date+"DTEND:20261013T090000Z\r\n",S::InvalidRelationship,C::Dtend);
    one(required+"DTEND;VALUE=DATE:20261013\r\n",S::InvalidRelationship,C::Dtend);
    one(date+"DURATION:P1D\r\n"); one(date+"DURATION:P1W\r\n");
    one(date+"DURATION:PT1H\r\n",S::InvalidRelationship,C::Duration);
    one(date+"DURATION:P1DT0H\r\n",S::InvalidRelationship,C::Duration);
    for (const char* d : {"-P1D","PT0S","P0D","-P0W"})
        one(required+"DURATION:"+d+"\r\n",S::InvalidRelationship,C::Duration);
    for (const char* end : {"20261012T090000Z","20261012T080000Z","20261012T100000"})
        one(required+"DTEND:"+end+"\r\n",S::InvalidRelationship,C::Dtend);
    one(uid+stamp+"DTSTART:20261012T090000\r\nDTEND:20261012T100000Z\r\n",S::InvalidRelationship,C::Dtend);
    for (const auto& times : std::vector<std::string>{
        "DTSTART:20261012T100000\r\nDTEND:20261012T090000\r\n",
        "DTSTART;TZID=A:20261012T090000\r\nDTEND;TZID=B:20261012T100000\r\n",
        "DTSTART:20260308T025500\r\nDTEND;TZID=A:20260308T030000\r\n"}) {
        const auto r=one(uid+stamp+times);
        assert((r.deferred & (E::TimezoneResolutionPending|E::LocalTimeOrderingPending))==
            (E::TimezoneResolutionPending|E::LocalTimeOrderingPending));
    }
    auto r=one(uid+stamp+"DTSTART:20161231T235960Z\r\nDTEND:20170101T000000Z\r\n");
    assert((r.deferred & E::LeapSecondPending) && (r.deferred & E::LocalTimeOrderingPending));
    assert(!(r.deferred & E::TimezoneResolutionPending));
    r=one(uid+"DTSTAMP:20161231T235960Z\r\n"+start); assert(r.deferred & E::LeapSecondPending);
}
void methodsAndTypes() {
    for (const char* method : {"CANCEL","X-UNSUPPORTED",""}) {
        auto r=one(uid+stamp,S::LocalChecksPassed,C::Unknown,std::string("METHOD:")+method+"\r\n");
        assert(r.deferred & E::MethodSemanticsPending);
    }
    auto r=one(uid+stamp+"DTEND:20261012T100000Z\r\n",S::LocalChecksPassed,C::Unknown,"METHOD:CANCEL\r\n");
    assert(r.deferred & E::LocalTimeOrderingPending);
    one(uid+stamp+"DURATION:PT1H\r\n",S::LocalChecksPassed,C::Unknown,"METHOD:CANCEL\r\n");
    one(uid+stamp+"DURATION:-PT1H\r\n",S::InvalidRelationship,C::Duration,"METHOD:CANCEL\r\n");
    r=one(uid+stamp+"STATUS:CONFIRMED\r\n",S::LocalChecksPassed,C::Unknown,"METHOD:CANCEL\r\n");
    assert(r.deferred & E::OptionalPropertyValidationPending); // No CANCEL/STATUS interpretation.
    Session s; parse(s,calendar(event(uid+stamp),"METHOD:CANCEL\r\n")+calendar(event(uid+stamp)));
    expect(checked(s),S::LocalChecksPassed);
    expect(checked(s,2,3),S::MissingRequired,C::Dtstart);
    expect(checked(s,0,3),S::InvalidAssociation);
    expect(checked(s,2,1),S::InvalidAssociation);
    one(uid+"DTSTAMP:bad\r\n"+start,S::MalformedValue,C::Dtstamp);
    one(uid+"DTSTAMP:20261012T080000\r\n"+start,S::MalformedValue,C::Dtstamp);
    one(uid+"DTSTAMP;TZID=A:20261012T080000\r\n"+start,S::InvalidParameter,C::Dtstamp);
    one(uid+stamp+"DTSTART:bad\r\n",S::MalformedValue,C::Dtstart);
    one(required+"DTEND;VALUE=INTEGER:1\r\n",S::InvalidValueType,C::Dtend);
    one(required+"RECURRENCE-ID;VALUE=PERIOD:20261012T090000Z/PT1H\r\n",S::InvalidValueType,C::RecurrenceId);
    one(required+"DTEND;VALUE=X-NEW:opaque\r\n",S::UnsupportedFeature,C::Dtend);
    one(required+"DTEND;VALUE=DATE-TIME;VALUE=DATE-TIME:20261012T100000Z\r\n",S::InvalidParameter,C::Dtend);
    one(required+"DURATION:PT1H2S\r\n",S::MalformedValue,C::Duration);
    one(required+"DURATION:P18446744073709551616D\r\n",S::ResourceLimit,C::Duration);
    one(required+"CREATED:bad\r\n",S::MalformedValue,C::Created);
    one(required+"LAST-MODIFIED:20261012T080000\r\n",S::MalformedValue,C::LastModified);
    r=one(required+"CREATED:20161231T235960Z\r\nLAST-MODIFIED:20161231T235960Z\r\n");
    assert(r.deferred & E::LeapSecondPending);
    for (const auto& p : std::vector<std::pair<C,std::string>>{{C::Summary,"SUMMARY"},
        {C::Description,"DESCRIPTION"},{C::Location,"LOCATION"}})
        one(required+p.second+":bad\\q\r\n",S::MalformedValue,p.first);
    one(required+"SUMMARY:\r\nDESCRIPTION:full\\nDetails\r\nLOCATION:Room\\; east\r\n");
    one(required+"SEQUENCE:-1\r\n"); // Primitive INTEGER, no revision policy.
    one(required+"SEQUENCE:2147483648\r\n",S::MalformedValue,C::Sequence);
}
void deferredAndIsolation() {
    for (const char* p : {"RRULE:FREQ=DAILY\r\n","RRULE:RSCALE=CHINESE;SKIP=FORWARD;FREQ=YEARLY\r\n",
        "RDATE:unparsed-list\r\n","EXDATE:unparsed-list\r\n","RECURRENCE-ID;RANGE=THISANDFUTURE:20261012T090000Z\r\n"}) {
        const auto r=one(required+p); assert(r.deferred & E::RecurrenceValidationPending);
    }
    auto r=one(required+"RRULE:FREQ=DAILY\r\nRRULE:FREQ=WEEKLY\r\n");
    assert(r.deferred & E::MultipleRrulePending);
    r=one(required+"X-UNKNOWN:complete\r\n"); assert(r.deferred & E::ExtensionReviewPending);
    r=one(uid+stamp+"DTSTART;X-UNKNOWN=value:20261012T090000Z\r\n"); assert(r.deferred & E::ExtensionReviewPending);
    r=one(required+"STATUS:WHATEVER\r\nGEO:unparsed\r\nURL:unparsed\r\n");
    assert(r.deferred & E::OptionalPropertyValidationPending);
    const std::string child="BEGIN:PARTICIPANT\r\nUID:child\r\nSUMMARY:child\r\nEND:PARTICIPANT\r\n";
    one(stamp+start+child,S::MissingRequired,C::Uid);
    r=one(required+child); assert(!(r.deferred & E::OptionalPropertyValidationPending));
    one(required+"SUMMARY:parent\r\n"+child);
    Session s; parse(s,calendar(event(stamp+start)+"BEGIN:X-OPAQUE\r\nUID:opaque\r\nEND:X-OPAQUE\r\n"));
    expect(checked(s),S::MissingRequired,C::Uid);
    r=one(required+"ATTACH:omitted\r\n");
    assert(!(r.deferred & E::OptionalPropertyValidationPending));
    r=one(required+"IMAGE;VALUE=URI:https://example.invalid/image\r\n",S::LocalChecksPassed,C::Unknown,"COLOR:blue\r\n");
    assert(r.deferred==(E::SourceValidationRequired|E::IdentityReconciliationPending));
    one(required+"ATTACH:a\r\nATTACH:b\r\nCOMMENT:a\r\nCOMMENT:b\r\n");
    const std::string repeated="ATTENDEE:mailto:a@example.invalid\r\nCATEGORIES:a\r\nCONTACT:a\r\n"
        "EXDATE:20261012T090000Z\r\nREQUEST-STATUS:2.0;success\r\nRELATED-TO:other\r\n"
        "RESOURCES:room\r\nRDATE:20261013T090000Z\r\n";
    one(required+repeated+repeated);
    r=one(required+"NAME:first\r\nNAME:second\r\n"); assert(r.deferred & E::ExtensionReviewPending);
    Session revisions;
    parse(revisions,calendar(event(required+"SEQUENCE:1\r\n")+event(required+"SEQUENCE:2\r\n")));
    expect(checked(revisions,0,1),S::LocalChecksPassed);
    expect(checked(revisions,0,2),S::LocalChecksPassed); // Same UID, no revision selection.
}
void admissionAndResources() {
    Session s; parse(s,calendar(event(required+"SUMMARY:title\r\n")));
    const auto base=checked(s); expect(base,S::LocalChecksPassed);
    expect(checked(s,100,1),S::InvalidArgument); expect(checked(s,0,SIZE_MAX),S::InvalidArgument);
    expect(checked(s,1,1),S::InvalidAssociation); expect(checked(s,0,0),S::InvalidAssociation);
    expect(checked(s,0,1,{2,4,5}),S::LocalChecksPassed);
    expect(checked(s,0,1,{1,4,5}),S::ResourceLimit,C::Prodid,1);
    expect(checked(s,0,1,{2,3,5}),S::ResourceLimit,C::Summary,5);
    expect(checked(s,0,1,{2,4,4}),S::ResourceLimit,C::Uid,2); // event length is five.
    expect(checked(s,0,1,{0,4,5}),S::ResourceLimit,C::Version,0);
    expect(checked(s,0,1,{2,0,5}),S::ResourceLimit,C::Uid,2);
    s.stage.release(); std::memset(s.data.data(),0xdd,s.data.capacity);
    expect(checked(s),S::Unavailable); // Preserved StagedSuccessfully is insufficient.
    Stage fresh;
    { Audit a; expect(E::validate(fresh,0,1,{2,4,5}),S::Unavailable); }
    // Deliberate test-only descriptor corruption exercises actual bounded readers.
    for (unsigned damage=0;damage<12;++damage) {
        Session broken; parse(broken,calendar(event(required)));
        auto* c=const_cast<Stage::Component*>(broken.stage.component(1));
        auto* p=const_cast<Stage::Property*>(broken.stage.property(c->firstProperty));
        switch (damage) {
        case 0: p->owner=0; break;
        case 1: p->next=Stage::None-1; break;
        case 2: p->next=c->firstProperty; break;
        case 3: c->lastProperty=c->firstProperty; break;
        case 4: ++c->observedProperties; break;
        case 5: c->firstProperty=Stage::None; break;
        case 6: c->closed=0; break;
        case 7: c->parentOrdinal=99; break;
        case 8: c->calendarOrdinal=99; break;
        case 9: const_cast<Stage::Component*>(broken.stage.component(0))->closed=0; break;
        case 10: c->ordinal=99; break;
        case 11: c->firstProperty=Stage::None-1; break;
        }
        assert(checked(broken).status==S::InvalidAssociation);
    }
    Session omitted; parse(omitted,calendar(event(required)));
    auto* p=const_cast<Stage::Property*>(omitted.stage.property(find(omitted.stage,C::Uid)));
    p->flags &= uint8_t(~Stage::ValueRetained);
    expect(checked(omitted),S::Unavailable,C::Uid,find(omitted.stage,C::Uid));
}
void fragments() {
    const std::string body=calendar(event(required+"SUMMARY:caf\xc3\r\n \xa9\r\nDURATION:PT1H\r\n"));
    for (bool chunked : {false,true}) for (size_t step : {size_t(1),size_t(7),size_t(31),SIZE_MAX}) {
        Session s; parse(s,body,chunked,step); expect(checked(s),S::LocalChecksPassed);
        expect(checked(s),S::LocalChecksPassed); // Stateless repeated calls.
    }
}
}
int main() {
    static_assert(std::is_pod<E::Report>::value && std::is_same<decltype(E::Report::deferred),uint32_t>::value,
        "bounded copied diagnostic report");
    requiredAndPrecedence(); relationships(); methodsAndTypes(); deferredAndIsolation(); admissionAndResources(); fragments();
    assert(allocations==0);
    std::printf("VEVENT local: %zu deterministic scenarios; zero audited allocations; report %zu/%zu\n",
        scenarios,sizeof(E::Report),alignof(E::Report));
}
