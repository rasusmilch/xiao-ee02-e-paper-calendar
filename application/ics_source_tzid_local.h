#ifndef EE02_ICS_SOURCE_TZID_LOCAL_H
#define EE02_ICS_SOURCE_TZID_LOCAL_H

#include "ics_source_local.h"
#include "ics_tzid_reference_local.h"

// Identity coverage only: complete source-local preflight, then direct VEVENT
// temporal properties in component/property order. No temporal payload decoding,
// timezone resolution, recurrence execution or source acceptance. Finish within
// the readable stage's withScratch lifetime, without concurrent mutation/release.
namespace IcsSourceTzidLocal {
struct Limits {
    IcsSourceLocal::Limits source;
    IcsTzidReferenceLocal::Limits reference;
    size_t maxTemporalProperties;
};
enum class Status {
    LocalCoveragePassed, SourceLocalFailure, TzidReferenceFailure,
    ResourceLimit, InvalidAssociation
};
struct Report {
    Status status;
    size_t componentIndex, propertyIndex;
    // Original source evidence on success or SourceLocalFailure only. The
    // reference slot is authoritative only on TzidReferenceFailure.
    IcsSourceLocal::Report sourceReport;
    IcsTzidReferenceLocal::Report referenceReport;
    size_t temporalPropertiesChecked;
    size_t noReference, emptyReference, noLocalDefinition;
    size_t uniqueLocalDefinition, multipleLocalDefinitions;
    bool globalRegistrySemanticsPending, definitionParameterReviewPending, timezoneResolutionPending;
};

namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using Kind = IcsStructure::Kind;
namespace S = IcsSourceLocal;
namespace R = IcsTzidReferenceLocal;
inline Report result(Status status, size_t component = Stage::None, size_t property = Stage::None) {
    Report r = {};
    r.status = status; r.componentIndex = component; r.propertyIndex = property;
    r.sourceReport.status = S::Status::InvalidArgument;
    r.sourceReport.componentIndex = r.sourceReport.propertyIndex = Stage::None;
    r.referenceReport.status = R::Status::InvalidArgument;
    r.referenceReport.temporalPropertyIndex = r.referenceReport.componentIndex =
        r.referenceReport.propertyIndex = r.referenceReport.uniqueComponentIndex = Stage::None;
    return r; // Inactive diagnostic fields are placeholders, never validation evidence.
}
inline bool selected(Code code) {
    return code == Code::Dtstart || code == Code::Dtend || code == Code::RecurrenceId ||
        code == Code::Rdate || code == Code::Exdate;
}
}

inline Report inspect(const IcsSourceStage& stage, const Limits& limits) {
    using namespace detail;
    const auto source = S::inspect(stage, limits.source); // Exactly one explicit preflight.
    if (source.status != S::Status::LocalCoveragePassed) {
        Report failure = result(Status::SourceLocalFailure, source.componentIndex, source.propertyIndex);
        failure.sourceReport = source; return failure;
    }
    Report r = result(Status::LocalCoveragePassed);
    // SourceLocal established representable counts and complete associations.
    const size_t components = static_cast<size_t>(stage.counts().components);
    for (size_t i = 0; i < components; ++i) {
        const auto* event = stage.component(i);
        if (!event) return result(Status::InvalidAssociation, i);
        if (event->kind != Kind::Event) continue;
        if (!event->parentOrdinal || event->parentOrdinal > i)
            return result(Status::InvalidAssociation, i);
        const size_t parentIndex = static_cast<size_t>(event->parentOrdinal - 1);
        const auto* calendar = stage.component(parentIndex);
        if (!calendar || calendar->kind != Kind::Calendar || calendar->parentOrdinal ||
            calendar->ordinal != event->parentOrdinal || !calendar->calendarOrdinal ||
            calendar->calendarOrdinal != event->calendarOrdinal)
            return result(Status::InvalidAssociation, i);

        size_t visited = 0; uint32_t last = Stage::None;
        for (uint32_t p = event->firstProperty; p != Stage::None;) {
            const auto* property = stage.property(p);
            if (!property || property->owner != i || visited >= event->observedProperties ||
                (property->next != Stage::None && (property->next <= p || property->next >= stage.counts().storedProperties)))
                return result(Status::InvalidAssociation, i, p);
            ++visited; last = p; // Bounded by the preflight-validated uint32_t descriptor count.
            if (selected(property->code)) {
                if (r.temporalPropertiesChecked == SIZE_MAX || r.temporalPropertiesChecked == limits.maxTemporalProperties)
                    return result(Status::ResourceLimit, i, p);
                const auto child = R::inspect(stage, parentIndex, i, p, limits.reference);
                if (child.status != R::Status::Inspected) {
                    Report failure = result(Status::TzidReferenceFailure, i, p);
                    failure.referenceReport = child; return failure;
                }
                size_t* count = nullptr;
                switch (child.match) {
                case R::Match::NoReference: count = &r.noReference; break;
                case R::Match::EmptyReference: count = &r.emptyReference; break;
                case R::Match::NoLocalDefinition: count = &r.noLocalDefinition; break;
                case R::Match::UniqueLocalDefinition: count = &r.uniqueLocalDefinition; break;
                case R::Match::MultipleLocalDefinitions: count = &r.multipleLocalDefinitions; break;
                default: return result(Status::InvalidAssociation, i, p);
                }
                if (*count == SIZE_MAX) return result(Status::ResourceLimit, i, p);
                ++r.temporalPropertiesChecked; ++*count;
                r.globalRegistrySemanticsPending |= child.globalRegistrySemanticsPending;
                r.definitionParameterReviewPending |= child.definitionParameterReviewPending;
                r.timezoneResolutionPending |= child.timezoneResolutionPending;
            }
            p = property->next;
        }
        if (visited != event->observedProperties || last != event->lastProperty)
            return result(Status::InvalidAssociation, i, last);
    }
    // Checked sum independently establishes the five-way partition of coverage.
    size_t total = 0;
    const size_t counts[] = {r.noReference, r.emptyReference, r.noLocalDefinition,
                            r.uniqueLocalDefinition, r.multipleLocalDefinitions};
    for (size_t i = 0; i < sizeof(counts) / sizeof(counts[0]); ++i) {
        if (counts[i] > SIZE_MAX - total) return result(Status::ResourceLimit);
        total += counts[i];
    }
    if (total != r.temporalPropertiesChecked) return result(Status::InvalidAssociation);
    r.sourceReport = source;
    return r;
}
}
#endif
