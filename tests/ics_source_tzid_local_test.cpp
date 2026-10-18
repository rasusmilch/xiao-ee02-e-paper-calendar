#include "../application/ics_source_tzid_local.h"
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
namespace N = IcsSourceTzidLocal;
namespace R = IcsTzidReferenceLocal;
namespace SL = IcsSourceLocal;
namespace T = IcsTypedValues;
namespace Z = IcsTimezoneLocal;
using Stage = IcsSourceStage;
using Code = Stage::Code;
using S = N::Status;
using M = R::Match;
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
std::string reference(const std::string& code = "DTSTART", const std::string& parameter = "Custom", const std::string& value = "20261017T130000") {
    return code + ";TZID=" + parameter + ":" + value + "\r\n";
}
std::string event(const std::string& fields = reference()) { return component("VEVENT", "UID:event\r\nDTSTAMP:20261017T120000Z\r\n" + fields); }
std::string zone(const std::string& fields = "TZID:Custom\r\n") { return component("VTIMEZONE", fields + component("STANDARD", "DTSTART:20261017T020000\r\nTZOFFSETFROM:+0000\r\nTZOFFSETTO:+0100\r\n")); }
std::string definition(const std::string& identity, const std::string& parameters = "") { return zone("TZID" + parameters + ":" + identity + "\r\n"); }
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
N::Limits limits() { return {{128,8,64,32,64,128,{128,128,4096},{128,128,4096,64,4096}}, {128,128,4096,{128,128,4096,64,4096}}, 128}; }
size_t find(const Stage& s, Code code = Code::Dtstart, size_t owner = 1) {
    for (uint32_t i = s.component(owner)->firstProperty; i != Stage::None; i = s.property(i)->next)
        if (s.property(i)->code == code) return i;
    return Stage::None;
}
void same(const SL::Report& a, const SL::Report& b) {
    assert(a.status == b.status && a.failureKind == b.failureKind && a.componentIndex == b.componentIndex &&
        a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode && a.eventStatus == b.eventStatus && a.timezoneStatus == b.timezoneStatus &&
        a.eventDeferred == b.eventDeferred && a.timezoneDeferred == b.timezoneDeferred && a.sourceDeferred == b.sourceDeferred &&
        a.calendars == b.calendars && a.events == b.events && a.timezones == b.timezones && a.observances == b.observances &&
        a.projectedNonEventComponents == b.projectedNonEventComponents);
}
void same(const R::Report& a, const R::Report& b) {
    assert(a.status == b.status && a.match == b.match && a.temporalPropertyIndex == b.temporalPropertyIndex && a.componentIndex == b.componentIndex &&
        a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode && a.typedStatus == b.typedStatus && a.timezoneStatus == b.timezoneStatus &&
        a.offset == b.offset && a.definitionsInspected == b.definitionsInspected && a.matchingCount == b.matchingCount && a.uniqueComponentIndex == b.uniqueComponentIndex &&
        a.globalRegistrySemanticsPending == b.globalRegistrySemanticsPending && a.definitionParameterReviewPending == b.definitionParameterReviewPending && a.timezoneResolutionPending == b.timezoneResolutionPending);
}
void same(const N::Report& a, const N::Report& b) {
    assert(a.status == b.status && a.componentIndex == b.componentIndex && a.propertyIndex == b.propertyIndex &&
        a.temporalPropertiesChecked == b.temporalPropertiesChecked && a.noReference == b.noReference &&
        a.emptyReference == b.emptyReference && a.noLocalDefinition == b.noLocalDefinition &&
        a.uniqueLocalDefinition == b.uniqueLocalDefinition && a.multipleLocalDefinitions == b.multipleLocalDefinitions &&
        a.globalRegistrySemanticsPending == b.globalRegistrySemanticsPending &&
        a.definitionParameterReviewPending == b.definitionParameterReviewPending && a.timezoneResolutionPending == b.timezoneResolutionPending);
    same(a.sourceReport, b.sourceReport); same(a.referenceReport, b.referenceReport);
}
void failed(const N::Report& r) {
    assert(!r.temporalPropertiesChecked && !r.noReference && !r.emptyReference && !r.noLocalDefinition &&
        !r.uniqueLocalDefinition && !r.multipleLocalDefinitions && !r.globalRegistrySemanticsPending &&
        !r.definitionParameterReviewPending && !r.timezoneResolutionPending);
    assert(!r.referenceReport.definitionsInspected && !r.referenceReport.matchingCount &&
        r.referenceReport.uniqueComponentIndex == Stage::None);
}
N::Report checked(Session& s, N::Limits b = limits()) {
    const auto bytes = s.data.bytes; const auto before = s.stage.counts(); const auto status = s.stage.status(); N::Report r;
    { Audit a; r = N::inspect(s.stage, b); }
    const auto after = s.stage.counts(); assert(bytes == s.data.bytes && status == s.stage.status()); s.data.check();
    assert(before.components == after.components && before.calendars == after.calendars && before.observedProperties == after.observedProperties &&
        before.ignoredOpaqueProperties == after.ignoredOpaqueProperties && before.storedProperties == after.storedProperties && before.poolBytes == after.poolBytes &&
        before.reviewItems == after.reviewItems && before.reviewBytes == after.reviewBytes);
    ++scenarios;
    if (r.status != S::LocalCoveragePassed) failed(r);
    else {
        same(r.sourceReport, SL::inspect(s.stage, b.source));
        assert(r.componentIndex == Stage::None && r.propertyIndex == Stage::None);
        assert(r.temporalPropertiesChecked == r.noReference + r.emptyReference + r.noLocalDefinition + r.uniqueLocalDefinition + r.multipleLocalDefinitions);
        assert(r.referenceReport.status == R::Status::InvalidArgument); // No last/provisional match escapes.
    }
    return r;
}
void expect(const N::Report& r, S status, size_t total = 0, size_t absent = 0, size_t empty = 0,
            size_t missing = 0, size_t unique = 0, size_t multiple = 0) {
    if (r.status != status || r.temporalPropertiesChecked != total || r.noReference != absent || r.emptyReference != empty ||
        r.noLocalDefinition != missing || r.uniqueLocalDefinition != unique || r.multipleLocalDefinitions != multiple)
        std::fprintf(stderr, "scenario %zu status %u, total %zu, classes %zu/%zu/%zu/%zu/%zu\n", scenarios,
            unsigned(r.status), r.temporalPropertiesChecked, r.noReference, r.emptyReference, r.noLocalDefinition, r.uniqueLocalDefinition, r.multipleLocalDefinitions);
    assert(r.status == status && r.temporalPropertiesChecked == total && r.noReference == absent && r.emptyReference == empty &&
        r.noLocalDefinition == missing && r.uniqueLocalDefinition == unique && r.multipleLocalDefinitions == multiple);
}
void sourceFailure(Session& s, N::Limits b = limits()) {
    const auto original = SL::inspect(s.stage, b.source); assert(original.status != SL::Status::LocalCoveragePassed);
    const auto r = checked(s, b); expect(r, S::SourceLocalFailure); same(original, r.sourceReport);
    assert(r.componentIndex == original.componentIndex && r.propertyIndex == original.propertyIndex);
}
void referenceFailure(Session& s, size_t owner, size_t property, N::Limits b = limits(), size_t parent = 0) {
    const auto original = R::inspect(s.stage, parent, owner, property, b.reference); assert(original.status != R::Status::Inspected);
    const auto r = checked(s, b); expect(r, S::TzidReferenceFailure); same(original, r.referenceReport);
    assert(r.componentIndex == owner && r.propertyIndex == property);
    assert(r.sourceReport.status == SL::Status::InvalidArgument); // No successful preflight counts on failure.
}
void enumeration() {
    { Session s; parse(s, calendar(event("DTSTART:20261017T130000\r\n")));
        auto b = limits(); b.reference.maxComponentsScanned = b.reference.maxDecodedIdentityBytes = 0;
        const auto r = checked(s, b); expect(r, S::LocalCoveragePassed, 1, 1); assert(!r.timezoneResolutionPending);
    }
    { Session s; parse(s, calendar(event() + zone())); const auto r = checked(s);
        expect(r, S::LocalCoveragePassed, 1, 0, 0, 0, 1); assert(r.timezoneResolutionPending);
    }
    // All five classes in one event, all five property kinds, repeated lists,
    // definition after the consumer, and three separately aggregated obligations.
    { Session s; parse(s, calendar(event("DTSTART:20261017T130000\r\n" + reference("DTEND", "", "20261017T140000") +
        reference("RECURRENCE-ID", "Missing") + reference("RDATE", "/Custom") + reference("RDATE", "/Custom") +
        reference("EXDATE", "Double") + "EXDATE:20261018T130000Z\r\n") + definition("/Custom", ";X-P=opaque") + definition("Double") + definition("Double")));
        const auto r = checked(s); expect(r, S::LocalCoveragePassed, 7, 2, 1, 1, 2, 1);
        assert(r.globalRegistrySemanticsPending && r.definitionParameterReviewPending && r.timezoneResolutionPending);
        assert(r.sourceReport.events == 1 && r.sourceReport.timezones == 3);
    }
    { Session s; parse(s, calendar(event() + event(reference("DTSTART", "Missing")) + zone()) +
        calendar(zone() + event() + event(reference("DTSTART", "Other"))));
        const auto r = checked(s); expect(r, S::LocalCoveragePassed, 4, 0, 0, 2, 2);
        assert(r.sourceReport.calendars == 2 && r.sourceReport.events == 4);
    }
    { Session s; parse(s, calendar(event()) + calendar(event() + zone()));
        expect(checked(s), S::LocalCoveragePassed, 2, 0, 0, 1, 1); // Other root cannot satisfy the first reference.
    }
    { Session s; parse(s, calendar(event(reference("DTSTART", "/Missing"))));
        const auto r = checked(s); expect(r, S::LocalCoveragePassed, 1, 0, 0, 1);
        assert(r.globalRegistrySemanticsPending && !r.definitionParameterReviewPending);
    }
    { Session s; parse(s, calendar(event(reference("DTSTART", "\"\""))));
        auto b = limits(); b.reference.maxComponentsScanned = b.reference.maxDecodedIdentityBytes = 0;
        expect(checked(s, b), S::LocalCoveragePassed, 1, 0, 1);
    }
    // No fabricated DTSTART in a METHOD context; RRULE/unknowns/nested fields
    // are excluded, including projected and opaque payloads.
    { Session s; parse(s, calendar(event("RRULE:FREQ=DAILY\r\nX-REVIEW:opaque\r\n"), "METHOD:PUBLISH\r\n"));
        auto b = limits(); b.maxTemporalProperties = 0; const auto r = checked(s, b);
        expect(r, S::LocalCoveragePassed); assert(r.sourceReport.sourceDeferred & SL::CalendarMetadataPending);
    }
    { Session s; parse(s, calendar(event("DTSTART:20261017T130000Z\r\nX-REVIEW:opaque\r\n" +
        component("VALARM", reference()) + component("PARTICIPANT", reference()) +
        component("VLOCATION", reference()) + component("VRESOURCE", reference())) +
        component("VTODO", reference() + component("VALARM", reference())) +
        component("VJOURNAL", reference()) + component("VFREEBUSY", reference()) +
        component("X-OPAQUE", reference() + component("X-NESTED", reference()))));
        const auto r = checked(s); expect(r, S::LocalCoveragePassed, 1, 1);
        assert(r.sourceReport.sourceDeferred & SL::NestedComponentReviewPending);
        assert(r.sourceReport.sourceDeferred & SL::NonEventProjectionObserved);
    }
    for (const char* kind : {"VTODO", "X-OPAQUE", "VTIMEZONE"}) {
        Session s; parse(s, calendar(std::string(kind) == "VTIMEZONE" ? zone() : component(kind, reference())));
        auto b = limits(); b.maxTemporalProperties = 0; const auto r = checked(s, b);
        expect(r, S::LocalCoveragePassed); assert(!r.sourceReport.events && !r.timezoneResolutionPending &&
            !r.globalRegistrySemanticsPending && !r.definitionParameterReviewPending);
    }
}
void payloadBoundary() {
    for (const auto& code : {std::make_pair("RDATE", Code::Rdate), std::make_pair("EXDATE", Code::Exdate)}) {
        for (const char* value : {"20261017T130000Z", "malformed", "20261017T130000,bad"}) {
            Session s; parse(s, calendar(event(reference() + reference(code.first, "Custom", value)) + zone()));
            assert(SL::inspect(s.stage, limits().source).status == SL::Status::LocalCoveragePassed);
            expect(checked(s), S::LocalCoveragePassed, 2, 0, 0, 0, 2);
            T::TemporalList list(s.stage, find(s.stage, code.second), {8,1024}); T::TemporalValue member = {};
            T::Result result = {};
            do { result = list.next(member); } while (T::parsed(result));
            assert(result.status != T::Status::End && !list.complete());
            assert(!T::parsed(list.status())); // Independent decoder rejects; identity coverage made no value claim.
        }
    }
    for (const char* field : {"DTSTART", "DTEND", "RECURRENCE-ID"}) {
        Session s; const std::string fields = std::string(field) == "DTSTART" ? reference(field, "Custom", "20261017T130000Z") :
            reference() + reference(field, "Custom", "20261017T140000Z");
        parse(s, calendar(event(fields) + zone())); sourceFailure(s);
    }
}
void budgetsAndOrdering() {
    Session s; parse(s, calendar(event("DTSTART:20261017T130000\r\n" + reference("RDATE") + reference("EXDATE")) + zone()));
    auto b = limits(); b.maxTemporalProperties = 3; b.source.maxComponents = 4;
    b.reference.maxComponentsScanned = 4; b.reference.maxEventProperties = 5; b.reference.maxDecodedIdentityBytes = 6;
    expect(checked(s, b), S::LocalCoveragePassed, 3, 1, 0, 0, 2);
    --b.maxTemporalProperties; const auto over = checked(s, b); expect(over, S::ResourceLimit);
    assert(over.componentIndex == 1 && over.propertyIndex == find(s.stage, Code::Exdate));
    b.maxTemporalProperties = 0; const auto zero = checked(s, b); expect(zero, S::ResourceLimit);
    assert(zero.propertyIndex == find(s.stage));
    b = limits(); b.source.maxComponents = 3; sourceFailure(s, b);
    b = limits(); b.source.maxEvents = 0; sourceFailure(s, b);
    b = limits(); b.reference.maxComponentsScanned = 3; referenceFailure(s, 1, find(s.stage, Code::Rdate), b);
    b.reference.maxComponentsScanned = 0; referenceFailure(s, 1, find(s.stage, Code::Rdate), b);
    b = limits(); b.reference.maxDecodedIdentityBytes = 5; referenceFailure(s, 1, find(s.stage, Code::Rdate), b);
    b.reference.maxDecodedIdentityBytes = 0; referenceFailure(s, 1, find(s.stage, Code::Rdate), b);
    b = limits(); b.reference.maxEventProperties = 4; referenceFailure(s, 1, find(s.stage), b);
    b = limits(); b.reference.timezone.maxTimezoneProperties = 0; referenceFailure(s, 1, find(s.stage, Code::Rdate), b);
    b = limits(); b.maxTemporalProperties = SIZE_MAX;
    expect(checked(s, b), S::LocalCoveragePassed, 3, 1, 0, 0, 2);
    { Session late; parse(late, calendar(event("DTSTART:20261017T130000\r\n" + reference("RDATE")) +
        zone() + definition("LongerIdentity")));
        auto budget = limits(); budget.reference.maxDecodedIdentityBytes = 6;
        referenceFailure(late, 1, find(late.stage, Code::Rdate), budget);
        const auto r = checked(late, budget);
        assert(r.referenceReport.componentIndex == 4 && r.referenceReport.propertyCode == Code::Tzid);
        // A provisional match against the earlier Custom definition cannot escape.
    }
    // Both failing properties occur late; the earlier property/component wins.
    { Session bad; parse(bad, calendar(event("DTSTART:20261017T130000\r\nEXDATE;VALUE=PERIOD:bad\r\nRDATE;VALUE=BAD:bad\r\n") +
        event(reference("DTSTART", "Longer")) + zone()));
        referenceFailure(bad, 1, find(bad.stage, Code::Exdate));
    }
    { Session bad; parse(bad, calendar(event("DTSTART:20261017T130000\r\n") +
        event("DTSTART:20261017T130000\r\nRDATE;VALUE=BAD:bad\r\n") + event("DTSTART:20261017T130000\r\nEXDATE;VALUE=PERIOD:bad\r\n")));
        referenceFailure(bad, 2, find(bad.stage, Code::Rdate, 2));
    }
    // Full preflight failure outranks an earlier identity-selection error.
    { Session bad; parse(bad, calendar(event("DTSTART:20261017T130000\r\nRDATE;VALUE=BAD:bad\r\n") +
        component("VEVENT", "DTSTAMP:20261017T120000Z\r\nDTSTART:20261017T130000Z\r\n")));
        sourceFailure(bad); assert(checked(bad).componentIndex == 2);
    }
    { Session bad; parse(bad, calendar(event("DTSTART:20261017T130000\r\nRDATE;VALUE=BAD:bad\r\n") + zone("")));
        sourceFailure(bad); // Bad later definition is a Phase 1 error, never hidden by identity dispatch.
    }
    { Session late; parse(late, calendar(event("DTSTART:20261017T130000\r\n") + event() + zone()));
        auto budget = limits(); budget.reference.maxComponentsScanned = 4;
        referenceFailure(late, 2, find(late.stage, Code::Dtstart, 2), budget); // First NoReference avoided scanning.
    }
}
void admissionAndLifetime() {
    { Stage fresh; N::Report r; { Audit a; r = N::inspect(fresh, limits()); } failed(r);
        assert(r.status == S::SourceLocalFailure && r.sourceReport.status == SL::Status::Unavailable);
    }
    { Session incomplete; sourceFailure(incomplete); }
    { Session cancelled; cancelled.stage.cancel(); sourceFailure(cancelled); }
    { Session failedStage; const std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        failedStage.http.feed(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()); failedStage.http.finish();
        failedStage.stage.finish(failedStage.http, failedStage.structure); sourceFailure(failedStage);
    }
    Session s; parse(s, calendar(event() + zone()) + calendar(event()));
    auto& eventDescriptor = *const_cast<Stage::Component*>(s.stage.component(1)); const auto saved = eventDescriptor;
    eventDescriptor.parentOrdinal = 5; sourceFailure(s); eventDescriptor = saved;
    ++eventDescriptor.calendarOrdinal; sourceFailure(s); eventDescriptor = saved;
    ++eventDescriptor.ordinal; sourceFailure(s); eventDescriptor = saved;
    eventDescriptor.closed = 0; sourceFailure(s); eventDescriptor = saved;
    auto& property = *const_cast<Stage::Property*>(s.stage.property(find(s.stage))); const auto original = property;
    property.owner = 5; sourceFailure(s); property = original;
    property.next = s.stage.component(1)->firstProperty; sourceFailure(s); property = original;
    property.next = s.stage.counts().storedProperties; sourceFailure(s); property = original;
    for (bool chunked : {false,true}) for (size_t split : {size_t(1),size_t(7),SIZE_MAX}) {
        Session live; parse(live, calendar(event(reference("DTSTART", "A^^B^nC")) + definition("A^B\\nC")), chunked, split);
        const auto r = checked(live); expect(r, S::LocalCoveragePassed, 1, 0, 0, 0, 1);
        same(r, checked(live)); const auto copy = r;
        live.stage.release(); assert(live.stage.status() == Stage::Status::StagedSuccessfully && !live.stage.component(0));
        sourceFailure(live); std::fill(live.data.data(), live.data.data() + live.data.capacity, 0xdd); same(r, copy);
    }
}
}
int main() {
    static_assert(std::is_standard_layout<N::Report>::value && std::is_trivially_copyable<N::Report>::value, "Scalar copied coverage evidence");
    static_assert(std::is_same<decltype(N::Report::sourceReport), SL::Report>::value, "Original source diagnostic");
    static_assert(std::is_same<decltype(N::Report::referenceReport), R::Report>::value, "Original reference diagnostic");
    enumeration(); payloadBoundary(); budgetsAndOrdering(); admissionAndLifetime();
    std::printf("Source-wide TZID identity coverage: %zu deterministic cases; zero audited allocations; report %zu/%zu\n", scenarios, sizeof(N::Report), alignof(N::Report));
}
