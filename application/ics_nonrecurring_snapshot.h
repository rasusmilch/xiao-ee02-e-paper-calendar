#ifndef EE02_ICS_NONRECURRING_SNAPSHOT_H
#define EE02_ICS_NONRECURRING_SNAPSHOT_H

#include "ics_source_local.h"
#include "five_day_snapshot.h"

// One source transaction for the closed passive-feed/nonrecurring event profile.
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
inline bool alpha(uint8_t c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); }
inline bool digit(uint8_t c) { return c >= '0' && c <= '9'; }
inline bool hex(uint8_t c) { return digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
// Conservative LANGUAGE subset: 2..3 ASCII letters, optional 4-letter script,
// optional 2-letter/3-digit region. At most 12 bytes; no variants/extensions or
// private-use tags. This is not full RFC 5646 validation or locale selection.
inline bool language(View v) {
    size_t i = 0;
    while (i < v.size && alpha(v.data[i])) ++i;
    if (i < 2 || i > 3) return false;
    if (i == v.size) return true;
    if (v.data[i++] != '-') return false;
    size_t begin = i;
    while (i < v.size && alpha(v.data[i])) ++i;
    if (i - begin == 4) {
        if (i == v.size) return true;
        if (v.data[i++] != '-') return false;
        begin = i;
        while (i < v.size && alpha(v.data[i])) ++i;
    }
    if (i - begin == 2) return i == v.size;
    i = begin;
    while (i < v.size && digit(v.data[i])) ++i;
    return i == v.size && i - begin == 3;
}
inline bool pchar(uint8_t c) {
    return alpha(c) || digit(c) || std::strchr("-._~!$&'()*+,;=:@", c);
}
// Ignored URL subset: absolute ASCII URI, complete percent triplets and RFC 3986
// path/query/fragment characters. Optional // authority has a nonempty reg-name
// and optional numeric port; userinfo and IP literals are unsupported. No lookup,
// percent decoding, scheme-specific interpretation or network action.
inline bool uri(View v) {
    if (!v.size || !alpha(v.data[0])) return false;
    size_t i = 1;
    while (i < v.size && v.data[i] && (alpha(v.data[i]) || digit(v.data[i]) || std::strchr("+.-", v.data[i]))) ++i;
    if (i == v.size || v.data[i++] != ':' || i == v.size) return false;
    if (v.size - i >= 2 && v.data[i] == '/' && v.data[i + 1] == '/') {
        i += 2; const size_t host = i;
        while (i < v.size && v.data[i] != '/' && v.data[i] != '?' && v.data[i] != '#' && v.data[i] != ':') {
            const uint8_t c = v.data[i];
            if (!c || c >= 128 || c == '@' || !pchar(c)) {
                if (c != '%' || v.size - i < 3 || !hex(v.data[i + 1]) || !hex(v.data[i + 2])) return false;
                i += 3;
            } else ++i;
        }
        if (i == host) return false;
        if (i < v.size && v.data[i] == ':') {
            const size_t port = ++i;
            while (i < v.size && digit(v.data[i])) ++i;
            if (i == port) return false;
        }
        if (i < v.size && v.data[i] != '/' && v.data[i] != '?' && v.data[i] != '#') return false;
    }
    bool query = false, fragment = false;
    for (; i < v.size; ++i) {
        const uint8_t c = v.data[i];
        if (c == '?' && !fragment) { query = true; continue; }
        if (c == '#' && !fragment) { fragment = true; continue; }
        if (c == '%') {
            if (v.size - i < 3 || !hex(v.data[i + 1]) || !hex(v.data[i + 2])) return false;
            i += 2;
        } else if (!c || c >= 128 || (!pchar(c) && c != '/' && !(c == '?' && (query || fragment)))) return false;
    }
    return true;
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
        using Kind = IcsStructure::Kind;
        const auto* parent = c->parentOrdinal && c->parentOrdinal <= i ? stage.component(size_t(c->parentOrdinal - 1)) : nullptr;
        const bool ordinary = (i == 0 && c->kind == Kind::Calendar && !c->parentOrdinal) ||
            ((c->kind == Kind::Event || c->kind == Kind::Timezone) && parent == stage.component(0)) ||
            ((c->kind == Kind::Standard || c->kind == Kind::Daylight) && parent && parent->kind == Kind::Timezone);
        const bool alarm = c->kind == Kind::Alarm && parent && parent->kind == Kind::Event;
        if ((!ordinary && !alarm) || (c->flags & Stage::OpaqueProjectedOut) ||
            bool(c->flags & Stage::ProjectedOut) != alarm)
            return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i));
        size_t visited = 0; uint32_t last = Stage::None;
        for (uint32_t p = c->firstProperty; p != Stage::None;) {
            const auto* property = stage.property(p); View value = {};
            // Source-local owns root/event/timezone chains; also protect the
            // projected alarm chain before using any of its stored descriptors.
            if (!property || property->owner != i || visited >= c->observedProperties ||
                (property->next != Stage::None && (property->next <= p || property->next >= stage.counts().storedProperties)))
                return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Admission, i, p));
            ++visited; last = p;
            const auto parameters = stage.parameters(p), name = stage.propertyName(p);
            if (!contains(liveStorage, property, sizeof(*property)) || !contains(liveStorage, parameters.data, parameters.size) ||
                (property->code == Code::Unknown && !contains(liveStorage, name.data, name.size)) ||
                ((property->flags & Stage::ValueRetained) &&
                 (stage.value(p, value) != Stage::Read::Ok || !contains(liveStorage, value.data, value.size))))
                return abort(snapshot, sourceIndex, result(Status::InvalidArgument, FailureCategory::Admission, i, p, property->code));
            p = property->next;
        }
        if (visited != c->observedProperties || last != c->lastProperty)
            return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Admission, i));
    }
    uint8_t* const first = scratch;
    uint8_t* const second = limits.maxDecodedTextBytes ? scratch + limits.maxDecodedTextBytes : scratch;
    size_t comparisons = 0; uint8_t rootFields = 0;
    uint32_t eventExpected = IcsEventLocal::SourceValidationRequired | IcsEventLocal::IdentityReconciliationPending;
    uint32_t sourceExpected = S::SourceSemanticsPending;
    for (size_t i = 0; i < components; ++i) {
        const auto& c = *stage.component(i);
        if (c.kind == IcsStructure::Kind::Alarm) { sourceExpected |= S::NonEventProjectionObserved; continue; }
        // These completely locally checked definitions are never consulted by
        // admitted UTC/DATE events. All their property-local semantic obligations
        // are nonapplicable to membership/timing, including retained extensions.
        if (i && c.kind != IcsStructure::Kind::Event) continue;
        for (uint32_t p = c.firstProperty; p != Stage::None; p = stage.property(p)->next) {
            const auto& property = *stage.property(p); const Code code = property.code;
            uint8_t rootBit = 0;
            if (!i) {
                switch (code) {
                case Code::Version: rootBit = 1; break;
                case Code::Prodid: rootBit = 2; break;
                case Code::Calscale: rootBit = 4; break;
                case Code::Method: rootBit = 8; break;
                case Code::Unknown: {
                    const View name = stage.propertyName(p);
                    if (IcsStructure::identifier(name, "X-WR-CALNAME")) rootBit = 16;
                    else if (IcsStructure::identifier(name, "X-WR-CALDESC")) rootBit = 32;
                    else if (IcsStructure::identifier(name, "X-WR-TIMEZONE")) rootBit = 64;
                    break;
                }
                default: break;
                }
                if (!rootBit || (rootFields & rootBit))
                    return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                rootFields |= rootBit;
            }
            const bool textField = code == Code::Uid || code == Code::Prodid || code == Code::Summary ||
                code == Code::Description || code == Code::Location || (!i && code == Code::Unknown);
            const bool temporalField = code == Code::Dtstamp || code == Code::Dtstart || code == Code::Dtend ||
                code == Code::Created || code == Code::LastModified;
            const bool scalarMetadata = code == Code::Sequence || code == Code::Url || code == Code::Status || code == Code::Class || code == Code::Transp;
            const bool attachment = i && (code == Code::Attach || code == Code::Image);
            if ((i && !textField && !temporalField && !scalarMetadata && !attachment) ||
                (property.flags & Stage::UnknownParameters) ||
                (property.disposition == Stage::Disposition::RetainUnreviewed && !(rootBit >= 16)))
                return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
            if (attachment) {
                if (property.disposition != Stage::Disposition::IgnoreByDeclaredProjection)
                    return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                continue; // Stage's selected omission profile only; no payload read.
            }
            const View encodedParameters = stage.parameters(p);
            if (property.parameterCount || encodedParameters.size) {
                IcsStructure::ParameterIterator parameters(encodedParameters); IcsStructure::Parameter parameter, extra;
                if (!i || property.parameterCount != 1 || !parameters.next(parameter) || parameters.next(extra))
                    return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                if (code == Code::Dtstart || code == Code::Dtend) {
                    if (!IcsStructure::identifier(parameter.name, "VALUE"))
                        return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                    // Accepted temporal decoding owns VALUE type/member checks.
                } else {
                    auto members = parameter.values(); IcsStructure::EncodedValue member, extraMember;
                    if ((code != Code::Summary && code != Code::Description && code != Code::Location) ||
                        !IcsStructure::identifier(parameter.name, "LANGUAGE") || !members.next(member) || members.next(extraMember) || !language(member.encoded))
                        return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
                }
            }
            if (textField) {
                size_t bytes = 0; const auto checked = text(stage, i, p, first, limits.maxDecodedTextBytes, bytes);
                if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
            } else if (i && temporalField) {
                T::TemporalValue value = {}; const auto checked = temporal(stage, i, p, value);
                if (checked.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, checked);
            } else {
                View value = {};
                if (stage.value(p, value) != Stage::Read::Ok)
                    return abort(snapshot, sourceIndex, result(Status::Unavailable, FailureCategory::Profile, i, p, code));
                bool valid = false;
                switch (code) {
                case Code::Version: valid = IcsStructure::identifier(value, "2.0") || IcsStructure::identifier(value, "2.0;2.0"); break;
                case Code::Calscale: valid = IcsStructure::identifier(value, "GREGORIAN"); break;
                case Code::Method: valid = IcsStructure::identifier(value, "PUBLISH"); break;
                case Code::Transp: valid = IcsStructure::identifier(value, "OPAQUE") || IcsStructure::identifier(value, "TRANSPARENT"); break;
                case Code::Status: valid = IcsStructure::identifier(value, "CONFIRMED"); break;
                case Code::Class: valid = IcsStructure::identifier(value, "PUBLIC"); break;
                case Code::Url:
                    if (value.size > limits.maxDecodedTextBytes)
                        return abort(snapshot, sourceIndex, result(Status::ResourceLimit, FailureCategory::Budget, i, p, code));
                    valid = uri(value); break;
                case Code::Sequence: {
                    int32_t number = 0; const auto decoded = T::decodeInteger(value, number);
                    if (decoded.status != T::Status::Ok) {
                        Report r = result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code);
                        r.typedStatus = decoded.status; r.offset = decoded.offset; return abort(snapshot, sourceIndex, r);
                    }
                    valid = number >= 0; break;
                }
                default: break;
                }
                if (!valid) return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, p, code));
            }
            if (!i) {
                if (rootBit >= 4) sourceExpected |= S::CalendarMetadataPending;
                if (rootBit >= 16) eventExpected |= IcsEventLocal::ExtensionReviewPending;
                if (code == Code::Method) eventExpected |= IcsEventLocal::MethodSemanticsPending;
            } else if (code == Code::Transp || code == Code::Status || code == Code::Class || code == Code::Url)
                eventExpected |= IcsEventLocal::OptionalPropertyValidationPending;
        }
        if (!i) continue;
        // METHOD:PUBLISH must never relax this producer's required DTSTART.
        if (find(stage, i, Code::Dtstart) == Stage::None)
            return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile, i, Stage::None, Code::Dtstart));
        const size_t uid = find(stage, i, Code::Uid); size_t currentBytes = 0;
        const auto current = text(stage, i, uid, first, limits.maxDecodedTextBytes, currentBytes);
        if (current.status != Status::SourceCommitted) return abort(snapshot, sourceIndex, current);
        for (size_t previous = 1; previous < i; ++previous) {
            if (stage.component(previous)->kind != IcsStructure::Kind::Event) continue;
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
    // Exact event/source masks are justified by the enumerated causes above.
    // Known timezone-domain obligations are nonapplicable ONLY after every event
    // has proven UTC/DATE independence and every definition passed source-local.
    const uint32_t timezoneKnown = IcsTimezoneLocal::SourceValidationRequired | IcsTimezoneLocal::TimezoneIdentityReconciliationPending |
        IcsTimezoneLocal::TransitionResolutionPending | IcsTimezoneLocal::RecurrenceValidationPending | IcsTimezoneLocal::MultipleRrulePending |
        IcsTimezoneLocal::ExtensionReviewPending | IcsTimezoneLocal::OptionalPropertyValidationPending | IcsTimezoneLocal::LeapSecondPending |
        IcsTimezoneLocal::ObservanceExdateReviewPending;
    if ((rootFields & 3) != 3 || local.eventDeferred != eventExpected || local.sourceDeferred != sourceExpected ||
        (local.timezoneDeferred & ~timezoneKnown) || (!local.timezones && local.timezoneDeferred))
        return abort(snapshot, sourceIndex, result(Status::UnsupportedProfile, FailureCategory::Profile));

    size_t inserted = 0, outside = 0;
    for (size_t i = 1; i < components; ++i) {
        if (stage.component(i)->kind != IcsStructure::Kind::Event) continue;
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
