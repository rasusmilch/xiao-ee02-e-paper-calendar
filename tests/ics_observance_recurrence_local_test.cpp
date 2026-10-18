#include "../application/ics_observance_recurrence_local.h"
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
namespace N = IcsObservanceRecurrenceLocal;
namespace Z = IcsTimezoneLocal;
namespace X = IcsRecurContext;
namespace R = IcsRecurValue;
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
Stage::Limits storage() { return {128, 512, 65536, 128, 16384}; }
IcsStructure::Limits syntax() { return {4096, 32, 32, 64, 8, 64, 4096, 8192, 128, 8}; }
size_t stageBytes() { size_t n = 0; assert(Stage::requiredBytes(storage(), n) == Stage::Status::InProgress); return n; }
size_t syntaxBytes() { size_t n = 0; assert(IcsStructure::scratchBytes(syntax(), n) == IcsStructure::Status::InProgress); return n; }
struct Session {
    Guard data, parser, wire; Stage stage; IcsStructure structure; IcsIngestion http;
    Session() : data(stageBytes()), parser(syntaxBytes()), wire(6144) {
        Audit a;
        assert(stage.initialize(data.data(), data.capacity, storage()) == Stage::Status::InProgress);
        assert(structure.initialize(parser.data(), parser.capacity, syntax(), Stage::sink, &stage) == IcsStructure::Status::InProgress);
        const IcsIngestion::Limits limits = {131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(wire.data(), wire.capacity, limits, IcsStructure::lineSink, &structure, clock, nullptr) == IcsIngestion::Status::InProgress);
    }
    ~Session() { Audit a; stage.release(); structure.release(); http.release(); data.check(); parser.check(); wire.check(); }
};
const std::string onset = "DTSTART:20261017T020000\r\n";
const std::string offsets = "TZOFFSETFROM:-0500\r\nTZOFFSETTO:-0400\r\n";
const std::string required = onset + offsets;
const std::string time1 = "20261017T020000", time2 = "20261018T020000";
std::string component(const std::string& kind, const std::string& fields) {
    return "BEGIN:" + kind + "\r\n" + fields + "END:" + kind + "\r\n";
}
std::string calendar(const std::string& children) { return component("VCALENDAR", "VERSION:2.0\r\nPRODID:synthetic\r\n" + children); }
std::string zone(const std::string& children) { return component("VTIMEZONE", "TZID:Custom\r\n" + children); }
std::string feed(const std::string& fields, const std::string& kind = "STANDARD") { return calendar(zone(component(kind, fields))); }
void parse(Session& s, const std::string& body, bool chunked = false, size_t split = SIZE_MAX) {
    std::string raw;
    if (!chunked) raw = "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
    else {
        raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
        for (char c : body) { raw += "1\r\n"; raw += c; raw += "\r\n"; } raw += "0\r\n\r\n";
    }
    { Audit a;
        for (size_t i = 0; i < raw.size();) {
            const size_t n = std::min(split, raw.size() - i);
            assert(s.http.feed(reinterpret_cast<const uint8_t*>(raw.data() + i), n) == IcsIngestion::Status::InProgress); i += n;
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
N::Limits limits() { return {32, {128, {4096, 64, 256}, {128, 128, 4096}, {128, 128, 4096, 64, 4096}}}; }
size_t find(const Stage& s, Code code, size_t occurrence = 0, size_t owner = 2) {
    for (uint32_t i = s.component(owner)->firstProperty; i != Stage::None; i = s.property(i)->next)
        if (s.property(i)->code == code && !occurrence--) return i;
    return Stage::None;
}
void sameLocal(const Z::Report& a, const Z::Report& b) {
    assert(a.status == b.status && a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode && a.deferred == b.deferred);
}
N::Report checked(Session& s, N::Limits budget = limits(), size_t parent = 1, size_t child = 2) {
    const auto bytes = s.data.bytes; const auto before = s.stage.counts(); const auto status = s.stage.status();
    const auto local = Z::validateObservance(s.stage, parent, child, budget.context.timezone);
    N::Report r;
    { Audit a; r = N::inspect(s.stage, parent, child, budget); }
    sameLocal(local, Z::validateObservance(s.stage, parent, child, budget.context.timezone));
    assert(bytes == s.data.bytes && status == s.stage.status()); s.data.check();
    const auto after = s.stage.counts();
    assert(before.components == after.components && before.calendars == after.calendars && before.observedProperties == after.observedProperties &&
        before.ignoredOpaqueProperties == after.ignoredOpaqueProperties && before.storedProperties == after.storedProperties &&
        before.poolBytes == after.poolBytes && before.reviewItems == after.reviewItems && before.reviewBytes == after.reviewBytes);
    ++scenarios;
    if (r.status == S::LocalCoveragePassed) {
        assert(r.failureDomain == D::None && local.status == Z::Status::LocalChecksPassed && r.observanceLocalDeferred == local.deferred);
    } else assert(!r.observanceLocalDeferred && !r.contextualRuleDeferred && !r.rruleProperties && !r.rdateProperties && !r.exdateProperties);
    return r;
}
void expect(const N::Report& r, S status, D domain = D::None, size_t property = Stage::None, Code code = Code::Unknown, size_t offset = 0) {
    if (r.status != status || r.failureDomain != domain || r.propertyIndex != property || r.propertyCode != code || r.offset != offset)
        std::fprintf(stderr, "scenario %zu expected %u/%u/%zu/%u/%zu got %u/%u/%zu/%u/%zu\n", scenarios,
            unsigned(status), unsigned(domain), property, unsigned(code), offset,
            unsigned(r.status), unsigned(r.failureDomain), r.propertyIndex, unsigned(r.propertyCode), r.offset);
    assert(r.status == status && r.failureDomain == domain && r.propertyIndex == property && r.propertyCode == code && r.offset == offset);
}
void counts(const N::Report& r, size_t rules, size_t dates, size_t exclusions) {
    expect(r, S::LocalCoveragePassed);
    assert(r.rruleProperties == rules && r.rdateProperties == dates && r.exdateProperties == exclusions);
}
void same(const N::Report& a, const N::Report& b) {
    assert(a.status == b.status && a.failureDomain == b.failureDomain && a.propertyIndex == b.propertyIndex && a.propertyCode == b.propertyCode &&
        a.offset == b.offset && a.observanceStatus == b.observanceStatus && a.contextStatus == b.contextStatus &&
        a.contextFailureDomain == b.contextFailureDomain && a.ruleStatus == b.ruleStatus &&
        a.observanceLocalDeferred == b.observanceLocalDeferred && a.contextualRuleDeferred == b.contextualRuleDeferred &&
        a.rruleProperties == b.rruleProperties && a.rdateProperties == b.rdateProperties && a.exdateProperties == b.exdateProperties);
}
void localFailure(Session& s, N::Limits budget = limits()) {
    const auto original = Z::validateObservance(s.stage, 1, 2, budget.context.timezone);
    assert(original.status != Z::Status::LocalChecksPassed);
    const auto r = checked(s, budget);
    expect(r, S::ObservanceLocalFailure, D::ObservanceLocal, original.propertyIndex, original.propertyCode);
    assert(r.observanceStatus == original.status);
    assert(r.contextFailureDomain == X::FailureDomain::None && r.contextStatus == X::Status::InvalidArgument && r.ruleStatus == R::Status::InvalidArgument);
}
void ruleFailure(Session& s, size_t property, N::Limits budget = limits()) {
    const auto original = X::validate(s.stage, 1, 2, property, budget.context);
    assert(original.status != X::Status::ContextChecksPassed);
    const auto r = checked(s, budget);
    expect(r, S::RruleContextFailure, D::RruleContext, original.propertyIndex, original.propertyCode, original.offset);
    assert(r.contextStatus == original.status && r.contextFailureDomain == original.failureDomain);
    if (original.failureDomain == X::FailureDomain::RuleGrammar) assert(r.ruleStatus == original.ruleStatus);
    else assert(r.ruleStatus == R::Status::InvalidArgument);
}
void rules() {
    for (const char* kind : {"STANDARD", "DAYLIGHT"}) {
        { Session s; parse(s, feed(required, kind)); auto b = limits(); b.maxRruleProperties = 0;
            b.context.maxComponentProperties = 0; b.context.rule = {0,0,0}; b.context.timezone.maxRdateMembers = b.context.timezone.maxRdateBytes = 0;
            const auto r = checked(s, b); counts(r, 0, 0, 0); assert(!r.contextualRuleDeferred);
            assert(r.observanceLocalDeferred == (Z::SourceValidationRequired | Z::TransitionResolutionPending));
            assert(r.observanceStatus == Z::Status::InvalidArgument && r.contextStatus == X::Status::InvalidArgument);
        }
        for (const std::string& fields : {required + "RRULE:FREQ=DAILY\r\n",
            "RRULE:FREQ=YEARLY;BYMONTH=3;BYDAY=-1SU\r\n" + required,
            onset + "RRULE:FREQ=YEARLY\r\n" + offsets + "RRULE:FREQ=MONTHLY\r\n"}) {
            Session s; parse(s, feed(fields, kind)); const auto r = checked(s);
            const size_t n = fields.find("MONTHLY") != std::string::npos ? 2 : 1;
            counts(r, n, 0, 0); uint32_t expected = 0;
            for (size_t i = 0; i < n; ++i) {
                const auto c = X::validate(s.stage, 1, 2, find(s.stage, Code::Rrule, i), limits().context);
                assert(c.status == X::Status::ContextChecksPassed); expected |= c.deferred;
            }
            assert(r.contextualRuleDeferred == expected);
            auto b = limits(); b.maxRruleProperties = n; same(r, checked(s, b));
            b.maxRruleProperties = n - 1;
            expect(checked(s, b), S::ResourceLimit, D::Budget, find(s.stage, Code::Rrule, n - 1), Code::Rrule);
            b.maxRruleProperties = 0;
            expect(checked(s, b), S::ResourceLimit, D::Budget, find(s.stage, Code::Rrule), Code::Rrule);
            if (n > 1) assert((r.contextualRuleDeferred & X::MultipleRrulePending) && (r.observanceLocalDeferred & Z::MultipleRrulePending));
        }
        for (const char* bad : {"FREQ=BOGUS", "FREQ=YEARLY;RSCALE=GREGORIAN", "FREQ=YEARLY;SKIP=OMIT", "FREQ=YEARLY;X-RULE=1",
            "FREQ=YEARLY;UNTIL=20271017T020000", "FREQ=YEARLY;UNTIL=20271017"}) {
            Session s; parse(s, feed(required + "RRULE:FREQ=YEARLY\r\nRRULE:" + bad + "\r\n", kind));
            ruleFailure(s, find(s.stage, Code::Rrule, 1));
        }
        { Session s; parse(s, feed(required + "RRULE:FREQ=BOGUS\r\nRRULE:FREQ=YEARLY;RSCALE=GREGORIAN\r\n", kind));
            ruleFailure(s, find(s.stage, Code::Rrule));
        }
        { Session s; parse(s, feed(required + "RRULE:FREQ=YEARLY;UNTIL=20271017T020060Z\r\n", kind));
            const auto r = checked(s); counts(r, 1, 0, 0); assert(r.contextualRuleDeferred & X::LeapSecondPending);
            assert(!(r.observanceLocalDeferred & Z::LeapSecondPending));
            auto b = limits(); b.context.rule.maxRuleBytes = 1; ruleFailure(s, find(s.stage, Code::Rrule), b);
            b = limits(); b.context.maxComponentProperties = 3; ruleFailure(s, find(s.stage, Code::Rrule), b);
        }
    }
}
void datesAndExclusions() {
    // Adjudicated composition: two contextual calls repeat local RDATE validation;
    // the coverage report still counts this RDATE property only once.
    for (const char* kind : {"STANDARD", "DAYLIGHT"}) {
        Session s; parse(s, feed(onset + "RRULE;X-REVIEW=opaque:FREQ=YEARLY\r\n" + offsets +
            "RDATE;X-REVIEW=opaque:" + time1 + "," + time2 + "\r\nRRULE:FREQ=MONTHLY\r\nEXDATE;VALUE=PERIOD:not-a-period\r\n", kind));
        const auto r = checked(s); counts(r, 2, 1, 1);
        assert(r.contextualRuleDeferred == (X::RecurrenceExpansionPending | X::RecurrenceSetReconciliationPending | X::MultipleRrulePending | X::ParameterReviewPending));
        assert((r.observanceLocalDeferred & (Z::ObservanceExdateReviewPending | Z::ExtensionReviewPending | Z::MultipleRrulePending)) ==
            (Z::ObservanceExdateReviewPending | Z::ExtensionReviewPending | Z::MultipleRrulePending));
        for (size_t i = 0; i < 2; ++i) assert(X::validate(s.stage, 1, 2, find(s.stage, Code::Rrule, i), limits().context).status == X::Status::ContextChecksPassed);
    }
    for (const std::string& fields : {"RDATE:" + time1 + "," + time2 + "\r\nRDATE:" + time1 + "\r\n",
        std::string("RDATE:20261017T020060\r\n"), std::string("EXDATE:uninterpreted,not-a-date\r\n"), std::string("EXDATE:\r\nEXDATE;X-KEEP=raw:opaque\r\n"),
        "RRULE:FREQ=YEARLY\r\nRDATE:" + time1 + "\r\nEXDATE:opaque\r\n"}) {
        Session s; parse(s, feed(required + fields)); const auto r = checked(s);
        const size_t dates = fields.find("RDATE") == std::string::npos ? 0 : fields.find("RDATE", fields.find("RDATE") + 1) == std::string::npos ? 1 : 2;
        const size_t excluded = fields.find("EXDATE") == std::string::npos ? 0 : fields.find("EXDATE", fields.find("EXDATE") + 1) == std::string::npos ? 1 : 2;
        counts(r, fields.find("RRULE") == std::string::npos ? 0 : 1, dates, excluded);
        if (excluded) assert((r.observanceLocalDeferred & (Z::ObservanceExdateReviewPending | Z::ExtensionReviewPending)) ==
            (Z::ObservanceExdateReviewPending | Z::ExtensionReviewPending));
        if (fields.find("020060") != std::string::npos) assert(r.observanceLocalDeferred & Z::LeapSecondPending);
    }
    for (const char* value : {"bad", "20261017T020000,", "20261017T020000,20260230T020000", "20261017T020000Z"}) {
        Session s; parse(s, feed(required + "RRULE:FREQ=BOGUS\r\nRDATE:" + time1 + "\r\nRDATE:" + value + "\r\n"));
        localFailure(s); // Even a malformed earlier RRULE cannot hide the initial RDATE failure.
    }
    for (const char* property : {"RDATE;VALUE=DATE:20261017", "RDATE;VALUE=PERIOD:20261017T020000/20261017T030000",
        "RDATE;TZID=Custom:20261017T020000", "RDATE;VALUE=DATE-TIME,DATE-TIME:20261017T020000"}) {
        Session s; parse(s, feed(required + property + "\r\n")); localFailure(s);
    }
    Session s; parse(s, feed(required + "RDATE;X-P=x:" + time1 + "," + time2 + "\r\nRDATE:" + time1 + "," + time2 + "\r\n"));
    auto b = limits(); const auto index = find(s.stage, Code::Rdate); Stage::View value = {};
    assert(s.stage.value(index, value) == Stage::Read::Ok);
    b.context.timezone.maxRdateMembers = 2; b.context.timezone.maxRdateBytes = value.size + s.stage.parameters(index).size;
    counts(checked(s, b), 0, 2, 0); // Limits are per property, not aggregates.
    --b.context.timezone.maxRdateBytes; localFailure(s, b);
    b = limits(); b.context.timezone.maxRdateMembers = 1; localFailure(s, b);
    b.context.timezone.maxRdateMembers = 0; localFailure(s, b);
    b = limits(); b.context.timezone.maxRdateBytes = 0; localFailure(s, b);
}
void admissionAndCorruption() {
    { Stage fresh; N::Report r; { Audit a; r = N::inspect(fresh, 1, 2, limits()); } expect(r, S::Unavailable, D::Admission); }
    { Session incomplete; expect(checked(incomplete), S::Unavailable, D::Admission); }
    { Session failed; failed.stage.cancel(); expect(checked(failed), S::Unavailable, D::Admission); }
    { Session failed; const std::string raw = "HTTP/1.1 200 OK\r\nContent-Length: 10\r\n\r\nshort";
        failed.http.feed(reinterpret_cast<const uint8_t*>(raw.data()), raw.size()); failed.http.finish(); failed.stage.finish(failed.http, failed.structure);
        expect(checked(failed), S::Unavailable, D::Admission);
    }
    Session s; parse(s, feed(required + "RRULE:FREQ=DAILY\r\n") + feed(required, "DAYLIGHT"));
    counts(checked(s, limits(), 4, 5), 0, 0, 0);
    expect(checked(s, limits(), 1, 5), S::InvalidAssociation, D::Admission);
    expect(checked(s, limits(), 4, 2), S::InvalidAssociation, D::Admission);
    expect(checked(s, limits(), SIZE_MAX, 2), S::InvalidArgument, D::Admission);
    expect(checked(s, limits(), 1, SIZE_MAX), S::InvalidArgument, D::Admission);
    expect(checked(s, limits(), 0, 2), S::InvalidAssociation, D::Admission);
    expect(checked(s, limits(), 1, 1), S::InvalidAssociation, D::Admission);
    auto& c = *const_cast<Stage::Component*>(s.stage.component(2)); const auto original = c;
    c.closed = 0; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    ++c.ordinal; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    ++c.calendarOrdinal; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    c.parentOrdinal = 0; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    c.flags |= Stage::ProjectedOut; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    c.flags |= Stage::OpaqueProjectedOut; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    c.kind = IcsStructure::Kind::Event; expect(checked(s), S::InvalidAssociation, D::Admission); c = original;
    auto& parent = *const_cast<Stage::Component*>(s.stage.component(1)); const auto oldParent = parent;
    parent.closed = 0; expect(checked(s), S::InvalidAssociation, D::Admission); parent = oldParent;
    ++parent.ordinal; expect(checked(s), S::InvalidAssociation, D::Admission); parent = oldParent;
    parent.calendarOrdinal = 0; expect(checked(s), S::InvalidAssociation, D::Admission); parent = oldParent;
    parent.flags |= Stage::ProjectedOut; expect(checked(s), S::InvalidAssociation, D::Admission); parent = oldParent;
    auto& p = *const_cast<Stage::Property*>(s.stage.property(find(s.stage, Code::Rrule))); const auto oldProperty = p;
    p.owner = 1; localFailure(s); p = oldProperty;
    p.next = c.firstProperty; localFailure(s); p = oldProperty;
    p.next = static_cast<uint32_t>(s.stage.counts().storedProperties); localFailure(s); p = oldProperty;
    c.firstProperty = Stage::None; localFailure(s); c = original;
    c.lastProperty = c.firstProperty; localFailure(s); c = original;
    ++c.observedProperties; localFailure(s); c = original;
    auto b = limits(); b.context.timezone.maxObservanceProperties = 4; counts(checked(s, b), 1, 0, 0);
    b.context.timezone.maxObservanceProperties = 3; localFailure(s, b);
    b.context.timezone.maxObservanceProperties = 0; localFailure(s, b);
    for (const std::string& fields : {offsets + "RRULE:FREQ=BOGUS\r\n", onset + "TZOFFSETFROM:+0000\r\nRRULE:FREQ=DAILY\r\n"}) {
        Session missing; parse(missing, feed(fields)); localFailure(missing);
    }
}
void lifetime() {
    const std::string fields = "DTSTART:20261017T020060\r\n" + offsets + "RRULE;X-P=x:FREQ=YEARLY\r\nRDATE:" + time1 +
        "\r\nRRULE:FREQ=MONTHLY\r\nEXDATE;VALUE=X-UNKNOWN:deliberately-uninterpreted\r\n";
    for (bool chunked : {false, true}) for (size_t split : {size_t(1), size_t(7), SIZE_MAX}) {
        Session s; parse(s, feed(fields), chunked, split);
        const auto r = checked(s); counts(r, 2, 1, 1); const auto copy = r;
        same(r, checked(s));
        assert((r.observanceLocalDeferred & Z::LeapSecondPending) && (r.contextualRuleDeferred & X::LeapSecondPending));
        s.stage.release(); assert(s.stage.status() == Stage::Status::StagedSuccessfully && !s.stage.component(0));
        expect(checked(s), S::Unavailable, D::Admission);
        std::fill(s.data.data(), s.data.data() + s.data.capacity, 0xdd);
        same(r, copy); assert(r.observanceLocalDeferred & Z::ObservanceExdateReviewPending);
    }
}
}
int main() {
    static_assert(std::is_standard_layout<N::Report>::value && std::is_trivially_copyable<N::Report>::value, "Pointer-free copied report");
    static_assert(std::is_same<decltype(N::Report::observanceStatus), Z::Status>::value, "Local status domain");
    static_assert(std::is_same<decltype(N::Report::contextStatus), X::Status>::value, "Context status domain");
    static_assert(std::is_same<decltype(N::Report::ruleStatus), R::Status>::value, "Grammar status domain");
    rules(); datesAndExclusions(); admissionAndCorruption(); lifetime();
    std::printf("Observance recurrence coverage: %zu deterministic cases; zero audited allocations; report %zu/%zu\n", scenarios, sizeof(N::Report), alignof(N::Report));
}
