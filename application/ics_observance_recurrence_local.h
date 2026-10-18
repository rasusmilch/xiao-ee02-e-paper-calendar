#ifndef EE02_ICS_OBSERVANCE_RECURRENCE_LOCAL_H
#define EE02_ICS_OBSERVANCE_RECURRENCE_LOCAL_H

#include "ics_recur_context.h"

// Direct observance recurrence-property coverage, not executable recurrences or
// transitions. Finish all reads inside the stage's scratch lifetime; no concurrent
// mutation/release, allocation, retained views or source/snapshot publication.
namespace IcsObservanceRecurrenceLocal {
enum class Status {
    LocalCoveragePassed, Unavailable, InvalidArgument, InvalidAssociation,
    ResourceLimit, ObservanceLocalFailure, RruleContextFailure
};
enum class FailureDomain { None, Admission, ObservanceLocal, RruleContext, Budget };
struct Limits {
    size_t maxRruleProperties;
    IcsRecurContext::Limits context;
};
struct Report {
    Status status;
    FailureDomain failureDomain;
    size_t propertyIndex;
    IcsSourceStage::Code propertyCode;
    size_t offset; // Rule-relative when applicable, zero otherwise.
    // Inactive diagnostic slots are placeholders, not evidence a validator ran.
    IcsTimezoneLocal::Status observanceStatus;
    IcsRecurContext::Status contextStatus;
    IcsRecurContext::FailureDomain contextFailureDomain;
    IcsRecurValue::Status ruleStatus; // Meaningful only for contextual RuleGrammar failures.
    uint32_t observanceLocalDeferred, contextualRuleDeferred; // Separate enum domains.
    size_t rruleProperties, rdateProperties, exdateProperties;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
namespace Z = IcsTimezoneLocal;
namespace X = IcsRecurContext;
inline Report result(Status status, FailureDomain domain, size_t index = Stage::None,
                     Code code = Code::Unknown, size_t offset = 0) {
    Report r = {};
    r.status = status; r.failureDomain = domain; r.propertyIndex = index;
    r.propertyCode = code; r.offset = offset;
    r.observanceStatus = Z::Status::InvalidArgument;
    r.contextStatus = X::Status::InvalidArgument;
    r.contextFailureDomain = X::FailureDomain::None;
    r.ruleStatus = IcsRecurValue::Status::InvalidArgument;
    return r;
}
}

inline Report inspect(const IcsSourceStage& stage, size_t timezoneComponentIndex,
                      size_t observanceComponentIndex, const Limits& limits) {
    using namespace detail;
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return result(Status::Unavailable, FailureDomain::Admission);
    const auto* parent = stage.component(timezoneComponentIndex);
    const auto* child = stage.component(observanceComponentIndex);
    if (!parent || !child) return result(Status::InvalidArgument, FailureDomain::Admission);
    if (parent->kind != Kind::Timezone || (child->kind != Kind::Standard && child->kind != Kind::Daylight) ||
        timezoneComponentIndex >= observanceComponentIndex || parent->closed != 1 || child->closed != 1 ||
        parent->ordinal != uint64_t(timezoneComponentIndex) + 1 ||
        child->ordinal != uint64_t(observanceComponentIndex) + 1 ||
        child->parentOrdinal != parent->ordinal || !parent->calendarOrdinal ||
        child->calendarOrdinal != parent->calendarOrdinal ||
        ((parent->flags | child->flags) & (Stage::ProjectedOut | Stage::OpaqueProjectedOut)))
        return result(Status::InvalidAssociation, FailureDomain::Admission);

    const auto local = Z::validateObservance(stage, timezoneComponentIndex, observanceComponentIndex,
                                            limits.context.timezone);
    if (local.status != Z::Status::LocalChecksPassed) {
        Report r = result(Status::ObservanceLocalFailure, FailureDomain::ObservanceLocal,
                          local.propertyIndex, local.propertyCode);
        r.observanceStatus = local.status; return r;
    }

    Report r = result(Status::LocalCoveragePassed, FailureDomain::None);
    r.observanceLocalDeferred = local.deferred;
    // The local precondition checked the entire direct chain and every RDATE.
    // Context validation repeats that precondition for each RRULE by design:
    // 1 + N local calls, no separate RDATE decoder/cache or exactly-once promise.
    for (uint32_t i = child->firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        if (p.code == Code::Rrule) {
            if (r.rruleProperties == SIZE_MAX || r.rruleProperties >= limits.maxRruleProperties)
                return result(Status::ResourceLimit, FailureDomain::Budget, i, p.code);
            const auto context = X::validate(stage, timezoneComponentIndex, observanceComponentIndex, i, limits.context);
            if (context.status != X::Status::ContextChecksPassed) {
                Report failure = result(Status::RruleContextFailure, FailureDomain::RruleContext,
                                        context.propertyIndex, context.propertyCode, context.offset);
                failure.contextStatus = context.status; failure.contextFailureDomain = context.failureDomain;
                if (context.failureDomain == X::FailureDomain::RuleGrammar) failure.ruleStatus = context.ruleStatus;
                if (context.failureDomain == X::FailureDomain::ObservanceLocal)
                    failure.observanceStatus = context.timezoneStatus;
                return failure;
            }
            ++r.rruleProperties; r.contextualRuleDeferred |= context.deferred;
        } else if (p.code == Code::Rdate || p.code == Code::Exdate) {
            size_t& count = p.code == Code::Rdate ? r.rdateProperties : r.exdateProperties;
            if (count == SIZE_MAX) return result(Status::ResourceLimit, FailureDomain::Budget, i, p.code);
            ++count; // Count properties only; EXDATE payload remains uninterpreted.
        }
    }
    return r;
}
}
#endif
