#ifndef EE02_ICS_TYPED_VALUES_H
#define EE02_ICS_TYPED_VALUES_H

#include "ics_source_stage.h"
#include <climits>

// Value representations only: no component/source validity, recurrence, timezone
// resolution, epoch arithmetic or publication. No allocation or retained outputs.
// All input spans must actually be readable. Borrowed stage/parameter views expire
// at release/destruction/scratch reuse; copied numeric fields do not. Single-threaded
// use only. Writers require storage disjoint from ALL live stage/parser storage;
// span/overlap checks cannot prove ownership or revoke escaped external aliases.
// POD output references must likewise not alias live input/stage storage. Error
// offsets are value-relative; PropertyView supplies non-sensitive owning context.
namespace IcsTypedValues {
using View = IcsStructure::View;
using Code = IcsSourceStage::Code;
using Kind = IcsStructure::Kind;
enum class Status { Ok, NeedsContext, InProgress, End, InvalidArgument, Unavailable,
    MalformedValue, InvalidParameter, InvalidValueType, InvalidCardinality,
    UnsupportedFeature, ResourceLimit };
struct Result { Status status; size_t offset; };
inline Result result(Status s, size_t offset = 0) { return {s, offset}; }
inline bool parsed(Result r) { return r.status == Status::Ok || r.status == Status::NeedsContext; }
struct Date { uint16_t year; uint8_t month, day; };
enum class TimeMode : uint8_t { Floating, Utc, Tzid };
struct DateTime { Date date; uint8_t hour, minute, second; TimeMode mode; };
struct Duration { uint64_t weeks, days, hours, minutes, seconds; bool negative, weekForm; };
struct Period { DateTime start, end; Duration duration; bool durationForm; };
enum class Type : uint8_t { Date, DateTime, Duration, Period };
struct TemporalValue {
    Type type;
    union { Date date; DateTime dateTime; Duration duration; Period period; };
};

namespace detail {
inline bool span(View v) { return ee02_ics_detail::span(v.data, v.size); }
inline uint8_t upper(uint8_t b) { return b >= 'a' && b <= 'z' ? uint8_t(b - 'a' + 'A') : b; }
inline bool equal(View v, const char* token) {
    const size_t n = std::strlen(token);
    if (v.size != n) return false;
    for (size_t i = 0; i < n; ++i) if (upper(v.data[i]) != uint8_t(token[i])) return false;
    return true;
}
inline bool digit(uint8_t b) { return b >= '0' && b <= '9'; }
inline unsigned pair(const uint8_t* p) { return unsigned(p[0] - '0') * 10 + p[1] - '0'; }
inline bool leap(unsigned y) { return y % 4 == 0 && (y % 100 != 0 || y % 400 == 0); }
// Complete UTF-8 scalar validation, including overlong/surrogate/out-of-range and
// iCalendar prohibited ASCII controls. HTAB is WSP, not a newline.
inline Result utf8(View v) {
    if (!span(v)) return result(Status::InvalidArgument);
    for (size_t i = 0; i < v.size;) {
        const size_t start = i;
        const uint8_t b = v.data[i++];
        if (b < 0x80) {
            if ((b < 0x20 && b != '\t') || b == 0x7f) return result(Status::MalformedValue, start);
            continue;
        }
        unsigned n = 0; uint32_t cp = 0, minimum = 0;
        if (b >= 0xc2 && b <= 0xdf) { n = 1; cp = b & 31; minimum = 0x80; }
        else if (b >= 0xe0 && b <= 0xef) { n = 2; cp = b & 15; minimum = 0x800; }
        else if (b >= 0xf0 && b <= 0xf4) { n = 3; cp = b & 7; minimum = 0x10000; }
        else return result(Status::MalformedValue, start);
        if (n > v.size - i) return result(Status::MalformedValue, start);
        while (n--) {
            const uint8_t c = v.data[i++];
            if ((c & 0xc0) != 0x80) return result(Status::MalformedValue, i - 1);
            cp = (cp << 6) | (c & 63);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
            return result(Status::MalformedValue, start);
    }
    return result(Status::Ok);
}
inline int compare(const DateTime& a, const DateTime& b) {
    const unsigned x[] = {a.date.year,a.date.month,a.date.day,a.hour,a.minute,a.second};
    const unsigned y[] = {b.date.year,b.date.month,b.date.day,b.hour,b.minute,b.second};
    for (size_t i = 0; i < 6; ++i) if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}
inline Result magnitude(View v, size_t& i, uint64_t& out) {
    if (i == v.size || !digit(v.data[i])) return result(Status::MalformedValue, i);
    uint64_t n = 0;
    while (i < v.size && digit(v.data[i])) {
        const unsigned d = v.data[i] - '0';
        if (n > (UINT64_MAX - d) / 10) return result(Status::ResourceLimit, i);
        n = n * 10 + d; ++i;
    }
    out = n; return result(Status::Ok);
}
}

inline Result decodeDate(View v, Date& out) {
    if (!detail::span(v)) return result(Status::InvalidArgument);
    if (v.size != 8) return result(Status::MalformedValue);
    for (size_t i = 0; i < v.size; ++i)
        if (!detail::digit(v.data[i])) return result(Status::MalformedValue, i);
    Date d = {uint16_t(detail::pair(v.data) * 100 + detail::pair(v.data + 2)),
              uint8_t(detail::pair(v.data + 4)), uint8_t(detail::pair(v.data + 6))};
    const uint8_t days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (!d.month || d.month > 12) return result(Status::MalformedValue, 4);
    const unsigned maximum = days[d.month - 1] + (d.month == 2 && detail::leap(d.year));
    if (!d.day || d.day > maximum) return result(Status::MalformedValue, 6);
    out = d; return result(Status::Ok);
}
inline Result decodeDateTime(View v, DateTime& out) {
    if (!detail::span(v)) return result(Status::InvalidArgument);
    if ((v.size != 15 && v.size != 16) || detail::upper(v.data[8]) != 'T' ||
        (v.size == 16 && detail::upper(v.data[15]) != 'Z')) return result(Status::MalformedValue);
    DateTime d = {};
    Result r = decodeDate({v.data, 8}, d.date); if (!parsed(r)) return r;
    for (size_t i = 9; i < 15; ++i)
        if (!detail::digit(v.data[i])) return result(Status::MalformedValue, i);
    d.hour = uint8_t(detail::pair(v.data + 9)); d.minute = uint8_t(detail::pair(v.data + 11));
    d.second = uint8_t(detail::pair(v.data + 13)); d.mode = v.size == 16 ? TimeMode::Utc : TimeMode::Floating;
    if (d.hour > 23 || d.minute > 59 || d.second > 60) return result(Status::MalformedValue, 9);
    out = d; return result(d.second == 60 ? Status::NeedsContext : Status::Ok);
}
inline Result decodeDuration(View v, Duration& out) {
    if (!detail::span(v)) return result(Status::InvalidArgument);
    Duration d = {}; size_t i = 0;
    if (i < v.size && (v.data[i] == '+' || v.data[i] == '-')) d.negative = v.data[i++] == '-';
    if (i == v.size || detail::upper(v.data[i++]) != 'P' || i == v.size)
        return result(Status::MalformedValue, i);
    if (detail::upper(v.data[i]) != 'T') {
        uint64_t n = 0; Result r = detail::magnitude(v, i, n); if (!parsed(r)) return r;
        if (i == v.size) return result(Status::MalformedValue, i);
        const uint8_t unit = detail::upper(v.data[i++]);
        if (unit == 'W') {
            if (i != v.size) return result(Status::MalformedValue, i);
            d.weeks = n; d.weekForm = true; out = d; return result(Status::Ok);
        }
        if (unit != 'D') return result(Status::MalformedValue, i - 1);
        d.days = n;
        if (i == v.size) { out = d; return result(Status::Ok); }
    }
    if (detail::upper(v.data[i++]) != 'T' || i == v.size) return result(Status::MalformedValue, i);
    unsigned previous = 0;
    while (i < v.size) {
        uint64_t n = 0; Result r = detail::magnitude(v, i, n); if (!parsed(r)) return r;
        if (i == v.size) return result(Status::MalformedValue, i);
        const uint8_t unit = detail::upper(v.data[i++]);
        const unsigned order = unit == 'H' ? 1 : unit == 'M' ? 2 : unit == 'S' ? 3 : 0;
        // dur-hour permits [dur-minute], and dur-minute permits [dur-second].
        if (!order || order <= previous || (previous && order != previous + 1))
            return result(Status::MalformedValue, i - 1);
        if (order == 1) d.hours = n;
        else if (order == 2) d.minutes = n;
        else d.seconds = n;
        previous = order;
    }
    out = d; return result(Status::Ok);
}
inline Result decodePeriod(View v, Period& out) {
    if (!detail::span(v)) return result(Status::InvalidArgument);
    size_t slash = 0; while (slash < v.size && v.data[slash] != '/') ++slash;
    if (slash == v.size || slash + 1 == v.size) return result(Status::MalformedValue, slash);
    Period p = {}; Result a = decodeDateTime({v.data, slash}, p.start);
    if (!parsed(a)) return a;
    View second = {v.data + slash + 1, v.size - slash - 1};
    const uint8_t b = detail::upper(second.data[0]);
    if (b == 'P' || b == '+' || b == '-') {
        Result r = decodeDuration(second, p.duration);
        if (!parsed(r)) { r.offset += slash + 1; return r; }
        if (p.duration.negative || !(p.duration.weeks || p.duration.days || p.duration.hours ||
            p.duration.minutes || p.duration.seconds)) return result(Status::MalformedValue, slash + 1);
        p.durationForm = true; out = p; return a;
    }
    Result r = decodeDateTime(second, p.end);
    if (!parsed(r)) { r.offset += slash + 1; return r; }
    if (a.status == Status::NeedsContext || r.status == Status::NeedsContext ||
        p.start.mode != TimeMode::Utc || p.end.mode != TimeMode::Utc) {
        out = p; return result(Status::NeedsContext);
    }
    if (detail::compare(p.start, p.end) >= 0) return result(Status::MalformedValue, slash + 1);
    out = p; return result(Status::Ok);
}
inline Result decodeInteger(View v, int32_t& out) {
    if (!detail::span(v)) return result(Status::InvalidArgument);
    size_t i = 0; bool negative = false;
    if (i < v.size && (v.data[i] == '+' || v.data[i] == '-')) negative = v.data[i++] == '-';
    if (i == v.size) return result(Status::MalformedValue, i);
    const uint32_t maximum = negative ? uint32_t(INT32_MAX) + 1U : uint32_t(INT32_MAX);
    uint32_t n = 0;
    for (; i < v.size; ++i) {
        if (!detail::digit(v.data[i])) return result(Status::MalformedValue, i);
        const unsigned d = v.data[i] - '0';
        if (n > (maximum - d) / 10) return result(Status::MalformedValue, i);
        n = n * 10 + d;
    }
    out = negative ? (n == uint32_t(INT32_MAX) + 1U ? INT32_MIN : -int32_t(n)) : int32_t(n);
    return result(Status::Ok);
}
inline Result decodeUtcOffset(View v, int32_t& out) {
    if (!detail::span(v)) return result(Status::InvalidArgument);
    if ((v.size != 5 && v.size != 7) || (v.data[0] != '+' && v.data[0] != '-'))
        return result(Status::MalformedValue);
    for (size_t i = 1; i < v.size; ++i)
        if (!detail::digit(v.data[i])) return result(Status::MalformedValue, i);
    const unsigned h = detail::pair(v.data + 1), m = detail::pair(v.data + 3);
    const unsigned s = v.size == 7 ? detail::pair(v.data + 5) : 0;
    const int32_t n = int32_t(h * 3600 + m * 60 + s);
    if (h > 23 || m > 59 || s > 59 || (v.data[0] == '-' && !n)) return result(Status::MalformedValue);
    out = v.data[0] == '-' ? -n : n; return result(Status::Ok);
}

namespace detail {
inline Result measured(View v, bool text, size_t& bytes) {
    bytes = 0; Result r = utf8(v); if (!parsed(r)) return r;
    size_t n = 0;
    for (size_t i = 0; i < v.size; ++i, ++n) {
        const uint8_t b = v.data[i];
        if (text && (b == ',' || b == ';')) return result(Status::MalformedValue, i);
        if (text && b == '\\') {
            if (++i == v.size) return result(Status::MalformedValue, i - 1);
            const uint8_t c = v.data[i];
            if (c != '\\' && c != ',' && c != ';' && c != 'n' && c != 'N')
                return result(Status::MalformedValue, i);
        } else if (!text && b == '^' && i + 1 < v.size &&
                   (v.data[i + 1] == 'n' || v.data[i + 1] == '^' || v.data[i + 1] == '\'')) ++i;
    }
    bytes = n; return result(Status::Ok);
}
inline Result written(View v, bool text, uint8_t* destination, size_t capacity,
                      bool terminate, size_t& bytes) {
    bytes = 0; size_t n = 0; Result r = measured(v, text, n); if (!parsed(r)) return r;
    if (!ee02_ics_detail::span(destination, capacity) ||
        ee02_ics_detail::overlap(v.data, v.size, destination, capacity)) return result(Status::InvalidArgument);
    if (n > capacity || (terminate && n == capacity)) return result(Status::ResourceLimit);
    size_t j = 0;
    for (size_t i = 0; i < v.size; ++i) {
        uint8_t b = v.data[i];
        if (text && b == '\\') { b = v.data[++i]; if (b == 'n' || b == 'N') b = '\n'; }
        else if (!text && b == '^' && i + 1 < v.size) {
            const uint8_t c = v.data[i + 1];
            if (c == 'n' || c == '^' || c == '\'') { ++i; b = c == 'n' ? '\n' : c == '\'' ? '"' : '^'; }
        }
        destination[j++] = b;
    }
    if (terminate) destination[j] = 0;
    bytes = j; return result(Status::Ok);
}
}
inline Result measureText(View v, size_t& bytes) { return detail::measured(v, true, bytes); }
inline Result writeText(View v, uint8_t* dst, size_t cap, bool nul, size_t& bytes) {
    return detail::written(v, true, dst, cap, nul, bytes);
}
// Member spans come from validated ParameterIterator/ValueIterator, excluding quotes.
inline Result measureParameter(IcsStructure::EncodedValue v, size_t& bytes) {
    return detail::measured(v.encoded, false, bytes);
}
inline Result writeParameter(IcsStructure::EncodedValue v, uint8_t* dst, size_t cap,
                             bool nul, size_t& bytes) {
    return detail::written(v.encoded, false, dst, cap, nul, bytes);
}

struct PropertyView {
    View name, value, parameters;
    Code code;
    Kind kind;
    IcsSourceStage::Disposition disposition;
    uint8_t flags;
    uint64_t component, parent, calendar;
    size_t index;
};
inline Result readProperty(const IcsSourceStage& stage, size_t index, PropertyView& out) {
    out = {};
    // status() deliberately survives release; reader availability is authoritative.
    const auto* p = stage.property(index);
    if (!p) return result(stage.component(0) ? Status::InvalidArgument : Status::Unavailable);
    const auto* c = stage.component(p->owner);
    if (!c || stage.payload(p->owner) != IcsSourceStage::Read::Ok) return result(Status::Unavailable);
    out.name = stage.propertyName(index); out.parameters = stage.parameters(index);
    out.code = p->code; out.kind = c->kind; out.disposition = p->disposition; out.flags = p->flags;
    out.component = c->ordinal; out.parent = c->parentOrdinal; out.calendar = c->calendarOrdinal; out.index = index;
    return result(stage.value(index, out.value) == IcsSourceStage::Read::Ok ? Status::Ok : Status::Unavailable);
}
struct Selection {
    Type type;
    bool list, utcRequired, observance, tzidPresent, review;
    IcsStructure::EncodedValue tzid; // Encoded reference, NOT an IANA/resolved zone.
};
inline Result selectTemporal(const IcsSourceStage& stage, size_t index, Selection& out) {
    out = {}; PropertyView p = {}; Result r = readProperty(stage, index, p); if (!parsed(r)) return r;
    Selection s = {}; s.type = Type::DateTime;
    bool valueAllowed = false, dateAllowed = false, periodAllowed = false, tzidAllowed = false;
    const bool event = p.kind == Kind::Event;
    const bool observance = p.kind == Kind::Standard || p.kind == Kind::Daylight;
    switch (p.code) {
    case Code::Dtstart:
        if (!event && !observance) return result(Status::UnsupportedFeature);
        valueAllowed = true; dateAllowed = event; tzidAllowed = event; s.observance = observance; break;
    case Code::Dtend: case Code::RecurrenceId:
        if (!event) return result(Status::UnsupportedFeature);
        valueAllowed = dateAllowed = tzidAllowed = true; break;
    case Code::Rdate: case Code::Exdate:
        if (!event && !observance) return result(Status::UnsupportedFeature);
        s.list = valueAllowed = true; dateAllowed = event; tzidAllowed = event;
        periodAllowed = event && p.code == Code::Rdate; s.observance = observance; break;
    case Code::Created: case Code::Dtstamp: case Code::LastModified:
        if (!event && !(p.kind == Kind::Timezone && p.code == Code::LastModified))
            return result(Status::UnsupportedFeature);
        s.utcRequired = true; break;
    case Code::Duration:
        if (!event) return result(Status::UnsupportedFeature);
        s.type = Type::Duration; break;
    default: return result(Status::UnsupportedFeature); // Includes all RECUR, even RSCALE/SKIP.
    }
    const Type defaultType = s.type;
    s.review = p.disposition == IcsSourceStage::Disposition::RetainUnreviewed ||
               (p.flags & IcsSourceStage::UnknownParameters);
    bool haveValue = false;
    IcsStructure::ParameterIterator parameters(p.parameters); IcsStructure::Parameter parameter;
    while (parameters.next(parameter)) {
        const bool value = detail::equal(parameter.name, "VALUE"), zone = detail::equal(parameter.name, "TZID");
        if (!value && !zone) {
            if (detail::equal(parameter.name, "RANGE")) {
                if (p.code != Code::RecurrenceId) return result(Status::InvalidParameter);
                s.review = true; // RANGE semantics remain deferred, even when known to staging.
            } else {
                const char* const otherStandard[] = {"ALTREP","CN","CUTYPE","DELEGATED-FROM","DELEGATED-TO",
                    "DIR","ENCODING","FMTTYPE","FBTYPE","LANGUAGE","MEMBER","PARTSTAT","RELATED",
                    "RELTYPE","ROLE","RSVP","SENT-BY"};
                for (const char* t : otherStandard)
                    if (detail::equal(parameter.name, t)) return result(Status::InvalidParameter);
                s.review = true; // Extension legality remains deferred.
            }
            continue;
        }
        if ((value && haveValue) || (zone && s.tzidPresent)) return result(Status::InvalidCardinality);
        IcsStructure::EncodedValue member, extra;
        auto members = parameter.values();
        if (!members.next(member) || members.next(extra)) return result(Status::InvalidCardinality);
        if (zone) {
            if (!tzidAllowed) return result(Status::InvalidParameter);
            s.tzidPresent = true; s.tzid = member; continue;
        }
        haveValue = true;
        if (!member.encoded.size) return result(Status::InvalidValueType);
        for (size_t i = 0; i < member.encoded.size; ++i) {
            const uint8_t b = detail::upper(member.encoded.data[i]);
            if (!(b >= 'A' && b <= 'Z') && !detail::digit(b) && b != '-')
                return result(Status::InvalidValueType);
        }
        if (detail::equal(member.encoded, "DATE")) s.type = Type::Date;
        else if (detail::equal(member.encoded, "DATE-TIME")) s.type = Type::DateTime;
        else if (detail::equal(member.encoded, "PERIOD")) s.type = Type::Period;
        else if (detail::equal(member.encoded, "DURATION")) s.type = Type::Duration;
        else {
            const char* const core[] = {"BINARY","BOOLEAN","CAL-ADDRESS","DURATION","FLOAT","INTEGER","RECUR","TEXT","TIME","URI","UTC-OFFSET"};
            for (const char* t : core) if (detail::equal(member.encoded, t)) return result(Status::InvalidValueType);
            return result(Status::UnsupportedFeature);
        }
    }
    // RFC 5545 3.2.20 permits an explicit default. That is not permission to
    // override a fixed-type property with a different standard value type.
    if ((!valueAllowed && s.type != defaultType) ||
        (s.type == Type::Duration && defaultType != Type::Duration) ||
        (s.type == Type::Date && !dateAllowed) || (s.type == Type::Period && !periodAllowed))
        return result(Status::InvalidValueType);
    if (s.tzidPresent && s.type == Type::Date) return result(Status::InvalidParameter);
    out = s; return result(Status::Ok);
}
namespace detail {
inline Result selected(View v, const Selection& s, TemporalValue& out) {
    TemporalValue t = {}; t.type = s.type; Result r = result(Status::UnsupportedFeature);
    // Decode into live local objects, then switch the trivial union member by
    // named assignment (C++11 lifetime rule). Never pass inactive union storage
    // through an output reference and assume that starts its object lifetime.
    if (s.type == Type::Date) { Date d = {}; r = decodeDate(v, d); if (parsed(r)) t.date = d; }
    else if (s.type == Type::DateTime) { DateTime d = {}; r = decodeDateTime(v, d); if (parsed(r)) t.dateTime = d; }
    else if (s.type == Type::Duration) { Duration d = {}; r = decodeDuration(v, d); if (parsed(r)) t.duration = d; }
    else { Period p = {}; r = decodePeriod(v, p); if (parsed(r)) t.period = p; }
    if (!parsed(r)) return r;
    if (s.type == Type::DateTime || s.type == Type::Period) {
        DateTime* first = s.type == Type::DateTime ? &t.dateTime : &t.period.start;
        DateTime* last = s.type == Type::Period && !t.period.durationForm ? &t.period.end : nullptr;
        if ((s.utcRequired && first->mode != TimeMode::Utc) ||
            (s.observance && first->mode != TimeMode::Floating)) return result(Status::MalformedValue);
        if (s.tzidPresent) {
            if (first->mode == TimeMode::Utc || (last && last->mode == TimeMode::Utc))
                return result(Status::InvalidParameter);
            first->mode = TimeMode::Tzid; if (last) last->mode = TimeMode::Tzid;
            if (!s.tzid.encoded.size) r = result(Status::NeedsContext);
        }
    }
    out = t; return r;
}
}
inline Result decodeTemporal(const IcsSourceStage& stage, size_t index,
                             TemporalValue& out, Selection& selection) {
    Selection s = {}; Result r = selectTemporal(stage, index, s); if (!parsed(r)) return r;
    if (s.list) return result(Status::InvalidArgument);
    PropertyView p = {}; r = readProperty(stage, index, p); if (!parsed(r)) return r;
    r = detail::selected(p.value, s, out); if (parsed(r)) selection = s; return r;
}

// Provisional members are not a validated list. Exhaust next() through End, then
// check status()==Ok/NeedsContext. Failure is sticky. Stage must remain alive and
// readable for every call; never dereference a prior borrowed view after release.
class TemporalList {
public:
    struct Limits { size_t members, bytes; }; // Value + encoded-parameter work bound.
    TemporalList(const IcsSourceStage& stage, size_t index, Limits limits)
        : _stage(stage), _index(index), _limits(limits) {
        PropertyView p = {}; Result r = readProperty(stage, index, p);
        if (!parsed(r)) { _result = r; return; }
        if (!limits.members || !limits.bytes) { _result = result(Status::InvalidArgument); return; }
        if (p.value.size > limits.bytes || p.parameters.size > limits.bytes - p.value.size) {
            _result = result(Status::ResourceLimit); return;
        }
        r = selectTemporal(stage, index, _selection);
        _result = !parsed(r) ? r : !_selection.list ? result(Status::InvalidArgument) : result(Status::InProgress);
    }
    Result next(TemporalValue& out) {
        if (_result.status != Status::InProgress) return _complete && parsed(_result) ? result(Status::End) : _result;
        PropertyView p = {}; Result r = readProperty(_stage, _index, p);
        if (!parsed(r)) return _result = r;
        if (_afterLast) {
            _complete = true; _result = result(_context ? Status::NeedsContext : Status::Ok);
            return result(Status::End);
        }
        if (_count == _limits.members) return _result = result(Status::ResourceLimit, _next);
        const size_t start = _next; size_t end = start;
        while (end < p.value.size && p.value.data[end] != ',') ++end;
        if (start == end) return _result = result(Status::MalformedValue, start);
        r = detail::selected({p.value.data + start, end - start}, _selection, out);
        if (!parsed(r)) { r.offset += start; return _result = r; }
        _context = _context || r.status == Status::NeedsContext; ++_count;
        _afterLast = end == p.value.size; _next = _afterLast ? end : end + 1;
        return r;
    }
    Result status() const { return _result; }
    bool complete() const { return _complete; }
    size_t count() const { return _count; }
    const Selection& selection() const { return _selection; }
private:
    const IcsSourceStage& _stage;
    size_t _index, _next = 0, _count = 0;
    Limits _limits;
    Selection _selection = {};
    Result _result = {Status::InProgress, 0};
    bool _afterLast = false, _context = false, _complete = false;
};
}
#endif
