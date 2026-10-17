#ifndef EE02_ICS_RECUR_CONTEXT_H
#define EE02_ICS_RECUR_CONTEXT_H

#include "ics_recur_value.h"
#include "ics_event_local.h"
#include "ics_timezone_local.h"

// One actual retained RRULE in its direct component context, not an executable
// recurrence set or source acceptance. All reads end inside the stage's scratch
// lifetime. No concurrent mutation/release, allocation, escaped views or rewriting.
namespace IcsRecurContext {
enum class Status {
    ContextChecksPassed, MissingStartContext, Unavailable, InvalidArgument,
    InvalidAssociation, LocalValidationFailed, MalformedRule, UnsupportedFeature,
    InvalidRelationship, ResourceLimit
};
enum class FailureDomain { None, EventLocal, ObservanceLocal, RuleGrammar, Context };
enum Deferred : uint32_t {
    RecurrenceExpansionPending = 1U << 0,
    RecurrenceSetReconciliationPending = 1U << 1,
    MultipleRrulePending = 1U << 2,
    ParameterReviewPending = 1U << 3,
    MissingDtstartPending = 1U << 4,
    TimezoneResolutionPending = 1U << 5,
    LeapSecondPending = 1U << 6,
    IgnoredTimePartsPresent = 1U << 7
};
struct Limits {
    size_t maxComponentProperties;
    IcsRecurValue::Limits rule;
    IcsEventLocal::Limits event;
    IcsTimezoneLocal::Limits timezone;
};
struct Report {
    Status status;
    FailureDomain failureDomain;
    size_t propertyIndex;
    IcsSourceStage::Code propertyCode;
    size_t offset; // Rule-relative when applicable; zero otherwise.
    // Diagnostic slots are meaningful only in their corresponding failure domain.
    IcsRecurValue::Status ruleStatus;
    IcsEventLocal::Status eventStatus;
    IcsTimezoneLocal::Status timezoneStatus;
    uint32_t deferred, ignoredCoreParts;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
using View = IcsStructure::View;
namespace T = IcsTypedValues;
namespace R = IcsRecurValue;
inline Report result(Status status, size_t property = Stage::None,
                     Code code = Code::Unknown, size_t offset = 0) {
    Report r = {};
    r.status = status; r.failureDomain = FailureDomain::Context;
    r.propertyIndex = property; r.propertyCode = code; r.offset = offset;
    r.ruleStatus = R::Status::CoreGrammarValid;
    r.eventStatus = IcsEventLocal::Status::LocalChecksPassed;
    r.timezoneStatus = IcsTimezoneLocal::Status::LocalChecksPassed;
    return r;
}
// Locate UNTIL only after complete core grammar success. No independent grammar
// acceptance, extension parsing, numeric interpretation or rule copy occurs here.
inline size_t untilValue(View rule, View& until) {
    for (size_t start = 0; start < rule.size;) {
        size_t equal = start;
        while (equal < rule.size && rule.data[equal] != '=') ++equal;
        size_t end = equal + 1; // '=' exists within this already validated part.
        while (end < rule.size && rule.data[end] != ';') ++end;
        if (IcsStructure::identifier({rule.data + start, equal - start}, "UNTIL")) {
            until = {rule.data + equal + 1, end - equal - 1}; return equal + 1;
        }
        start = end == rule.size ? end : end + 1;
    }
    until = {nullptr, 0}; return rule.size;
}
}

inline Report validate(const IcsSourceStage& stage, size_t parentComponentIndex,
                       size_t componentIndex, size_t rrulePropertyIndex, const Limits& limits) {
    using namespace detail;
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return result(Status::Unavailable);
    const auto* parent = stage.component(parentComponentIndex);
    const auto* component = stage.component(componentIndex);
    if (!parent || !component) return result(Status::InvalidArgument);
    const bool event = component->kind == Kind::Event;
    const bool observance = component->kind == Kind::Standard || component->kind == Kind::Daylight;
    if ((!event && !observance) || parent->kind != (event ? Kind::Calendar : Kind::Timezone) ||
        parentComponentIndex >= componentIndex || parent->closed != 1 || component->closed != 1 ||
        parent->ordinal != uint64_t(parentComponentIndex) + 1 ||
        component->ordinal != uint64_t(componentIndex) + 1 ||
        component->parentOrdinal != parent->ordinal || !parent->calendarOrdinal ||
        component->calendarOrdinal != parent->calendarOrdinal || (event && parent->parentOrdinal) ||
        ((parent->flags | component->flags) & (Stage::ProjectedOut | Stage::OpaqueProjectedOut)))
        return result(Status::InvalidAssociation);
    const auto* rule = stage.property(rrulePropertyIndex);
    if (!rule) return result(Status::InvalidArgument, rrulePropertyIndex);
    if (rule->owner != componentIndex) return result(Status::InvalidAssociation, rrulePropertyIndex, rule->code);
    if (rule->code != Code::Rrule) return result(Status::InvalidArgument, rrulePropertyIndex, rule->code);
    View value = {};
    if (stage.value(rrulePropertyIndex, value) != Stage::Read::Ok)
        return result(Status::Unavailable, rrulePropertyIndex, Code::Rrule);

    if (event) {
        const auto local = IcsEventLocal::validate(stage, parentComponentIndex, componentIndex, limits.event);
        if (local.status != IcsEventLocal::Status::LocalChecksPassed) {
            Report failure = result(Status::LocalValidationFailed, local.propertyIndex, local.propertyCode);
            failure.failureDomain = FailureDomain::EventLocal; failure.eventStatus = local.status; return failure;
        }
    } else {
        const auto local = IcsTimezoneLocal::validateObservance(stage, parentComponentIndex, componentIndex, limits.timezone);
        if (local.status != IcsTimezoneLocal::Status::LocalChecksPassed) {
            Report failure = result(Status::LocalValidationFailed, local.propertyIndex, local.propertyCode);
            failure.failureDomain = FailureDomain::ObservanceLocal; failure.timezoneStatus = local.status; return failure;
        }
    }

    // One complete bounded direct-chain scan after local success. The independent
    // budget is not substituted for the unchanged local validator's own limits.
    size_t visited = 0, rules = 0, startIndex = Stage::None;
    uint32_t last = Stage::None;
    bool found = false;
    for (uint32_t i = component->firstProperty; i != Stage::None;) {
        const auto* p = stage.property(i);
        if (!p || p->owner != componentIndex)
            return result(Status::InvalidAssociation, i, p ? p->code : Code::Unknown);
        if (visited == limits.maxComponentProperties || visited == SIZE_MAX)
            return result(Status::ResourceLimit, i, p->code);
        if (visited >= component->observedProperties || (p->next != Stage::None &&
            (p->next <= i || p->next >= stage.counts().storedProperties)))
            return result(Status::InvalidAssociation, i, p->code);
        ++visited; last = i; found = found || i == rrulePropertyIndex;
        if (p->code == Code::Dtstart) startIndex = i; // Local singleton validation already passed.
        if (p->code == Code::Rrule) {
            if (rules == SIZE_MAX) return result(Status::ResourceLimit, i, p->code);
            ++rules;
        }
        i = p->next;
    }
    if (visited != component->observedProperties || last != component->lastProperty)
        return result(Status::InvalidAssociation, last, last == Stage::None ? Code::Unknown : stage.property(last)->code);
    if (!found) return result(Status::InvalidAssociation, rrulePropertyIndex, Code::Rrule);

    const auto grammar = R::validate(value, limits.rule);
    if (grammar.status != R::Status::CoreGrammarValid) {
        Status status = Status::InvalidArgument;
        switch (grammar.status) {
        case R::Status::MalformedRule: status = Status::MalformedRule; break;
        case R::Status::UnsupportedFeature: status = Status::UnsupportedFeature; break;
        case R::Status::ResourceLimit: status = Status::ResourceLimit; break;
        default: break;
        }
        Report failure = result(status, rrulePropertyIndex, Code::Rrule, grammar.offset);
        failure.failureDomain = FailureDomain::RuleGrammar; failure.ruleStatus = grammar.status; return failure;
    }
    Report r = result(Status::ContextChecksPassed);
    r.failureDomain = FailureDomain::None;
    r.deferred = RecurrenceExpansionPending | RecurrenceSetReconciliationPending;
    if (rules > 1) r.deferred |= MultipleRrulePending;
    if (stage.parameters(rrulePropertyIndex).size) r.deferred |= ParameterReviewPending;
    if (grammar.leapSecondPending) r.deferred |= LeapSecondPending;
    if (startIndex == Stage::None) {
        // Only a METHOD-bearing event can reach this point after local success.
        r.status = Status::MissingStartContext; r.deferred |= MissingDtstartPending; return r;
    }

    T::TemporalValue start = {}; T::Selection selection = {};
    const auto decoded = T::decodeTemporal(stage, startIndex, start, selection);
    // Immutable input plus local success guarantees a decodable DTSTART. An
    // inconsistent readable association is never converted to contextual success.
    if (!T::parsed(decoded)) return result(Status::InvalidAssociation, startIndex, Code::Dtstart);
    const bool date = start.type == T::Type::Date;
    if (!date) {
        if (start.dateTime.second == 60) r.deferred |= LeapSecondPending;
        if (start.dateTime.mode == T::TimeMode::Tzid) r.deferred |= TimezoneResolutionPending;
    } else {
        r.ignoredCoreParts = grammar.presence & (R::BYSECOND | R::BYMINUTE | R::BYHOUR);
        if (r.ignoredCoreParts) r.deferred |= IgnoredTimePartsPresent;
    }
    if (grammar.presence & R::UNTIL) {
        View until = {};
        const size_t offset = untilValue(value, until);
        const bool untilDate = until.size == 8;
        T::Date fields = {}; T::DateTime time = {};
        const auto decodedUntil = untilDate ? T::decodeDate(until, fields) : T::decodeDateTime(until, time);
        if (!T::parsed(decodedUntil)) return result(Status::InvalidAssociation, rrulePropertyIndex, Code::Rrule, offset);
        const bool needUtc = observance || (!date && start.dateTime.mode != T::TimeMode::Floating);
        if (date != untilDate || (!date && (needUtc != (time.mode == T::TimeMode::Utc))))
            return result(Status::InvalidRelationship, rrulePropertyIndex, Code::Rrule, offset);
        if (decodedUntil.status == T::Status::NeedsContext) r.deferred |= LeapSecondPending;
    }
    return r;
}
}
#endif
