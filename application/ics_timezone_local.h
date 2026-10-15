#ifndef EE02_ICS_TIMEZONE_LOCAL_H
#define EE02_ICS_TIMEZONE_LOCAL_H

#include "ics_typed_values.h"

// Selected local schema checks, not timezone resolution or source acceptance.
// Finish all calls inside the readable stage's withScratch lifetime. No concurrent
// mutation/release, allocation, retained views, transition table or publication.
namespace IcsTimezoneLocal {
enum class Status {
    LocalChecksPassed, InvalidArgument, Unavailable, InvalidAssociation,
    MissingRequired, DuplicateProperty, MalformedValue, InvalidParameter,
    InvalidValueType, UnsupportedFeature, ResourceLimit
};
enum Deferred : uint32_t {
    SourceValidationRequired = 1U << 0,
    TimezoneIdentityReconciliationPending = 1U << 1,
    TransitionResolutionPending = 1U << 2,
    RecurrenceValidationPending = 1U << 3,
    MultipleRrulePending = 1U << 4,
    ExtensionReviewPending = 1U << 5,
    OptionalPropertyValidationPending = 1U << 6,
    LeapSecondPending = 1U << 7,
    ObservanceExdateReviewPending = 1U << 8
};
struct Limits {
    size_t maxTimezoneProperties, maxObservanceProperties, maxTzidTextBytes;
    size_t maxRdateMembers, maxRdateBytes;
};
struct Report {
    Status status;
    uint32_t deferred;
    size_t propertyIndex;
    IcsSourceStage::Code propertyCode;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
namespace T = IcsTypedValues;
inline Report failure(Status status, size_t index = Stage::None, Code code = Code::Unknown) {
    return {status, 0, index, code};
}
inline Status typedStatus(T::Status status) {
    switch (status) {
    case T::Status::InvalidArgument: return Status::InvalidArgument;
    case T::Status::Unavailable: return Status::Unavailable;
    case T::Status::InvalidParameter: case T::Status::InvalidCardinality: return Status::InvalidParameter;
    case T::Status::InvalidValueType: return Status::InvalidValueType;
    case T::Status::UnsupportedFeature: return Status::UnsupportedFeature;
    case T::Status::ResourceLimit: return Status::ResourceLimit;
    default: return Status::MalformedValue;
    }
}
inline Report admission(const Stage& stage, size_t parent, size_t child, bool timezone) {
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return failure(Status::Unavailable);
    const auto* p = stage.component(parent);
    const auto* c = stage.component(child);
    if (!p || !c) return failure(Status::InvalidArgument);
    if (p->kind != (timezone ? Kind::Calendar : Kind::Timezone) ||
        (timezone ? c->kind != Kind::Timezone : c->kind != Kind::Standard && c->kind != Kind::Daylight) ||
        p->closed != 1 || c->closed != 1 ||
        p->ordinal != uint64_t(parent) + 1 || c->ordinal != uint64_t(child) + 1 ||
        c->parentOrdinal != p->ordinal || !p->calendarOrdinal || c->calendarOrdinal != p->calendarOrdinal)
        return failure(Status::InvalidAssociation);
    return {Status::LocalChecksPassed, 0, Stage::None, Code::Unknown};
}
inline Report links(const Stage& stage, size_t owner, const Stage::Component& c, size_t limit) {
    size_t visited = 0;
    uint32_t last = Stage::None;
    for (uint32_t i = c.firstProperty; i != Stage::None;) {
        const auto* p = stage.property(i);
        if (!p || p->owner != owner) return failure(Status::InvalidAssociation, i, p ? p->code : Code::Unknown);
        if (visited == limit) return failure(Status::ResourceLimit, i, p->code);
        // Append-only staging links strictly increase; this also rejects cycles.
        if (visited >= c.observedProperties || (p->next != Stage::None &&
            (p->next <= i || p->next >= stage.counts().storedProperties)))
            return failure(Status::InvalidAssociation, i, p->code);
        ++visited; last = i; i = p->next;
    }
    if (visited != c.observedProperties || last != c.lastProperty)
        return failure(Status::InvalidAssociation, last, last == Stage::None ? Code::Unknown : stage.property(last)->code);
    return {Status::LocalChecksPassed, 0, Stage::None, Code::Unknown};
}
inline bool reviewed(const Stage::Property& p) {
    return p.disposition == Stage::Disposition::RetainUnreviewed || (p.flags & Stage::UnknownParameters);
}
inline bool core(Code c, bool timezone) {
    return timezone ? c == Code::Tzid || c == Code::LastModified || c == Code::Tzurl :
        c == Code::Dtstart || c == Code::Tzoffsetfrom || c == Code::Tzoffsetto ||
        c == Code::Rrule || c == Code::Rdate || c == Code::Tzname || c == Code::Comment;
}
inline Report validate(const Stage& stage, size_t parent, size_t child, const Limits& limits, bool timezone) {
    // A/B: no semantic failure can hide malformed associations or incomplete lists.
    Report r = admission(stage, parent, child, timezone);
    if (r.status != Status::LocalChecksPassed) return r;
    const auto& c = *stage.component(child);
    r = links(stage, child, c, timezone ? limits.maxTimezoneProperties : limits.maxObservanceProperties);
    if (r.status != Status::LocalChecksPassed) return r;
    // C/D: only the three specified singletons; missing priority is array order.
    const Code singleton[3] = {timezone ? Code::Tzid : Code::Dtstart,
        timezone ? Code::LastModified : Code::Tzoffsetfrom, timezone ? Code::Tzurl : Code::Tzoffsetto};
    size_t first[3] = {Stage::None, Stage::None, Stage::None};
    for (uint32_t i = c.firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        for (size_t n = 0; n < 3; ++n) if (p.code == singleton[n]) {
            if (first[n] != Stage::None) return failure(Status::DuplicateProperty, i, p.code);
            first[n] = i;
        }
    }
    for (size_t n = 0; n < (timezone ? size_t(1) : size_t(3)); ++n)
        if (first[n] == Stage::None) return failure(Status::MissingRequired, Stage::None, singleton[n]);

    // E: typed checks in retained input order. No cross-observance interpretation.
    uint32_t deferred = SourceValidationRequired | TransitionResolutionPending;
    if (timezone) deferred |= TimezoneIdentityReconciliationPending;
    bool rrule = false;
    for (uint32_t i = c.firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        if (reviewed(p) || (!core(p.code, timezone) &&
            p.disposition != Stage::Disposition::IgnoreByDeclaredProjection)) deferred |= ExtensionReviewPending;
        if (timezone && p.code == Code::Tzid) {
            T::View value = {};
            if (stage.value(i, value) != Stage::Read::Ok) return failure(Status::Unavailable, i, p.code);
            if (value.size > limits.maxTzidTextBytes) return failure(Status::ResourceLimit, i, p.code);
            size_t decoded = 0;
            const auto result = T::measureText(value, decoded);
            if (!T::parsed(result)) return failure(typedStatus(result.status), i, p.code);
            if (!decoded) return failure(Status::MalformedValue, i, p.code);
            if (p.parameterCount) deferred |= ExtensionReviewPending;
        } else if ((timezone && p.code == Code::LastModified) || (!timezone && p.code == Code::Dtstart)) {
            T::TemporalValue value = {}; T::Selection selection = {};
            const auto result = T::decodeTemporal(stage, i, value, selection);
            if (!T::parsed(result)) return failure(typedStatus(result.status), i, p.code);
            if (selection.review) deferred |= ExtensionReviewPending;
            if (value.dateTime.second == 60) deferred |= LeapSecondPending;
        } else if (!timezone && (p.code == Code::Tzoffsetfrom || p.code == Code::Tzoffsetto)) {
            T::View value = {}; int32_t seconds = 0;
            if (stage.value(i, value) != Stage::Read::Ok) return failure(Status::Unavailable, i, p.code);
            const auto result = T::decodeUtcOffset(value, seconds);
            if (!T::parsed(result)) return failure(typedStatus(result.status), i, p.code);
            if (p.parameterCount) deferred |= ExtensionReviewPending;
        } else if (!timezone && p.code == Code::Rdate) {
            if (!limits.maxRdateMembers || !limits.maxRdateBytes) return failure(Status::ResourceLimit, i, p.code);
            T::TemporalList list(stage, i, {limits.maxRdateMembers, limits.maxRdateBytes});
            T::TemporalValue value = {};
            for (;;) {
                const auto result = list.next(value);
                if (result.status == T::Status::End) break;
                if (!T::parsed(result)) return failure(typedStatus(result.status), i, p.code);
                if (value.dateTime.second == 60) deferred |= LeapSecondPending;
            }
            if (!list.complete() || !T::parsed(list.status()))
                return failure(typedStatus(list.status().status), i, p.code);
            if (list.selection().review) deferred |= ExtensionReviewPending;
            deferred |= RecurrenceValidationPending;
        } else if (!timezone && p.code == Code::Rrule) {
            deferred |= RecurrenceValidationPending;
            if (rrule) deferred |= MultipleRrulePending;
            rrule = true;
        } else if (!timezone && p.code == Code::Exdate) {
            deferred |= ObservanceExdateReviewPending | ExtensionReviewPending;
        } else if (p.code != Code::Unknown && (p.flags & Stage::ValueRetained)) {
            deferred |= OptionalPropertyValidationPending;
        }
    }
    return {Status::LocalChecksPassed, deferred, Stage::None, Code::Unknown};
}
}

inline Report validateTimezone(const IcsSourceStage& stage, size_t calendarComponentIndex,
                               size_t timezoneComponentIndex, const Limits& limits) {
    return detail::validate(stage, calendarComponentIndex, timezoneComponentIndex, limits, true);
}
inline Report validateObservance(const IcsSourceStage& stage, size_t timezoneComponentIndex,
                                 size_t observanceComponentIndex, const Limits& limits) {
    return detail::validate(stage, timezoneComponentIndex, observanceComponentIndex, limits, false);
}
}
#endif
