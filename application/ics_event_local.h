#ifndef EE02_ICS_EVENT_LOCAL_H
#define EE02_ICS_EVENT_LOCAL_H

#include "ics_typed_values.h"

// Selected VEVENT-local checks only. No allocation, mutation, normalized event,
// source acceptance or publication. Call inside the stage's withScratch lifetime;
// no concurrent mutation/release or escaped views. Report contains no pointers.
namespace IcsEventLocal {
enum class Status {
    LocalChecksPassed, InvalidArgument, Unavailable, InvalidAssociation,
    MissingRequired, DuplicateProperty, ConflictingProperties, InvalidRelationship,
    MalformedValue, InvalidParameter, InvalidValueType, UnsupportedFeature, ResourceLimit
};
enum Deferred : uint32_t {
    SourceValidationRequired = 1U << 0,
    MethodSemanticsPending = 1U << 1,
    RecurrenceValidationPending = 1U << 2,
    MultipleRrulePending = 1U << 3,
    TimezoneResolutionPending = 1U << 4,
    LocalTimeOrderingPending = 1U << 5,
    LeapSecondPending = 1U << 6,
    ExtensionReviewPending = 1U << 7,
    OptionalPropertyValidationPending = 1U << 8,
    IdentityReconciliationPending = 1U << 9
};
struct Limits { size_t maxCalendarProperties, maxEventProperties, maxTextBytes; };
struct Report {
    Status status;
    uint32_t deferred;
    size_t propertyIndex;
    IcsSourceStage::Code propertyCode;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
namespace T = IcsTypedValues;
inline Report failure(Status status, size_t index = Stage::None, Code code = Code::Unknown) {
    return {status, 0, index, code};
}
enum class Slot { Uid, Stamp, Start, Class, Created, Description, Geo, Modified,
    Location, Organizer, Priority, Sequence, Status, Summary, Transp, Url,
    RecurrenceId, End, Duration, Count };
inline Slot slot(Code c) {
    switch (c) {
    case Code::Uid: return Slot::Uid; case Code::Dtstamp: return Slot::Stamp;
    case Code::Dtstart: return Slot::Start; case Code::Class: return Slot::Class;
    case Code::Created: return Slot::Created; case Code::Description: return Slot::Description;
    case Code::Geo: return Slot::Geo; case Code::LastModified: return Slot::Modified;
    case Code::Location: return Slot::Location; case Code::Organizer: return Slot::Organizer;
    case Code::Priority: return Slot::Priority; case Code::Sequence: return Slot::Sequence;
    case Code::Status: return Slot::Status; case Code::Summary: return Slot::Summary;
    case Code::Transp: return Slot::Transp; case Code::Url: return Slot::Url;
    case Code::RecurrenceId: return Slot::RecurrenceId; case Code::Dtend: return Slot::End;
    case Code::Duration: return Slot::Duration; default: return Slot::Count;
    }
}
inline bool eventCore(Code c) {
    if (slot(c) != Slot::Count) return true;
    switch (c) {
    case Code::Attach: case Code::Attendee: case Code::Categories: case Code::Comment:
    case Code::Contact: case Code::Exdate: case Code::RequestStatus: case Code::RelatedTo:
    case Code::Resources: case Code::Rdate: case Code::Rrule: return true;
    default: return false;
    }
}
inline bool reviewed(const Stage::Property& p) {
    return p.disposition == Stage::Disposition::RetainUnreviewed || (p.flags & Stage::UnknownParameters);
}
// Validate both complete chains before any duplicate/missing/value failure. The
// accepted append-only stage links properties in increasing stored-index order;
// a nonincreasing edge violates that contract and catches every possible cycle.
inline Report links(const Stage& stage, size_t owner, const Stage::Component& c, size_t limit) {
    size_t visited = 0; uint32_t last = Stage::None;
    for (uint32_t i = c.firstProperty; i != Stage::None;) {
        const auto* p = stage.property(i);
        if (!p || p->owner != owner) return failure(Status::InvalidAssociation, i, p ? p->code : Code::Unknown);
        if (visited == limit) return failure(Status::ResourceLimit, i, p->code);
        if (visited >= c.observedProperties || (p->next != Stage::None &&
            (p->next <= i || p->next >= stage.counts().storedProperties)))
            return failure(Status::InvalidAssociation, i, p->code);
        ++visited; last = i; i = p->next;
    }
    if (visited != c.observedProperties || last != c.lastProperty)
        return failure(Status::InvalidAssociation, last, last == Stage::None ? Code::Unknown : stage.property(last)->code);
    return {Status::LocalChecksPassed, 0, Stage::None, Code::Unknown};
}
inline Status typedStatus(T::Status s) {
    switch (s) {
    case T::Status::InvalidArgument: return Status::InvalidArgument;
    case T::Status::Unavailable: return Status::Unavailable;
    case T::Status::InvalidParameter: case T::Status::InvalidCardinality: return Status::InvalidParameter;
    case T::Status::InvalidValueType: return Status::InvalidValueType;
    case T::Status::UnsupportedFeature: return Status::UnsupportedFeature;
    case T::Status::ResourceLimit: return Status::ResourceLimit;
    default: return Status::MalformedValue;
    }
}
inline int compareDate(const T::Date& a, const T::Date& b) {
    if (a.year != b.year) return a.year < b.year ? -1 : 1;
    if (a.month != b.month) return a.month < b.month ? -1 : 1;
    return a.day == b.day ? 0 : a.day < b.day ? -1 : 1;
}
inline int compareUtc(const T::DateTime& a, const T::DateTime& b) {
    const int date = compareDate(a.date,b.date); if (date) return date;
    if (a.hour != b.hour) return a.hour < b.hour ? -1 : 1;
    if (a.minute != b.minute) return a.minute < b.minute ? -1 : 1;
    return a.second == b.second ? 0 : a.second < b.second ? -1 : 1;
}
}

inline Report validate(const IcsSourceStage& stage, size_t calendarComponentIndex,
                       size_t eventComponentIndex, const Limits& limits) {
    using namespace detail;
    // A: admission/association. A preserved terminal status does not prove binding.
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return failure(Status::Unavailable);
    const auto* calendar = stage.component(calendarComponentIndex);
    const auto* event = stage.component(eventComponentIndex);
    if (!calendar || !event) return failure(Status::InvalidArgument);
    if (calendar->kind != IcsStructure::Kind::Calendar || event->kind != IcsStructure::Kind::Event ||
        calendar->closed != 1 || event->closed != 1 ||
        calendar->ordinal != uint64_t(calendarComponentIndex) + 1 ||
        event->ordinal != uint64_t(eventComponentIndex) + 1 ||
        event->parentOrdinal != calendar->ordinal || !calendar->calendarOrdinal ||
        event->calendarOrdinal != calendar->calendarOrdinal)
        return failure(Status::InvalidAssociation);
    Report check = links(stage,calendarComponentIndex,*calendar,limits.maxCalendarProperties);
    if (check.status != Status::LocalChecksPassed) return check;
    check = links(stage,eventComponentIndex,*event,limits.maxEventProperties);
    if (check.status != Status::LocalChecksPassed) return check;

    // B: cardinality, direct owning-calendar context and pending obligations.
    uint32_t deferred = SourceValidationRequired | IdentityReconciliationPending;
    bool method = false, rrule = false;
    for (uint32_t i = calendar->firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        method = method || p.code == Code::Method;
        if (reviewed(p) || (p.disposition != Stage::Disposition::IgnoreByDeclaredProjection &&
            p.code != Code::Version && p.code != Code::Prodid &&
            p.code != Code::Method && p.code != Code::Calscale)) deferred |= ExtensionReviewPending;
    }
    if (method) deferred |= MethodSemanticsPending;
    size_t first[static_cast<size_t>(Slot::Count)];
    for (size_t& i : first) i = Stage::None;
    for (uint32_t i = event->firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i); const Slot field = slot(p.code);
        if (field != Slot::Count) {
            size_t& previous = first[static_cast<size_t>(field)];
            if (previous != Stage::None) return failure(Status::DuplicateProperty,i,p.code);
            previous = i;
        }
        if (reviewed(p) || (p.disposition != Stage::Disposition::IgnoreByDeclaredProjection && !eventCore(p.code)))
            deferred |= ExtensionReviewPending;
        if (p.code == Code::Rrule || p.code == Code::Rdate || p.code == Code::Exdate || p.code == Code::RecurrenceId)
            deferred |= RecurrenceValidationPending;
        if (p.code == Code::Rrule) {
            if (rrule) deferred |= MultipleRrulePending;
            rrule = true;
        }
    }
    const size_t uid = first[static_cast<size_t>(Slot::Uid)], stamp = first[static_cast<size_t>(Slot::Stamp)],
        start = first[static_cast<size_t>(Slot::Start)], end = first[static_cast<size_t>(Slot::End)],
        duration = first[static_cast<size_t>(Slot::Duration)];
    // C: fixed missing-field priority. METHOD is presence only, even if empty.
    if (uid == Stage::None) return failure(Status::MissingRequired,Stage::None,Code::Uid);
    if (stamp == Stage::None) return failure(Status::MissingRequired,Stage::None,Code::Dtstamp);
    if (start == Stage::None && !method) return failure(Status::MissingRequired,Stage::None,Code::Dtstart);
    // D: diagnose the second conflicting property in preserved input order.
    if (end != Stage::None && duration != Stage::None) {
        const size_t second = end > duration ? end : duration;
        return failure(Status::ConflictingProperties,second,stage.property(second)->code);
    }

    // E: selected typed checks in input order. Only relationship operands survive
    // this pass; no decoded event record, UID/text buffer or secondary table.
    T::DateTime startFields = {}, endFields = {};
    T::Duration durationFields = {};
    bool startDate = false, endDate = false, durationTimeSyntax = false;
    for (uint32_t i = event->firstProperty; i != Stage::None; i = stage.property(i)->next) {
        const auto& p = *stage.property(i);
        const bool text = p.code == Code::Uid || p.code == Code::Summary ||
            p.code == Code::Description || p.code == Code::Location;
        const bool temporal = p.code == Code::Dtstamp || p.code == Code::Dtstart ||
            p.code == Code::Dtend || p.code == Code::Duration || p.code == Code::RecurrenceId ||
            p.code == Code::Created || p.code == Code::LastModified;
        if (text || p.code == Code::Sequence) {
            T::View value = {}; if (stage.value(i,value) != Stage::Read::Ok) return failure(Status::Unavailable,i,p.code);
            if (text && value.size > limits.maxTextBytes) return failure(Status::ResourceLimit,i,p.code);
            size_t decoded = 0; int32_t integer = 0;
            const T::Result r = text ? T::measureText(value,decoded) : T::decodeInteger(value,integer);
            if (!T::parsed(r)) return failure(typedStatus(r.status),i,p.code);
            if (p.code == Code::Uid && !decoded) return failure(Status::MalformedValue,i,p.code);
        } else if (temporal) {
            T::TemporalValue value = {}; T::Selection selection = {};
            const T::Result r = T::decodeTemporal(stage,i,value,selection);
            if (!T::parsed(r)) return failure(typedStatus(r.status),i,p.code);
            if (selection.review) deferred |= ExtensionReviewPending;
            if (value.type == T::Type::DateTime) {
                if (value.dateTime.second == 60) deferred |= LeapSecondPending;
                if (value.dateTime.mode != T::TimeMode::Utc) deferred |= TimezoneResolutionPending;
            }
            if (p.code == Code::Dtstart || p.code == Code::Dtend) {
                T::DateTime& fields = p.code == Code::Dtstart ? startFields : endFields;
                bool& date = p.code == Code::Dtstart ? startDate : endDate;
                date = value.type == T::Type::Date;
                if (date) fields.date = value.date; else fields = value.dateTime;
            } else if (p.code == Code::Duration) {
                durationFields = value.duration;
                T::View raw = {}; stage.value(i,raw);
                for (size_t n = 0; n < raw.size; ++n)
                    if (raw.data[n] == 'T' || raw.data[n] == 't') durationTimeSyntax = true;
            }
        } else if (p.flags & Stage::ValueRetained) {
            deferred |= OptionalPropertyValidationPending;
        }
    }
    // F: locally decidable relationships; never naive ordering of local times.
    if (end != Stage::None) {
        if (start == Stage::None) deferred |= LocalTimeOrderingPending;
        else if (startDate != endDate) return failure(Status::InvalidRelationship,end,Code::Dtend);
        else if (startDate) {
            if (compareDate(startFields.date,endFields.date) >= 0)
                return failure(Status::InvalidRelationship,end,Code::Dtend);
        } else {
            const bool startUtc = startFields.mode == T::TimeMode::Utc, endUtc = endFields.mode == T::TimeMode::Utc;
            if (startUtc != endUtc) return failure(Status::InvalidRelationship,end,Code::Dtend);
            if (!startUtc || startFields.second == 60 || endFields.second == 60) deferred |= LocalTimeOrderingPending;
            else if (compareUtc(startFields,endFields) >= 0) return failure(Status::InvalidRelationship,end,Code::Dtend);
        }
    }
    if (duration != Stage::None) {
        if (durationFields.negative || !(durationFields.weeks || durationFields.days ||
            durationFields.hours || durationFields.minutes || durationFields.seconds) ||
            (start != Stage::None && startDate && durationTimeSyntax))
            return failure(Status::InvalidRelationship,duration,Code::Duration);
    }
    // G: no source/calendar/snapshot semantic acceptance is exposed.
    return {Status::LocalChecksPassed,deferred,Stage::None,Code::Unknown};
}
}
#endif
