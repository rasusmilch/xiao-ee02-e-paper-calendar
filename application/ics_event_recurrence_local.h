#ifndef EE02_ICS_EVENT_RECURRENCE_LOCAL_H
#define EE02_ICS_EVENT_RECURRENCE_LOCAL_H

#include "ics_recur_context.h"

// Complete direct VEVENT recurrence-property coverage only, not a recurrence set
// or source acceptance. Finish inside the stage's scratch lifetime. No concurrent
// mutation/release, allocation, saved views, occurrence production or publication.
namespace IcsEventRecurrenceLocal {
enum class Status {
    LocalCoveragePassed, Unavailable, InvalidArgument, InvalidAssociation, ResourceLimit,
    EventLocalFailure, RruleContextFailure, TemporalListFailure
};
enum class FailureDomain { None, EventLocal, RruleContext, TemporalList, Association, Budget };
enum Deferred : uint32_t {
    SourceSemanticValidationPending = 1U << 0,
    RecurrenceSetReconciliationPending = 1U << 1,
    RecurrenceExpansionPending = 1U << 2,
    MissingStartContextPending = 1U << 3,
    MultipleRrulePending = 1U << 4,
    DateListContextPending = 1U << 5,
    ParameterReviewPending = 1U << 6,
    TimezoneResolutionPending = 1U << 7,
    LeapSecondPending = 1U << 8,
    PeriodSemanticsPending = 1U << 9,
    OverrideIdentityPending = 1U << 10,
    RangeReviewPending = 1U << 11
};
struct Limits {
    size_t maxComponentProperties, maxRruleProperties, maxRdateProperties, maxExdateProperties;
    size_t maxTotalListMembers, maxTotalListBytes;
    IcsRecurContext::Limits context;
};
struct Report {
    Status status;
    FailureDomain failureDomain;
    size_t propertyIndex;
    IcsSourceStage::Code propertyCode;
    size_t offset; // Property-value-relative when applicable, zero otherwise.
    // Inactive diagnostic slots are placeholders, not validation evidence.
    IcsEventLocal::Status eventStatus;
    IcsRecurContext::Status contextStatus;
    IcsRecurContext::FailureDomain contextFailureDomain;
    IcsTypedValues::Status temporalStatus;
    uint32_t deferred, contextualRuleDeferred; // Distinct bit domains, never conflated.
    size_t rruleProperties, rdateProperties, exdateProperties, rdateMembers, exdateMembers;
    bool recurrenceIdPresent;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
namespace E = IcsEventLocal;
namespace X = IcsRecurContext;
namespace T = IcsTypedValues;
inline Report result(Status status, FailureDomain domain, size_t index = Stage::None,
                     Code code = Code::Unknown, size_t offset = 0) {
    Report r = {};
    r.status = status; r.failureDomain = domain; r.propertyIndex = index; r.propertyCode = code; r.offset = offset;
    r.eventStatus = E::Status::LocalChecksPassed; r.contextStatus = X::Status::ContextChecksPassed;
    r.contextFailureDomain = X::FailureDomain::None; r.temporalStatus = T::Status::Ok;
    return r;
}
inline bool increment(size_t& count, size_t limit) {
    if (count == SIZE_MAX || count >= limit) return false;
    ++count; return true;
}
inline Report listFailure(T::Result error, size_t index, Code code) {
    // Iterator exhaustion of the supplied aggregate budget is still a resource
    // failure. TemporalList domain keeps the original typed status/offset useful.
    Report r = result(error.status == T::Status::ResourceLimit ? Status::ResourceLimit : Status::TemporalListFailure,
        FailureDomain::TemporalList, index, code, error.offset);
    r.temporalStatus = error.status; return r;
}
inline uint32_t contextObligations(uint32_t original) {
    uint32_t pending = RecurrenceExpansionPending | RecurrenceSetReconciliationPending;
    if (original & X::MissingDtstartPending) pending |= MissingStartContextPending;
    if (original & X::MultipleRrulePending) pending |= MultipleRrulePending;
    if (original & X::ParameterReviewPending) pending |= ParameterReviewPending;
    if (original & X::TimezoneResolutionPending) pending |= TimezoneResolutionPending;
    if (original & X::LeapSecondPending) pending |= LeapSecondPending;
    // IgnoredTimePartsPresent remains in the separate, original contextual mask.
    return pending;
}
inline void temporalEvidence(const T::TemporalValue& value, uint32_t& pending) {
    if (value.type == T::Type::DateTime) {
        if (value.dateTime.mode == T::TimeMode::Tzid) pending |= TimezoneResolutionPending;
        if (value.dateTime.second == 60) pending |= LeapSecondPending;
    } else if (value.type == T::Type::Period) {
        pending |= PeriodSemanticsPending;
        if (value.period.start.mode == T::TimeMode::Tzid) pending |= TimezoneResolutionPending;
        if (value.period.start.second == 60 || (!value.period.durationForm && value.period.end.second == 60))
            pending |= LeapSecondPending;
    }
}
}

inline Report inspect(const IcsSourceStage& stage, size_t calendarComponentIndex,
                      size_t eventComponentIndex, const Limits& limits) {
    using namespace detail;
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return result(Status::Unavailable, FailureDomain::Association);
    const auto* calendar = stage.component(calendarComponentIndex);
    const auto* event = stage.component(eventComponentIndex);
    if (!calendar || !event) return result(Status::InvalidArgument, FailureDomain::Association);
    if (calendar->kind != Kind::Calendar || event->kind != Kind::Event ||
        calendarComponentIndex >= eventComponentIndex || calendar->parentOrdinal ||
        calendar->closed != 1 || event->closed != 1 ||
        calendar->ordinal != uint64_t(calendarComponentIndex) + 1 || event->ordinal != uint64_t(eventComponentIndex) + 1 ||
        event->parentOrdinal != calendar->ordinal || !calendar->calendarOrdinal ||
        event->calendarOrdinal != calendar->calendarOrdinal ||
        ((calendar->flags | event->flags) & (Stage::ProjectedOut | Stage::OpaqueProjectedOut)))
        return result(Status::InvalidAssociation, FailureDomain::Association);
    const auto local = E::validate(stage, calendarComponentIndex, eventComponentIndex, limits.context.event);
    if (local.status != E::Status::LocalChecksPassed) {
        Report failure = result(Status::EventLocalFailure, FailureDomain::EventLocal, local.propertyIndex, local.propertyCode);
        failure.eventStatus = local.status; return failure;
    }

    // Entire chain preflight before ANY recurrence interpretation. The unchanged
    // event validator's limits do not replace this independent coverage bound.
    size_t visited = 0; uint32_t last = Stage::None;
    for (uint32_t i = event->firstProperty; i != Stage::None;) {
        const auto* p = stage.property(i);
        if (!p || p->owner != eventComponentIndex)
            return result(Status::InvalidAssociation, FailureDomain::Association, i, p ? p->code : Code::Unknown);
        if (!increment(visited, limits.maxComponentProperties)) return result(Status::ResourceLimit, FailureDomain::Budget, i, p->code);
        if (visited > event->observedProperties || (p->next != Stage::None &&
            (p->next <= i || p->next >= stage.counts().storedProperties)))
            return result(Status::InvalidAssociation, FailureDomain::Association, i, p->code);
        last = i; i = p->next;
    }
    if (visited != event->observedProperties || last != event->lastProperty)
        return result(Status::InvalidAssociation, FailureDomain::Association, last,
            last == Stage::None ? Code::Unknown : stage.property(last)->code);

    Report r = result(Status::LocalCoveragePassed, FailureDomain::None);
    r.deferred = SourceSemanticValidationPending;
    size_t remainingMembers = limits.maxTotalListMembers, remainingBytes = limits.maxTotalListBytes;
    for (uint32_t i = event->firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        if (p.code == Code::Rrule) {
            if (!increment(r.rruleProperties, limits.maxRruleProperties))
                return result(Status::ResourceLimit, FailureDomain::Budget, i, p.code);
            const auto context = X::validate(stage, calendarComponentIndex, eventComponentIndex, i, limits.context);
            if (context.status != X::Status::ContextChecksPassed && context.status != X::Status::MissingStartContext) {
                Report failure = result(Status::RruleContextFailure, FailureDomain::RruleContext,
                    context.propertyIndex, context.propertyCode, context.offset);
                failure.contextStatus = context.status; failure.contextFailureDomain = context.failureDomain; return failure;
            }
            r.contextualRuleDeferred |= context.deferred;
            r.deferred |= contextObligations(context.deferred);
            if (context.status == X::Status::MissingStartContext) r.deferred |= MissingStartContextPending;
            if (r.rruleProperties > 1) r.deferred |= MultipleRrulePending;
        } else if (p.code == Code::Rdate || p.code == Code::Exdate) {
            const bool inclusion = p.code == Code::Rdate;
            if (!increment(inclusion ? r.rdateProperties : r.exdateProperties,
                           inclusion ? limits.maxRdateProperties : limits.maxExdateProperties))
                return result(Status::ResourceLimit, FailureDomain::Budget, i, p.code);
            Stage::View value = {};
            if (stage.value(i, value) != Stage::Read::Ok) return listFailure(T::result(T::Status::Unavailable), i, p.code);
            const auto parameters = stage.parameters(i);
            // Subtraction checks the full sum without first overflowing it.
            if (!remainingMembers || !remainingBytes || value.size > remainingBytes || parameters.size > remainingBytes - value.size)
                return result(Status::ResourceLimit, FailureDomain::Budget, i, p.code);
            T::TemporalList list(stage, i, {remainingMembers, remainingBytes});
            T::TemporalValue member = {};
            for (;;) {
                const auto next = list.next(member);
                if (next.status == T::Status::End) break;
                if (!T::parsed(next)) return listFailure(next, i, p.code);
                if (!remainingMembers || !increment(inclusion ? r.rdateMembers : r.exdateMembers, SIZE_MAX))
                    return result(Status::ResourceLimit, FailureDomain::Budget, i, p.code);
                --remainingMembers; temporalEvidence(member, r.deferred);
            }
            if (!list.complete() || !T::parsed(list.status())) return listFailure(list.status(), i, p.code);
            remainingBytes -= value.size; remainingBytes -= parameters.size;
            r.deferred |= RecurrenceSetReconciliationPending | DateListContextPending;
            if (list.selection().review) r.deferred |= ParameterReviewPending;
            // NeedsContext remains pending even without a TZID or leap second
            // (e.g. a PERIOD whose local end ordering is not yet established).
            if (list.status().status == T::Status::NeedsContext) r.deferred |= DateListContextPending;
        } else if (p.code == Code::RecurrenceId) {
            // Cardinality and temporal decoding already passed in EventLocal.
            // Inspect parameter metadata only; never decode/compare the identity.
            r.recurrenceIdPresent = true;
            r.deferred |= RecurrenceSetReconciliationPending | OverrideIdentityPending;
            if (p.disposition == Stage::Disposition::RetainUnreviewed || (p.flags & Stage::UnknownParameters))
                r.deferred |= ParameterReviewPending;
            IcsStructure::ParameterIterator parameters(stage.parameters(i)); IcsStructure::Parameter parameter;
            while (parameters.next(parameter)) {
                if (IcsStructure::identifier(parameter.name, "RANGE")) r.deferred |= RangeReviewPending;
                else if (!IcsStructure::identifier(parameter.name, "VALUE") && !IcsStructure::identifier(parameter.name, "TZID"))
                    r.deferred |= ParameterReviewPending;
            }
        }
    }
    return r;
}
}
#endif
