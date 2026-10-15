#include "../application/ics_timezone_local.h"
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
namespace E = IcsTimezoneLocal;
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
const std::string onset="DTSTART:20261012T020000\r\n";
const std::string offsets="TZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\n";
const std::string required=onset+offsets;
std::string observance(const std::string& p,const std::string& kind="STANDARD") {
    return "BEGIN:"+kind+"\r\n"+p+"END:"+kind+"\r\n";
}
std::string timezone(const std::string& p="TZID:Test\r\n",const std::string& children=observance(required)) {
    return "BEGIN:VTIMEZONE\r\n"+p+children+"END:VTIMEZONE\r\n";
}
std::string calendar(const std::string& p) {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\n"+p+"END:VCALENDAR\r\n";
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
E::Limits limits() { return {128,128,4096,64,4096}; }
E::Report checked(Session& s,bool zone=true,size_t parent=0,size_t child=1,E::Limits budget=limits()) {
    const auto original=s.data.bytes;
    E::Report r;
    { Audit a; r=zone ? E::validateTimezone(s.stage,parent,child,budget) : E::validateObservance(s.stage,parent,child,budget); }
    assert(original==s.data.bytes); s.data.check(); ++scenarios; return r;
}
void expect(E::Report r,S status,C code=C::Unknown,size_t index=Stage::None) {
    if (r.status!=status || r.propertyCode!=code || (index!=SIZE_MAX && r.propertyIndex!=index))
        std::fprintf(stderr,"scenario %zu expected %u/%u/%zu got %u/%u/%zu\n",scenarios,
            unsigned(status),unsigned(code),index,unsigned(r.status),unsigned(r.propertyCode),r.propertyIndex);
    assert(r.status==status && r.propertyCode==code);
    if (index!=SIZE_MAX) assert(r.propertyIndex==index);
    if (status==S::LocalChecksPassed)
        assert((r.deferred & (E::SourceValidationRequired|E::TransitionResolutionPending))==
            (E::SourceValidationRequired|E::TransitionResolutionPending));
    else assert(r.deferred==0);
}
size_t find(const Stage& stage,C code,size_t nth=0,size_t owner=1) {
    const auto* c=stage.component(owner); assert(c);
    for (uint32_t i=c->firstProperty;i!=Stage::None;i=stage.property(i)->next) {
        if (stage.property(i)->code==code) { if (!nth) return i; --nth; }
    }
    return Stage::None;
}
E::Report one(const std::string& p,bool zone,S status=S::LocalChecksPassed,C code=C::Unknown) {
    Session s; parse(s,calendar(zone ? timezone(p) : timezone("TZID:Test\r\n",observance(p))));
    auto r=checked(s,zone,zone ? 0 : 1,zone ? 1 : 2);
    expect(r,status,code,status==S::LocalChecksPassed || status==S::MissingRequired ? size_t(Stage::None) : SIZE_MAX);
    return r;
}
void cardinality() {
    auto r=one("TZID:Test\r\n",true);
    assert(r.deferred==(E::SourceValidationRequired|E::TransitionResolutionPending|E::TimezoneIdentityReconciliationPending));
    r=one(required,false); assert(r.deferred==(E::SourceValidationRequired|E::TransitionResolutionPending));
    one("",true,S::MissingRequired,C::Tzid);
    one("",false,S::MissingRequired,C::Dtstart);
    one(onset,false,S::MissingRequired,C::Tzoffsetfrom);
    one(onset+"TZOFFSETFROM:-0500\r\n",false,S::MissingRequired,C::Tzoffsetto);
    one(offsets,false,S::MissingRequired,C::Dtstart);
    one(onset+"TZOFFSETTO:-0400\r\n",false,S::MissingRequired,C::Tzoffsetfrom);
    for (bool zone : {true,false}) {
        const std::vector<std::pair<C,std::string>> fields=zone ?
            std::vector<std::pair<C,std::string>>{{C::Tzid,"TZID:Test\r\n"},
                {C::LastModified,"LAST-MODIFIED:20261012T080000Z\r\n"},{C::Tzurl,"TZURL:opaque\r\n"}} :
            std::vector<std::pair<C,std::string>>{{C::Dtstart,onset},{C::Tzoffsetfrom,"TZOFFSETFROM:-0500\r\n"},
                {C::Tzoffsetto,"TZOFFSETTO:-0400\r\n"}};
        for (const auto& field : fields) {
            Session s;
            const std::string base=zone ? "TZID:Test\r\n" : required;
            const bool inBase=!zone || field.first==C::Tzid;
            const std::string p=base+field.second+(inBase ? "" : field.second);
            parse(s,calendar(zone ? timezone(p) : timezone("TZID:Test\r\n",observance(p))));
            expect(checked(s,zone,zone ? 0 : 1,zone ? 1 : 2),S::DuplicateProperty,field.first,
                find(s.stage,field.first,1,zone ? 1 : 2));
        }
    }
    // Integrity before cardinality, cardinality before presence, presence before values.
    one("LAST-MODIFIED:bad\r\nLAST-MODIFIED:bad\r\n",true,S::DuplicateProperty,C::LastModified);
    one("LAST-MODIFIED:bad\r\n",true,S::MissingRequired,C::Tzid);
    one("TZOFFSETFROM:bad\r\nTZOFFSETFROM:bad\r\n",false,S::DuplicateProperty,C::Tzoffsetfrom);
    one("DTSTART:bad\r\n",false,S::MissingRequired,C::Tzoffsetfrom);
    for (const std::string kind : {"STANDARD","DAYLIGHT"}) {
        Session s; parse(s,calendar(timezone("TZID:Test\r\n",observance(required,kind))));
        expect(checked(s),S::LocalChecksPassed); expect(checked(s,false,1,2),S::LocalChecksPassed);
    }
    Session s; parse(s,calendar(timezone("TZID:Test\r\n",observance(required)+observance(required,"DAYLIGHT"))));
    expect(checked(s),S::LocalChecksPassed); expect(checked(s,false,1,2),S::LocalChecksPassed);
    expect(checked(s,false,1,3),S::LocalChecksPassed);
}
void typedValues() {
    for (const std::string id : {"plain","America/Chicago","/example.invalid/Custom","My-Zone","caf\xc3\xa9"})
        one("TZID:"+id+"\r\n",true);
    one("TZID:"+std::string(600,'x')+"\xc3\xa9\r\n",true);
    one("TZID:\r\n",true,S::MalformedValue,C::Tzid);
    one("TZID:bad\\q\r\n",true,S::MalformedValue,C::Tzid);
    one("TZID:raw,comma\r\n",true,S::MalformedValue,C::Tzid);
    auto r=one("TZID:Test\r\nLAST-MODIFIED:20261012T080000Z\r\n",true);
    assert(!(r.deferred&E::LeapSecondPending));
    r=one("TZID:Test\r\nLAST-MODIFIED:20261012T080060Z\r\n",true); assert(r.deferred&E::LeapSecondPending);
    for (const std::string p : {"LAST-MODIFIED:20261012T080000\r\n","LAST-MODIFIED:20261012\r\n",
        "LAST-MODIFIED:bad\r\n"}) one("TZID:Test\r\n"+p,true,S::MalformedValue,C::LastModified);
    one("TZID:Test\r\nLAST-MODIFIED;VALUE=DATE:20261012\r\n",true,S::InvalidValueType,C::LastModified);
    one("TZID:Test\r\nLAST-MODIFIED;TZID=Test:20261012T080000\r\n",true,S::InvalidParameter,C::LastModified);
    one("DTSTART;VALUE=DATE-TIME:20261012T020000\r\n"+offsets,false);
    for (const std::string v : {"20261012T020000Z","20261012","20260229T020000","20261012T240000","bad"})
        one("DTSTART:"+v+"\r\n"+offsets,false,S::MalformedValue,C::Dtstart);
    one("DTSTART;VALUE=DATE:20261012\r\n"+offsets,false,S::InvalidValueType,C::Dtstart);
    one("DTSTART;VALUE=X-NEW:x\r\n"+offsets,false,S::UnsupportedFeature,C::Dtstart);
    one("DTSTART;TZID=Test:20261012T020000\r\n"+offsets,false,S::InvalidParameter,C::Dtstart);
    one("DTSTART;VALUE=DATE-TIME;VALUE=DATE-TIME:20261012T020000\r\n"+offsets,false,S::InvalidParameter,C::Dtstart);
    r=one("DTSTART:20261012T020060\r\n"+offsets,false); assert(r.deferred&E::LeapSecondPending);
    for (const std::string value : {"+0100","-0130","+014530","+0000","+000000","+235959","-235959"}) {
        one(onset+"TZOFFSETFROM:"+value+"\r\nTZOFFSETTO:"+value+"\r\n",false);
    }
    for (const std::string value : {"-0000","-000000","+2400","+0060","+000060","+0100x","0100","","+100"}) {
        one(onset+"TZOFFSETFROM:"+value+"\r\nTZOFFSETTO:-0400\r\n",false,S::MalformedValue,C::Tzoffsetfrom);
        one(onset+"TZOFFSETFROM:-0500\r\nTZOFFSETTO:"+value+"\r\n",false,S::MalformedValue,C::Tzoffsetto);
    }
    // Selected typed checks follow retained input order, not required-field order.
    one("TZOFFSETTO:bad\r\nDTSTART:bad\r\nTZOFFSETFROM:bad\r\n",false,S::MalformedValue,C::Tzoffsetto);
    one("LAST-MODIFIED:bad\r\nTZID:bad\\q\r\n",true,S::MalformedValue,C::LastModified);
}
void recurrenceAndReview() {
    for (const std::string rule : {"FREQ=YEARLY;BYMONTH=3;BYDAY=2SU","FREQ=YEARLY;RSCALE=HEBREW;SKIP=FORWARD","bad"}) {
        auto r=one(required+"RRULE:"+rule+"\r\n",false);
        assert((r.deferred&E::RecurrenceValidationPending) && !(r.deferred&E::MultipleRrulePending));
    }
    auto r=one(required+"RRULE:FREQ=YEARLY\r\nRRULE:bad\r\n",false);
    assert((r.deferred&E::MultipleRrulePending) && (r.deferred&E::RecurrenceValidationPending));
    for (const std::string p : {"RDATE:20261013T020000\r\n","RDATE:20261013T020000,20261014T020000\r\n",
        "RDATE;VALUE=DATE-TIME:20261013T020000\r\nRDATE:20261013T020000\r\n"}) {
        r=one(required+p,false); assert(r.deferred&E::RecurrenceValidationPending);
    }
    r=one(required+"RDATE:20261013T020060\r\n",false); assert(r.deferred&E::LeapSecondPending);
    for (const std::string v : {"","20261013T020000,",",20261013T020000","20261013T020000,,20261014T020000",
        "20261013T020000,bad","20261013","20261013T020000Z","20260229T020000"})
        one(required+"RDATE:"+v+"\r\n",false,S::MalformedValue,C::Rdate);
    one(required+"RDATE;VALUE=DATE:20261013\r\n",false,S::InvalidValueType,C::Rdate);
    one(required+"RDATE;VALUE=PERIOD:20261013T020000/20261013T030000\r\n",false,S::InvalidValueType,C::Rdate);
    one(required+"RDATE;VALUE=X-TIME:opaque\r\n",false,S::UnsupportedFeature,C::Rdate);
    one(required+"RDATE;TZID=Test:20261013T020000\r\n",false,S::InvalidParameter,C::Rdate);
    one(required+"RDATE;VALUE=DATE-TIME,DATE-TIME:20261013T020000\r\n",false,S::InvalidParameter,C::Rdate);
    r=one(required+"EXDATE:uninterpreted\r\n",false);
    assert((r.deferred&E::ObservanceExdateReviewPending) && (r.deferred&E::ExtensionReviewPending));
    assert(!(r.deferred&E::RecurrenceValidationPending));
    r=one("TZID:Test\r\nTZURL:unparsed\r\n",true); assert(r.deferred&E::OptionalPropertyValidationPending);
    for (bool zone : {true,false}) {
        r=one((zone ? std::string("TZID:Test\r\n") : required)+"X-UNKNOWN;X-P=^^n:raw\\q\r\n",zone);
        assert(r.deferred&E::ExtensionReviewPending);
    }
    r=one("TZID;VALUE=TEXT:Test\r\n",true); assert(r.deferred&E::ExtensionReviewPending);
    r=one(onset+"TZOFFSETFROM;VALUE=UTC-OFFSET:-0500\r\nTZOFFSETTO;X-P=x:-0400\r\n",false);
    assert(r.deferred&E::ExtensionReviewPending);
    r=one("DTSTART;X-P=^^n:20261012T020000\r\n"+offsets,false); assert(r.deferred&E::ExtensionReviewPending);
    r=one(required+"RDATE;X-P=x:20261013T020000\r\n",false); assert(r.deferred&E::ExtensionReviewPending);
    r=one(required+"TZNAME:ignored\\q\r\nCOMMENT:ignored\\q\r\nTZNAME:again\r\n",false);
    assert(r.deferred==(E::SourceValidationRequired|E::TransitionResolutionPending));
    r=one(required+"TZNAME;X-P=x:retained\r\nCOMMENT;X-P=x:retained\r\n",false);
    assert((r.deferred&E::ExtensionReviewPending) && (r.deferred&E::OptionalPropertyValidationPending));
    Session s; parse(s,calendar(timezone("TZID:Test\r\n",observance(required,"DAYLIGHT"))));
    expect(checked(s,false,1,2),S::LocalChecksPassed);
    Session daylight; parse(daylight,calendar(timezone("TZID:Test\r\n",observance(required+
        "X-ONSET;X-P=x:uninterpreted\r\n","DAYLIGHT"))));
    r=checked(daylight,false,1,2); expect(r,S::LocalChecksPassed); assert(r.deferred&E::ExtensionReviewPending);
    // Parent success does not assert child schema success or resolve a definition.
    Session childFailure; parse(childFailure,calendar(timezone("TZID:Test\r\n",observance(""))));
    expect(checked(childFailure),S::LocalChecksPassed);
    expect(checked(childFailure,false,1,2),S::MissingRequired,C::Dtstart);
    Session omission; parse(omission,calendar(timezone("TZID:Test\r\n",observance(required+"TZNAME:ignored\r\nCOMMENT:ignored\r\n"))));
    IcsSourceStage::View v;
    assert(omission.stage.value(find(omission.stage,C::Tzname,0,2),v)==Stage::Read::ValueOmitted);
    assert(omission.stage.value(find(omission.stage,C::Comment,0,2),v)==Stage::Read::ValueOmitted);
    expect(checked(omission,false,1,2),S::LocalChecksPassed);
}
void budgetsAndAdmission() {
    Session s; parse(s,calendar(timezone("TZID:Test\r\n",observance(required+"RDATE:20261013T020000,20261014T020000\r\n"))));
    auto l=limits(); l.maxTimezoneProperties=1; l.maxTzidTextBytes=4;
    expect(checked(s,true,0,1,l),S::LocalChecksPassed);
    l.maxTimezoneProperties=0; expect(checked(s,true,0,1,l),S::ResourceLimit,C::Tzid,2);
    l=limits(); l.maxTzidTextBytes=3; expect(checked(s,true,0,1,l),S::ResourceLimit,C::Tzid,2);
    l=limits(); l.maxObservanceProperties=3;
    expect(checked(s,false,1,2,l),S::ResourceLimit,C::Rdate,find(s.stage,C::Rdate,0,2));
    l.maxObservanceProperties=0; expect(checked(s,false,1,2,l),S::ResourceLimit,C::Dtstart,3);
    l=limits(); l.maxObservanceProperties=4; l.maxRdateMembers=2; l.maxRdateBytes=31;
    expect(checked(s,false,1,2,l),S::LocalChecksPassed);
    l.maxRdateMembers=1; expect(checked(s,false,1,2,l),S::ResourceLimit,C::Rdate,find(s.stage,C::Rdate,0,2));
    l.maxRdateMembers=0; expect(checked(s,false,1,2,l),S::ResourceLimit,C::Rdate,find(s.stage,C::Rdate,0,2));
    l.maxRdateMembers=2; l.maxRdateBytes=30; expect(checked(s,false,1,2,l),S::ResourceLimit,C::Rdate,find(s.stage,C::Rdate,0,2));
    l.maxRdateBytes=0; expect(checked(s,false,1,2,l),S::ResourceLimit,C::Rdate,find(s.stage,C::Rdate,0,2));
    l=limits(); l.maxTimezoneProperties=0; // Parent properties are not scanned by observance validation.
    expect(checked(s,false,1,2,l),S::LocalChecksPassed);
    Session parameterBytes; parse(parameterBytes,calendar(timezone("TZID:Test\r\n",observance(required+
        "RDATE;VALUE=DATE-TIME:20261013T020000\r\nRDATE:20261013T020000\r\n"))));
    const auto pi=find(parameterBytes.stage,C::Rdate,0,2);
    const auto* pp=parameterBytes.stage.property(pi);
    l=limits(); l.maxRdateMembers=1; l.maxRdateBytes=pp->valueLength+pp->parameterLength;
    expect(checked(parameterBytes,false,1,2,l),S::LocalChecksPassed); // Per-property bounds, not global recurrence quota.
    --l.maxRdateBytes;
    expect(checked(parameterBytes,false,1,2,l),S::ResourceLimit,C::Rdate,pi);
    Session noList; parse(noList,calendar(timezone())); l=limits(); l.maxRdateMembers=l.maxRdateBytes=0;
    expect(checked(noList,false,1,2,l),S::LocalChecksPassed); // No list work consumes no list budget.
    expect(checked(s,true,999,1),S::InvalidArgument); expect(checked(s,true,0,SIZE_MAX),S::InvalidArgument);
    expect(checked(s,false,1,SIZE_MAX),S::InvalidArgument); expect(checked(s,false,SIZE_MAX,2),S::InvalidArgument);
    expect(checked(s,true,1,1),S::InvalidAssociation); expect(checked(s,true,0,0),S::InvalidAssociation);
    expect(checked(s,false,0,2),S::InvalidAssociation); expect(checked(s,false,1,1),S::InvalidAssociation);
    s.stage.release(); std::memset(s.data.data(),0xdd,s.data.capacity);
    expect(checked(s),S::Unavailable); expect(checked(s,false,1,2),S::Unavailable);
    Stage fresh; { Audit a; expect(E::validateTimezone(fresh,0,1,limits()),S::Unavailable); }
    for (bool zone : {true,false}) for (unsigned damage=0;damage<14;++damage) {
        Session b; parse(b,calendar(timezone()));
        const size_t child=zone ? 1 : 2;
        auto* c=const_cast<Stage::Component*>(b.stage.component(child));
        auto* p=const_cast<Stage::Property*>(b.stage.property(c->firstProperty));
        switch (damage) {
        case 0: p->owner=0; break;
        case 1: p->next=Stage::None-1; break;
        case 2: p->next=c->firstProperty; break;
        case 3: c->lastProperty=Stage::None; break;
        case 4: ++c->observedProperties; break;
        case 5: c->firstProperty=Stage::None; break;
        case 6: c->closed=0; break;
        case 7: c->parentOrdinal=99; break;
        case 8: c->calendarOrdinal=99; break;
        case 9: const_cast<Stage::Component*>(b.stage.component(zone ? 0 : 1))->closed=0; break;
        case 10: c->ordinal=99; break;
        case 11: c->firstProperty=Stage::None-1; break;
        case 12: const_cast<Stage::Component*>(b.stage.component(zone ? 0 : 1))->ordinal=99; break;
        case 13: c->observedProperties=0; break;
        }
        assert(checked(b,zone,zone ? 0 : 1,child).status==S::InvalidAssociation);
    }
    // Required omitted payload is unavailable; malformed retained UTF-8 fails TEXT.
    for (unsigned damage=0;damage<2;++damage) {
        Session b; parse(b,calendar(timezone())); auto* p=const_cast<Stage::Property*>(b.stage.property(2));
        if (!damage) p->flags &= uint8_t(~Stage::ValueRetained);
        else { Stage::View v; assert(b.stage.value(2,v)==Stage::Read::Ok); const_cast<uint8_t*>(v.data)[0]=0xff; }
        expect(checked(b),damage ? S::MalformedValue : S::Unavailable,C::Tzid,2);
    }
}
void associationAndFragments() {
    const std::string body=calendar(timezone("TZID:Same\r\n")+timezone("TZID:Same\r\n",observance(required,"DAYLIGHT")))+
        calendar(timezone("TZID:Same\r\n"));
    Session s; parse(s,body);
    for (const auto& pair : {std::pair<size_t,size_t>{0,1},{0,3},{5,6}}) expect(checked(s,true,pair.first,pair.second),S::LocalChecksPassed);
    expect(checked(s,true,0,6),S::InvalidAssociation);
    expect(checked(s,false,3,2),S::InvalidAssociation); expect(checked(s,false,1,4),S::InvalidAssociation);
    expect(checked(s,false,3,4),S::LocalChecksPassed);
    const std::string folded=calendar(timezone("TZID:caf\xc3\r\n \xa9\r\n",observance(required+
        "RDATE:20261013T020000,\r\n 20261014T020000\r\n")))+
        calendar("BEGIN:X-CUSTOM\r\nTZID:irrelevant\r\nEND:X-CUSTOM\r\n");
    for (bool chunked : {false,true}) for (size_t step : {size_t(1),size_t(7),size_t(31),SIZE_MAX}) {
        Session b; parse(b,folded,chunked,step);
        expect(checked(b),S::LocalChecksPassed); expect(checked(b,false,1,2),S::LocalChecksPassed);
        expect(checked(b),S::LocalChecksPassed); // Repeated stateless inspection.
    }
    // Staged callbacks from earlier components never admit a failed source.
    for (bool truncated : {false,true}) {
        Session b; std::string raw=response(calendar(timezone())+(truncated ? "" : "bad\r\n"),false);
        if (truncated) raw.resize(raw.size()-8);
        { Audit a;
            const auto feed=b.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size());
            if (truncated) { assert(feed==IcsIngestion::Status::InProgress); assert(b.http.finish()!=IcsIngestion::Status::Complete); }
            else {
                assert(feed==IcsIngestion::Status::InProgress || feed==IcsIngestion::Status::CallbackFailure);
                assert(b.http.finish()==IcsIngestion::Status::CallbackFailure);
                assert(b.structure.status()==IcsStructure::Status::MalformedProperty);
            }
            assert(b.stage.finish(b.http,b.structure)==Stage::Status::UpstreamFailure);
        }
        expect(checked(b),S::Unavailable);
    }
}
}
int main() {
    static_assert(std::is_pod<E::Report>::value && std::is_same<decltype(E::Report::deferred),uint32_t>::value,
        "bounded copied diagnostic report");
    cardinality(); typedValues(); recurrenceAndReview(); budgetsAndAdmission(); associationAndFragments();
    assert(allocations==0);
    std::printf("Timezone local: %zu deterministic scenarios; zero audited allocations; report %zu/%zu\n",
        scenarios,sizeof(E::Report),alignof(E::Report));
}
