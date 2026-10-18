#ifndef EE02_ICS_NONRECURRING_SNAPSHOT_H
#define EE02_ICS_NONRECURRING_SNAPSHOT_H

#include "ics_source_local.h"
#include "five_day_snapshot.h"

// One source transaction for the deliberately closed Calendar/Event profile.
// Caller supplies genuinely readable spans, and the complete live backing region
// of staging/parser/ingestion storage. No concurrent mutation/release/transactions.
// Scratch must also be disjoint from any other live caller objects. No ownership,
// aliases, decoded event catalogue or UID registry survives this call.
namespace IcsNonrecurringSnapshot {
enum class Status {
    SourceCommitted, InvalidArgument, InvalidState, Unavailable, LocalValidationFailure,
    UnsupportedProfile, DuplicateUid, InvalidTemporalValue, InvalidText, ResourceLimit, SnapshotFailure
};
enum class FailureCategory { None, Admission, LocalValidation, Profile, Identity, Temporal, Text, Budget, Snapshot };
struct Limits {
    IcsSourceLocal::Limits source;
    size_t maxDecodedTextBytes, maxUidComparisons;
};
struct Report {
    Status status;
    FailureCategory failureCategory;
    size_t componentIndex, propertyIndex;
    IcsSourceStage::Code propertyCode;
    // Only LocalValidation failure makes this original typed report authoritative.
    IcsSourceLocal::Report localReport;
    // Original typed result when a typed operation supplied the diagnostic.
    IcsTypedValues::Status typedStatus;
    size_t offset;
    FiveDaySnapshot::Result snapshotResult;
    // Complete transaction counts only. Both are zero on every failure.
    size_t publishedOccurrences, outsideWindowOccurrences;
};
namespace detail {
using Stage = IcsSourceStage;
using Code = Stage::Code;
using View = IcsStructure::View;
using Snapshot = FiveDaySnapshot;
namespace T = IcsTypedValues;
namespace S = IcsSourceLocal;
inline Report result(Status status, FailureCategory category, size_t component = Stage::None,
                     size_t property = Stage::None, Code code = Code::Unknown) {
    Report r = {}; r.status = status; r.failureCategory = category;
    r.componentIndex = component; r.propertyIndex = property; r.propertyCode = code;
    r.localReport.status = S::Status::InvalidArgument;
    r.localReport.componentIndex = r.localReport.propertyIndex = Stage::None;
    r.typedStatus = T::Status::InvalidArgument; r.snapshotResult = Snapshot::Result::NotInitialized;
    return r;
}
inline Report abort(Snapshot& snapshot, size_t source, Report r) {
    // addOccurrence may already have rolled back and set the precise reason.
    if (snapshot.source(source)->outcome == Snapshot::Outcome::Pending) {
        const auto reason = r.status == Status::InvalidText ? Snapshot::Reason::InvalidText :
            r.status == Status::InvalidTemporalValue ? Snapshot::Reason::InvalidOccurrence : Snapshot::Reason::ProducerFailure;
        const auto rollback = snapshot.failSource(source, reason);
        if (rollback != Snapshot::Result::Ok) {
            r.status = Status::SnapshotFailure; r.failureCategory = FailureCategory::Snapshot; r.snapshotResult = rollback;
        }
    }
    return r;
}
inline bool contains(View region, const void* data, size_t bytes) {
    const uintptr_t base = reinterpret_cast<uintptr_t>(region.data), p = reinterpret_cast<uintptr_t>(data);
    return p >= base && p - base <= region.size && bytes <= region.size - (p - base);
}
inline size_t find(const Stage& stage, size_t owner, Code code) {
    for (uint32_t p = stage.component(owner)->firstProperty; p != Stage::None; p = stage.property(p)->next)
        if (stage.property(p)->code == code) return p;
    return Stage::None;
}
inline Report text(const Stage& stage, size_t owner, size_t index, uint8_t* dst, size_t capacity, size_t& bytes) {
    View value = {}; bytes = 0;
    const auto code = stage.property(index)->code;
    if (stage.value(index, value) != Stage::Read::Ok)
        return result(Status::Unavailable, FailureCategory::Text, owner, index, code);
    const auto measured = T::measureText(value, bytes);
    if (measured.status != T::Status::Ok) {
        Report r = result(Status::InvalidText, FailureCategory::Text, owner, index, code);
        r.typedStatus = measured.status; r.offset = measured.offset; return r;
    }
    if (bytes > capacity) return result(Status::ResourceLimit, FailureCategory::Budget, owner, index, code);
    if ((code == Code::Uid || code == Code::Prodid) && !bytes)
        return result(Status::InvalidText, FailureCategory::Text, owner, index, code);
    const auto written = T::writeText(value, dst, capacity, false, bytes);
    if (written.status != T::Status::Ok) {
        Report r = result(written.status == T::Status::ResourceLimit ? Status::ResourceLimit : Status::InvalidText,
                          FailureCategory::Text, owner, index, code);
        r.typedStatus = written.status; r.offset = written.offset; return r;
    }
    return result(Status::SourceCommitted, FailureCategory::None); // Internal operation, not publication evidence.
}
inline Report temporal(const Stage& stage, size_t owner, size_t index, T::TemporalValue& value) {
    T::Selection selection = {};
    const auto decoded = T::decodeTemporal(stage, index, value, selection);
    if (decoded.status != T::Status::Ok) {
        Report r = result(Status::InvalidTemporalValue, FailureCategory::Temporal, owner, index, stage.property(index)->code);
        r.typedStatus = decoded.status; r.offset = decoded.offset; return r;
    }
    if (value.type != T::Type::Date && (value.type != T::Type::DateTime || value.dateTime.mode != T::TimeMode::Utc))
        return result(Status::UnsupportedProfile, FailureCategory::Profile, owner, index, stage.property(index)->code);
    return result(Status::SourceCommitted, FailureCategory::None);
}
// Typed dates have years 0000..9999. All intermediates (including BCE epoch
// differences) fit int64; explicit checks guard narrowing/multiplication/addition.
inline bool civil(const T::Date& date, int32_t& out) {
    int64_t y = date.year; const unsigned m = date.month;
    y -= m <= 2;
    const int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned year = static_cast<unsigned>(y - era * 400);
    const unsigned month = m > 2 ? m - 3 : m + 9;
    const int64_t day = era * 146097 + year * 365 + year / 4 - year / 100 + (153 * month + 2) / 5 + date.day - 1 - 719468;
    if (day < INT32_MIN || day > INT32_MAX) return false;
    out = static_cast<int32_t>(day); return true;
}
inline bool utc(const T::DateTime& time, int64_t& out) {
    int32_t day = 0; if (!civil(time.date, day)) return false;
    const int64_t d = day, seconds = time.hour * 3600 + time.minute * 60 + time.second;
    if (d < INT64_MIN / 86400 || d > INT64_MAX / 86400) return false;
    const int64_t midnight = d * 86400;
    if (midnight > INT64_MAX - seconds) return false;
    out = midnight + seconds; return true;
}
}

// liveStorage is the caller's complete source backing span, not inferred from
// individual property values (which omit unused capacity). It may cover multiple
// disjoint users in one contiguous workspace prefix. Public readers are also
// checked to be contained before the first scratch write. Parser/HTTP object
// lifetimes must end before their slices are reused. No accepted interface changes.
inline Report publish(const IcsSourceStage& stage, FiveDaySnapshot& snapshot, size_t sourceIndex,
                      const Limits& limits, uint8_t* scratch, size_t scratchBytes, IcsStructure::View liveStorage) {
    using namespace detail;
    const auto* source = snapshot.source(sourceIndex);
    if (!snapshot.initialized() || !source)
        return result(Status::InvalidArgument, FailureCategory::Admission);
    if (source->outcome != Snapshot::Outcome::Pending)
        return result(Status::InvalidState, FailureCategory::Admission);
    if (!ee02_ics_detail::span(scratch, scratchBytes) || !ee02_ics_detail::span(liveStorage.data, liveStorage.size) ||
        ee02_ics_detail::overlap(scratch, scratchBytes, liveStorage.data, liveStorage.size) ||
        ee02_ics_detail::overlap(scratch, scratchBytes, &stage, sizeof(stage)) ||
        ee02_ics_detail::overlap(scratch, scratchBytes, &limits, sizeof(limits)) ||
        ee02_ics_detail::overlap(scratch, scratchBytes, &snapshot, sizeof(snapshot)) ||
        ee02_ics_detail::overlap(scratch, scratchBytes, snapshot.window(), snapshot.allocationBytes()))
        return result(Status::InvalidArgument, FailureCategory::Admission);
    const auto begun = snapshot.beginSource(sourceIndex); // Detect conflicting active transaction without changing it.
    if (begun != Snapshot::Result::Ok) {
        Report r = result(Status::InvalidState, FailureCategory::Admission); r.snapshotResult = begun; return r;
    }
    const auto local = S::inspect(stage, limits.source); // Exactly one local precondition; never semantic acceptance.
    if (local.status != S::Status::LocalCoveragePassed) {
        Report r = result(local.status == S::Status::Unavailable ? Status::Unavailable : Status::LocalValidationFailure,
            FailureCategory::LocalValidation, local.componentIndex, local.propertyIndex, local.propertyCode);
        r.localReport = local; return abort(snapshot, sourceIndex, r);
    }
    if (limits.maxDecodedTextBytes > SIZE_MAX / 2 || scratchBytes < 2 * limits.maxDecodedTextBytes)
        return abort(snapshot, sourceIndex, result(Status::ResourceLimit, FailureCategory::Budget));
    if (local.calendars != 1 || !local.events)
        return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile));
    const size_t components = static_cast<size_t>(stage.counts().components);
    // Check complete storage admission before any decoding into caller scratch.
    for (size_t i = 0; i < components; ++i) {
        const auto* c = stage.component(i);
        if (!contains(liveStorage, c, sizeof(*c)) ||
            !contains(liveStorage, stage.componentName(i).data, stage.componentName(i).size))
            return abort(snapshot, sourceIndex, result(Status::InvalidArgument, FailureCategory::Admission, i));
        if (c->kind != (i == 0 ? IcsStructure::Kind::Calendar : IcsStructure::Kind::Event))
            return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i));
        for (uint32_t p = c->firstProperty; p != Stage::None; p = stage.property(p)->next) {
            const auto* property = stage.property(p); View value = {};
            const auto parameters = stage.parameters(p);
            if (!contains(liveStorage, property, sizeof(*property)) || !contains(liveStorage, parameters.data, parameters.size) ||
                ((property->flags & Stage::ValueRetained) &&
                 (stage.value(p, value) != Stage::Read::Ok || !contains(liveStorage, value.data, value.size))))
                return abort(snapshot, sourceIndex, result(Status::InvalidArgument, FailureCategory::Admission, i, p, property->code));
        }
    }
    uint8_t* const first = scratch;
    uint8_t* const second = limits.maxDecodedTextBytes ? scratch + limits.maxDecodedTextBytes : scratch;
    size_t comparisons = 0; bool version = false, prodid = false, scale = false;
    for (size_t i = 0; i < components; ++i) {
        const auto& c = *stage.component(i);
        for (uint32_t p = c.firstProperty; p != Stage::None; p = stage.property(p)->next) {
            const auto& property = *stage.property(p); const Code code = property.code;
            bool allowed = false;
            if (!i) allowed = code == Code::Version || code == Code::Prodid || code == Code::Calscale;
            else allowed = code == Code::Uid || code == Code::Dtstamp || code == Code::Dtstart || code == Code::Dtend || code == Code::Summary;
            if (!allowed || property.disposition == Stage::Disposition::RetainUnreviewed || (property.flags & Stage::UnknownParameters))
                return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
            const View encodedParameters = stage.parameters(p);
            if (property.parameterCount || encodedParameters.size) {
                if (!i || (code != Code::Dtstart && code != Code::Dtend) || property.parameterCount != 1)
                    return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                IcsStructure::ParameterIterator parameters(encodedParameters); IcsStructure::Parameter parameter, extra;
                if (!parameters.next(parameter) || !IcsStructure::identifier(parameter.name, "VALUE") || parameters.next(extra))
                    return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                // Existing temporal decoding below owns VALUE type/member checks.
            }
            if (!i) {
                bool& present = code == Code::Version ? version : code == Code::Prodid ? prodid : scale;
                if (present) return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                present = true;
                if (code != Code::Prodid) {
                    View value = {}; stage.value(p, value);
                    const bool valid = code == Code::Version ? IcsStructure::identifier(value, "2.0") || IcsStructure::identifier(value, "2.0;2.0") :
                        IcsStructure::identifier(value, "GREGORIAN");
                    if (!valid) return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                }
            }
            if (code == Code::Uid || code == Code::Summary || code == Code::Prodid) {
                size_t bytes = 0; const auto checked = text(stage, i, p, first, limits.maxDecodedTextBytes, bytes);
                if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
            } else if (i) {
                T::TemporalValue value = {}; const auto checked = temporal(stage, i, p, value);
                if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
            }
        }
        if (!i) continue;
        const size_t uid = find(stage, i, Code::Uid); size_t currentBytes = 0;
        const auto current = text(stage, i, uid, first, limits.maxDecodedTextBytes, currentBytes);
        if (current.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, current);
        for (size_t previous = 1; previous < i; ++previous) {
            if (comparisons == limits.maxUidComparisons || comparisons == SIZE_MAX)
                return abort(snapshot, sourceIndex, result(Status::ResourceLimit, FailureCategory::Budget, i, uid, Code::Uid));
            ++comparisons;
            size_t previousBytes = 0;
            const auto decoded = text(stage, previous, find(stage, previous, Code::Uid), second, limits.maxDecodedTextBytes, previousBytes);
            if (decoded.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, decoded);
            if (currentBytes == previousBytes && !std::memcmp(first, second, currentBytes))
                return abort(snapshot, sourceIndex, result(Status::DuplicateUid, FailureCategory::Identity, i, uid, Code::Uid));
        }
    }
    // Closed profile discharges only these named obligations; no child masks
    // are edited. New/unexpected deferred conditions must fail closed.
    const uint32_t eventExpected = IcsEventLocal::SourceValidationRequired | IcsEventLocal::IdentityReconciliationPending;
    const uint32_t sourceExpected = S::SourceSemanticsPending | (scale ? S::CalendarMetadataPending : 0U);
    if (!version || !prodid || local.eventDeferred != eventExpected || local.sourceDeferred != sourceExpected || local.timezoneDeferred)
        return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile));

    size_t inserted = 0, outside = 0;
    for (size_t i = 1; i < components; ++i) {
        const size_t start = find(stage, i, Code::Dtstart), end = find(stage, i, Code::Dtend), summary = find(stage, i, Code::Summary);
        T::TemporalValue a = {}, b = {};
        auto checked = temporal(stage, i, start, a);
        if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
        if (end != Stage::None) {
            checked = temporal(stage, i, end, b);
            if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
        }
        Snapshot::Input input = {}; bool converted = true;
        if (a.type == T::Type::Date) {
            input.kind = Snapshot::Kind::AllDay; auto& range = input.range.civil;
            converted = civil(a.date, range.start);
            if (end != Stage::None) converted = converted && b.type == T::Type::Date && civil(b.date, range.end);
            else { converted = converted && range.start < INT32_MAX; if (converted) range.end = range.start + 1; }
        } else {
            input.kind = Snapshot::Kind::Timed; auto& range = input.range.timed;
            converted = utc(a.dateTime, range.start);
            if (end != Stage::None) converted = converted && b.type == T::Type::DateTime && utc(b.dateTime, range.end);
            else range.end = range.start;
        }
        if (!converted) return abort(snapshot, sourceIndex, result(Status::ResourceLimit, FailureCategory::Budget, i, start, Code::Dtstart));
        size_t titleBytes = 0;
        if (summary != Stage::None) {
            checked = text(stage, i, summary, first, limits.maxDecodedTextBytes, titleBytes);
            if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
        }
        input.title = {reinterpret_cast<const char*>(first), titleBytes};
        const auto added = snapshot.addOccurrence(sourceIndex, input);
        if (added == Snapshot::Result::Ok || added == Snapshot::Result::OutsideWindow) {
            size_t& count = added == Snapshot::Result::Ok ? inserted : outside;
            if (count == SIZE_MAX)
                return abort(snapshot, sourceIndex, result(Status::ResourceLimit, FailureCategory::Budget, i));
            ++count;
        }
        else {
            Report r = result(added == Snapshot::Result::RecordCapacity || added == Snapshot::Result::TextCapacity ? Status::ResourceLimit : Status::SnapshotFailure,
                FailureCategory::Snapshot, i, summary, summary == Stage::None ? Code::Unknown : Code::Summary);
            r.snapshotResult = added; return abort(snapshot, sourceIndex, r);
        }
    }
    const auto committed = snapshot.completeSource(sourceIndex);
    if (committed != Snapshot::Result::Ok) {
        Report r = result(Status::SnapshotFailure, FailureCategory::Snapshot); r.snapshotResult = committed;
        return abort(snapshot, sourceIndex, r);
    }
    Report r = result(Status::SourceCommitted, FailureCategory::None);
    r.snapshotResult = committed; r.publishedOccurrences = inserted; r.outsideWindowOccurrences = outside;
    return r;
}
}
#endif
