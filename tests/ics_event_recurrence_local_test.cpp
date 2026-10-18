#include "../application/ics_event_recurrence_local.h"
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
    void* p=std::malloc(n?n:1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
namespace {
namespace N=IcsEventRecurrenceLocal;
namespace E=IcsEventLocal;
namespace X=IcsRecurContext;
namespace T=IcsTypedValues;
using Stage=IcsSourceStage;
using Code=Stage::Code;
using S=N::Status;
using D=N::FailureDomain;
struct Audit {
    size_t before;
    Audit() : before(allocations) { assert(!audit); audit=true; }
    ~Audit() { audit=false; assert(before==allocations); }
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
Stage::Limits storage() { return {128,512,65536,128,16384}; }
IcsStructure::Limits syntax() { return {4096,32,32,64,8,64,4096,8192,128,8}; }
size_t stageBytes() { size_t n=0; assert(Stage::requiredBytes(storage(),n)==Stage::Status::InProgress); return n; }
size_t syntaxBytes() { size_t n=0; assert(IcsStructure::scratchBytes(syntax(),n)==IcsStructure::Status::InProgress); return n; }
struct Session {
    Guard data,parser,wire; Stage stage; IcsStructure structure; IcsIngestion http;
    Session() : data(stageBytes()),parser(syntaxBytes()),wire(6144) {
        Audit a;
        assert(stage.initialize(data.data(),data.capacity,storage())==Stage::Status::InProgress);
        assert(structure.initialize(parser.data(),parser.capacity,syntax(),Stage::sink,&stage)==IcsStructure::Status::InProgress);
        const IcsIngestion::Limits limits={131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(wire.data(),wire.capacity,limits,IcsStructure::lineSink,&structure,clock,nullptr)==IcsIngestion::Status::InProgress);
    }
    ~Session() { Audit a; stage.release(); structure.release(); http.release(); data.check(); parser.check(); wire.check(); }
};
const std::string identity="UID:event\r\nDTSTAMP:20261017T120000Z\r\n";
const std::string utc="DTSTART:20261017T130000Z\r\n";
const std::string date="DTSTART;VALUE=DATE:20261017\r\n";
const std::string time1="20261017T130000Z",time2="20261018T130000Z",time3="20261019T130000Z";
std::string component(const std::string& kind,const std::string& fields) { return "BEGIN:"+kind+"\r\n"+fields+"END:"+kind+"\r\n"; }
std::string event(const std::string& recurrence="",const std::string& start=utc) { return component("VEVENT",identity+start+recurrence); }
std::string calendar(const std::string& children,const std::string& metadata="") { return component("VCALENDAR","VERSION:2.0\r\nPRODID:synthetic\r\n"+metadata+children); }
void parse(Session& s,const std::string& body,bool chunked=false,size_t split=SIZE_MAX) {
    std::string raw;
    if (!chunked) raw="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    else {
        raw="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
        for (char c : body) { raw+="1\r\n"; raw+=c; raw+="\r\n"; } raw+="0\r\n\r\n";
    }
    { Audit a;
        for (size_t i=0;i<raw.size();) { const size_t n=std::min(split,raw.size()-i);
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
N::Limits limits() { return {128,32,32,32,128,32768,{128,{4096,64,256},{128,128,4096},{128,128,4096,64,4096}}}; }
size_t find(const Stage& s,Code code,size_t occurrence=0,size_t owner=1) {
    for (uint32_t i=s.component(owner)->firstProperty;i!=Stage::None;i=s.property(i)->next)
        if (s.property(i)->code==code && !occurrence--) return i;
    return Stage::None;
}
N::Report checked(Session& s,N::Limits budget=limits(),size_t parent=0,size_t owner=1) {
    const auto bytes=s.data.bytes; const auto before=s.stage.counts(); const auto status=s.stage.status(); N::Report r;
    { Audit a; r=N::inspect(s.stage,parent,owner,budget); }
    const auto after=s.stage.counts();
    assert(bytes==s.data.bytes && status==s.stage.status()); s.data.check();
    assert(before.components==after.components && before.calendars==after.calendars && before.observedProperties==after.observedProperties &&
        before.ignoredOpaqueProperties==after.ignoredOpaqueProperties && before.storedProperties==after.storedProperties &&
        before.poolBytes==after.poolBytes && before.reviewItems==after.reviewItems && before.reviewBytes==after.reviewBytes);
    ++scenarios;
    if (r.status==S::LocalCoveragePassed) assert(r.failureDomain==D::None && (r.deferred&N::SourceSemanticValidationPending));
    else assert(!r.deferred && !r.contextualRuleDeferred && !r.rruleProperties && !r.rdateProperties &&
        !r.exdateProperties && !r.rdateMembers && !r.exdateMembers && !r.recurrenceIdPresent);
    return r;
}
void expect(const N::Report& r,S status,D domain=D::None,size_t property=Stage::None,Code code=Code::Unknown,size_t offset=0) {
    if (r.status!=status || r.failureDomain!=domain || r.propertyIndex!=property || r.propertyCode!=code || r.offset!=offset)
        std::fprintf(stderr,"scenario %zu expected %u/%u/%zu/%u/%zu got %u/%u/%zu/%u/%zu\n",scenarios,
            unsigned(status),unsigned(domain),property,unsigned(code),offset,
            unsigned(r.status),unsigned(r.failureDomain),r.propertyIndex,unsigned(r.propertyCode),r.offset);
    assert(r.status==status && r.failureDomain==domain && r.propertyIndex==property && r.propertyCode==code && r.offset==offset);
}
void counts(const N::Report& r,size_t rules,size_t dates,size_t exclusions,size_t members,size_t excluded,bool id=false) {
    expect(r,S::LocalCoveragePassed);
    assert(r.rruleProperties==rules && r.rdateProperties==dates && r.exdateProperties==exclusions &&
        r.rdateMembers==members && r.exdateMembers==excluded && r.recurrenceIdPresent==id);
}
void same(const N::Report& a,const N::Report& b) {
    assert(a.status==b.status && a.failureDomain==b.failureDomain && a.propertyIndex==b.propertyIndex && a.propertyCode==b.propertyCode &&
        a.offset==b.offset && a.eventStatus==b.eventStatus && a.contextStatus==b.contextStatus && a.contextFailureDomain==b.contextFailureDomain &&
        a.temporalStatus==b.temporalStatus && a.deferred==b.deferred && a.contextualRuleDeferred==b.contextualRuleDeferred &&
        a.rruleProperties==b.rruleProperties && a.rdateProperties==b.rdateProperties && a.exdateProperties==b.exdateProperties &&
        a.rdateMembers==b.rdateMembers && a.exdateMembers==b.exdateMembers && a.recurrenceIdPresent==b.recurrenceIdPresent);
}
void eventFailure(Session& s,N::Limits budget=limits()) {
    const auto original=E::validate(s.stage,0,1,budget.context.event); assert(original.status!=E::Status::LocalChecksPassed);
    const auto r=checked(s,budget); expect(r,S::EventLocalFailure,D::EventLocal,original.propertyIndex,original.propertyCode);
    assert(r.eventStatus==original.status);
}
void ruleFailure(Session& s,size_t property,N::Limits budget=limits()) {
    const auto original=X::validate(s.stage,0,1,property,budget.context);
    assert(original.status!=X::Status::ContextChecksPassed && original.status!=X::Status::MissingStartContext);
    const auto r=checked(s,budget); expect(r,S::RruleContextFailure,D::RruleContext,original.propertyIndex,original.propertyCode,original.offset);
    assert(r.contextStatus==original.status && r.contextFailureDomain==original.failureDomain);
}
void listFailure(Session& s,size_t property,T::TemporalList::Limits cursorBudget={128,32768},N::Limits budget=limits()) {
    T::TemporalList cursor(s.stage,property,cursorBudget); T::TemporalValue value={}; T::Result original={};
    do { original=cursor.next(value); } while (T::parsed(original));
    assert(original.status!=T::Status::End);
    const auto r=checked(s,budget);
    expect(r,original.status==T::Status::ResourceLimit?S::ResourceLimit:S::TemporalListFailure,
        D::TemporalList,property,s.stage.property(property)->code,original.offset);
    assert(r.temporalStatus==original.status);
}
size_t listBytes(const Stage& s) {
    size_t bytes=0;
    for (uint32_t i=s.component(1)->firstProperty;i!=Stage::None;i=s.property(i)->next)
        if (s.property(i)->code==Code::Rdate || s.property(i)->code==Code::Exdate) {
            Stage::View v={}; assert(s.value(i,v)==Stage::Read::Ok); bytes+=v.size+s.parameters(i).size;
        }
    return bytes;
}
void rulesAndNoRecurrence() {
    { Session s; parse(s,calendar(event())); auto b=limits(); b.maxRruleProperties=b.maxRdateProperties=b.maxExdateProperties=0;
        b.maxTotalListMembers=b.maxTotalListBytes=0; const auto r=checked(s,b); counts(r,0,0,0,0,0);
        assert(r.deferred==N::SourceSemanticValidationPending && !r.contextualRuleDeferred);
    }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY\r\n"))); auto b=limits(); b.maxTotalListMembers=b.maxTotalListBytes=0;
        counts(checked(s,b),1,0,0,0,0);
    }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY\r\nRRULE:FREQ=WEEKLY\r\n")));
        const auto a=X::validate(s.stage,0,1,find(s.stage,Code::Rrule),limits().context);
        const auto b=X::validate(s.stage,0,1,find(s.stage,Code::Rrule,1),limits().context);
        const auto r=checked(s); counts(r,2,0,0,0,0); assert(r.contextualRuleDeferred==(a.deferred|b.deferred));
        assert(r.deferred==(N::SourceSemanticValidationPending|N::RecurrenceSetReconciliationPending|N::RecurrenceExpansionPending|N::MultipleRrulePending));
        auto budget=limits(); budget.maxRruleProperties=2; counts(checked(s,budget),2,0,0,0,0);
        budget.maxRruleProperties=1; expect(checked(s,budget),S::ResourceLimit,D::Budget,find(s.stage,Code::Rrule,1),Code::Rrule);
        budget.maxRruleProperties=0; expect(checked(s,budget),S::ResourceLimit,D::Budget,find(s.stage,Code::Rrule),Code::Rrule);
        budget=limits(); budget.context.maxComponentProperties=3; ruleFailure(s,find(s.stage,Code::Rrule),budget);
    }
    for (const char* bad : {"FREQ=BAD","FREQ=YEARLY;BYMONTH=5L;RSCALE=HEBREW","FREQ=DAILY;SKIP=OMIT"}) {
        Session s; parse(s,calendar(event(std::string("RRULE:FREQ=DAILY\r\nRRULE:")+bad+"\r\n")));
        ruleFailure(s,find(s.stage,Code::Rrule,1));
    }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY;UNTIL=20271017T130000\r\n"))); ruleFailure(s,find(s.stage,Code::Rrule)); }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY;BYSECOND=60;BYMINUTE=1;BYHOUR=2\r\n",date)));
        const auto r=checked(s); counts(r,1,0,0,0,0); assert(r.contextualRuleDeferred&X::IgnoredTimePartsPresent);
        assert(r.deferred&N::LeapSecondPending);
    }
    { Session s; parse(s,calendar(event("RRULE;X-REVIEW=yes:FREQ=DAILY;BYSECOND=60\r\nRRULE:FREQ=WEEKLY\r\n","DTSTART;TZID=Unresolved:20261017T130000\r\n")));
        const auto r=checked(s); counts(r,2,0,0,0,0);
        assert((r.deferred&(N::ParameterReviewPending|N::TimezoneResolutionPending|N::LeapSecondPending|N::MultipleRrulePending))==
            (N::ParameterReviewPending|N::TimezoneResolutionPending|N::LeapSecondPending|N::MultipleRrulePending));
    }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY\r\n",""),"METHOD:CANCEL\r\n"));
        const auto r=checked(s); counts(r,1,0,0,0,0); assert(r.deferred&N::MissingStartContextPending);
        assert(r.contextualRuleDeferred&X::MissingDtstartPending);
    }
    { Session s; parse(s,calendar(event("RRULE:FREQ=BAD\r\n",""),"METHOD:CANCEL\r\n")); ruleFailure(s,find(s.stage,Code::Rrule)); }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY\r\n",""))); eventFailure(s); }
}
void listsAndRelationships() {
    { Session s; parse(s,calendar(event("RDATE:"+time1+","+time2+"\r\nRDATE:"+time1+"\r\nEXDATE:"+time1+","+time3+"\r\nEXDATE:"+time1+"\r\n")));
        const auto r=checked(s); counts(r,0,2,2,3,3); assert(!r.contextualRuleDeferred);
        assert(r.deferred==(N::SourceSemanticValidationPending|N::RecurrenceSetReconciliationPending|N::DateListContextPending));
    }
    for (const char* name : {"RDATE","EXDATE"}) for (const std::string& start : {date,utc}) {
        Session s; parse(s,calendar(event(std::string(name)+";VALUE=DATE:20261017,20261018\r\n"+name+":"+time1+"\r\n",start)));
        const auto r=checked(s); const bool inclusion=std::string(name)=="RDATE";
        counts(r,0,inclusion?2:0,inclusion?0:2,inclusion?3:0,inclusion?0:3);
        assert(r.deferred&N::DateListContextPending);
    }
    for (const char* value : {"20261017T130000Z/20261017T140000Z","20261017T130000Z/PT1H",
        "20261017T130000/20261017T140000","20261017T130000/P1D","20261017T235960Z/PT1H"}) {
        Session s; parse(s,calendar(event(std::string("RDATE;VALUE=PERIOD:")+value+"\r\n")));
        const auto r=checked(s); counts(r,0,1,0,1,0); assert(r.deferred&N::PeriodSemanticsPending);
        assert(r.deferred&N::DateListContextPending);
        assert(bool(r.deferred&N::LeapSecondPending)==(std::string(value).find("60Z")!=std::string::npos));
    }
    { Session s; parse(s,calendar(event("RDATE;VALUE=PERIOD;TZID=Custom:20261017T130000/20261017T140000\r\n")));
        const auto r=checked(s); counts(r,0,1,0,1,0); assert(r.deferred&N::TimezoneResolutionPending);
    }
    { Session s; parse(s,calendar(event("RDATE;TZID=Other:20261017T130000\r\nEXDATE;TZID=Different:20261018T130000\r\n",
        "DTSTART;TZID=Original:20261017T130000\r\n")));
        const auto r=checked(s); counts(r,0,1,1,1,1); assert(r.deferred&N::TimezoneResolutionPending);
    }
    { Session s; parse(s,calendar(event("EXDATE;TZID=\"\":20261017T130000\r\n")));
        const auto r=checked(s); counts(r,0,0,1,0,1); assert(r.deferred&N::TimezoneResolutionPending);
        assert(r.deferred&N::DateListContextPending);
    }
    { Session s; parse(s,calendar(event("RDATE:20261017T130000,"+time2+"\r\nEXDATE:20261017T235960Z\r\n")));
        const auto r=checked(s); counts(r,0,1,1,2,1); assert(r.deferred&N::LeapSecondPending);
    }
    { Session s; parse(s,calendar(event("RDATE;X-REVIEW=\"one,^'two\":20261017T130000Z\r\nEXDATE;VALUE=DATE;X-P=yes:20261017\r\n")));
        const auto r=checked(s); counts(r,0,1,1,1,1); assert(r.deferred&N::ParameterReviewPending);
    }
    { Session s; parse(s,calendar(event("RDATE:"+time1+"\r\n",""),"METHOD:CANCEL\r\n")); counts(checked(s),0,1,0,1,0); }
}
void malformedListsAndFirstFailure() {
    for (const char* name : {"RDATE","EXDATE"}) {
        const Code code=std::string(name)=="RDATE"?Code::Rdate:Code::Exdate;
        for (const std::string& bad : {std::string(""),time1+",",","+time1,time1+",,"+time2,time1+",20261317T130000Z",time1+",20261017T240000Z"}) {
            Session s; parse(s,calendar(event(std::string(name)+":"+bad+"\r\n"))); listFailure(s,find(s.stage,code));
        }
        for (const char* bad : {";VALUE=DATE;TZID=Custom:20261017",";TZID=Custom:20261017T130000Z",
            ";VALUE=DATE;VALUE=DATE:20261017",";TZID=One;TzId=Two:20261017T130000",
            ";VALUE=DATE,DATE:20261017",";VALUE=X-FUTURE:token",";VALUE=INTEGER:1",";LANGUAGE=en:20261017T130000Z"}) {
            Session s; parse(s,calendar(event(std::string(name)+bad+"\r\n"))); listFailure(s,find(s.stage,code));
        }
    }
    for (const char* bad : {"20261017/20261018","20261017T130000Z/-PT1H","20261017T130000Z/PT0S","20261017T140000Z/20261017T130000Z"}) {
        Session s; parse(s,calendar(event(std::string("RDATE;VALUE=PERIOD:")+bad+"\r\n"))); listFailure(s,find(s.stage,Code::Rdate));
    }
    { Session s; parse(s,calendar(event("EXDATE;VALUE=PERIOD:20261017T130000Z/PT1H\r\n"))); listFailure(s,find(s.stage,Code::Exdate)); }
    { Session s; parse(s,calendar(event("RRULE:FREQ=DAILY\r\nEXDATE:"+time1+",bad\r\nRDATE:bad\r\n"))); listFailure(s,find(s.stage,Code::Exdate)); }
    { Session s; parse(s,calendar(event("RDATE:"+time1+"\r\nRRULE:FREQ=BAD\r\nEXDATE:bad\r\n"))); ruleFailure(s,find(s.stage,Code::Rrule)); }
    { Session s; parse(s,calendar(event("EXDATE:"+time1+"\r\nRDATE:bad\r\nRRULE:FREQ=BAD\r\n"))); listFailure(s,find(s.stage,Code::Rdate)); }
    { Session s; parse(s,calendar(event("RDATE:"+time1+"\r\nRDATE:bad\r\n"))); listFailure(s,find(s.stage,Code::Rdate,1)); }
    { Session s; parse(s,calendar(event("EXDATE:"+time1+"\r\nEXDATE:bad\r\n"))); listFailure(s,find(s.stage,Code::Exdate,1)); }
}
void aggregateBudgets() {
    Session s; parse(s,calendar(event("RDATE;X-REVIEW=yes:"+time1+","+time2+"\r\nEXDATE;VALUE=DATE:20261017\r\n")));
    auto b=limits(); b.maxTotalListMembers=3; b.maxTotalListBytes=listBytes(s.stage);
    counts(checked(s,b),0,1,1,2,1);
    b.maxTotalListMembers=2; expect(checked(s,b),S::ResourceLimit,D::Budget,find(s.stage,Code::Exdate),Code::Exdate);
    b.maxTotalListMembers=3; --b.maxTotalListBytes; expect(checked(s,b),S::ResourceLimit,D::Budget,find(s.stage,Code::Exdate),Code::Exdate);
    b=limits(); b.maxTotalListMembers=0; expect(checked(s,b),S::ResourceLimit,D::Budget,find(s.stage,Code::Rdate),Code::Rdate);
    b=limits(); b.maxTotalListBytes=0; expect(checked(s,b),S::ResourceLimit,D::Budget,find(s.stage,Code::Rdate),Code::Rdate);
    b=limits(); b.maxTotalListMembers=1; listFailure(s,find(s.stage,Code::Rdate),{1,32768},b);
    { Session more; parse(more,calendar(event("RDATE:"+time1+"\r\nEXDATE:"+time1+","+time2+"\r\n")));
        b=limits(); b.maxTotalListMembers=2; listFailure(more,find(more.stage,Code::Exdate),{1,32768},b);
    }
    { Session exact; parse(exact,calendar(event("RDATE;VALUE=DATE:20261017\r\n"))); b=limits(); b.maxTotalListMembers=1;
        b.maxTotalListBytes=listBytes(exact.stage); counts(checked(exact,b),0,1,0,1,0);
        --b.maxTotalListBytes; expect(checked(exact,b),S::ResourceLimit,D::Budget,find(exact.stage,Code::Rdate),Code::Rdate);
    }
    for (bool inclusion : {false,true}) {
        const std::string name=inclusion?"RDATE":"EXDATE"; const Code code=inclusion?Code::Rdate:Code::Exdate;
        Session repeated; parse(repeated,calendar(event(name+":"+time1+"\r\n"+name+":"+time2+"\r\n")));
        b=limits(); (inclusion?b.maxRdateProperties:b.maxExdateProperties)=2; counts(checked(repeated,b),0,inclusion?2:0,inclusion?0:2,inclusion?2:0,inclusion?0:2);
        (inclusion?b.maxRdateProperties:b.maxExdateProperties)=1; expect(checked(repeated,b),S::ResourceLimit,D::Budget,find(repeated.stage,code,1),code);
        (inclusion?b.maxRdateProperties:b.maxExdateProperties)=0; expect(checked(repeated,b),S::ResourceLimit,D::Budget,find(repeated.stage,code),code);
    }
    b=limits(); b.maxComponentProperties=5; counts(checked(s,b),0,1,1,2,1);
    b.maxComponentProperties=4; expect(checked(s,b),S::ResourceLimit,D::Budget,find(s.stage,Code::Exdate),Code::Exdate);
    b.maxComponentProperties=0; expect(checked(s,b),S::ResourceLimit,D::Budget,find(s.stage,Code::Uid),Code::Uid);
    b=limits(); b.maxComponentProperties=b.maxRruleProperties=b.maxRdateProperties=b.maxExdateProperties=SIZE_MAX;
    b.maxTotalListMembers=b.maxTotalListBytes=SIZE_MAX; counts(checked(s,b),0,1,1,2,1);
    { Session arithmetic; parse(arithmetic,calendar(event("RDATE:"+time1+"\r\n")));
        auto& p=*const_cast<Stage::Property*>(arithmetic.stage.property(find(arithmetic.stage,Code::Rdate)));
        const auto original=p; p.valueLength=UINT32_MAX; p.parameterLength=1; b=limits(); b.maxTotalListBytes=UINT32_MAX;
        expect(checked(arithmetic,b),S::ResourceLimit,D::Budget,find(arithmetic.stage,Code::Rdate),Code::Rdate); p=original;
        // Rejects the unrepresentable 32-bit sum before reading either forged span.
    }
}
void recurrenceIdentity() {
    for (const char* parameters : {"",";RANGE=THISANDFUTURE",";RANGE=X-UNRESOLVED;X-REVIEW=yes",";X-REVIEW=yes"}) {
        Session s; parse(s,calendar(event(std::string("RECURRENCE-ID")+parameters+":"+time1+"\r\n")));
        const auto r=checked(s); counts(r,0,0,0,0,0,true); assert(r.deferred&N::OverrideIdentityPending);
        assert(r.deferred&N::RecurrenceSetReconciliationPending); assert(!(r.deferred&N::RecurrenceExpansionPending));
        assert(bool(r.deferred&N::RangeReviewPending)==(std::string(parameters).find("RANGE")!=std::string::npos));
        assert(bool(r.deferred&N::ParameterReviewPending)==(std::string(parameters).find("X-REVIEW")!=std::string::npos));
    }
    for (const char* invalid : {"RECURRENCE-ID:bad\r\n","RECURRENCE-ID;VALUE=PERIOD:20261017T130000Z/PT1H\r\n",
        "RECURRENCE-ID:20261017T130000Z\r\nRECURRENCE-ID:20261018T130000Z\r\n"}) {
        Session s; parse(s,calendar(event(invalid))); eventFailure(s);
    }
}
void admissionAndCorruption() {
    { Stage fresh; N::Report r; { Audit a; r=N::inspect(fresh,0,1,limits()); } ++scenarios;
        expect(r,S::Unavailable,D::Association);
        assert(!r.deferred && !r.contextualRuleDeferred && !r.rruleProperties && !r.rdateProperties && !r.exdateProperties &&
            !r.rdateMembers && !r.exdateMembers && !r.recurrenceIdPresent);
    }
    { Session fresh; expect(checked(fresh),S::Unavailable,D::Association); }
    { Session failed; failed.stage.cancel(); expect(checked(failed),S::Unavailable,D::Association); }
    { Session failed; const std::string raw="HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        failed.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size()); failed.http.finish(); failed.stage.finish(failed.http,failed.structure);
        expect(checked(failed),S::Unavailable,D::Association);
    }
    Session s; parse(s,calendar(event("RDATE:"+time1+"\r\n"))+calendar(event()));
    expect(checked(s,limits(),SIZE_MAX,1),S::InvalidArgument,D::Association);
    expect(checked(s,limits(),0,SIZE_MAX),S::InvalidArgument,D::Association);
    expect(checked(s,limits(),0,0),S::InvalidAssociation,D::Association);
    expect(checked(s,limits(),0,3),S::InvalidAssociation,D::Association);
    expect(checked(s,limits(),2,1),S::InvalidAssociation,D::Association);
    counts(checked(s,limits(),2,3),0,0,0,0,0);
    auto& c=*const_cast<Stage::Component*>(s.stage.component(1)); const auto original=c;
    c.closed=0; expect(checked(s),S::InvalidAssociation,D::Association); c=original;
    ++c.ordinal; expect(checked(s),S::InvalidAssociation,D::Association); c=original;
    ++c.calendarOrdinal; expect(checked(s),S::InvalidAssociation,D::Association); c=original;
    c.parentOrdinal=0; expect(checked(s),S::InvalidAssociation,D::Association); c=original;
    c.flags|=Stage::ProjectedOut; expect(checked(s),S::InvalidAssociation,D::Association); c=original;
    auto& root=*const_cast<Stage::Component*>(s.stage.component(0)); const auto originalRoot=root;
    root.parentOrdinal=1; expect(checked(s),S::InvalidAssociation,D::Association); root=originalRoot;
    root.closed=0; expect(checked(s),S::InvalidAssociation,D::Association); root=originalRoot;
    ++root.ordinal; expect(checked(s),S::InvalidAssociation,D::Association); root=originalRoot;
    root.calendarOrdinal=0; expect(checked(s),S::InvalidAssociation,D::Association); root=originalRoot;
    auto& p=*const_cast<Stage::Property*>(s.stage.property(find(s.stage,Code::Rdate))); const auto originalP=p;
    p.owner=0; eventFailure(s); p=originalP;
    p.next=c.firstProperty; eventFailure(s); p=originalP;
    p.next=static_cast<uint32_t>(s.stage.counts().storedProperties); eventFailure(s); p=originalP;
    c.lastProperty=c.firstProperty; eventFailure(s); c=original;
    ++c.observedProperties; eventFailure(s); c=original;
    p.flags&=~Stage::ValueRetained; listFailure(s,find(s.stage,Code::Rdate)); p=originalP;
    auto b=limits(); b.context.event.maxEventProperties=0; eventFailure(s,b);
    b=limits(); b.context.event.maxCalendarProperties=0; eventFailure(s,b);
    { Session bad; parse(bad,calendar(component("VEVENT","DTSTAMP:20261017T120000Z\r\n"+utc+"RRULE:FREQ=BAD\r\nEXDATE:bad\r\n"))); eventFailure(bad); }
    { Session bad; parse(bad,calendar(event("RDATE:bad\r\nSUMMARY:bad\\escape\r\n"))); eventFailure(bad); }
    for (const char* kind : {"VTODO","VJOURNAL","VFREEBUSY","X-OPAQUE"}) {
        Session other; parse(other,calendar(component(kind,"RRULE:FREQ=DAILY\r\nRDATE:bad\r\n")));
        expect(checked(other),S::InvalidAssociation,D::Association);
    }
    for (const char* kind : {"STANDARD","DAYLIGHT"}) {
        Session zone; parse(zone,calendar(component("VTIMEZONE","TZID:Custom\r\n"+component(kind,"DTSTART:20261017T010000\r\nTZOFFSETFROM:+0000\r\nTZOFFSETTO:+0100\r\n"))));
        expect(checked(zone),S::InvalidAssociation,D::Association); expect(checked(zone,limits(),1,2),S::InvalidAssociation,D::Association);
    }
    for (const char* kind : {"PARTICIPANT","VLOCATION","VRESOURCE","VALARM"}) {
        Session kids; parse(kids,calendar(event(component(kind,"RRULE:FREQ=BAD\r\nRDATE:bad\r\n"))));
        counts(checked(kids),0,0,0,0,0); expect(checked(kids,limits(),1,2),S::InvalidAssociation,D::Association);
    }
}
void compositionAndLifetime() {
    const std::string recurrence="RRULE:FREQ=DAILY\r\nRDATE:"+time1+","+time2+"\r\nEXDATE:"+time1+
        "\r\nRECURRENCE-ID;RANGE=THISANDFUTURE:"+time1+"\r\n";
    for (bool chunked : {false,true}) for (size_t split : {size_t(1),size_t(7),SIZE_MAX}) {
        Session s; parse(s,calendar(event(recurrence)),chunked,split);
        const IcsSourceLocal::Limits sl={128,8,32,32,64,128,limits().context.event,limits().context.timezone};
        const auto source=IcsSourceLocal::inspect(s.stage,sl); const auto r=checked(s); counts(r,1,1,1,2,1,true);
        const auto again=checked(s); same(r,again);
        const auto after=IcsSourceLocal::inspect(s.stage,sl);
        assert(source.status==after.status && source.eventDeferred==after.eventDeferred && source.sourceDeferred==after.sourceDeferred);
        assert(after.sourceDeferred&IcsSourceLocal::SourceSemanticsPending);
        s.stage.release(); expect(checked(s),S::Unavailable,D::Association);
        assert(s.stage.status()==Stage::Status::StagedSuccessfully && !s.stage.component(0));
        std::fill(s.data.data(),s.data.data()+s.data.capacity,0xdd); same(r,again);
        assert(r.deferred&N::RangeReviewPending);
    }
}
}
int main() {
    static_assert(std::is_standard_layout<N::Report>::value && std::is_trivially_copyable<N::Report>::value,"Copied diagnostics have no owner");
    static_assert(std::is_same<decltype(N::Report::eventStatus),E::Status>::value,"Event status domain preserved");
    static_assert(std::is_same<decltype(N::Report::contextStatus),X::Status>::value,"Context status domain preserved");
    static_assert(std::is_same<decltype(N::Report::temporalStatus),T::Status>::value,"Typed list status domain preserved");
    rulesAndNoRecurrence(); listsAndRelationships(); malformedListsAndFirstFailure(); aggregateBudgets();
    recurrenceIdentity(); admissionAndCorruption(); compositionAndLifetime();
    std::printf("VEVENT recurrence coverage: %zu deterministic cases; zero audited allocations; report %zu/%zu\n",scenarios,sizeof(N::Report),alignof(N::Report));
}
