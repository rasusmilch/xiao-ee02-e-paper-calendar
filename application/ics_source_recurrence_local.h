#ifndef EE02_ICS_SOURCE_RECURRENCE_LOCAL_H
#define EE02_ICS_SOURCE_RECURRENCE_LOCAL_H

#include "ics_source_local.h"
#include "ics_event_recurrence_local.h"
#include "ics_observance_recurrence_local.h"

// Two-phase local coverage only: whole-source preflight, then recurrence coverage
// in component order. No source acceptance, expansion, resolution or publication.
// Finish within the readable stage's withScratch lifetime; no concurrent mutation
// or release. Reports own no staged references and calls allocate no storage.
namespace IcsSourceRecurrenceLocal {
struct Limits {
    IcsSourceLocal::Limits source;
    IcsEventRecurrenceLocal::Limits event;
    IcsObservanceRecurrenceLocal::Limits observance;
};
enum class Status {
    LocalCoveragePassed, SourceLocalFailure, EventRecurrenceFailure,
    ObservanceRecurrenceFailure, InvalidAssociation
};
enum class FailureDomain { None, SourceLocal, EventRecurrence, ObservanceRecurrence, Association };
struct Report {
    Status status;
    FailureDomain failureDomain;
    size_t componentIndex;
    // Only the active failure-domain slot is authoritative; others are placeholders.
    IcsSourceLocal::Report sourceReport;
    IcsEventRecurrenceLocal::Report eventReport;
    IcsObservanceRecurrenceLocal::Report observanceReport;
    // Each mask retains its original enum domain. Never combine their meanings.
    uint32_t sourceDeferred, sourceEventDeferred, sourceTimezoneDeferred;
    uint32_t eventRecurrenceDeferred, eventContextualRuleDeferred;
    uint32_t observanceLocalDeferred, observanceContextualRuleDeferred;
    // Complete coverage counts only; zero on every failure.
    size_t calendars, events, timezones, observances, projectedNonEventComponents;
    size_t eventsRecurrenceChecked, observancesRecurrenceChecked;
};

namespace detail {
using Stage = IcsSourceStage;
using Kind = IcsStructure::Kind;
namespace S = IcsSourceLocal;
namespace E = IcsEventRecurrenceLocal;
namespace O = IcsObservanceRecurrenceLocal;
inline Report result(Status status, FailureDomain domain, size_t index = Stage::None) {
    Report r = {};
    r.status = status; r.failureDomain = domain; r.componentIndex = index;
    r.sourceReport.status = S::Status::InvalidArgument;
    r.sourceReport.componentIndex = r.sourceReport.propertyIndex = Stage::None;
    r.eventReport.status = E::Status::InvalidArgument; r.eventReport.propertyIndex = Stage::None;
    r.observanceReport.status = O::Status::InvalidArgument; r.observanceReport.propertyIndex = Stage::None;
    return r;
}
}

inline Report inspect(const IcsSourceStage& stage, const Limits& limits) {
    using namespace detail;
    // Exactly one explicit source preflight. Its failures outrank ALL recurrence
    // failures, including recurrence errors in earlier components.
    const auto source = S::inspect(stage, limits.source);
    if (source.status != S::Status::LocalCoveragePassed) {
        Report r = result(Status::SourceLocalFailure, FailureDomain::SourceLocal, source.componentIndex);
        r.sourceReport = source; return r;
    }

    Report r = result(Status::LocalCoveragePassed, FailureDomain::None);
    // SourceLocal established a size_t-representable component count and complete
    // associations. Use checked readers again before any dispatch dereference.
    const size_t components = static_cast<size_t>(stage.counts().components);
    for (size_t i = 0; i < components; ++i) {
        const auto* c = stage.component(i);
        if (!c) return result(Status::InvalidAssociation, FailureDomain::Association, i);
        const bool event = c->kind == Kind::Event;
        if (!event && c->kind != Kind::Standard && c->kind != Kind::Daylight) continue;
        if (!c->parentOrdinal || c->parentOrdinal > i)
            return result(Status::InvalidAssociation, FailureDomain::Association, i);
        const size_t parentIndex = static_cast<size_t>(c->parentOrdinal - 1); // Earlier index before conversion.
        const auto* parent = stage.component(parentIndex);
        if (!parent || parent->kind != (event ? Kind::Calendar : Kind::Timezone) ||
            parent->ordinal != c->parentOrdinal || !parent->calendarOrdinal ||
            parent->calendarOrdinal != c->calendarOrdinal)
            return result(Status::InvalidAssociation, FailureDomain::Association, i);

        if (event) {
            const auto child = E::inspect(stage, parentIndex, i, limits.event);
            if (child.status != E::Status::LocalCoveragePassed) {
                Report failure = result(Status::EventRecurrenceFailure, FailureDomain::EventRecurrence, i);
                failure.eventReport = child; return failure;
            }
            if (r.eventsRecurrenceChecked == SIZE_MAX)
                return result(Status::InvalidAssociation, FailureDomain::Association, i);
            ++r.eventsRecurrenceChecked;
            r.eventRecurrenceDeferred |= child.deferred;
            r.eventContextualRuleDeferred |= child.contextualRuleDeferred;
        } else {
            const auto child = O::inspect(stage, parentIndex, i, limits.observance);
            if (child.status != O::Status::LocalCoveragePassed) {
                Report failure = result(Status::ObservanceRecurrenceFailure, FailureDomain::ObservanceRecurrence, i);
                failure.observanceReport = child; return failure;
            }
            if (r.observancesRecurrenceChecked == SIZE_MAX)
                return result(Status::InvalidAssociation, FailureDomain::Association, i);
            ++r.observancesRecurrenceChecked;
            r.observanceLocalDeferred |= child.observanceLocalDeferred;
            r.observanceContextualRuleDeferred |= child.contextualRuleDeferred;
        }
    }
    if (r.eventsRecurrenceChecked != source.events || r.observancesRecurrenceChecked != source.observances)
        return result(Status::InvalidAssociation, FailureDomain::Association);
    r.sourceDeferred = source.sourceDeferred;
    r.sourceEventDeferred = source.eventDeferred;
    r.sourceTimezoneDeferred = source.timezoneDeferred;
    r.calendars = source.calendars; r.events = source.events; r.timezones = source.timezones;
    r.observances = source.observances; r.projectedNonEventComponents = source.projectedNonEventComponents;
    return r;
}
}
#endif
