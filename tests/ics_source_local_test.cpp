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
namespace E = IcsSourceLocal;
namespace EV = IcsEventLocal;
namespace TZ = IcsTimezoneLocal;
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
const std::string eventFields="UID:event\r\nDTSTAMP:20261012T080000Z\r\nDTSTART:20261012T090000Z\r\n";
std::string event(const std::string& p=eventFields) { return "BEGIN:VEVENT\r\n"+p+"END:VEVENT\r\n"; }
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
E::Limits limits() { return {128,8,64,32,64,128,{128,1024,4096},{128,128,4096,64,4096}}; }
E::Report checked(Session& s,E::Limits budget=limits()) {
    const auto bytes=s.data.bytes; const auto before=s.stage.counts(); const auto status=s.stage.status();
    E::Report r;
    { Audit a; r=E::inspect(s.stage,budget); }
    const auto after=s.stage.counts();
    assert(bytes==s.data.bytes && status==s.stage.status()); s.data.check();
    assert(before.components==after.components && before.calendars==after.calendars &&
        before.observedProperties==after.observedProperties && before.ignoredOpaqueProperties==after.ignoredOpaqueProperties &&
        before.reviewItems==after.reviewItems && before.storedProperties==after.storedProperties &&
        before.poolBytes==after.poolBytes && before.reviewBytes==after.reviewBytes);
    ++scenarios;
    if (r.status==S::LocalCoveragePassed) assert(r.sourceDeferred&E::SourceSemanticsPending);
    else {
        assert(!(r.eventDeferred|r.timezoneDeferred|r.sourceDeferred));
        assert(!(r.calendars|r.events|r.timezones|r.observances|r.projectedNonEventComponents));
    }
    return r;
}
void expect(const E::Report& r,S status,size_t component=Stage::None,size_t property=Stage::None,C code=C::Unknown) {
    if (r.status!=status || r.componentIndex!=component || r.propertyIndex!=property || r.propertyCode!=code)
        std::fprintf(stderr,"scenario %zu expected %u/%zu/%zu/%u got %u/%zu/%zu/%u\n",scenarios,
            unsigned(status),component,property,unsigned(code),unsigned(r.status),r.componentIndex,r.propertyIndex,unsigned(r.propertyCode));
    assert(r.status==status && r.componentIndex==component && r.propertyIndex==property && r.propertyCode==code);
    if (status==S::LocalCoveragePassed || (status!=S::EventLocalFailure && status!=S::TimezoneLocalFailure && status!=S::ObservanceLocalFailure))
        assert(r.failureKind==E::FailureKind::None);
}
void counts(const E::Report& r,size_t roots,size_t events,size_t zones,size_t onsets,size_t projected) {
    expect(r,S::LocalCoveragePassed);
    assert(r.calendars==roots && r.events==events && r.timezones==zones &&
        r.observances==onsets && r.projectedNonEventComponents==projected);
}
size_t find(const Stage& s,size_t owner,C code) {
    for (uint32_t i=s.component(owner)->firstProperty;i!=Stage::None;i=s.property(i)->next)
        if (s.property(i)->code==code) return i;
    return Stage::None;
}
E::Report one(const std::string& body,size_t roots,size_t events,size_t zones,size_t onsets,size_t projected) {
    Session s; parse(s,body); const auto r=checked(s); counts(r,roots,events,zones,onsets,projected); return r;
}
void enumeration() {
    auto r=one(calendar(event()),1,1,0,0,0);
    assert(r.sourceDeferred==E::SourceSemanticsPending && !r.timezoneDeferred);
    assert(r.eventDeferred==(EV::SourceValidationRequired|EV::IdentityReconciliationPending));
    one(calendar(event()+event()+event()),1,3,0,0,0); // Repeated UIDs are not reconciled.
    one(calendar(event())+calendar(event()),2,2,0,0,0);
    one(calendar(timezone()),1,0,1,1,0); // Coverage of zero events is not a complete empty schedule.
    one(calendar(timezone("TZID:Test\r\n",observance(required)+observance(required,"DAYLIGHT"))),1,0,1,2,0);
    const std::string referencing=event("UID:a\r\nDTSTAMP:20261012T080000Z\r\nDTSTART;TZID=Test:20261012T090000\r\n");
    for (bool late : {false,true}) {
        r=one(calendar(late ? referencing+timezone() : timezone()+referencing),1,1,1,1,0);
        assert(r.eventDeferred&EV::TimezoneResolutionPending);
        assert(r.timezoneDeferred&TZ::TransitionResolutionPending);
    }
    one(calendar(timezone()+timezone()),1,0,2,2,0); // No duplicate-TZID selection.
    Session s; parse(s,calendar(event()+timezone())+calendar(timezone()+event()));
    assert(s.stage.component(4)->kind==IcsStructure::Kind::Calendar);
    assert(s.stage.component(4)->ordinal==5 && s.stage.component(4)->calendarOrdinal==2);
    counts(checked(s),2,2,2,2,0); counts(checked(s),2,2,2,2,0);
}
void firstFailure() {
    {
        Session s; parse(s,calendar(event()+event("DTSTAMP:20261012T080000Z\r\nDTSTART:20261012T090000Z\r\n")));
        auto r=checked(s); expect(r,S::EventLocalFailure,2,Stage::None,C::Uid);
        assert(r.failureKind==E::FailureKind::Event && r.eventStatus==EV::Status::MissingRequired);
    }
    for (const std::string& bad : {std::string("UID:bad\r\nDTSTAMP:bad\r\nDTSTART:20261012T090000Z\r\n"),
        eventFields+"SUMMARY:bad\\q\r\n"}) {
        Session s; parse(s,calendar(event()+event(bad)));
        const auto local=EV::validate(s.stage,0,2,limits().event); const auto r=checked(s);
        expect(r,S::EventLocalFailure,2,local.propertyIndex,local.propertyCode);
        assert(r.failureKind==E::FailureKind::Event && r.eventStatus==local.status && r.eventStatus==EV::Status::MalformedValue);
    }
    {
        Session s; parse(s,calendar(event()+timezone("TZID:Test\r\n",observance(required)+
            observance(onset+"TZOFFSETFROM:-0500\r\n","DAYLIGHT"))));
        const auto r=checked(s); expect(r,S::ObservanceLocalFailure,4,Stage::None,C::Tzoffsetto);
        assert(r.failureKind==E::FailureKind::Observance && r.timezoneStatus==TZ::Status::MissingRequired);
    }
    {
        Session s; parse(s,calendar(event()+timezone("TZID:bad\\q\r\n")));
        const auto local=TZ::validateTimezone(s.stage,0,2,limits().timezone); const auto r=checked(s);
        expect(r,S::TimezoneLocalFailure,2,local.propertyIndex,C::Tzid);
        assert(r.failureKind==E::FailureKind::Timezone && r.timezoneStatus==local.status);
    }
    {
        Session s; parse(s,calendar(timezone("",observance(""))+event("")));
        const auto r=checked(s); expect(r,S::TimezoneLocalFailure,1,Stage::None,C::Tzid);
        assert(r.timezoneStatus==TZ::Status::MissingRequired); // Parent before observance and later event.
    }
    {
        Session s; parse(s,calendar(event(""))+calendar(event()));
        const_cast<Stage::Component*>(s.stage.component(3))->closed=0;
        expect(checked(s),S::EventLocalFailure,1,Stage::None,C::Uid); // No prepass hides earlier local failure.
    }
}
void metadataAndProjection() {
    const std::string opaque="BEGIN:X-CUSTOM\r\nUID:ignored\r\nSUMMARY:ignored\r\n"
        "BEGIN:REGISTERED-LOOKING\r\nRRULE:ignored\r\nEND:REGISTERED-LOOKING\r\nEND:X-CUSTOM\r\n";
    auto r=one(calendar(opaque),1,0,0,0,2);
    assert(r.sourceDeferred==(E::SourceSemanticsPending|E::NonEventProjectionObserved));
    assert(!r.eventDeferred && !r.timezoneDeferred);
    one(calendar(event()+opaque),1,1,0,0,2);
    one(calendar("BEGIN:X-LARGE\r\nX-P:"+std::string(3000,'x')+"\r\nEND:X-LARGE\r\n"),1,0,0,0,1);
    for (const std::string m : {"METHOD:CANCEL\r\n","METHOD:\r\n","CALSCALE:GREGORIAN\r\n",
        "NAME:Calendar title\r\n","DESCRIPTION:details\r\n","UID:calendar\r\n","X-META;X-P=^^n:unreviewed\r\n"}) {
        r=one(calendar(opaque,m),1,0,0,0,2); assert(r.sourceDeferred&E::CalendarMetadataPending);
    }
    r=one(calendar(opaque,"COLOR:red\r\nSOURCE:https://example.invalid\r\nREFRESH-INTERVAL:PT1H\r\n"),1,0,0,0,2);
    assert(!(r.sourceDeferred&E::CalendarMetadataPending));
    r=one(calendar(opaque,"COLOR;X-P=unknown:red\r\n"),1,0,0,0,2);
    assert(r.sourceDeferred&E::CalendarMetadataPending); // Explicit omission promoted to unreviewed retention.
    const std::string children="BEGIN:PARTICIPANT\r\nUID:child\r\nSUMMARY:child\r\n"
        "BEGIN:VLOCATION\r\nUID:location\r\nEND:VLOCATION\r\n"
        "BEGIN:VRESOURCE\r\nUID:resource\r\nEND:VRESOURCE\r\nEND:PARTICIPANT\r\n"
        "BEGIN:VLOCATION\r\nUID:location2\r\nEND:VLOCATION\r\n"
        "BEGIN:VRESOURCE\r\nUID:resource2\r\nEND:VRESOURCE\r\n";
    r=one(calendar(event(eventFields+children)),1,1,0,0,0);
    assert(r.sourceDeferred==(E::SourceSemanticsPending|E::NestedComponentReviewPending));
    assert(r.eventDeferred==(EV::SourceValidationRequired|EV::IdentityReconciliationPending));
    Session missing; parse(missing,calendar(event("DTSTAMP:20261012T080000Z\r\nDTSTART:20261012T090000Z\r\n"+children)));
    expect(checked(missing),S::EventLocalFailure,1,Stage::None,C::Uid);
    const std::string projected="BEGIN:VTODO\r\nUID:task\r\nDESCRIPTION:omitted\\q\r\n"
        "BEGIN:VALARM\r\nACTION:DISPLAY\r\nEND:VALARM\r\n"+children+"END:VTODO\r\n"
        "BEGIN:VJOURNAL\r\nUID:journal\r\nEND:VJOURNAL\r\nBEGIN:VFREEBUSY\r\nUID:freebusy\r\nEND:VFREEBUSY\r\n";
    r=one(calendar(projected),1,0,0,0,9);
    assert(r.sourceDeferred==(E::SourceSemanticsPending|E::NonEventProjectionObserved));
    r=one(calendar(event(eventFields+"BEGIN:VALARM\r\nACTION:DISPLAY\r\nEND:VALARM\r\n")),1,1,0,0,1);
    assert(r.sourceDeferred&E::NonEventProjectionObserved);
    Session methods; parse(methods,calendar(event("UID:a\r\nDTSTAMP:20261012T080000Z\r\n"),"METHOD:CANCEL\r\n")+
        calendar(event("UID:b\r\nDTSTAMP:20261012T080000Z\r\n")));
    expect(checked(methods),S::EventLocalFailure,3,Stage::None,C::Dtstart);
}
void masks() {
    const std::string e="UID:event\r\nDTSTAMP:20261012T080060Z\r\nDTSTART;TZID=Test:20261012T090000\r\n"
        "DTEND;TZID=Other:20261012T100000\r\nRRULE:FREQ=YEARLY\r\nRRULE:RSCALE=HEBREW;SKIP=FORWARD\r\n"
        "RDATE:unparsed\r\nRECURRENCE-ID;RANGE=THISANDFUTURE:20261012T090000\r\nSTATUS:CANCELLED\r\nX-E:review\r\n";
    const std::string z=timezone("TZID:Test\r\nLAST-MODIFIED:20261012T080060Z\r\nTZURL:unvalidated\r\nX-Z:review\r\n",
        observance(required+"RRULE:FREQ=YEARLY\r\nRRULE:RSCALE=HEBREW\r\nRDATE:20261013T020060\r\nEXDATE:review\r\nX-O:review\r\n"));
    Session s; parse(s,calendar(event(e)+z,"METHOD:CANCEL\r\n"));
    const auto r=checked(s); counts(r,1,1,1,1,0);
    uint32_t em=0,zm=0;
    { Audit a;
        const auto ev=EV::validate(s.stage,0,1,limits().event);
        const auto tz=TZ::validateTimezone(s.stage,0,2,limits().timezone);
        const auto ob=TZ::validateObservance(s.stage,2,3,limits().timezone);
        assert(ev.status==EV::Status::LocalChecksPassed && tz.status==TZ::Status::LocalChecksPassed && ob.status==TZ::Status::LocalChecksPassed);
        em=ev.deferred; zm=tz.deferred|ob.deferred;
    }
    assert(r.eventDeferred==em && r.timezoneDeferred==zm);
    assert(r.eventDeferred==((1U<<10)-1) && r.timezoneDeferred==((1U<<9)-1));
    assert(r.sourceDeferred==(E::SourceSemanticsPending|E::CalendarMetadataPending));
    Session separate; parse(separate,calendar(event(eventFields+"RRULE:deferred\r\n")+
        event(eventFields+"X-REVIEW:deferred\r\n"))+calendar(timezone()));
    const auto d=checked(separate); counts(d,2,2,1,1,0);
    assert(d.eventDeferred==(EV::SourceValidationRequired|EV::IdentityReconciliationPending|
        EV::RecurrenceValidationPending|EV::ExtensionReviewPending|EV::OptionalPropertyValidationPending));
    assert(d.timezoneDeferred==(TZ::SourceValidationRequired|TZ::TransitionResolutionPending|TZ::TimezoneIdentityReconciliationPending));
}
void budgets() {
    Session s; parse(s,calendar(event()+event()+timezone("TZID:Test\r\n",observance(required)+observance(required,"DAYLIGHT")))+
        calendar(event()+timezone()));
    auto l=limits(); l.maxComponents=10; l.maxCalendars=2; l.maxEvents=3; l.maxTimezones=2; l.maxObservances=3;
    l.maxCalendarProperties=2; counts(checked(s,l),2,3,2,3,0);
    l.maxComponents=9; expect(checked(s,l),S::ResourceLimit); l.maxComponents=10;
    l.maxComponents=0; expect(checked(s,l),S::ResourceLimit); l.maxComponents=10;
    l.maxCalendars=1; expect(checked(s,l),S::ResourceLimit,6); l.maxCalendars=2;
    l.maxCalendars=0; expect(checked(s,l),S::ResourceLimit,0); l.maxCalendars=2;
    l.maxEvents=2; expect(checked(s,l),S::ResourceLimit,7); l.maxEvents=3;
    l.maxEvents=0; expect(checked(s,l),S::ResourceLimit,1); l.maxEvents=3;
    l.maxTimezones=1; expect(checked(s,l),S::ResourceLimit,8); l.maxTimezones=2;
    l.maxTimezones=0; expect(checked(s,l),S::ResourceLimit,3); l.maxTimezones=2;
    l.maxObservances=2; expect(checked(s,l),S::ResourceLimit,9); l.maxObservances=3;
    l.maxObservances=0; expect(checked(s,l),S::ResourceLimit,4); l.maxObservances=3;
    l.maxCalendarProperties=1; expect(checked(s,l),S::ResourceLimit,0,1,C::Prodid);
    l.maxCalendarProperties=0; expect(checked(s,l),S::ResourceLimit,0,0,C::Version);
    l=limits(); l.event.maxEventProperties=2;
    auto r=checked(s,l); expect(r,S::EventLocalFailure,1,find(s.stage,1,C::Dtstart),C::Dtstart);
    assert(r.eventStatus==EV::Status::ResourceLimit);
    l=limits(); l.event.maxCalendarProperties=1;
    r=checked(s,l); expect(r,S::EventLocalFailure,1,1,C::Prodid); assert(r.eventStatus==EV::Status::ResourceLimit);
    l=limits(); l.timezone.maxTimezoneProperties=0;
    r=checked(s,l); expect(r,S::TimezoneLocalFailure,3,find(s.stage,3,C::Tzid),C::Tzid);
    assert(r.timezoneStatus==TZ::Status::ResourceLimit);
    l=limits(); l.timezone.maxObservanceProperties=2;
    r=checked(s,l); expect(r,S::ObservanceLocalFailure,4,find(s.stage,4,C::Tzoffsetto),C::Tzoffsetto);
    assert(r.timezoneStatus==TZ::Status::ResourceLimit);
    l=limits(); l.maxComponents=l.maxCalendars=l.maxEvents=l.maxTimezones=l.maxObservances=SIZE_MAX;
    counts(checked(s,l),2,3,2,3,0);
    Session root; parse(root,calendar(event())+calendar(event(),"METHOD:PUBLISH\r\n"));
    l=limits(); l.maxCalendarProperties=2;
    expect(checked(root,l),S::ResourceLimit,2,find(root.stage,2,C::Method),C::Method);
    Session none; parse(none,calendar(timezone())); l=limits(); l.maxEvents=0; counts(checked(none,l),1,0,1,1,0);
    Session bad; parse(bad,calendar(event(""))); l=limits(); l.maxEvents=0;
    expect(checked(bad,l),S::ResourceLimit,1);
    const_cast<Stage::Component*>(bad.stage.component(1))->closed=0;
    expect(checked(bad,l),S::InvalidAssociation,1);
    l.maxComponents=1; expect(checked(bad,l),S::ResourceLimit); // Source capacity precedes descriptor inspection.
}
void corruptions() {
    for (unsigned damage=0;damage<11;++damage) {
        Session s; parse(s,calendar(event())+calendar(event()));
        auto* c=const_cast<Stage::Component*>(s.stage.component(3));
        switch (damage) {
        case 0: c->ordinal=99; break;
        case 1: c->closed=0; break;
        case 2: c->parentOrdinal=0; break;
        case 3: c->parentOrdinal=99; break;
        case 4: c->parentOrdinal=4; break;
        case 5: c->calendarOrdinal=1; break;
        case 6: c->parentOrdinal=1; break;
        case 7: c->kind=IcsStructure::Kind::Unsupported; break;
        case 8: c->flags|=Stage::ProjectedOut; break;
        case 9: c->flags|=Stage::OpaqueProjectedOut; break;
        case 10: c->parentOrdinal=1; c->calendarOrdinal=1; break; // Both match old root, not active root.
        }
        expect(checked(s),S::InvalidAssociation,3);
    }
    for (unsigned damage=0;damage<4;++damage) {
        Session s; parse(s,calendar(event())+calendar(event())); auto* c=const_cast<Stage::Component*>(s.stage.component(2));
        if (!damage) c->parentOrdinal=1;
        else if (damage==1) c->calendarOrdinal=0;
        else if (damage==2) c->calendarOrdinal=1;
        else c->ordinal=1;
        expect(checked(s),S::InvalidAssociation,2);
    }
    for (unsigned damage=0;damage<5;++damage) {
        Session s; parse(s,calendar(timezone())); auto* c=const_cast<Stage::Component*>(s.stage.component(2));
        if (!damage) c->parentOrdinal=1;
        else if (damage==1) c->parentOrdinal=UINT64_MAX;
        else if (damage==2) c->calendarOrdinal=99;
        else if (damage==3) c->closed=0;
        else c->kind=IcsStructure::Kind::Event;
        expect(checked(s),S::InvalidAssociation,2);
    }
    for (bool root : {true,false}) for (unsigned damage=0;damage<6;++damage) {
        Session s; parse(s,calendar(event())); const size_t owner=root ? 0 : 1;
        auto* c=const_cast<Stage::Component*>(s.stage.component(owner));
        auto* p=const_cast<Stage::Property*>(s.stage.property(c->firstProperty));
        switch (damage) {
        case 0: p->owner=99; break;
        case 1: p->next=c->firstProperty; break;
        case 2: p->next=Stage::None-1; break;
        case 3: c->lastProperty=c->firstProperty; break;
        case 4: ++c->observedProperties; break;
        case 5: c->firstProperty=Stage::None-1; break;
        }
        const auto r=checked(s); assert(r.componentIndex==owner);
        if (root) assert(r.status==S::InvalidAssociation && r.failureKind==E::FailureKind::None);
        else assert(r.status==S::EventLocalFailure && r.failureKind==E::FailureKind::Event && r.eventStatus==EV::Status::InvalidAssociation);
    }
    Session opaque; parse(opaque,calendar("BEGIN:X-CUSTOM\r\nUID:ignored\r\nEND:X-CUSTOM\r\n"));
    const_cast<Stage::Component*>(opaque.stage.component(1))->flags &= uint16_t(~Stage::OpaqueProjectedOut);
    expect(checked(opaque),S::InvalidAssociation,1);
    Session child; parse(child,calendar(event(eventFields+"BEGIN:PARTICIPANT\r\nUID:child\r\nEND:PARTICIPANT\r\n")));
    const_cast<Stage::Component*>(child.stage.component(2))->parentOrdinal=1;
    expect(checked(child),S::InvalidAssociation,2);
}
void lifetimeAndIntegration() {
    Stage fresh; { Audit a; expect(E::inspect(fresh,limits()),S::Unavailable); }
    Session s; expect(checked(s),S::Unavailable); parse(s,calendar(event()));
    const E::Report saved=checked(s); s.stage.release(); std::memset(s.data.data(),0xdd,s.data.capacity);
    expect(checked(s),S::Unavailable); counts(saved,1,1,0,0,0); // Report has no dependency on released bytes.
    const std::string folded=calendar(event(eventFields+"SUMMARY:caf\xc3\r\n \xa9\r\n")+timezone());
    for (bool chunked : {false,true}) for (size_t step : {size_t(1),size_t(7),size_t(31),SIZE_MAX}) {
        Session b; parse(b,folded,chunked,step); counts(checked(b),1,1,1,1,0);
    }
    for (bool truncated : {false,true}) {
        Session b; std::string raw=response(calendar(event())+(truncated ? "" : "bad\r\n"),false);
        if (truncated) raw.resize(raw.size()-8);
        { Audit a;
            const auto fed=b.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size());
            assert(fed==IcsIngestion::Status::InProgress || fed==IcsIngestion::Status::CallbackFailure);
            assert(b.http.finish()!=IcsIngestion::Status::Complete);
            assert(b.stage.finish(b.http,b.structure)==Stage::Status::UpstreamFailure);
        }
        expect(checked(b),S::Unavailable);
    }
}
}
int main() {
    static_assert(std::is_pod<E::Report>::value &&
        std::is_same<decltype(E::Report::eventStatus),EV::Status>::value &&
        std::is_same<decltype(E::Report::timezoneStatus),TZ::Status>::value,
        "copied bounded report, preserving distinct local diagnostic domains");
    enumeration(); firstFailure(); metadataAndProjection(); masks(); budgets(); corruptions(); lifetimeAndIntegration();
    assert(allocations==0);
    std::printf("Source-local coverage: %zu deterministic scenarios; zero audited allocations; report %zu/%zu\n",
        scenarios,sizeof(E::Report),alignof(E::Report));
}
