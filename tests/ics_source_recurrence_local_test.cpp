#include "../application/ics_source_recurrence_local.h"
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
namespace N = IcsSourceRecurrenceLocal;
namespace SL = IcsSourceLocal;
namespace E = IcsEventRecurrenceLocal;
namespace O = IcsObservanceRecurrenceLocal;
namespace X = IcsRecurContext;
namespace Z = IcsTimezoneLocal;
using Stage = IcsSourceStage;
using Code = Stage::Code;
using S = N::Status;
using D = N::FailureDomain;
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
Stage::Limits storage() { return {128, 1024, 65536, 128, 16384}; }
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
const std::string identity = "UID:shared\r\nDTSTAMP:20261017T120000Z\r\n";
const std::string utc = "DTSTART:20261017T130000Z\r\n";
const std::string onset = "DTSTART:20261017T020000\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\n";
std::string component(const std::string& kind, const std::string& fields) { return "BEGIN:" + kind + "\r\n" + fields + "END:" + kind + "\r\n"; }
std::string event(const std::string& recurrence = "", const std::string& start = utc) { return component("VEVENT", identity + start + recurrence); }
std::string observance(const std::string& recurrence = "", const std::string& kind = "STANDARD") { return component(kind, onset + recurrence); }
std::string zone(const std::string& children = observance()) { return component("VTIMEZONE", "TZID:Custom\r\n" + children); }
std::string calendar(const std::string& children, const std::string& metadata = "") { return component("VCALENDAR", "VERSION:2.0\r\nPRODID:synthetic\r\n" + metadata + children); }
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
            if (fed != IcsIngestion::Status::InProgress)
                std::fprintf(stderr, "fixture after %zu cases: ingestion %u, structure %u, stage %u\n", scenarios,
                    unsigned(fed), unsigned(s.structure.status()), unsigned(s.stage.status()));
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
N::Limits limits() {
    const X::Limits context = {128, {4096,64,256}, {128,128,4096}, {128,128,4096,64,4096}};
    return {{128,8,64,32,64,128,context.event,context.timezone}, {128,32,32,32,128,32768,context}, {32,context}};
}
void same(const SL::Report& a, const SL::Report& b) {
    assert(a.status == b.status && a.failureKind == b.failureKind && a.componentIndex == b.componentIndex &&
        a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode && a.eventStatus == b.eventStatus && a.timezoneStatus == b.timezoneStatus &&
        a.eventDeferred == b.eventDeferred && a.timezoneDeferred == b.timezoneDeferred && a.sourceDeferred == b.sourceDeferred &&
        a.calendars == b.calendars && a.events == b.events && a.timezones == b.timezones && a.observances == b.observances &&
        a.projectedNonEventComponents == b.projectedNonEventComponents);
}
void same(const E::Report& a, const E::Report& b) {
    assert(a.status == b.status && a.failureDomain == b.failureDomain && a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode &&
        a.offset == b.offset && a.eventStatus == b.eventStatus && a.contextStatus == b.contextStatus && a.contextFailureDomain == b.contextFailureDomain &&
        a.temporalStatus == b.temporalStatus && a.deferred == b.deferred && a.contextualRuleDeferred == b.contextualRuleDeferred &&
        a.rruleProperties == b.rruleProperties && a.rdateProperties == b.rdateProperties && a.exdateProperties == b.exdateProperties &&
        a.rdateMembers == b.rdateMembers && a.exdateMembers == b.exdateMembers && a.recurrenceIdPresent == b.recurrenceIdPresent);
}
void same(const O::Report& a, const O::Report& b) {
    assert(a.status == b.status && a.failureDomain == b.failureDomain && a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode &&
        a.offset == b.offset && a.observanceStatus == b.observanceStatus && a.contextStatus == b.contextStatus &&
        a.contextFailureDomain == b.contextFailureDomain && a.ruleStatus == b.ruleStatus && a.observanceLocalDeferred == b.observanceLocalDeferred &&
        a.contextualRuleDeferred == b.contextualRuleDeferred && a.rruleProperties == b.rruleProperties && a.rdateProperties == b.rdateProperties && a.exdateProperties == b.exdateProperties);
}
void same(const N::Report& a, const N::Report& b) {
    assert(a.status == b.status && a.failureDomain == b.failureDomain && a.componentIndex == b.componentIndex);
    same(a.sourceReport, b.sourceReport); same(a.eventReport, b.eventReport); same(a.observanceReport, b.observanceReport);
    assert(a.sourceDeferred == b.sourceDeferred && a.sourceEventDeferred == b.sourceEventDeferred && a.sourceTimezoneDeferred == b.sourceTimezoneDeferred &&
        a.eventRecurrenceDeferred == b.eventRecurrenceDeferred && a.eventContextualRuleDeferred == b.eventContextualRuleDeferred &&
        a.observanceLocalDeferred == b.observanceLocalDeferred && a.observanceContextualRuleDeferred == b.observanceContextualRuleDeferred &&
        a.calendars == b.calendars && a.events == b.events && a.timezones == b.timezones && a.observances == b.observances &&
        a.projectedNonEventComponents == b.projectedNonEventComponents && a.eventsRecurrenceChecked == b.eventsRecurrenceChecked &&
        a.observancesRecurrenceChecked == b.observancesRecurrenceChecked);
}
void failed(const N::Report& r) {
    assert(!r.sourceDeferred && !r.sourceEventDeferred && !r.sourceTimezoneDeferred && !r.eventRecurrenceDeferred && !r.eventContextualRuleDeferred &&
        !r.observanceLocalDeferred && !r.observanceContextualRuleDeferred);
    assert(!r.calendars && !r.events && !r.timezones && !r.observances && !r.projectedNonEventComponents &&
        !r.eventsRecurrenceChecked && !r.observancesRecurrenceChecked);
}
N::Report checked(Session& s, N::Limits b = limits()) {
    const auto bytes = s.data.bytes; const auto before = s.stage.counts(); const auto status = s.stage.status(); N::Report r;
    { Audit a; r = N::inspect(s.stage, b); }
    const auto after = s.stage.counts(); assert(bytes == s.data.bytes && status == s.stage.status()); s.data.check();
    assert(before.components == after.components && before.calendars == after.calendars && before.observedProperties == after.observedProperties &&
        before.ignoredOpaqueProperties == after.ignoredOpaqueProperties && before.storedProperties == after.storedProperties && before.poolBytes == after.poolBytes &&
        before.reviewItems == after.reviewItems && before.reviewBytes == after.reviewBytes);
    ++scenarios;
    if (r.status == S::LocalCoveragePassed) {
        const auto source = SL::inspect(s.stage, b.source); assert(source.status == SL::Status::LocalCoveragePassed);
        assert(r.failureDomain == D::None && r.componentIndex == Stage::None && (r.sourceDeferred & SL::SourceSemanticsPending));
        assert(r.sourceDeferred == source.sourceDeferred && r.sourceEventDeferred == source.eventDeferred && r.sourceTimezoneDeferred == source.timezoneDeferred);
        assert(r.calendars == source.calendars && r.events == source.events && r.timezones == source.timezones && r.observances == source.observances &&
            r.projectedNonEventComponents == source.projectedNonEventComponents && r.eventsRecurrenceChecked == source.events && r.observancesRecurrenceChecked == source.observances);
    } else failed(r);
    return r;
}
void expect(const N::Report& r, S status, D domain = D::None, size_t index = Stage::None) {
    if (r.status != status || r.failureDomain != domain || r.componentIndex != index)
        std::fprintf(stderr, "scenario %zu expected %u/%u/%zu got %u/%u/%zu\n", scenarios,
            unsigned(status), unsigned(domain), index, unsigned(r.status), unsigned(r.failureDomain), r.componentIndex);
    assert(r.status == status && r.failureDomain == domain && r.componentIndex == index);
}
void counts(const N::Report& r, size_t roots, size_t events, size_t zones, size_t onsets, size_t projected = 0) {
    expect(r, S::LocalCoveragePassed);
    assert(r.calendars == roots && r.events == events && r.timezones == zones && r.observances == onsets && r.projectedNonEventComponents == projected);
    assert(r.eventsRecurrenceChecked == events && r.observancesRecurrenceChecked == onsets);
}
void sourceFailure(Session& s, N::Limits b = limits()) {
    const auto original = SL::inspect(s.stage, b.source); assert(original.status != SL::Status::LocalCoveragePassed);
    const auto r = checked(s, b); expect(r, S::SourceLocalFailure, D::SourceLocal, original.componentIndex); same(r.sourceReport, original);
}
void eventFailure(Session& s, size_t parent, size_t owner, N::Limits b = limits()) {
    assert(SL::inspect(s.stage, b.source).status == SL::Status::LocalCoveragePassed);
    const auto original = E::inspect(s.stage, parent, owner, b.event); assert(original.status != E::Status::LocalCoveragePassed);
    const auto r = checked(s, b); expect(r, S::EventRecurrenceFailure, D::EventRecurrence, owner); same(r.eventReport, original);
}
void observanceFailure(Session& s, size_t parent, size_t owner, N::Limits b = limits()) {
    assert(SL::inspect(s.stage, b.source).status == SL::Status::LocalCoveragePassed);
    const auto original = O::inspect(s.stage, parent, owner, b.observance); assert(original.status != O::Status::LocalCoveragePassed);
    const auto r = checked(s, b); expect(r, S::ObservanceRecurrenceFailure, D::ObservanceRecurrence, owner); same(r.observanceReport, original);
}
void enumerationAndMasks() {
    { Session s; parse(s, calendar(event())); const auto r = checked(s); counts(r, 1, 1, 0, 0);
        assert(r.eventRecurrenceDeferred == E::SourceSemanticValidationPending && !r.eventContextualRuleDeferred &&
            !r.observanceLocalDeferred && !r.observanceContextualRuleDeferred && !r.sourceTimezoneDeferred);
    }
    { Session s; parse(s, calendar(event() + event("RRULE:FREQ=DAILY\r\n") + event("RDATE:20261018T130000Z\r\nEXDATE:20261017T130000Z\r\nRECURRENCE-ID:20261017T130000Z\r\n")));
        counts(checked(s), 1, 3, 0, 0);
    }
    for (bool late : {false, true}) {
        const auto referencing = event("RRULE:FREQ=DAILY;UNTIL=20271017T130000Z\r\n", "DTSTART;TZID=Custom:20261017T130000\r\n");
        Session s; parse(s, calendar(late ? referencing + zone() : zone() + referencing)); const auto r = checked(s); counts(r, 1, 1, 1, 1);
        assert(r.eventRecurrenceDeferred & E::TimezoneResolutionPending);
    }
    { Session s; parse(s, calendar(zone(observance() + observance("RDATE:20261018T020000,20261019T020000\r\n", "DAYLIGHT"))));
        const auto r = checked(s); counts(r, 1, 0, 1, 2);
        assert(!r.eventRecurrenceDeferred && !r.eventContextualRuleDeferred && !r.sourceEventDeferred && !r.observanceContextualRuleDeferred);
    }
    const auto noStart = event("RRULE:FREQ=DAILY\r\n", "");
    const auto dated = event("RRULE:FREQ=DAILY;BYHOUR=2\r\nRDATE;VALUE=DATE:20261018\r\n", "DTSTART;VALUE=DATE:20261017\r\n");
    const auto detailed = event("RRULE;X-P=x:FREQ=DAILY\r\nRRULE:FREQ=WEEKLY\r\nRDATE;VALUE=PERIOD:20261018T130000Z/PT1H\r\n"
        "EXDATE;TZID=Other:20261017T130000\r\nRECURRENCE-ID;RANGE=THISANDFUTURE;X-P=x:20261017T130000Z\r\nX-E:unknown\r\n",
        "DTSTART;TZID=Custom:20261017T130060\r\n");
    const auto special = observance("RRULE;X-P=x:FREQ=YEARLY\r\nRRULE:FREQ=MONTHLY\r\nRDATE:20261018T020060\r\nEXDATE;VALUE=PERIOD:not-a-period\r\nX-O:unknown\r\n");
    Session s; parse(s, calendar(noStart + zone(special + observance("RRULE:FREQ=YEARLY\r\n", "DAYLIGHT")) + dated + zone(), "METHOD:PUBLISH\r\n") +
        calendar(zone(observance() + observance("RRULE:FREQ=YEARLY;UNTIL=20271017T020060Z\r\n", "DAYLIGHT")) + detailed, "CALSCALE:GREGORIAN\r\n"));
    const auto r = checked(s); counts(r, 2, 3, 3, 5);
    const auto a = E::inspect(s.stage, 0, 1, limits().event), b = E::inspect(s.stage, 0, 5, limits().event), c = E::inspect(s.stage, 8, 12, limits().event);
    assert(r.eventRecurrenceDeferred == (a.deferred | b.deferred | c.deferred));
    assert(r.eventContextualRuleDeferred == (a.contextualRuleDeferred | b.contextualRuleDeferred | c.contextualRuleDeferred));
    const auto d = O::inspect(s.stage, 2, 3, limits().observance), e = O::inspect(s.stage, 2, 4, limits().observance),
        f = O::inspect(s.stage, 6, 7, limits().observance), g = O::inspect(s.stage, 9, 10, limits().observance), h = O::inspect(s.stage, 9, 11, limits().observance);
    assert(r.observanceLocalDeferred == (d.observanceLocalDeferred | e.observanceLocalDeferred | f.observanceLocalDeferred | g.observanceLocalDeferred | h.observanceLocalDeferred));
    assert(r.observanceContextualRuleDeferred == (d.contextualRuleDeferred | e.contextualRuleDeferred | f.contextualRuleDeferred | g.contextualRuleDeferred | h.contextualRuleDeferred));
    const uint32_t eventBits = E::MissingStartContextPending | E::MultipleRrulePending | E::DateListContextPending | E::ParameterReviewPending |
        E::TimezoneResolutionPending | E::LeapSecondPending | E::PeriodSemanticsPending | E::OverrideIdentityPending | E::RangeReviewPending;
    assert((r.eventRecurrenceDeferred & eventBits) == eventBits && (r.sourceDeferred & SL::CalendarMetadataPending));
    assert(r.eventContextualRuleDeferred & X::IgnoredTimePartsPresent);
    assert((r.observanceLocalDeferred & (Z::ObservanceExdateReviewPending | Z::ExtensionReviewPending)) == (Z::ObservanceExdateReviewPending | Z::ExtensionReviewPending));
    assert(r.observanceContextualRuleDeferred & X::MultipleRrulePending);
}
void projections() {
    const auto opaque = component("X-OPAQUE", "UID:hidden\r\nRRULE:bad\r\n" + component("X-NESTED", "RRULE:bad\r\n"));
    for (const auto& children : {opaque, component("VTODO", "RRULE:bad\r\nRDATE:bad\r\n"), component("VJOURNAL", "RRULE:bad\r\n"), component("VFREEBUSY", "RRULE:bad\r\n")}) {
        Session s; parse(s, calendar(children)); const auto r = checked(s);
        counts(r, 1, 0, 0, 0, children == opaque ? 2 : 1);
        assert(r.sourceDeferred & SL::NonEventProjectionObserved);
        assert(!r.eventRecurrenceDeferred && !r.observanceLocalDeferred);
    }
    const auto nested = component("PARTICIPANT", "UID:child\r\nRRULE:bad\r\n" + component("VLOCATION", "RRULE:bad\r\n") + component("VRESOURCE", "RRULE:bad\r\n"));
    Session s; parse(s, calendar(event("X-E;X-P=x:review\r\n" + nested) + component("VTODO", component("VALARM", "ACTION:ignored\r\n") +
        component("PARTICIPANT", component("VLOCATION", "RRULE:bad\r\n"))) + component("VJOURNAL", "RRULE:bad\r\n") +
        component("VFREEBUSY", "RRULE:bad\r\n") + opaque, "METHOD:CANCEL\r\nCALSCALE:GREGORIAN\r\nX-META:review\r\n"));
    const auto r = checked(s); counts(r, 1, 1, 0, 0, 8);
    assert(r.sourceDeferred == (SL::SourceSemanticsPending | SL::CalendarMetadataPending | SL::NestedComponentReviewPending | SL::NonEventProjectionObserved));
    assert(r.eventRecurrenceDeferred == E::SourceSemanticValidationPending && !r.eventContextualRuleDeferred);
}
void failureOrdering() {
    { Session s; parse(s, calendar(event("RRULE:FREQ=BOGUS\r\n") + component("VEVENT", "DTSTAMP:20261017T120000Z\r\n" + utc)));
        assert(E::inspect(s.stage, 0, 1, limits().event).status == E::Status::RruleContextFailure);
        sourceFailure(s); const auto r = checked(s); assert(r.componentIndex == 2 && r.sourceReport.propertyCode == Code::Uid);
    }
    { Session s; parse(s, calendar(zone(observance("RRULE:FREQ=BOGUS\r\n") + observance("RDATE:bad\r\n", "DAYLIGHT"))));
        sourceFailure(s); assert(checked(s).componentIndex == 3);
    }
    { Session s; parse(s, calendar(event("RRULE:FREQ=BOGUS\r\n") + component("VTIMEZONE", "TZID:\r\n" + observance()))); sourceFailure(s); }
    { Session s; parse(s, calendar(event("RRULE:FREQ=BOGUS\r\n") + zone(observance("RRULE:FREQ=YEARLY;RSCALE=GREGORIAN\r\n")))); eventFailure(s, 0, 1); }
    { Session s; parse(s, calendar(zone(observance("RRULE:FREQ=YEARLY;UNTIL=20271017T020000\r\n")) + event("RRULE:FREQ=BOGUS\r\n"))); observanceFailure(s, 1, 2); }
    for (const char* bad : {"RRULE:FREQ=DAILY\r\nRRULE:FREQ=BOGUS\r\n", "RRULE:FREQ=DAILY;RSCALE=GREGORIAN\r\n", "EXDATE:20261017T130000Z,bad\r\n"}) {
        Session s; parse(s, calendar(event()) + calendar(event(bad))); eventFailure(s, 2, 3);
    }
    for (const char* bad : {"RRULE:FREQ=YEARLY\r\nRRULE:FREQ=BOGUS\r\n", "RRULE:FREQ=YEARLY;SKIP=OMIT\r\n"}) {
        Session s; parse(s, calendar(event() + zone()) + calendar(zone(observance() + observance(bad, "DAYLIGHT")))); observanceFailure(s, 5, 7);
    }
}
void budgets() {
    Session s; parse(s, calendar(event() + zone(observance() + observance("", "DAYLIGHT"))));
    auto exact = limits(); exact.source = {5,1,1,1,2,2,{2,3,4096},{1,3,4096,0,0}};
    exact.event.maxComponentProperties = 3; exact.event.context.event = {2,3,4096};
    exact.event.maxRruleProperties = exact.event.maxRdateProperties = exact.event.maxExdateProperties = 0;
    exact.event.maxTotalListMembers = exact.event.maxTotalListBytes = 0;
    exact.observance.maxRruleProperties = 0; exact.observance.context.timezone = {1,3,4096,0,0};
    counts(checked(s, exact), 1, 1, 1, 2);
    for (size_t field = 0; field < 6; ++field) {
        auto b = exact;
        switch (field) {
        case 0: --b.source.maxComponents; break; case 1: --b.source.maxCalendars; break;
        case 2: --b.source.maxEvents; break; case 3: --b.source.maxTimezones; break;
        case 4: --b.source.maxObservances; break; default: --b.source.maxCalendarProperties; break;
        }
        sourceFailure(s, b);
    }
    auto b = exact; b.event.maxComponentProperties = 2; eventFailure(s, 0, 1, b);
    b = exact; b.event.context.event.maxEventProperties = 2; eventFailure(s, 0, 1, b);
    b = exact; b.source.event.maxEventProperties = 2; sourceFailure(s, b);
    b = exact; b.observance.context.timezone.maxObservanceProperties = 2; observanceFailure(s, 2, 3, b);
    b = exact; b.source.timezone.maxObservanceProperties = 2; sourceFailure(s, b);
    { Session lists; parse(lists, calendar(event("RDATE:20261018T130000Z,20261019T130000Z\r\n") + zone(observance("RDATE:20261018T020000,20261019T020000\r\n"))));
        auto budget = limits(); budget.event.maxTotalListMembers = 2; budget.observance.context.timezone.maxRdateMembers = 2;
        counts(checked(lists, budget), 1, 1, 1, 1);
        budget.event.maxTotalListMembers = 1; eventFailure(lists, 0, 1, budget);
        budget = limits(); budget.observance.context.timezone.maxRdateMembers = 1; observanceFailure(lists, 2, 3, budget);
        budget = limits(); budget.source.timezone.maxRdateMembers = 1; sourceFailure(lists, budget);
    }
    { Session rules; parse(rules, calendar(event("RRULE:FREQ=DAILY\r\n") + zone(observance("RRULE:FREQ=YEARLY\r\n"))));
        auto budget = limits(); budget.event.context.rule.maxRuleBytes = 0; eventFailure(rules, 0, 1, budget);
        budget = limits(); budget.observance.context.maxComponentProperties = 3; observanceFailure(rules, 2, 3, budget);
    }
}
void corruptionAndLifetime() {
    { Stage fresh; N::Report r; { Audit a; r = N::inspect(fresh, limits()); }
        expect(r, S::SourceLocalFailure, D::SourceLocal); assert(r.sourceReport.status == SL::Status::Unavailable); failed(r);
    }
    { Session incomplete; sourceFailure(incomplete); }
    { Session cancelled; cancelled.stage.cancel(); sourceFailure(cancelled); }
    { Session incomplete; const std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        incomplete.http.feed(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()); incomplete.http.finish(); incomplete.stage.finish(incomplete.http, incomplete.structure);
        sourceFailure(incomplete);
    }
    Session s; parse(s, calendar(event() + zone()) + calendar(event()));
    auto& c = *const_cast<Stage::Component*>(s.stage.component(5)); const auto original = c;
    c.parentOrdinal = 1; sourceFailure(s); c = original;
    c.parentOrdinal = UINT64_MAX; sourceFailure(s); c = original;
    ++c.ordinal; sourceFailure(s); c = original;
    c.closed = 0; sourceFailure(s); c = original;
    ++c.calendarOrdinal; sourceFailure(s); c = original;
    auto& p = *const_cast<Stage::Property*>(s.stage.property(s.stage.component(1)->firstProperty)); const auto saved = p;
    p.next = s.stage.component(1)->firstProperty; sourceFailure(s); p = saved;
    const std::string body = calendar(event("RRULE:FREQ=DAILY\r\n") + zone(observance("RRULE:FREQ=YEARLY\r\nRDATE:20261018T020000\r\nEXDATE:uninterpreted\r\n") + observance("", "DAYLIGHT")));
    for (bool chunked : {false,true}) for (size_t split : {size_t(1),size_t(7),SIZE_MAX}) {
        Session live; parse(live, body, chunked, split); const auto r = checked(live); counts(r, 1, 1, 1, 2);
        const auto copy = r; same(r, checked(live));
        live.stage.release(); assert(live.stage.status() == Stage::Status::StagedSuccessfully && !live.stage.component(0)); sourceFailure(live);
        std::fill(live.data.data(), live.data.data() + live.data.capacity, 0xdd); same(r, copy);
        assert(r.sourceDeferred & SL::SourceSemanticsPending); assert(r.observanceLocalDeferred & Z::ObservanceExdateReviewPending);
    }
}
}
int main() {
    static_assert(std::is_trivially_copyable<N::Report>::value && std::is_standard_layout<N::Report>::value, "Copied scalar reports own no source storage");
    static_assert(std::is_same<decltype(N::Report::sourceReport), SL::Report>::value, "Original source diagnostic");
    static_assert(std::is_same<decltype(N::Report::eventReport), E::Report>::value, "Original event diagnostic");
    static_assert(std::is_same<decltype(N::Report::observanceReport), O::Report>::value, "Original observance diagnostic");
    enumerationAndMasks(); projections(); failureOrdering(); budgets(); corruptionAndLifetime();
    std::printf("Source recurrence coverage: %zu deterministic cases; zero audited allocations; report %zu/%zu\n", scenarios, sizeof(N::Report), alignof(N::Report));
}
