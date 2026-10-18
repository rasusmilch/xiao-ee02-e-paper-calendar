#include "../application/ics_tzid_reference_local.h"
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
namespace N = IcsTzidReferenceLocal;
namespace T = IcsTypedValues;
namespace Z = IcsTimezoneLocal;
using Stage = IcsSourceStage;
using Code = Stage::Code;
using S = N::Status;
using M = N::Match;
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
std::string calendar(const std::string& children) { return component("VCALENDAR", "VERSION:2.0\r\nPRODID:synthetic\r\n" + children); }
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
N::Limits limits() { return {128,128,4096,{128,128,4096,64,4096}}; }
size_t find(const Stage& s, Code code = Code::Dtstart, size_t owner = 1) {
    for (uint32_t i = s.component(owner)->firstProperty; i != Stage::None; i = s.property(i)->next)
        if (s.property(i)->code == code) return i;
    return Stage::None;
}
void failed(const N::Report& r) {
    assert(r.match == M::NoReference && !r.definitionsInspected && !r.matchingCount && r.uniqueComponentIndex == Stage::None);
    assert(!r.globalRegistrySemanticsPending && !r.definitionParameterReviewPending && !r.timezoneResolutionPending);
}
N::Report checked(Session& s, size_t selected, N::Limits b = limits(), size_t parent = 0, size_t child = 1) {
    const auto bytes = s.data.bytes; const auto before = s.stage.counts(); const auto status = s.stage.status(); N::Report r;
    { Audit a; r = N::inspect(s.stage, parent, child, selected, b); }
    assert(bytes == s.data.bytes && status == s.stage.status()); s.data.check(); const auto after = s.stage.counts();
    assert(before.components == after.components && before.calendars == after.calendars && before.observedProperties == after.observedProperties &&
        before.ignoredOpaqueProperties == after.ignoredOpaqueProperties && before.storedProperties == after.storedProperties && before.poolBytes == after.poolBytes &&
        before.reviewItems == after.reviewItems && before.reviewBytes == after.reviewBytes);
    ++scenarios; assert(r.temporalPropertyIndex == selected);
    if (r.status != S::Inspected) failed(r);
    else {
        assert(r.matchingCount <= 2 && r.timezoneResolutionPending == (r.match != M::NoReference));
        assert((r.uniqueComponentIndex != Stage::None) == (r.match == M::UniqueLocalDefinition));
    }
    return r;
}
void expect(const N::Report& r, S status, M match = M::NoReference, size_t definitions = 0, unsigned matches = 0, size_t unique = Stage::None) {
    if (r.status != status || r.match != match || r.definitionsInspected != definitions || r.matchingCount != matches || r.uniqueComponentIndex != unique)
        std::fprintf(stderr, "case %zu expected %u/%u/%zu/%u/%zu got %u/%u/%zu/%u/%zu\n", scenarios,
            unsigned(status), unsigned(match), definitions, matches, unique,
            unsigned(r.status), unsigned(r.match), r.definitionsInspected, unsigned(r.matchingCount), r.uniqueComponentIndex);
    assert(r.status == status && r.match == match && r.definitionsInspected == definitions && r.matchingCount == matches && r.uniqueComponentIndex == unique);
}
void same(const N::Report& a, const N::Report& b) {
    assert(a.status == b.status && a.match == b.match && a.temporalPropertyIndex == b.temporalPropertyIndex && a.componentIndex == b.componentIndex &&
        a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode && a.typedStatus == b.typedStatus && a.timezoneStatus == b.timezoneStatus &&
        a.offset == b.offset && a.definitionsInspected == b.definitionsInspected && a.matchingCount == b.matchingCount && a.uniqueComponentIndex == b.uniqueComponentIndex &&
        a.globalRegistrySemanticsPending == b.globalRegistrySemanticsPending && a.definitionParameterReviewPending == b.definitionParameterReviewPending && a.timezoneResolutionPending == b.timezoneResolutionPending);
}
void selectorFailure(Session& s, size_t p) {
    T::Selection selection = {}; const auto original = T::selectTemporal(s.stage, p, selection); assert(!T::parsed(original));
    const auto r = checked(s, p); expect(r, S::TemporalSelectionFailure);
    assert(r.typedStatus == original.status && r.offset == original.offset && r.propertyIndex == p && r.propertyCode == s.stage.property(p)->code);
}
void timezoneFailure(Session& s, size_t componentIndex, N::Limits b = limits()) {
    const auto original = Z::validateTimezone(s.stage, 0, componentIndex, b.timezone); assert(original.status != Z::Status::LocalChecksPassed);
    const auto r = checked(s, find(s.stage), b); expect(r, S::TimezoneLocalFailure);
    assert(r.timezoneStatus == original.status && r.componentIndex == componentIndex && r.propertyIndex == original.propertyIndex && r.propertyCode == original.propertyCode);
    assert(r.offset == 0); // The accepted timezone-local Report supplies no byte offset.
}
void basicAndScope() {
    for (const char* field : {"DTSTART:20261017T130000\r\n", "DTSTART:20261017T130000Z\r\n", "DTSTART:malformed\r\n"}) {
        Session s; parse(s, calendar(event(field) + zone(""))); auto b = limits(); b.maxComponentsScanned = b.maxDecodedIdentityBytes = 0;
        expect(checked(s, find(s.stage), b), S::Inspected); // No definition scan or value decoding.
    }
    for (const char* empty : {"", "\"\""}) {
        Session s; parse(s, calendar(event(reference("DTSTART", empty)) + zone(""))); auto b = limits(); b.maxComponentsScanned = b.maxDecodedIdentityBytes = 0;
        expect(checked(s, find(s.stage), b), S::Inspected, M::EmptyReference);
    }
    { Session s; parse(s, calendar(zone() + event())); expect(checked(s, find(s.stage, Code::Dtstart, 3), limits(), 0, 3), S::Inspected, M::UniqueLocalDefinition, 1, 1, 1); }
    { Session s; parse(s, calendar(event() + zone())); expect(checked(s, find(s.stage)), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2); }
    { Session s; parse(s, calendar(event())); expect(checked(s, find(s.stage)), S::Inspected, M::NoLocalDefinition); }
    { Session s; parse(s, calendar(event() + definition("Other"))); expect(checked(s, find(s.stage)), S::Inspected, M::NoLocalDefinition, 1); }
    { Session s; parse(s, calendar(event() + zone() + zone() + zone())); expect(checked(s, find(s.stage)), S::Inspected, M::MultipleLocalDefinitions, 3, 2); }
    { Session s; parse(s, calendar(event() + definition("Other") + zone() + definition("Different")));
        expect(checked(s, find(s.stage)), S::Inspected, M::UniqueLocalDefinition, 3, 1, 4);
    }
    { Session s; parse(s, calendar(event()) + calendar(event() + zone()));
        expect(checked(s, find(s.stage)), S::Inspected, M::NoLocalDefinition);
        expect(checked(s, find(s.stage, Code::Dtstart, 3), limits(), 2, 3), S::Inspected, M::UniqueLocalDefinition, 1, 1, 4);
    }
    { Session s; parse(s, calendar(event() + zone()) + calendar(zone() + event()));
        expect(checked(s, find(s.stage)), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
        expect(checked(s, find(s.stage, Code::Dtstart, 7), limits(), 4, 7), S::Inspected, M::UniqueLocalDefinition, 1, 1, 5);
    }
    { Session s; parse(s, calendar(event() + zone()) + calendar(zone("")));
        expect(checked(s, find(s.stage)), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2); // Unrelated invalid definitions are not selected.
    }
}
void decodedIdentities() {
    struct Example { const char* parameter; const char* text; bool match; };
    const Example examples[] = {{"America/New_York","America/New_York",true}, {"Custom","custom",false},
        {"\"Custom\"","Custom",true}, {"\"A,B;C\"","A\\,B\\;C",true}, {"A^^B^'C^nD","A^B\"C\\nD",true},
        {"\"A\\B\"","A\\\\B",true}, {"A^'B","A\"B",true}, {"A^nB","A\\NB",true}, {"A^N^z^","A^N^z^",true},
        {"A^N","A\\n",false}, {"A^^n","A^n",true}, {"Caf\xc3\xa9","Caf\xc3\xa9",true},
        {"Caf\xc3\xa9","Cafe\xcc\x81",false}, {"/Custom","/Custom",true}, {"/Custom","Custom",false}};
    for (const auto& c : examples) {
        Session s; parse(s, calendar(event(reference("DTSTART", c.parameter)) + definition(c.text)));
        const auto r = checked(s, find(s.stage));
        expect(r, S::Inspected, c.match ? M::UniqueLocalDefinition : M::NoLocalDefinition, 1, c.match ? 1 : 0, c.match ? 2 : Stage::None);
        assert(r.globalRegistrySemanticsPending == (c.parameter[0] == '/'));
    }
    { Session s; parse(s, calendar(event(reference("DTSTART", "/NoRegistry"))));
        const auto r = checked(s, find(s.stage)); expect(r, S::Inspected, M::NoLocalDefinition); assert(r.globalRegistrySemanticsPending);
    }
    { Session s; parse(s, calendar(event() + definition("Custom", ";X-P=opaque")));
        const auto r = checked(s, find(s.stage)); expect(r, S::Inspected, M::UniqueLocalDefinition, 1, 1, 2); assert(r.definitionParameterReviewPending);
    }
}
void payloadBoundary() {
    const std::pair<const char*,Code> properties[] = {{"DTSTART",Code::Dtstart},{"DTEND",Code::Dtend},{"RECURRENCE-ID",Code::RecurrenceId},{"RDATE",Code::Rdate},{"EXDATE",Code::Exdate}};
    for (const auto& c : properties) {
        Session s; parse(s, calendar(event(reference(c.first)) + zone())); const size_t p = find(s.stage, c.second);
        expect(checked(s, p), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
        T::Selection selection = {}; assert(T::selectTemporal(s.stage, p, selection).status == T::Status::Ok);
        Session invalid; parse(invalid, calendar(event(reference(c.first, "Custom", "20261017T130000Z")) + zone()));
        const size_t q = find(invalid.stage, c.second); assert(T::selectTemporal(invalid.stage, q, selection).status == T::Status::Ok);
        expect(checked(invalid, q), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
        T::TemporalValue value = {};
        if (c.second == Code::Rdate || c.second == Code::Exdate) {
            T::TemporalList list(invalid.stage, q, {8,1024});
            assert(list.next(value).status == T::Status::InvalidParameter && !list.complete());
            assert(list.next(value).status == T::Status::InvalidParameter && list.status().status == T::Status::InvalidParameter);
        } else assert(T::decodeTemporal(invalid.stage, q, value, selection).status == T::Status::InvalidParameter);
    }
    for (const char* property : {"DTSTART;VALUE=DATE;TZID=Custom:20261017\r\n", "DTSTART;TZID=a;TZID=b:20261017T130000\r\n",
        "DTSTART;TZID=a,b:20261017T130000\r\n", "DTSTART;LANGUAGE=en;TZID=Custom:20261017T130000\r\n",
        "DTSTART;VALUE=X-UNKNOWN;TZID=Custom:opaque\r\n", "DTSTART;VALUE=DATE;VALUE=DATE;TZID=Custom:20261017\r\n"}) {
        Session s; parse(s, calendar(event(property) + zone())); selectorFailure(s, find(s.stage));
    }
    { Session s; parse(s, calendar(event(reference("RDATE", "Custom", "bad,,payload")) + zone()));
        expect(checked(s, find(s.stage, Code::Rdate)), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
    }
    { Session s; parse(s, calendar(event("RDATE;VALUE=PERIOD;TZID=Custom:uninterpreted-payload\r\n") + zone()));
        expect(checked(s, find(s.stage, Code::Rdate)), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
    }
    { Session s; parse(s, calendar(event("EXDATE;VALUE=PERIOD;TZID=Custom:uninterpreted-payload\r\n") + zone()));
        selectorFailure(s, find(s.stage, Code::Exdate));
    }
}
void budgetsAndDefinitions() {
    Session s; parse(s, calendar(event(reference() + "SUMMARY:bad\\q\r\n") + zone())); const auto p = find(s.stage);
    auto b = limits(); b.maxComponentsScanned = 4; b.maxEventProperties = 4; b.maxDecodedIdentityBytes = 6;
    expect(checked(s, p, b), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
    --b.maxComponentsScanned; const auto late = checked(s, p, b); expect(late, S::ResourceLimit); assert(late.componentIndex == 3);
    b = limits(); b.maxComponentsScanned = 0; expect(checked(s, p, b), S::ResourceLimit);
    b = limits(); b.maxEventProperties = 3; const auto chain = checked(s, p, b); expect(chain, S::ResourceLimit); assert(chain.propertyCode == Code::Summary);
    b.maxEventProperties = 0; expect(checked(s, p, b), S::ResourceLimit);
    b = limits(); b.maxDecodedIdentityBytes = 5; expect(checked(s, p, b), S::ResourceLimit);
    b.maxDecodedIdentityBytes = 0; expect(checked(s, p, b), S::ResourceLimit);
    { Session encoded; parse(encoded, calendar(event(reference("DTSTART", "\"a,b\"")) + definition("a\\,b")));
        auto budget = limits(); budget.maxDecodedIdentityBytes = 3; expect(checked(encoded, find(encoded.stage), budget), S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
        budget.timezone.maxTzidTextBytes = 3; timezoneFailure(encoded, 2, budget); // Encoded local limit remains independent.
    }
    { Session longer; parse(longer, calendar(event() + definition("LongerIdentity"))); auto budget = limits(); budget.maxDecodedIdentityBytes = 6;
        const auto r = checked(longer, find(longer.stage), budget); expect(r, S::ResourceLimit); assert(r.componentIndex == 2 && r.propertyCode == Code::Tzid);
    }
    for (const char* fields : {"", "TZID:\r\n", "TZID:bad\\q\r\n", "TZID:Custom\r\nTZID:Custom\r\n", "TZID:Custom\r\nLAST-MODIFIED:bad\r\n"}) {
        Session bad; parse(bad, calendar(event() + zone() + zone(fields))); timezoneFailure(bad, 4);
    }
    { Session bad; parse(bad, calendar(event() + zone() + zone() + zone(""))); timezoneFailure(bad, 6); }
    b = limits(); b.timezone.maxTimezoneProperties = 0; timezoneFailure(s, 2, b);
    auto& z = *const_cast<Stage::Component*>(s.stage.component(2)); const auto saved = z;
    auto& identity = *const_cast<Stage::Property*>(s.stage.property(find(s.stage, Code::Tzid, 2))); const auto original = identity;
    identity.next = z.firstProperty; timezoneFailure(s, 2); identity = original;
    identity.owner = 1; timezoneFailure(s, 2); identity = original;
    ++z.observedProperties; timezoneFailure(s, 2); z = saved;
    z.closed = 0; timezoneFailure(s, 2); z = saved;
    ++z.ordinal; timezoneFailure(s, 2); z = saved;
    z.calendarOrdinal = 2; expect(checked(s, p), S::InvalidAssociation); z = saved;
    z.flags |= Stage::ProjectedOut; expect(checked(s, p), S::InvalidAssociation); z = saved;
}
void admissionAndLifetime() {
    { Stage fresh; N::Report r; { Audit a; r = N::inspect(fresh, 0, 1, Stage::None, limits()); } expect(r, S::Unavailable); failed(r); }
    { Session incomplete; expect(checked(incomplete, Stage::None), S::Unavailable); }
    { Session cancelled; cancelled.stage.cancel(); expect(checked(cancelled, Stage::None), S::Unavailable); }
    { Session failed; const std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        failed.http.feed(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()); failed.http.finish(); failed.stage.finish(failed.http, failed.structure);
        expect(checked(failed, Stage::None), S::Unavailable);
    }
    Session s; parse(s, calendar(event() + zone()) + calendar(event())); const auto p = find(s.stage);
    expect(checked(s, SIZE_MAX), S::InvalidArgument);
    expect(checked(s, p, limits(), SIZE_MAX, 1), S::InvalidArgument);
    expect(checked(s, p, limits(), 0, SIZE_MAX), S::InvalidArgument);
    expect(checked(s, p, limits(), 4, 1), S::InvalidAssociation);
    expect(checked(s, find(s.stage, Code::Dtstart, 5)), S::InvalidAssociation);
    expect(checked(s, find(s.stage, Code::Tzid, 2), limits(), 0, 2), S::InvalidAssociation);
    expect(checked(s, find(s.stage, Code::Uid)), S::InvalidArgument);
    auto& c = *const_cast<Stage::Component*>(s.stage.component(1)); const auto saved = c;
    c.closed = 0; expect(checked(s, p), S::InvalidAssociation); c = saved;
    ++c.ordinal; expect(checked(s, p), S::InvalidAssociation); c = saved;
    c.parentOrdinal = 2; expect(checked(s, p), S::InvalidAssociation); c = saved;
    ++c.calendarOrdinal; expect(checked(s, p), S::InvalidAssociation); c = saved;
    c.flags |= Stage::OpaqueProjectedOut; expect(checked(s, p), S::InvalidAssociation); c = saved;
    auto& root = *const_cast<Stage::Component*>(s.stage.component(0)); const auto oldRoot = root;
    root.parentOrdinal = 1; expect(checked(s, p), S::InvalidAssociation); root = oldRoot;
    root.closed = 0; expect(checked(s, p), S::InvalidAssociation); root = oldRoot;
    ++root.ordinal; expect(checked(s, p), S::InvalidAssociation); root = oldRoot;
    root.calendarOrdinal = 0; expect(checked(s, p), S::InvalidAssociation); root = oldRoot;
    root.flags |= Stage::ProjectedOut; expect(checked(s, p), S::InvalidAssociation); root = oldRoot;
    auto& prior = *const_cast<Stage::Property*>(s.stage.property(find(s.stage, Code::Dtstamp))); const auto oldPrior = prior;
    prior.next = Stage::None; c.lastProperty = find(s.stage, Code::Dtstamp); --c.observedProperties;
    expect(checked(s, p), S::InvalidAssociation); prior = oldPrior; c = saved; // Readable owner claim, absent from chain.
    prior.next = c.firstProperty; expect(checked(s, p), S::InvalidAssociation); prior = oldPrior;
    auto& selected = *const_cast<Stage::Property*>(s.stage.property(p)); const auto oldSelected = selected;
    selected.flags &= ~Stage::ValueRetained; selectorFailure(s, p); selected = oldSelected;
    for (const char* kind : {"VTODO", "VJOURNAL", "VFREEBUSY", "X-OPAQUE"}) {
        Session other; parse(other, calendar(component(kind, reference())));
        const auto property = find(other.stage);
        expect(checked(other, property), property == Stage::None ? S::InvalidArgument : S::InvalidAssociation);
    }
    const std::pair<const char*,Code> unsupported[] = {{"RRULE:FREQ=DAILY\r\n",Code::Rrule}, {"DURATION:PT1H\r\n",Code::Duration}, {"X-EXT:opaque\r\n",Code::Unknown}};
    for (const auto& field : unsupported) {
        Session other; parse(other, calendar(event(field.first)));
        expect(checked(other, find(other.stage, field.second)), S::InvalidArgument);
    }
    { Session child; parse(child, calendar(event(component("PARTICIPANT", reference())) + zone()));
        expect(checked(child, find(child.stage, Code::Dtstart, 2), limits(), 0, 2), S::InvalidAssociation);
    }
    for (bool chunked : {false,true}) for (size_t split : {size_t(1),size_t(7),SIZE_MAX}) {
        Session live; parse(live, calendar(event(reference("DTSTART", "A^^B^nC")) + definition("A^B\\nC")), chunked, split);
        const auto index = find(live.stage); const auto r = checked(live, index); expect(r, S::Inspected, M::UniqueLocalDefinition, 1, 1, 2);
        const auto copy = r; same(r, checked(live, index));
        live.stage.release(); assert(live.stage.status() == Stage::Status::StagedSuccessfully && !live.stage.component(0));
        expect(checked(live, index), S::Unavailable);
        std::fill(live.data.data(), live.data.data() + live.data.capacity, 0xdd); same(r, copy);
    }
}
}
int main() {
    static_assert(std::is_standard_layout<N::Report>::value && std::is_trivially_copyable<N::Report>::value, "Copied scalar identity evidence");
    static_assert(std::is_same<decltype(N::Report::typedStatus), T::Status>::value, "Typed diagnostic domain");
    static_assert(std::is_same<decltype(N::Report::timezoneStatus), Z::Status>::value, "Timezone diagnostic domain");
    basicAndScope(); decodedIdentities(); payloadBoundary(); budgetsAndDefinitions(); admissionAndLifetime();
    std::printf("TZID identity matching: %zu deterministic cases; zero audited allocations; report %zu/%zu\n", scenarios, sizeof(N::Report), alignof(N::Report));
}
