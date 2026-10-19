#ifndef EE02_FIVE_DAY_RENDERER_H
#define EE02_FIVE_DAY_RENDERER_H

#include "five_day_snapshot.h"
#include "five_day_local_time.h"
#include <Seeed_GFX.h>
#include <cstdio>

// Provisional five-column agenda. No acquisition, normalization or source mutation.
// Conversion must be deterministic, use the window's explicit display timezone,
// and remain available for the whole call. No views escape render().
namespace FiveDayRenderer {
struct Region { int32_t x, y, width, height; };
using LocalTime = FiveDayLocalTime::Value;
using Convert = bool (*)(int64_t utc, LocalTime& local, void* context);
struct TimeContext { Convert convert; void* context; };
enum class Status { Drawn, InvalidSnapshot, InvalidRegion, InvalidTimeContext };
struct Report {
    Status status;
    size_t dayEntriesDrawn, dayEntriesOmitted, titlesShortened, glyphsReplaced;
    size_t completeSources, incompleteSources, pendingSources;
};
namespace detail {
// Gregorian arithmetic for civil labels only; never substitutes for UTC conversion.
inline int64_t civilDay(int64_t year, unsigned month, unsigned day) {
    return FiveDayLocalTime::civilDay(year,month,day);
}
inline void date(int32_t day, char* out, size_t bytes) {
    const auto d=FiveDayLocalTime::date(day);
    std::snprintf(out, bytes, "%04ld-%02u-%02u", static_cast<long>(d.year), d.month, d.day);
}
inline bool local(const TimeContext& zone, int64_t utc, LocalTime& out) {
    if (!zone.convert || !zone.convert(utc, out, zone.context) || out.hour > 23 || out.minute > 59 || out.second > 59)
        return false;
    // Verify the supplied local representation agrees with the supplied UTC
    // instant and offset; int32 civil days keep this calculation in int64 range.
    return int64_t(out.civilDay) * 86400 + out.hour * 3600 + out.minute * 60 + out.second - out.offsetSeconds == utc;
}
inline void time(const LocalTime& t, char* out, size_t bytes) {
    const int64_t offset = t.offsetSeconds < 0 ? -int64_t(t.offsetSeconds) : t.offsetSeconds;
    char clock[16], suffix[24];
    if (t.second) std::snprintf(clock, sizeof(clock), "%02u:%02u:%02u", t.hour, t.minute, t.second);
    else std::snprintf(clock, sizeof(clock), "%02u:%02u", t.hour, t.minute);
    if (offset % 60) std::snprintf(suffix, sizeof(suffix), "%c%02ld%02ld%02ld", t.offsetSeconds < 0 ? '-' : '+',
        static_cast<long>(offset / 3600), static_cast<long>(offset / 60 % 60), static_cast<long>(offset % 60));
    else std::snprintf(suffix, sizeof(suffix), "%c%02ld%02ld", t.offsetSeconds < 0 ? '-' : '+',
        static_cast<long>(offset / 3600), static_cast<long>(offset / 60 % 60));
    std::snprintf(out, bytes, "%s %s", clock, suffix);
}
inline bool overlaps(const FiveDaySnapshot::Occurrence& e, const FiveDaySnapshot::Window& w, size_t day) {
    if (e.kind == FiveDaySnapshot::Kind::AllDay)
        return e.range.civil.start <= w.firstCivilDay + int32_t(day) && e.range.civil.end > w.firstCivilDay + int32_t(day);
    const auto t = e.range.timed;
    return t.start == t.end ? t.start >= w.utcBoundaries[day] && t.start < w.utcBoundaries[day + 1] :
        t.start < w.utcBoundaries[day + 1] && t.end > w.utcBoundaries[day];
}
inline bool before(const FiveDaySnapshot& s, size_t a, size_t b) {
    const auto& x = *s.occurrence(a); const auto& y = *s.occurrence(b);
    if (x.kind != y.kind) return x.kind == FiveDaySnapshot::Kind::AllDay;
    const int64_t xs = x.kind == FiveDaySnapshot::Kind::AllDay ? x.range.civil.start : x.range.timed.start;
    const int64_t ys = y.kind == FiveDaySnapshot::Kind::AllDay ? y.range.civil.start : y.range.timed.start;
    return xs < ys || (xs == ys && a < b); // Stable distinct entries, including repeated wall-clock times.
}
inline FiveDaySnapshot::Text text(const char* s) { return {s, std::strlen(s)}; }
// Fixed GLCD cell output. Consume whole UTF-8 codepoints; unsupported glyphs
// become one '?'. At most 'lines' lines, with explicit ellipsis on shortening.
inline bool label(Seeed_GFX& gfx, FiveDaySnapshot::Text value, int32_t x, int32_t y,
                  size_t cells, size_t lines, uint8_t scale, uint32_t color, size_t& replacements) {
    const size_t slots = cells * lines;
    size_t byte = 0;
    for (size_t pos = 0; pos < slots && byte < value.size; ++pos) {
        const uint8_t c = static_cast<uint8_t>(value.data[byte]);
        size_t step = c < 0x80 ? 1 : c < 0xe0 ? 2 : c < 0xf0 ? 3 : 4;
        // Snapshot owns valid UTF-8. Still bound every read by its stored length.
        if (step > value.size - byte) step = value.size - byte;
        if (pos == slots - 3 && value.size - byte > 3) {
            // Determine if the remaining codepoints actually exceed three cells.
            size_t look = byte, remaining = 0;
            while (look < value.size && remaining <= 3) {
                const uint8_t b = static_cast<uint8_t>(value.data[look]);
                const size_t n = b < 0x80 ? 1 : b < 0xe0 ? 2 : b < 0xf0 ? 3 : 4;
                look += n <= value.size - look ? n : value.size - look; ++remaining;
            }
            if (remaining > 3) {
                for (size_t j = 0; j < 3; ++j) {
                    const size_t cell = pos + j;
                    gfx.drawChar(x + int32_t(cell % cells) * 6 * scale, y + int32_t(cell / cells) * 8 * scale,
                        '.', color, TFT_WHITE, scale);
                }
                return true;
            }
        }
        uint8_t glyph = c;
        if (c >= 0x80) { glyph = '?'; ++replacements; }
        else if (c < 32 || c == 127) glyph = ' ';
        gfx.drawChar(x + int32_t(pos % cells) * 6 * scale, y + int32_t(pos / cells) * 8 * scale,
                     glyph, color, TFT_WHITE, scale);
        byte += step;
    }
    return byte < value.size;
}
inline const char* reason(FiveDaySnapshot::Reason r) {
    switch (r) {
    case FiveDaySnapshot::Reason::ProducerFailure: return "producer failed";
    case FiveDaySnapshot::Reason::Cancelled: return "cancelled";
    case FiveDaySnapshot::Reason::InvalidOccurrence: return "invalid event";
    case FiveDaySnapshot::Reason::InvalidText: return "invalid text";
    case FiveDaySnapshot::Reason::RecordCapacity: return "event capacity";
    case FiveDaySnapshot::Reason::TextCapacity: return "text capacity";
    default: return "unknown reason";
    }
}
struct ViewportScope {
    Seeed_GFX& gfx;
    int32_t x, y, width, height, originX, originY;
    bool datum;
    ViewportScope(Seeed_GFX& g, Region r) : gfx(g), x(g.getViewportX()), y(g.getViewportY()),
        width(g.getViewportWidth()), height(g.getViewportHeight()), originX(g.getOriginX()),
        originY(g.getOriginY()), datum(g.getViewportDatum()) {
        gfx.setViewport(r.x, r.y, r.width, r.height, false);
    }
    ~ViewportScope() {
        gfx.setViewport(x, y, width, height, datum); gfx.setOrigin(originX, originY);
    }
};
}

// Region coordinates are half-open; x=10,y=10,w=1025,h=1025 includes pixel 1034.
// GLCD scale is an explicit preview typography input (1..3), not final UI policy.
inline Report render(Seeed_GFX& gfx, const FiveDaySnapshot& snapshot, Region region,
                     TimeContext zone, uint8_t scale = 2) {
    using namespace detail;
    Report r = {}; r.status = Status::InvalidSnapshot;
    if (!snapshot.initialized() || !snapshot.window()) return r;
    const auto& w = *snapshot.window();
    r.status = Status::InvalidRegion;
    if (!scale || scale > 3 || region.x < 0 || region.y < 0 || region.width <= 0 || region.height <= 0 ||
        region.x > gfx.width() || region.width > gfx.width() - region.x ||
        region.y > gfx.height() || region.height > gfx.height() - region.y) return r;
    const int32_t cell = 6 * scale, line = 8 * scale;
    const int32_t column = region.width / 5;
    // Four source/status lines, two day-header lines, an eight-line event and
    // one reserved overflow line must fit. Times are never silently shortened.
    if (column < 16 * cell + 8 || region.height < 15 * line + 16) return r;
    const size_t cells = static_cast<size_t>((column - 8) / cell);
    const size_t rows = static_cast<size_t>((region.height - 7 * line - 16) / (8 * line + 8));
    if (!rows) return r;
    r.status = Status::InvalidTimeContext;
    for (size_t d = 0; d < 6; ++d) {
        LocalTime b = {};
        if (!local(zone, w.utcBoundaries[d], b) || b.civilDay != w.firstCivilDay + int32_t(d) || b.hour || b.minute || b.second)
            return r;
        if (d) {
            LocalTime last = {};
            if (!local(zone, w.utcBoundaries[d] - 1, last) || last.civilDay != w.firstCivilDay + int32_t(d) - 1) return r;
        }
    }
    // Check every timed endpoint, including entries that may not fit on screen.
    // No graphics is emitted with missing/invalid conversion context.
    for (size_t i = 0; i < snapshot.occurrenceCount(); ++i) {
        const auto* e = snapshot.occurrence(i);
        if (!e || !snapshot.source(e->sourceIndex) || snapshot.source(e->sourceIndex)->outcome != FiveDaySnapshot::Outcome::Complete)
            { r.status = Status::InvalidSnapshot; return r; }
        if (e->kind == FiveDaySnapshot::Kind::Timed) {
            LocalTime a = {}, b = {}; char value[48];
            if (!local(zone, e->range.timed.start, a) || !local(zone, e->range.timed.end, b)) return r;
            time(a, value, sizeof(value)); if (std::strlen(value) > cells) { r.status = Status::InvalidRegion; return r; }
            time(b, value, sizeof(value)); if (std::strlen(value) > cells) { r.status = Status::InvalidRegion; return r; }
        }
    }
    for (size_t i = 0; i < snapshot.sourceCount(); ++i) {
        const auto* source = snapshot.source(i);
        if (!source) { r.status = Status::InvalidSnapshot; return r; }
        if (source->outcome == FiveDaySnapshot::Outcome::Complete) ++r.completeSources;
        else if (source->outcome == FiveDaySnapshot::Outcome::Incomplete) ++r.incompleteSources;
        else ++r.pendingSources;
    }
    const bool complete = snapshot.sourceCount() && r.completeSources == snapshot.sourceCount();
    ViewportScope viewport(gfx, region); // Absolute region even if caller had an origin/viewport.
    gfx.setFreeFont(nullptr); // Six-argument drawChar uses the verified 6x8 GLCD implementation.
    gfx.fillRect(region.x, region.y, region.width, region.height, TFT_WHITE);
    size_t unused = 0;
    const size_t fullCells = static_cast<size_t>((region.width - 8) / cell);
    const char* status = !snapshot.sourceCount() ? "No calendars selected" : complete ? (snapshot.occurrenceCount() ? "Schedule: all sources complete" : "No events - all sources complete") :
        r.completeSources ? "Incomplete schedule - available events shown" : "Calendar unavailable - check sources / retry";
    label(gfx, text(status), region.x + 4, region.y + 4, fullCells, 1, scale, complete ? TFT_BLACK : TFT_RED, unused);
    // Two bounded source lines plus an explicit count of additional affected sources.
    size_t affected = 0;
    for (size_t i = 0; i < snapshot.sourceCount(); ++i) {
        const auto& s = *snapshot.source(i);
        if (s.outcome == FiveDaySnapshot::Outcome::Complete) continue;
        if (affected < 2) {
            const int32_t y = region.y + 4 + int32_t(affected + 1) * line;
            label(gfx, snapshot.sourceLabel(i), region.x + 4, y, fullCells / 2, 1, scale, TFT_RED, unused);
            label(gfx, text(s.outcome == FiveDaySnapshot::Outcome::Pending ? "Pending - await source" : reason(s.reason)),
                region.x + 4 + int32_t(fullCells / 2) * cell, y, fullCells - fullCells / 2, 1, scale, TFT_RED, unused);
        }
        ++affected;
    }
    if (affected > 2) {
        char more[48]; std::snprintf(more, sizeof(more), "+%lu other affected sources", static_cast<unsigned long>(affected - 2));
        label(gfx, text(more), region.x + 4, region.y + 4 + 3 * line, fullCells, 1, scale, TFT_RED, unused);
    }
    const char* weekdays[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    const int32_t headerY = region.y + 4 + 4 * line;
    for (size_t d = 0; d < 5; ++d) {
        const int32_t x = region.x + int32_t(d) * column + 4;
        if (d) gfx.fillRect(x - 4, headerY, 1, region.y + region.height - headerY, TFT_BLACK);
        const int32_t day = w.firstCivilDay + int32_t(d);
        char dateText[32]; date(day, dateText, sizeof(dateText));
        label(gfx, text(weekdays[(int64_t(day) % 7 + 11) % 7]), x, headerY, cells, 1, scale, TFT_BLACK, unused);
        label(gfx, text(dateText), x, headerY + line, cells, 1, scale, TFT_BLACK, unused);
        size_t count = 0;
        for (size_t i = 0; i < snapshot.occurrenceCount(); ++i) if (overlaps(*snapshot.occurrence(i), w, d)) ++count;
        size_t previous = SIZE_MAX;
        const size_t visible = count < rows ? count : rows;
        for (size_t row = 0; row < visible; ++row) {
            size_t selected = SIZE_MAX;
            for (size_t i = 0; i < snapshot.occurrenceCount(); ++i)
                if (overlaps(*snapshot.occurrence(i), w, d) && (previous == SIZE_MAX || before(snapshot, previous, i)) &&
                    (selected == SIZE_MAX || before(snapshot, i, selected))) selected = i;
            const auto& e = *snapshot.occurrence(selected);
            const int32_t y = headerY + 2 * line + 8 + int32_t(row) * (8 * line + 8);
            char a[32], b[32], from[48], to[48]; bool continued;
            if (e.kind == FiveDaySnapshot::Kind::AllDay) {
                date(e.range.civil.start, a, sizeof(a)); date(e.range.civil.end, b, sizeof(b));
                std::snprintf(from, sizeof(from), "to %s", b);
                label(gfx, text("All day"), x, y, cells, 1, scale, TFT_BLACK, unused);
                label(gfx, text(a), x, y + line, cells, 1, scale, TFT_BLACK, unused);
                label(gfx, text(from), x, y + 2 * line, cells, 1, scale, TFT_BLACK, unused);
                label(gfx, text("end exclusive"), x, y + 3 * line, cells, 1, scale, TFT_BLACK, unused);
                continued = e.range.civil.start < day;
            } else {
                LocalTime start = {}, end = {};
                if (!local(zone, e.range.timed.start, start) || !local(zone, e.range.timed.end, end)) { r.status = Status::InvalidTimeContext; return r; }
                date(start.civilDay, a, sizeof(a)); date(end.civilDay, b, sizeof(b));
                time(start, from, sizeof(from)); time(end, to, sizeof(to));
                label(gfx, text(a), x, y, cells, 1, scale, TFT_BLACK, unused);
                label(gfx, text(from), x, y + line, cells, 1, scale, TFT_BLACK, unused);
                char endDate[48]; std::snprintf(endDate, sizeof(endDate), "to %s", b);
                label(gfx, text(e.range.timed.start == e.range.timed.end ? "Point" : endDate), x, y + 2 * line, cells, 1, scale, TFT_BLACK, unused);
                if (e.range.timed.start != e.range.timed.end) label(gfx, text(to), x, y + 3 * line, cells, 1, scale, TFT_BLACK, unused);
                continued = e.range.timed.start < w.utcBoundaries[d];
            }
            if (label(gfx, snapshot.title(selected), x, y + 4 * line, cells, 2, scale, TFT_BLACK, r.glyphsReplaced)) ++r.titlesShortened;
            label(gfx, snapshot.sourceLabel(e.sourceIndex), x, y + 6 * line, cells, 1, scale, TFT_BLACK, unused);
            if (continued) label(gfx, text("Continued"), x, y + 7 * line, cells, 1, scale, TFT_BLACK, unused);
            gfx.fillRect(x, y + 8 * line + 2, int32_t(cells) * cell, 1, TFT_BLACK);
            ++r.dayEntriesDrawn; previous = selected;
        }
        if (count > visible) {
            char more[48]; std::snprintf(more, sizeof(more), "+%lu more events", static_cast<unsigned long>(count - visible));
            label(gfx, text(more), x, region.y + region.height - line - 4, cells, 1, scale, TFT_RED, unused);
            r.dayEntriesOmitted += count - visible;
        }
    }
    r.status = Status::Drawn;
    return r;
}
}
#endif
