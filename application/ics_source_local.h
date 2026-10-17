#ifndef EE02_ICS_SOURCE_LOCAL_H
#define EE02_ICS_SOURCE_LOCAL_H

#include "ics_event_local.h"
#include "ics_timezone_local.h"

// Coverage of accepted local checks only. Finish all reads inside the stage's
// withScratch lifetime, without concurrent mutation/release. No source acceptance,
// identity matching, allocation, retained views or snapshot transaction occurs.
namespace IcsSourceLocal {
enum class Status {
    LocalCoveragePassed, Unavailable, InvalidArgument, InvalidAssociation,
    ResourceLimit, EventLocalFailure, TimezoneLocalFailure, ObservanceLocalFailure
};
enum class FailureKind { None, Event, Timezone, Observance };
enum Deferred : uint32_t {
    SourceSemanticsPending = 1U << 0,
    CalendarMetadataPending = 1U << 1,
    NestedComponentReviewPending = 1U << 2,
    NonEventProjectionObserved = 1U << 3
};
struct Limits {
    size_t maxComponents, maxCalendars, maxEvents, maxTimezones, maxObservances;
    size_t maxCalendarProperties;
    IcsEventLocal::Limits event;
    IcsTimezoneLocal::Limits timezone;
};
struct Report {
    Status status;
    FailureKind failureKind;
    size_t componentIndex, propertyIndex;
    IcsSourceStage::Code propertyCode;
    // Meaningful as failure diagnostics only for the corresponding FailureKind.
    // Inactive slots use LocalChecksPassed; they do not prove a call occurred.
    IcsEventLocal::Status eventStatus;
    IcsTimezoneLocal::Status timezoneStatus;
    uint32_t eventDeferred, timezoneDeferred, sourceDeferred;
    // Authoritative coverage counts only on success; all are zero on failure.
    size_t calendars, events, timezones, observances, projectedNonEventComponents;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
inline Report result(Status status, size_t component = Stage::None,
                     size_t property = Stage::None, Code code = Code::Unknown) {
    Report r = {};
    r.status = status; r.componentIndex = component; r.propertyIndex = property; r.propertyCode = code;
    r.eventStatus = IcsEventLocal::Status::LocalChecksPassed;
    r.timezoneStatus = IcsTimezoneLocal::Status::LocalChecksPassed;
    return r;
}
inline bool increment(size_t& count, size_t limit) {
    if (count == SIZE_MAX || count >= limit) return false;
    ++count; return true;
}
// Admission for dispatch in the accepted hierarchy, not another component parser
// or extension registry. Ordering/property grammar remain upstream responsibilities.
inline bool parentKind(Kind parent, Kind child) {
    switch (child) {
    case Kind::Event: case Kind::Timezone: case Kind::Todo: case Kind::Journal: case Kind::FreeBusy:
        return parent == Kind::Calendar;
    case Kind::Standard: case Kind::Daylight: return parent == Kind::Timezone;
    case Kind::Alarm: return parent == Kind::Event || parent == Kind::Todo;
    case Kind::Participant:
        return parent == Kind::Event || parent == Kind::Todo || parent == Kind::Journal || parent == Kind::FreeBusy;
    case Kind::Location: case Kind::Resource:
        return parent == Kind::Event || parent == Kind::Todo || parent == Kind::Journal ||
            parent == Kind::FreeBusy || parent == Kind::Participant;
    case Kind::Opaque: return parent == Kind::Calendar || parent == Kind::Opaque;
    default: return false;
    }
}
inline Report metadata(const Stage& stage, size_t owner, const Stage::Component& c, size_t limit) {
    size_t visited = 0; uint32_t last = Stage::None;
    // Complete chain integrity before observing pending metadata, even if METHOD
    // appears before a later corrupt link. No value/schema interpretation here.
    for (uint32_t i = c.firstProperty; i != Stage::None;) {
        const auto* p = stage.property(i);
        if (!p || p->owner != owner)
            return result(Status::InvalidAssociation, owner, i, p ? p->code : Code::Unknown);
        if (visited == limit) return result(Status::ResourceLimit, owner, i, p->code);
        if (visited >= c.observedProperties || (p->next != Stage::None &&
            (p->next <= i || p->next >= stage.counts().storedProperties)))
            return result(Status::InvalidAssociation, owner, i, p->code);
        ++visited; last = i; i = p->next;
    }
    if (visited != c.observedProperties || last != c.lastProperty)
        return result(Status::InvalidAssociation, owner, last,
            last == Stage::None ? Code::Unknown : stage.property(last)->code);
    Report r = result(Status::LocalCoveragePassed);
    for (uint32_t i = c.firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        if (p.code == Code::Method || p.code == Code::Calscale ||
            p.disposition == Stage::Disposition::RetainUnreviewed || (p.flags & Stage::UnknownParameters) ||
            ((p.flags & Stage::ValueRetained) && p.code != Code::Version && p.code != Code::Prodid))
            r.sourceDeferred |= CalendarMetadataPending;
    }
    return r;
}
}

inline Report inspect(const IcsSourceStage& stage, const Limits& limits) {
    using namespace detail;
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return result(Status::Unavailable);
    const auto counts = stage.counts();
    if (counts.components > SIZE_MAX || counts.components > limits.maxComponents || counts.calendars > SIZE_MAX)
        return result(Status::ResourceLimit);
    if (!counts.components) return result(Status::InvalidArgument);
    Report r = result(Status::LocalCoveragePassed);
    r.sourceDeferred = SourceSemanticsPending;
    size_t activeCalendar = Stage::None;
    for (size_t i = 0; i < static_cast<size_t>(counts.components); ++i) {
        const auto* c = stage.component(i);
        if (!c || c->ordinal != uint64_t(i) + 1 || c->closed != 1)
            return result(Status::InvalidAssociation, i);
        size_t parentIndex = Stage::None;
        bool projected = false;
        if (c->kind == Kind::Calendar) {
            if (c->parentOrdinal || !c->calendarOrdinal || c->calendarOrdinal - 1 != r.calendars)
                return result(Status::InvalidAssociation, i);
        } else {
            if (activeCalendar == Stage::None || !c->parentOrdinal || c->parentOrdinal > i)
                return result(Status::InvalidAssociation, i);
            parentIndex = static_cast<size_t>(c->parentOrdinal - 1); // Earlier index, checked before conversion.
            const auto* parent = stage.component(parentIndex);
            const auto* calendar = stage.component(activeCalendar);
            if (!parent || !calendar || parent->ordinal != c->parentOrdinal || parent->closed != 1 ||
                c->calendarOrdinal != parent->calendarOrdinal || c->calendarOrdinal != calendar->calendarOrdinal ||
                !parentKind(parent->kind, c->kind)) return result(Status::InvalidAssociation, i);
            projected = (parent->flags & Stage::ProjectedOut) || c->kind == Kind::Todo ||
                c->kind == Kind::Journal || c->kind == Kind::FreeBusy ||
                c->kind == Kind::Alarm || c->kind == Kind::Opaque;
        }
        if (bool(c->flags & Stage::ProjectedOut) != projected ||
            bool(c->flags & Stage::OpaqueProjectedOut) != (c->kind == Kind::Opaque))
            return result(Status::InvalidAssociation, i);

        switch (c->kind) {
        case Kind::Calendar: {
            if (!increment(r.calendars, limits.maxCalendars)) return result(Status::ResourceLimit, i);
            const Report root = metadata(stage, i, *c, limits.maxCalendarProperties);
            if (root.status != Status::LocalCoveragePassed) return root;
            activeCalendar = i; r.sourceDeferred |= root.sourceDeferred;
            break;
        }
        case Kind::Event: {
            if (!increment(r.events, limits.maxEvents)) return result(Status::ResourceLimit, i);
            const auto local = IcsEventLocal::validate(stage, parentIndex, i, limits.event);
            if (local.status != IcsEventLocal::Status::LocalChecksPassed) {
                Report failure = result(Status::EventLocalFailure, i, local.propertyIndex, local.propertyCode);
                failure.failureKind = FailureKind::Event; failure.eventStatus = local.status;
                return failure;
            }
            r.eventDeferred |= local.deferred; break;
        }
        case Kind::Timezone: case Kind::Standard: case Kind::Daylight: {
            const bool zone = c->kind == Kind::Timezone;
            if (!increment(zone ? r.timezones : r.observances, zone ? limits.maxTimezones : limits.maxObservances))
                return result(Status::ResourceLimit, i);
            const auto local = zone ? IcsTimezoneLocal::validateTimezone(stage, parentIndex, i, limits.timezone) :
                IcsTimezoneLocal::validateObservance(stage, parentIndex, i, limits.timezone);
            if (local.status != IcsTimezoneLocal::Status::LocalChecksPassed) {
                Report failure = result(zone ? Status::TimezoneLocalFailure : Status::ObservanceLocalFailure,
                    i, local.propertyIndex, local.propertyCode);
                failure.failureKind = zone ? FailureKind::Timezone : FailureKind::Observance;
                failure.timezoneStatus = local.status; return failure;
            }
            r.timezoneDeferred |= local.deferred; break;
        }
        case Kind::Participant: case Kind::Location: case Kind::Resource:
            if (!projected) r.sourceDeferred |= NestedComponentReviewPending;
            break;
        case Kind::Todo: case Kind::Journal: case Kind::FreeBusy: case Kind::Alarm: case Kind::Opaque:
            break; // Never traverse opaque property links or interpret projected payloads.
        default: return result(Status::InvalidAssociation, i);
        }
        if (projected) {
            if (!increment(r.projectedNonEventComponents, SIZE_MAX)) return result(Status::ResourceLimit, i);
            r.sourceDeferred |= NonEventProjectionObserved;
        }
    }
    if (!r.calendars || r.calendars != counts.calendars) return result(Status::InvalidAssociation);
    return r;
}
}
#endif
