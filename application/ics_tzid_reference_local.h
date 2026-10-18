#ifndef EE02_ICS_TZID_REFERENCE_LOCAL_H
#define EE02_ICS_TZID_REFERENCE_LOCAL_H

#include "ics_timezone_local.h"

// One selected parameter identity, NOT temporal payload validity or a resolved
// timezone. Finish within the stage's withScratch lifetime, without concurrent
// mutation/release. No allocation, saved views, global lookup or publication.
namespace IcsTzidReferenceLocal {
enum class Status {
    Inspected, Unavailable, InvalidArgument, InvalidAssociation,
    TemporalSelectionFailure, TimezoneLocalFailure, ResourceLimit
};
enum class Match { NoReference, EmptyReference, NoLocalDefinition, UniqueLocalDefinition, MultipleLocalDefinitions };
struct Limits {
    size_t maxComponentsScanned, maxEventProperties, maxDecodedIdentityBytes;
    IcsTimezoneLocal::Limits timezone;
};
struct Report {
    Status status;
    Match match; // Authoritative only on Inspected; never temporal-value validity.
    size_t temporalPropertyIndex, componentIndex, propertyIndex;
    IcsSourceStage::Code propertyCode;
    // Original diagnostics only for an active typed-selection/measurement or local failure.
    IcsTypedValues::Status typedStatus;
    IcsTimezoneLocal::Status timezoneStatus;
    size_t offset; // Relative to the measured/selected value where supplied, zero otherwise.
    size_t definitionsInspected;
    uint8_t matchingCount; // Saturated 0/1/2+, never a definition choice.
    size_t uniqueComponentIndex; // Temporary source-local evidence, not a durable reference.
    bool globalRegistrySemanticsPending, definitionParameterReviewPending, timezoneResolutionPending;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
using View = IcsStructure::View;
namespace T = IcsTypedValues;
namespace Z = IcsTimezoneLocal;
inline Report result(Status status, size_t selected, size_t component = Stage::None,
                     size_t property = Stage::None, Code code = Code::Unknown) {
    Report r = {};
    r.status = status; r.temporalPropertyIndex = selected; r.componentIndex = component;
    r.propertyIndex = property; r.propertyCode = code; r.uniqueComponentIndex = Stage::None;
    r.typedStatus = T::Status::InvalidArgument; r.timezoneStatus = Z::Status::InvalidArgument;
    return r;
}
inline Report typedFailure(size_t selected, size_t component, size_t property, Code code, T::Result error) {
    Report r = result(Status::TemporalSelectionFailure, selected, component, property, code);
    r.typedStatus = error.status; r.offset = error.offset; return r;
}
inline bool supported(Code code) {
    return code == Code::Dtstart || code == Code::Dtend || code == Code::RecurrenceId ||
           code == Code::Rdate || code == Code::Exdate;
}
// Purpose-specific byte comparison only, after COMPLETE public helper measurement.
// Parameter spans exclude quotes. The admitted immutable representations guarantee
// TEXT escapes have a following byte; unknown/trailing parameter carets stay literal.
struct Cursor {
    View value; size_t offset; bool parameter;
    uint8_t next() {
        uint8_t b = value.data[offset++];
        if (!parameter && b == '\\') {
            b = value.data[offset++]; if (b == 'n' || b == 'N') b = '\n';
        } else if (parameter && b == '^' && offset < value.size) {
            const uint8_t c = value.data[offset];
            if (c == 'n' || c == '^' || c == '\'') {
                ++offset; b = c == 'n' ? '\n' : c == '\'' ? '"' : '^';
            }
        }
        return b;
    }
};
inline bool equal(View parameter, View text, size_t decodedBytes) {
    Cursor a = {parameter, 0, true}, b = {text, 0, false};
    for (size_t i = 0; i < decodedBytes; ++i) if (a.next() != b.next()) return false;
    return true;
}
}

inline Report inspect(const IcsSourceStage& stage, size_t calendarComponentIndex,
                      size_t eventComponentIndex, size_t temporalPropertyIndex, const Limits& limits) {
    using namespace detail;
    const size_t selected = temporalPropertyIndex;
    if (stage.status() != Stage::Status::StagedSuccessfully || !stage.component(0))
        return result(Status::Unavailable, selected);
    const auto* calendar = stage.component(calendarComponentIndex);
    const auto* event = stage.component(eventComponentIndex);
    const auto* property = stage.property(selected);
    if (!calendar || !event || !property) return result(Status::InvalidArgument, selected);
    if (calendar->kind != Kind::Calendar || event->kind != Kind::Event || calendar->parentOrdinal ||
        calendarComponentIndex >= eventComponentIndex || calendar->closed != 1 || event->closed != 1 ||
        calendar->ordinal != uint64_t(calendarComponentIndex) + 1 || event->ordinal != uint64_t(eventComponentIndex) + 1 ||
        event->parentOrdinal != calendar->ordinal || !calendar->calendarOrdinal ||
        event->calendarOrdinal != calendar->calendarOrdinal ||
        ((calendar->flags | event->flags) & (Stage::ProjectedOut | Stage::OpaqueProjectedOut)) || property->owner != eventComponentIndex)
        return result(Status::InvalidAssociation, selected, eventComponentIndex, selected, property->code);
    if (!supported(property->code)) return result(Status::InvalidArgument, selected, eventComponentIndex, selected, property->code);

    size_t visited = 0; uint32_t last = Stage::None; bool found = false;
    for (uint32_t i = event->firstProperty; i != Stage::None;) {
        const auto* p = stage.property(i);
        if (!p || p->owner != eventComponentIndex)
            return result(Status::InvalidAssociation, selected, eventComponentIndex, i, p ? p->code : Code::Unknown);
        if (visited == limits.maxEventProperties || visited == SIZE_MAX)
            return result(Status::ResourceLimit, selected, eventComponentIndex, i, p->code);
        if (visited >= event->observedProperties || (p->next != Stage::None &&
            (p->next <= i || p->next >= stage.counts().storedProperties)))
            return result(Status::InvalidAssociation, selected, eventComponentIndex, i, p->code);
        ++visited; last = i; found = found || i == selected; i = p->next;
    }
    if (visited != event->observedProperties || last != event->lastProperty)
        return result(Status::InvalidAssociation, selected, eventComponentIndex, last,
                      last == Stage::None ? Code::Unknown : stage.property(last)->code);
    if (!found) return result(Status::InvalidAssociation, selected, eventComponentIndex, selected, property->code);

    T::Selection selection = {};
    const auto selectionResult = T::selectTemporal(stage, selected, selection);
    if (!T::parsed(selectionResult)) return typedFailure(selected, eventComponentIndex, selected, property->code, selectionResult);
    Report r = result(Status::Inspected, selected);
    if (!selection.tzidPresent) return r; // No temporal bytes inspected, no definition scan.
    size_t referenceBytes = 0;
    const auto measured = T::measureParameter(selection.tzid, referenceBytes);
    if (!T::parsed(measured)) return typedFailure(selected, eventComponentIndex, selected, property->code, measured);
    if (referenceBytes > limits.maxDecodedIdentityBytes)
        return result(Status::ResourceLimit, selected, eventComponentIndex, selected, property->code);
    r.timezoneResolutionPending = true;
    if (!referenceBytes) { r.match = Match::EmptyReference; return r; }
    Cursor prefix = {selection.tzid.encoded, 0, true};
    r.globalRegistrySemanticsPending = prefix.next() == '/';

    const auto counts = stage.counts();
    if (counts.components > SIZE_MAX) return result(Status::ResourceLimit, selected);
    for (size_t i = 0; i < static_cast<size_t>(counts.components); ++i) {
        if (i == limits.maxComponentsScanned) return result(Status::ResourceLimit, selected, i);
        const auto* c = stage.component(i);
        if (!c) return result(Status::InvalidAssociation, selected, i);
        if (c->kind != Kind::Timezone) continue;
        const bool parentMatches = c->parentOrdinal == calendar->ordinal;
        const bool calendarMatches = c->calendarOrdinal == calendar->calendarOrdinal;
        if (!parentMatches && !calendarMatches) continue; // Other calendar objects cannot satisfy this reference.
        if (!parentMatches || !calendarMatches || i <= calendarComponentIndex ||
            (c->flags & (Stage::ProjectedOut | Stage::OpaqueProjectedOut)))
            return result(Status::InvalidAssociation, selected, i);
        const auto local = Z::validateTimezone(stage, calendarComponentIndex, i, limits.timezone);
        if (local.status != Z::Status::LocalChecksPassed) {
            Report failure = result(Status::TimezoneLocalFailure, selected, i, local.propertyIndex, local.propertyCode);
            failure.timezoneStatus = local.status; return failure;
        }
        size_t identity = Stage::None;
        for (uint32_t p = c->firstProperty; p != Stage::None; p = stage.property(p)->next)
            if (stage.property(p)->code == Code::Tzid) { identity = p; break; }
        View value = {};
        if (identity == Stage::None || stage.value(identity, value) != Stage::Read::Ok)
            return result(Status::Unavailable, selected, i, identity, Code::Tzid);
        size_t definitionBytes = 0;
        const auto text = T::measureText(value, definitionBytes);
        if (!T::parsed(text)) return typedFailure(selected, i, identity, Code::Tzid, text);
        if (definitionBytes > limits.maxDecodedIdentityBytes || r.definitionsInspected == SIZE_MAX)
            return result(Status::ResourceLimit, selected, i, identity, Code::Tzid);
        ++r.definitionsInspected;
        if (referenceBytes == definitionBytes && equal(selection.tzid.encoded, value, referenceBytes)) {
            if (!r.matchingCount) { r.matchingCount = 1; r.uniqueComponentIndex = i; }
            else { r.matchingCount = 2; r.uniqueComponentIndex = Stage::None; }
            if (stage.property(identity)->parameterCount) r.definitionParameterReviewPending = true;
        }
    }
    r.match = !r.matchingCount ? Match::NoLocalDefinition :
        r.matchingCount == 1 ? Match::UniqueLocalDefinition : Match::MultipleLocalDefinitions;
    return r;
}
}
#endif
