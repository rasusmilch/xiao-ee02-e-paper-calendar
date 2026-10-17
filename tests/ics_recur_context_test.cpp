#include "../application/ics_recur_context.h"
#include "../application/ics_source_local.h"
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
namespace X = IcsRecurContext;
namespace R = IcsRecurValue;
namespace E = IcsEventLocal;
namespace Z = IcsTimezoneLocal;
using Stage = IcsSourceStage;
using Code = Stage::Code;
using S = X::Status;
using D = X::FailureDomain;
struct Audit {
    size_t before;
    Audit() : before(allocations) { assert(!audit); audit = true; }
    ~Audit() { audit = false; assert(allocations == before); }
};
struct Guard {
    std::vector<uint8_t> bytes; size_t capacity;
    explicit Guard(size_t n) : bytes(n+32,0xa5), capacity(n) {}
    uint8_t* data() { return bytes.data()+16; }
    void check() const {
        for (size_t i=0;i<16;++i) assert(bytes[i]==0xa5);
        for (size_t i=16+capacity;i<bytes.size();++i) assert(bytes[i]==0xa5);
    }
};
uint64_t clock(void*) { return 0; }
Stage::Limits storage() { return {128,512,65536,128,16384}; }
IcsStructure::Limits syntax() { return {4096,32,32,64,8,64,4096,8192,128,8}; }
size_t stageBytes() { size_t n=0; assert(Stage::requiredBytes(storage(),n)==Stage::Status::InProgress); return n; }
size_t syntaxBytes() { size_t n=0; assert(IcsStructure::scratchBytes(syntax(),n)==IcsStructure::Status::InProgress); return n; }
struct Session {
    Guard data, parser, wire;
    Stage stage; IcsStructure structure; IcsIngestion http;
    Session() : data(stageBytes()), parser(syntaxBytes()), wire(6144) {
        Audit a;
        assert(stage.initialize(data.data(),data.capacity,storage())==Stage::Status::InProgress);
        assert(structure.initialize(parser.data(),parser.capacity,syntax(),Stage::sink,&stage)==IcsStructure::Status::InProgress);
        const IcsIngestion::Limits limits={131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(wire.data(),wire.capacity,limits,IcsStructure::lineSink,&structure,clock,nullptr)==IcsIngestion::Status::InProgress);
    }
    ~Session() { Audit a; stage.release(); structure.release(); http.release(); data.check(); parser.check(); wire.check(); }
};
const std::string identity="UID:unchanged-identity\r\nDTSTAMP:20261017T120000Z\r\n";
const std::string utc="DTSTART:20261017T130000Z\r\n";
const std::string local="DTSTART:20261017T130000\r\n";
const std::string date="DTSTART;VALUE=DATE:20261017\r\n";
const std::string tzid="DTSTART;TZID=Custom/Unresolved:20261017T130000\r\n";
const std::string offsets="TZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\n";
std::string component(const std::string& kind,const std::string& fields) {
    return "BEGIN:"+kind+"\r\n"+fields+"END:"+kind+"\r\n";
}
std::string calendar(const std::string& children,const std::string& metadata="") {
    return component("VCALENDAR","VERSION:2.0\r\nPRODID:synthetic\r\n"+metadata+children);
}
std::string ruleLine(const std::string& value,const std::string& parameters="") { return "RRULE"+parameters+":"+value+"\r\n"; }
std::string event(const std::string& start=utc,const std::string& rule="FREQ=DAILY",const std::string& parameters="",const std::string& extra="") {
    return component("VEVENT",identity+start+ruleLine(rule,parameters)+extra);
}
std::string timezone(const std::string& children) { return component("VTIMEZONE","TZID:Custom/Unresolved\r\n"+children); }
std::string observance(const std::string& kind,const std::string& rule="FREQ=YEARLY",const std::string& start=local,
                       const std::string& parameters="",const std::string& extra="") {
    return component(kind,start+offsets+ruleLine(rule,parameters)+extra);
}
void parse(Session& s,const std::string& body,bool chunked=false,size_t split=SIZE_MAX) {
    std::string raw;
    if (!chunked) raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    else {
        raw="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
        for (char b : body) { raw+="1\r\n"; raw+=b; raw+="\r\n"; }
        raw+="0\r\n\r\n";
    }
    { Audit a;
        for (size_t i=0;i<raw.size();) {
            const size_t n=std::min(split,raw.size()-i);
            assert(s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()+i),n)==IcsIngestion::Status::InProgress); i+=n;
        }
        assert(s.http.finish()==IcsIngestion::Status::Complete);
        assert(s.structure.finish()==IcsStructure::Status::StructurallyComplete);
        assert(s.stage.finish(s.http,s.structure)==Stage::Status::StagedSuccessfully);
        s.http.release(); s.structure.release();
    }
    std::fill(raw.begin(),raw.end(),'!');
    std::fill(s.parser.data(),s.parser.data()+s.parser.capacity,0xdd);
    std::fill(s.wire.data(),s.wire.data()+s.wire.capacity,0xdd);
}
X::Limits limits() { return {128,{4096,64,256},{128,128,4096},{128,128,4096,64,4096}}; }
size_t find(const Stage& s,size_t componentIndex,Code code=Code::Rrule,size_t occurrence=0) {
    for (uint32_t i=s.component(componentIndex)->firstProperty;i!=Stage::None;i=s.property(i)->next)
        if (s.property(i)->code==code && !occurrence--) return i;
    return Stage::None;
}
X::Report checked(Session& s,size_t parent,size_t owner,size_t property,X::Limits budget=limits()) {
    const auto bytes=s.data.bytes; const auto before=s.stage.counts(); const auto status=s.stage.status();
    X::Report r;
    { Audit a; r=X::validate(s.stage,parent,owner,property,budget); }
    const auto after=s.stage.counts();
    assert(bytes==s.data.bytes && status==s.stage.status()); s.data.check();
    assert(before.components==after.components && before.calendars==after.calendars &&
        before.observedProperties==after.observedProperties && before.ignoredOpaqueProperties==after.ignoredOpaqueProperties &&
        before.storedProperties==after.storedProperties && before.poolBytes==after.poolBytes &&
        before.reviewItems==after.reviewItems && before.reviewBytes==after.reviewBytes);
    ++scenarios;
    if (r.status==S::ContextChecksPassed || r.status==S::MissingStartContext) {
        assert(r.failureDomain==D::None);
        assert((r.deferred&(X::RecurrenceExpansionPending|X::RecurrenceSetReconciliationPending))==
            (X::RecurrenceExpansionPending|X::RecurrenceSetReconciliationPending));
        assert(!(r.ignoredCoreParts&~uint32_t(R::BYSECOND|R::BYMINUTE|R::BYHOUR)));
        assert(bool(r.ignoredCoreParts)==bool(r.deferred&X::IgnoredTimePartsPresent));
    } else assert(!r.deferred && !r.ignoredCoreParts);
    return r;
}
void expect(const X::Report& r,S status,D domain=D::None,size_t property=Stage::None,Code code=Code::Unknown,size_t offset=0) {
    if (r.status!=status || r.failureDomain!=domain || r.propertyIndex!=property || r.propertyCode!=code || r.offset!=offset)
        std::fprintf(stderr,"scenario %zu expected %u/%u/%zu/%u/%zu got %u/%u/%zu/%u/%zu\n",scenarios,
            unsigned(status),unsigned(domain),property,unsigned(code),offset,
            unsigned(r.status),unsigned(r.failureDomain),r.propertyIndex,unsigned(r.propertyCode),r.offset);
    assert(r.status==status && r.failureDomain==domain && r.propertyIndex==property && r.propertyCode==code && r.offset==offset);
}
void eventFailure(Session& s,size_t parent,size_t owner,size_t property,X::Limits budget=limits()) {
    const auto original=E::validate(s.stage,parent,owner,budget.event);
    assert(original.status!=E::Status::LocalChecksPassed);
    const auto r=checked(s,parent,owner,property,budget);
    expect(r,S::LocalValidationFailed,D::EventLocal,original.propertyIndex,original.propertyCode);
    assert(r.eventStatus==original.status);
}
void observanceFailure(Session& s,size_t owner,size_t property,X::Limits budget=limits()) {
    const auto original=Z::validateObservance(s.stage,1,owner,budget.timezone);
    assert(original.status!=Z::Status::LocalChecksPassed);
    const auto r=checked(s,1,owner,property,budget);
    expect(r,S::LocalValidationFailed,D::ObservanceLocal,original.propertyIndex,original.propertyCode);
    assert(r.timezoneStatus==original.status);
}
void grammarFailure(Session& s,size_t parent,size_t owner,size_t property,X::Limits budget=limits()) {
    Stage::View value={}; assert(s.stage.value(property,value)==Stage::Read::Ok);
    const auto original=R::validate(value,budget.rule); assert(original.status!=R::Status::CoreGrammarValid);
    const S status=original.status==R::Status::MalformedRule ? S::MalformedRule :
        original.status==R::Status::UnsupportedFeature ? S::UnsupportedFeature :
        original.status==R::Status::ResourceLimit ? S::ResourceLimit : S::InvalidArgument;
    const auto r=checked(s,parent,owner,property,budget);
    expect(r,status,D::RuleGrammar,property,Code::Rrule,original.offset);
    assert(r.ruleStatus==original.status);
}
void untilMatrix() {
    const std::string starts[]={date,local,utc,tzid};
    const char* const ends[]={"20261016","20261016T010000","20261016T010000Z"}; // Earlier is not rejected.
    for (size_t start=0;start<4;++start) for (size_t end=0;end<3;++end) {
        Session s; parse(s,calendar(event(starts[start],std::string("UNTIL=")+ends[end]+";FREQ=DAILY")));
        const size_t p=find(s.stage,1); const auto r=checked(s,0,1,p);
        const bool valid=start==0 ? end==0 : start==1 ? end==1 : end==2;
        if (!valid) expect(r,S::InvalidRelationship,D::Context,p,Code::Rrule,6);
        else {
            expect(r,S::ContextChecksPassed);
            assert(bool(r.deferred&X::TimezoneResolutionPending)==(start==3));
            assert(!r.ignoredCoreParts && !(r.deferred&X::LeapSecondPending));
        }
    }
    for (const char* kind : {"STANDARD","DAYLIGHT"}) for (size_t end=0;end<3;++end) {
        Session s; parse(s,calendar(timezone(observance(kind,std::string("FREQ=YEARLY;UNTIL=")+ends[end]))));
        const size_t p=find(s.stage,2); const auto r=checked(s,1,2,p);
        if (end==2) expect(r,S::ContextChecksPassed);
        else expect(r,S::InvalidRelationship,D::Context,p,Code::Rrule,18);
    }
}
void contextualSuccess() {
    for (const std::string& start : {date,local,utc,tzid}) for (const char* rule : {"FREQ=DAILY","FREQ=DAILY;COUNT=4"}) {
        Session s; parse(s,calendar(event(start,rule))); expect(checked(s,0,1,find(s.stage,1)),S::ContextChecksPassed);
    }
    const char* const timeParts[]={"BYSECOND=0","BYMINUTE=59","BYHOUR=23","BYSECOND=60;BYMINUTE=1;BYHOUR=2"};
    const uint32_t masks[]={R::BYSECOND,R::BYMINUTE,R::BYHOUR,R::BYSECOND|R::BYMINUTE|R::BYHOUR};
    for (size_t i=0;i<4;++i) for (bool allDay : {false,true}) {
        Session s; parse(s,calendar(event(allDay?date:utc,std::string("FREQ=DAILY;")+timeParts[i])));
        const auto r=checked(s,0,1,find(s.stage,1)); expect(r,S::ContextChecksPassed);
        assert(r.ignoredCoreParts==(allDay?masks[i]:0));
        assert(bool(r.deferred&X::LeapSecondPending)==(i==3));
    }
    for (const std::string& body : {
        calendar(event("DTSTART:20261231T235960Z\r\n")),
        calendar(event(utc,"FREQ=DAILY;UNTIL=20261231T235960Z")),
        calendar(event("DTSTART;TZID=Custom:20261231T235960\r\n")),
        calendar(timezone(observance("STANDARD","FREQ=YEARLY;UNTIL=20261231T235960Z"))),
        calendar(timezone(observance("DAYLIGHT","FREQ=YEARLY","DTSTART:20261231T235960\r\n")))}) {
        Session s; parse(s,body); const bool ev=s.stage.component(1)->kind==IcsStructure::Kind::Event;
        const auto r=checked(s,ev?0:1,ev?1:2,find(s.stage,ev?1:2)); expect(r,S::ContextChecksPassed);
        assert(r.deferred&X::LeapSecondPending);
    }
    // No pattern-membership, date occurrence, candidate-count or UNTIL ordering test.
    for (const char* rule : {"FREQ=MONTHLY;BYMONTHDAY=31", "FREQ=YEARLY;BYMONTH=2;BYMONTHDAY=30",
        "FREQ=DAILY;UNTIL=00000101T000000Z", "FREQ=SECONDLY;COUNT=18446744073709551615",
        "fReQ=dAiLy;uNtIl=20271017t120000z"}) {
        Session s; parse(s,calendar(event(utc,rule))); expect(checked(s,0,1,find(s.stage,1)),S::ContextChecksPassed);
    }
    Session gap; parse(gap,calendar(event("DTSTART;TZID=Custom:20260308T023000\r\n")));
    const auto r=checked(gap,0,1,find(gap.stage,1)); expect(r,S::ContextChecksPassed); assert(r.deferred&X::TimezoneResolutionPending);
}
void missingAndMultiple() {
    for (const char* method : {"METHOD:CANCEL\r\n","METHOD:\r\n","METHOD:X-UNRESOLVED\r\n"}) {
        Session s; parse(s,calendar(event("","FREQ=DAILY;UNTIL=20261016T010000Z"),method));
        const auto r=checked(s,0,1,find(s.stage,1)); expect(r,S::MissingStartContext);
        assert(r.deferred&X::MissingDtstartPending); assert(!r.ignoredCoreParts);
    }
    for (const char* rule : {"FREQ=BAD","FREQ=DAILY;RSCALE=GREGORIAN;SKIP=OMIT"}) {
        Session s; parse(s,calendar(event("",rule),"METHOD:CANCEL\r\n")); grammarFailure(s,0,1,find(s.stage,1));
    }
    for (const char* end : {"20261016","20261016T010000","20261016T010000Z"}) {
        Session s; parse(s,calendar(event("",std::string("FREQ=DAILY;UNTIL=")+end),"METHOD:CANCEL\r\n"));
        expect(checked(s,0,1,find(s.stage,1)),S::MissingStartContext);
    }
    { Session s; parse(s,calendar(event(""))); eventFailure(s,0,1,find(s.stage,1)); }
    { Session s; parse(s,calendar(timezone(observance("STANDARD","FREQ=YEARLY","")))); observanceFailure(s,2,find(s.stage,2)); }
    { Session s; parse(s,calendar(event(utc,"FREQ=DAILY","",ruleLine("FREQ=WEEKLY"))));
        for (size_t n=0;n<2;++n) { const auto r=checked(s,0,1,find(s.stage,1,Code::Rrule,n)); expect(r,S::ContextChecksPassed); assert(r.deferred&X::MultipleRrulePending); }
    }
    { Session s; parse(s,calendar(event(utc,"FREQ=BAD","",ruleLine("FREQ=DAILY"))));
        grammarFailure(s,0,1,find(s.stage,1)); const auto r=checked(s,0,1,find(s.stage,1,Code::Rrule,1));
        expect(r,S::ContextChecksPassed); assert(r.deferred&X::MultipleRrulePending);
    }
    { Session s; parse(s,calendar(event(utc,"FREQ=DAILY","",ruleLine("FREQ=BAD"))));
        const auto r=checked(s,0,1,find(s.stage,1)); expect(r,S::ContextChecksPassed);
        assert(r.deferred&X::MultipleRrulePending); grammarFailure(s,0,1,find(s.stage,1,Code::Rrule,1));
    }
    { Session s; parse(s,calendar(timezone(observance("DAYLIGHT","FREQ=YEARLY","DTSTART:20261017T010000\r\n","",ruleLine("FREQ=DAILY")))));
        for (size_t n=0;n<2;++n) { const auto r=checked(s,1,2,find(s.stage,2,Code::Rrule,n)); expect(r,S::ContextChecksPassed); assert(r.deferred&X::MultipleRrulePending); }
    }
    { Session s; parse(s,calendar(event("","FREQ=DAILY;BYSECOND=60",";X-REVIEW=yes",ruleLine("FREQ=WEEKLY")),"METHOD:CANCEL\r\n"));
        const auto r=checked(s,0,1,find(s.stage,1)); expect(r,S::MissingStartContext);
        assert(r.deferred==(X::RecurrenceExpansionPending|X::RecurrenceSetReconciliationPending|X::MissingDtstartPending|
            X::MultipleRrulePending|X::ParameterReviewPending|X::LeapSecondPending));
    }
}
void parametersAndGrammar() {
    for (const char* parameters : {"",";X-REVIEW=token",";X-REVIEW=\"^n,^'^^\";VALUE=DATE;TZID=No-Reinterpretation"}) {
        Session s; parse(s,calendar(event(utc,"FREQ=DAILY;UNTIL=20271017T000000Z",parameters)));
        const auto r=checked(s,0,1,find(s.stage,1)); expect(r,S::ContextChecksPassed);
        assert(bool(r.deferred&X::ParameterReviewPending)==(parameters[0]!=0));
        assert(!(r.deferred&X::TimezoneResolutionPending));
    }
    for (const char* kind : {"STANDARD","DAYLIGHT"}) {
        Session s; parse(s,calendar(timezone(observance(kind,"FREQ=YEARLY;UNTIL=20271017T000000Z",local,";VALUE=TEXT;X-P=one,two"))));
        const auto r=checked(s,1,2,find(s.stage,2)); expect(r,S::ContextChecksPassed); assert(r.deferred&X::ParameterReviewPending);
    }
    for (const char* rule : {"FREQ=BAD","FREQ=DAILY;BYSECOND=61","FREQ=DAILY;COUNT=18446744073709551616",
        "FREQ=YEARLY;BYMONTH=5L;RSCALE=HEBREW", "FREQ=DAILY;SKIP=OMIT", "FREQ=DAILY;X-PART=value"}) {
        Session s; parse(s,calendar(event(utc,rule))); grammarFailure(s,0,1,find(s.stage,1));
        Session z; parse(z,calendar(timezone(observance("STANDARD",rule)))); grammarFailure(z,1,2,find(z.stage,2));
    }
    { Session s; parse(s,calendar(event(date,"FREQ=DAILY;BYSECOND=61"))); grammarFailure(s,0,1,find(s.stage,1)); }
    { Session s; parse(s,calendar(event(date,"FREQ=DAILY;BYHOUR=1;UNTIL=20271017T000000Z",";X-REVIEW=yes")));
        const size_t p=find(s.stage,1); expect(checked(s,0,1,p),S::InvalidRelationship,D::Context,p,Code::Rrule,26);
    }
    Session s; parse(s,calendar(event())); const size_t p=find(s.stage,1); auto budget=limits();
    budget.rule.maxRuleBytes=10; expect(checked(s,0,1,p,budget),S::ContextChecksPassed);
    budget.rule.maxRuleBytes=9; grammarFailure(s,0,1,p,budget);
    budget=limits(); budget.rule.maxParts=0; grammarFailure(s,0,1,p,budget);
    Session list; parse(list,calendar(event(utc,"FREQ=DAILY;BYHOUR=0,1"))); budget=limits();
    budget.rule.maxListItems=2; expect(checked(list,0,1,find(list.stage,1),budget),S::ContextChecksPassed);
    budget.rule.maxListItems=1; grammarFailure(list,0,1,find(list.stage,1),budget);
}
void associationsAndLimits() {
    Session s; parse(s,calendar(event()+event())); const size_t p=find(s.stage,1), other=find(s.stage,2);
    expect(checked(s,0,1,other),S::InvalidAssociation,D::Context,other,Code::Rrule);
    expect(checked(s,1,2,other),S::InvalidAssociation,D::Context);
    expect(checked(s,0,0,p),S::InvalidAssociation,D::Context);
    expect(checked(s,SIZE_MAX,1,p),S::InvalidArgument,D::Context);
    expect(checked(s,0,SIZE_MAX,p),S::InvalidArgument,D::Context);
    expect(checked(s,0,1,SIZE_MAX),S::InvalidArgument,D::Context,SIZE_MAX);
    const size_t uid=find(s.stage,1,Code::Uid);
    expect(checked(s,0,1,uid),S::InvalidArgument,D::Context,uid,Code::Uid);
    auto budget=limits(); budget.maxComponentProperties=4;
    expect(checked(s,0,1,p,budget),S::ContextChecksPassed);
    budget.maxComponentProperties=3; expect(checked(s,0,1,p,budget),S::ResourceLimit,D::Context,p,Code::Rrule);
    budget.maxComponentProperties=0; expect(checked(s,0,1,p,budget),S::ResourceLimit,D::Context,uid,Code::Uid);
    budget=limits(); budget.event.maxEventProperties=0; eventFailure(s,0,1,p,budget);
    budget=limits(); budget.event.maxCalendarProperties=0; eventFailure(s,0,1,p,budget);
    { Session z; parse(z,calendar(timezone(observance("STANDARD")))); budget=limits();
        const size_t q=find(z.stage,2); budget.maxComponentProperties=4; expect(checked(z,1,2,q,budget),S::ContextChecksPassed);
        budget.maxComponentProperties=3; expect(checked(z,1,2,q,budget),S::ResourceLimit,D::Context,q,Code::Rrule);
        budget=limits(); budget.timezone.maxObservanceProperties=0; observanceFailure(z,2,q,budget);
        expect(checked(z,0,1,q),S::InvalidAssociation,D::Context); // VTIMEZONE is not an observance.
    }
    { Session roots; parse(roots,calendar(event())+calendar(event()));
        const size_t q=find(roots.stage,3);
        expect(checked(roots,0,3,q),S::InvalidAssociation,D::Context);
        expect(checked(roots,0,1,q),S::InvalidAssociation,D::Context,q,Code::Rrule);
        expect(checked(roots,2,3,q),S::ContextChecksPassed);
    }
    { Session kids; parse(kids,calendar(event(utc,"FREQ=DAILY","",component("PARTICIPANT",ruleLine("FREQ=YEARLY")+"UID:child\r\n"))));
        const size_t q=find(kids.stage,2);
        expect(checked(kids,1,2,q),S::InvalidAssociation,D::Context);
        expect(checked(kids,0,1,q),S::InvalidAssociation,D::Context,q,Code::Rrule);
        expect(checked(kids,0,1,find(kids.stage,1)),S::ContextChecksPassed);
    }
    { Session kids; parse(kids,calendar(event("","FREQ=DAILY","",component("PARTICIPANT",utc)),"METHOD:CANCEL\r\n"));
        expect(checked(kids,0,1,find(kids.stage,1)),S::MissingStartContext);
    }
    for (const char* kind : {"VLOCATION","VRESOURCE"}) {
        Session kids; parse(kids,calendar(event(utc,"FREQ=DAILY","",component(kind,ruleLine("FREQ=YEARLY")))));
        expect(checked(kids,1,2,find(kids.stage,2)),S::InvalidAssociation,D::Context);
        expect(checked(kids,0,1,find(kids.stage,2)),S::InvalidAssociation,D::Context,find(kids.stage,2),Code::Rrule);
    }
    for (const char* kind : {"VTODO","VJOURNAL","VFREEBUSY","X-OPAQUE"}) {
        Session projected; parse(projected,calendar(component(kind,ruleLine("FREQ=DAILY"))));
        expect(checked(projected,0,1,0),S::InvalidAssociation,D::Context);
    }
}
void corruptionAndLocalFailures() {
    { Session s; parse(s,calendar(event())); const size_t p=find(s.stage,1);
        auto& c=*const_cast<Stage::Component*>(s.stage.component(1));
        const auto original=c;
        c.closed=0; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); c=original;
        ++c.ordinal; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); c=original;
        ++c.calendarOrdinal; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); c=original;
        c.parentOrdinal=0; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); c=original;
        c.flags|=Stage::ProjectedOut; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); c=original;
        auto& root=*const_cast<Stage::Component*>(s.stage.component(0)); const auto originalRoot=root;
        root.parentOrdinal=1; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); root=originalRoot;
        root.closed=0; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); root=originalRoot;
        ++root.ordinal; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); root=originalRoot;
        root.calendarOrdinal=0; expect(checked(s,0,1,p),S::InvalidAssociation,D::Context); root=originalRoot;
        auto& rule=*const_cast<Stage::Property*>(s.stage.property(p)); const auto originalRule=rule;
        rule.flags&=~Stage::ValueRetained; expect(checked(s,0,1,p),S::Unavailable,D::Context,p,Code::Rrule); rule=originalRule;
        rule.next=c.firstProperty; eventFailure(s,0,1,p); rule=originalRule;
        rule.next=static_cast<uint32_t>(s.stage.counts().storedProperties); eventFailure(s,0,1,p); rule=originalRule;
        c.lastProperty=c.firstProperty; eventFailure(s,0,1,p); c=original;
        ++c.observedProperties; eventFailure(s,0,1,p); c=original;
        auto& first=*const_cast<Stage::Property*>(s.stage.property(c.firstProperty)); const auto originalFirst=first;
        first.owner=0; eventFailure(s,0,1,p); first=originalFirst;
        // The supplied property exists and claims this owner but is not in its
        // complete direct chain. Local invariants can still pass without RRULE.
        const size_t start=find(s.stage,1,Code::Dtstart);
        auto& prior=*const_cast<Stage::Property*>(s.stage.property(start)); const auto originalPrior=prior;
        prior.next=Stage::None; c.lastProperty=static_cast<uint32_t>(start); --c.observedProperties;
        expect(checked(s,0,1,p),S::InvalidAssociation,D::Context,p,Code::Rrule); prior=originalPrior; c=original;
    }
    for (const std::string& fields : {std::string("DTSTAMP:20261017T120000Z\r\n")+utc,
        "UID:x\r\nDTSTAMP:bad\r\n"+utc, identity+"DTSTART;VALUE=DATE:bad\r\n"}) {
        Session s; parse(s,calendar(component("VEVENT",fields+ruleLine("FREQ=BAD")))); eventFailure(s,0,1,find(s.stage,1));
    }
    { Session s; parse(s,calendar(timezone(component("STANDARD",local+"TZOFFSETTO:+0100\r\n"+ruleLine("FREQ=BAD")))));
        observanceFailure(s,2,find(s.stage,2));
    }
    { Session s; parse(s,calendar(timezone(observance("DAYLIGHT")))); const size_t p=find(s.stage,2);
        auto& rule=*const_cast<Stage::Property*>(s.stage.property(p)); rule.next=s.stage.component(2)->firstProperty;
        observanceFailure(s,2,p);
    }
}
void lifecycleAndComposition() {
    { Session s; expect(checked(s,0,1,0),S::Unavailable,D::Context); }
    { Session s; const std::string incomplete="HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        s.http.feed(reinterpret_cast<const uint8_t*>(incomplete.data()),incomplete.size()); s.http.finish();
        s.stage.finish(s.http,s.structure); expect(checked(s,0,1,0),S::Unavailable,D::Context);
    }
    { Session s; s.stage.cancel(); expect(checked(s,0,1,0),S::Unavailable,D::Context); }
    for (bool chunked : {false,true}) for (size_t step : {size_t(1),size_t(7),SIZE_MAX}) {
        Session s; parse(s,calendar(event(date,"FREQ=DAILY;BYHOUR=1",";X-REVIEW=yes")+
            timezone(observance("STANDARD","FREQ=YEARLY;UNTIL=20271017T000000Z")+observance("DAYLIGHT"))),chunked,step);
        const IcsSourceLocal::Limits sl={128,8,32,32,64,128,{128,128,4096},{128,128,4096,64,4096}};
        const auto before=IcsSourceLocal::inspect(s.stage,sl);
        expect(checked(s,0,1,find(s.stage,1)),S::ContextChecksPassed);
        expect(checked(s,2,3,find(s.stage,3)),S::ContextChecksPassed);
        expect(checked(s,2,4,find(s.stage,4)),S::ContextChecksPassed);
        const auto report=checked(s,0,1,find(s.stage,1));
        const auto after=IcsSourceLocal::inspect(s.stage,sl);
        assert(before.status==IcsSourceLocal::Status::LocalCoveragePassed && before.status==after.status &&
            before.eventDeferred==after.eventDeferred && before.timezoneDeferred==after.timezoneDeferred &&
            before.sourceDeferred==after.sourceDeferred && before.events==after.events && before.observances==after.observances);
        assert(after.sourceDeferred&IcsSourceLocal::SourceSemanticsPending);
        s.stage.release(); expect(checked(s,0,1,0),S::Unavailable,D::Context);
        assert(s.stage.status()==Stage::Status::StagedSuccessfully && !s.stage.component(0));
        std::fill(s.data.data(),s.data.data()+s.data.capacity,0xdd);
        expect(report,S::ContextChecksPassed); assert(report.ignoredCoreParts==R::BYHOUR);
        assert(report.deferred&X::ParameterReviewPending);
    }
}
}

int main() {
    static_assert(std::is_trivially_copyable<X::Report>::value,"Copied evidence has no memory owner");
    static_assert(std::is_standard_layout<X::Report>::value,"Report is bounded scalar diagnostics");
    static_assert(std::is_same<decltype(X::Report::ruleStatus),R::Status>::value,"Original grammar status domain");
    static_assert(std::is_same<decltype(X::Report::eventStatus),E::Status>::value,"Original event status domain");
    static_assert(std::is_same<decltype(X::Report::timezoneStatus),Z::Status>::value,"Original observance status domain");
    untilMatrix(); contextualSuccess(); missingAndMultiple(); parametersAndGrammar();
    associationsAndLimits(); corruptionAndLocalFailures(); lifecycleAndComposition();
    std::printf("Contextual RRULE: %zu deterministic cases; zero audited allocations; report %zu/%zu\n",
        scenarios,sizeof(X::Report),alignof(X::Report));
}
