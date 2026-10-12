#ifndef EE02_FIVE_DAY_SNAPSHOT_H
#define EE02_FIVE_DAY_SNAPSHOT_H

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <type_traits>
#include <esp32-hal-psram.h>

// Wake-cycle data only. Single-threaded publication is atomic at API boundaries,
// not concurrent/lock-free publication. Input spans must actually be readable;
// returned const views must not outlive this owner or release(). No provider IDs.
class FiveDaySnapshot {
public:
    enum class Kind : uint8_t { Timed, AllDay };
    enum class Outcome : uint8_t { Pending, Complete, Incomplete };
    enum class Reason : uint16_t {
        None, ProducerFailure, Cancelled, InvalidOccurrence, InvalidText,
        RecordCapacity, TextCapacity
    };
    enum class Result {
        Ok, OutsideWindow, NotInitialized, InvalidState, InvalidSource,
        InvalidWindow, InvalidCapacity, InvalidText, AllocationFailed,
        Overflow, InvalidOccurrence, RecordCapacity, TextCapacity, InvalidReason
    };
    struct Text { const char* data; size_t size; };
    struct TimedRange { int64_t start; int64_t end; }; // UTC POSIX seconds.
    struct CivilRange { int32_t start; int32_t end; }; // Days since 1970-01-01.
    union Range { TimedRange timed; CivilRange civil; };
    struct Input { Range range; Kind kind; Text title; };
    struct Occurrence {
        Range range;
        uint32_t titleOffset;
        uint16_t sourceIndex;
        Kind kind;
        uint8_t reserved;
    };
    struct Source {
        uint32_t labelOffset;
        uint32_t firstOccurrence;
        uint32_t occurrenceCount;
        Reason reason;
        Outcome outcome;
        uint8_t reserved;
    };
    struct Window {
        int64_t utcBoundaries[6];
        int32_t firstCivilDay;
        uint32_t reserved;
    };

    FiveDaySnapshot() = default;
    FiveDaySnapshot(const FiveDaySnapshot&) = delete;
    FiveDaySnapshot& operator=(const FiveDaySnapshot&) = delete;
    FiveDaySnapshot(FiveDaySnapshot&&) = delete;
    FiveDaySnapshot& operator=(FiveDaySnapshot&&) = delete;
    ~FiveDaySnapshot() { free(_storage); }

    // All source labels are copied before any transactions. No partial catalogue
    // is published. Capacities are arguments, never fixed product defaults.
    Result initialize(const Window& window, size_t recordCapacity,
                      size_t textCapacity, const Text* labels, size_t sourceCount) {
        if (_storage) return Result::InvalidState;
        if (!validWindow(window)) return Result::InvalidWindow;
        if (recordCapacity > UINT32_MAX || textCapacity > UINT32_MAX ||
            sourceCount > static_cast<size_t>(UINT16_MAX) + 1) return Result::InvalidCapacity;
        if (sourceCount && !labels) return Result::InvalidSource;
        if (sourceCount > static_cast<size_t>(PTRDIFF_MAX) / sizeof(Text) ||
            (labels && sourceCount * sizeof(Text) >
                UINTPTR_MAX - reinterpret_cast<uintptr_t>(labels))) return Result::Overflow;

        size_t total = 0, windowOffset = 0, recordsOffset = 0, sourcesOffset = 0, textOffset = 0;
        if (!region(total, 1, sizeof(Window), alignof(Window), windowOffset) ||
            !region(total, recordCapacity, sizeof(Occurrence), alignof(Occurrence), recordsOffset) ||
            !region(total, sourceCount, sizeof(Source), alignof(Source), sourcesOffset) ||
            !region(total, textCapacity, 1, 1, textOffset)) return Result::Overflow;

        size_t labelBytes = 0;
        for (size_t i = 0; i < sourceCount; ++i) {
            if (!readableText(labels[i]) || labels[i].size == 0) return Result::InvalidText;
            if (labels[i].size >= textCapacity - labelBytes) return Result::TextCapacity;
            if (!validUtf8(labels[i])) return Result::InvalidText;
            labelBytes += labels[i].size + 1;
        }
        if (!psramFound()) return Result::AllocationFailed;
        auto* storage = static_cast<uint8_t*>(ps_malloc(total)); // SPIRAM | 8BIT, no fallback.
        if (!storage) return Result::AllocationFailed;
        const uintptr_t address = reinterpret_cast<uintptr_t>(storage);
        if (total > UINTPTR_MAX - address || address % alignof(Window) ||
            address % alignof(Occurrence) || address % alignof(Source)) {
            free(storage);
            return Result::Overflow;
        }
        // Construct trivial objects explicitly in the one allocation.
        auto* storedWindow = new (storage + windowOffset) Window(window);
        storedWindow->reserved = 0;
        size_t cursor = 0;
        for (size_t i = 0; i < sourceCount; ++i) {
            auto* source = new (storage + sourcesOffset + i * sizeof(Source)) Source{};
            source->labelOffset = static_cast<uint32_t>(cursor);
            source->outcome = Outcome::Pending;
            source->reason = Reason::None;
            std::memcpy(storage + textOffset + cursor, labels[i].data, labels[i].size);
            storage[textOffset + cursor + labels[i].size] = 0;
            cursor += labels[i].size + 1;
        }
        _storage = storage;
        _bytes = total;
        _recordsOffset = recordsOffset;
        _sourcesOffset = sourcesOffset;
        _textOffset = textOffset;
        _recordCapacity = recordCapacity;
        _textCapacity = textCapacity;
        _sourceCount = sourceCount;
        _publishedText = _textCursor = cursor;
        return Result::Ok;
    }

    Result beginSource(size_t index) {
        const Result state = sourceState(index);
        if (state != Result::Ok) return state;
        if (_active != NoSource || sourceAt(index)->outcome != Outcome::Pending)
            return Result::InvalidState;
        // Published cursors are the immutable append checkpoints.
        _active = index;
        return Result::Ok;
    }

    Result addOccurrence(size_t index, const Input& input) {
        const Result state = transactionState(index);
        if (state != Result::Ok) {
            // A misaddressed add must not leave an active source eligible for a
            // false Complete if its producer ignores the error.
            return _active != NoSource ? abort(state, Reason::InvalidOccurrence) : state;
        }
        bool included = false;
        if (input.kind == Kind::Timed) {
            const auto& t = input.range.timed;
            if (t.end < t.start) return abort(Result::InvalidOccurrence, Reason::InvalidOccurrence);
            const auto& w = *window();
            included = t.start == t.end
                ? (t.start >= w.utcBoundaries[0] && t.start < w.utcBoundaries[5])
                : (t.start < w.utcBoundaries[5] && t.end > w.utcBoundaries[0]);
        } else if (input.kind == Kind::AllDay) {
            const auto& d = input.range.civil;
            if (d.end <= d.start) return abort(Result::InvalidOccurrence, Reason::InvalidOccurrence);
            included = d.start < window()->firstCivilDay + 5 && d.end > window()->firstCivilDay;
        } else {
            return abort(Result::InvalidOccurrence, Reason::InvalidOccurrence);
        }
        if (!readableText(input.title)) return abort(Result::InvalidText, Reason::InvalidText);
        if (included && _recordCursor == _recordCapacity)
            return abort(Result::RecordCapacity, Reason::RecordCapacity);
        if (included && input.title.size >= _textCapacity - _textCursor)
            return abort(Result::TextCapacity, Reason::TextCapacity);
        if (!validUtf8(input.title)) return abort(Result::InvalidText, Reason::InvalidText);
        if (!included) return Result::OutsideWindow; // Valid exclusion, not incomplete input.

        auto* record = new (recordAt(_recordCursor)) Occurrence{};
        if (input.kind == Kind::Timed) record->range.timed = input.range.timed;
        else record->range.civil = input.range.civil;
        record->kind = input.kind;
        record->sourceIndex = static_cast<uint16_t>(index);
        record->titleOffset = static_cast<uint32_t>(_textCursor);
        if (input.title.size)
            std::memcpy(_storage + _textOffset + _textCursor, input.title.data, input.title.size);
        _storage[_textOffset + _textCursor + input.title.size] = 0;
        _textCursor += input.title.size + 1;
        ++_recordCursor;
        return Result::Ok;
    }

    // Producer assertion: transport, recurrence/identity reconciliation, timezone
    // normalization and completeness checks have all finished. Not verified here.
    Result completeSource(size_t index) {
        const Result state = transactionState(index);
        if (state != Result::Ok) return state;
        auto* source = sourceAt(index);
        source->firstOccurrence = static_cast<uint32_t>(_publishedRecords);
        source->occurrenceCount = static_cast<uint32_t>(_recordCursor - _publishedRecords);
        source->outcome = Outcome::Complete;
        source->reason = Reason::None;
        _publishedRecords = _recordCursor;
        _publishedText = _textCursor;
        _active = NoSource;
        return Result::Ok;
    }

    Result failSource(size_t index, Reason reason) {
        const Result state = transactionState(index);
        if (state != Result::Ok) return state;
        if (reason == Reason::None || reason > Reason::TextCapacity) return Result::InvalidReason;
        rollback(reason);
        return Result::Ok;
    }

    bool initialized() const { return _storage != nullptr; }
    size_t occurrenceCount() const { return _publishedRecords; }
    size_t sourceCount() const { return _sourceCount; }
    size_t allocationBytes() const { return _bytes; }
    const Window* window() const { return _storage ? reinterpret_cast<const Window*>(_storage) : nullptr; }
    const Source* source(size_t index) const {
        return _storage && index < _sourceCount ? sourceAt(index) : nullptr;
    }
    const Occurrence* occurrence(size_t index) const {
        return _storage && index < _publishedRecords ? recordAt(index) : nullptr;
    }
    Text sourceLabel(size_t index) const {
        const auto* s = source(index);
        return s ? textAt(s->labelOffset) : Text{nullptr, 0};
    }
    Text title(size_t index) const {
        const auto* r = occurrence(index);
        return r ? textAt(r->titleOffset) : Text{nullptr, 0};
    }

    Result release() {
        if (_active != NoSource) return Result::InvalidState;
        free(_storage);
        _storage = nullptr;
        _bytes = _recordsOffset = _sourcesOffset = _textOffset = 0;
        _recordCapacity = _textCapacity = _sourceCount = 0;
        _publishedRecords = _recordCursor = _publishedText = _textCursor = 0;
        return Result::Ok;
    }

private:
    static constexpr size_t NoSource = SIZE_MAX;
    uint8_t* _storage = nullptr;
    size_t _bytes = 0, _recordsOffset = 0, _sourcesOffset = 0, _textOffset = 0;
    size_t _recordCapacity = 0, _textCapacity = 0, _sourceCount = 0;
    size_t _publishedRecords = 0, _recordCursor = 0;
    size_t _publishedText = 0, _textCursor = 0, _active = NoSource;

    static bool validWindow(const Window& w) {
        if (w.firstCivilDay > INT32_MAX - 5) return false;
        for (size_t i = 1; i < 6; ++i)
            if (w.utcBoundaries[i] <= w.utcBoundaries[i - 1]) return false;
        return true; // Caller guarantees timezone/civil-date correspondence.
    }
    static bool region(size_t& total, size_t count, size_t size, size_t alignment, size_t& offset) {
        const size_t limit = static_cast<size_t>(PTRDIFF_MAX);
        const size_t padding = (alignment - total % alignment) % alignment;
        if (padding > limit - total) return false;
        total += padding;
        if (count > (limit - total) / size) return false;
        offset = total;
        total += count * size;
        return true;
    }
    static bool readableText(Text t) {
        return t.size <= static_cast<size_t>(PTRDIFF_MAX) &&
               (t.data || t.size == 0) &&
               (!t.data || t.size <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(t.data));
    }
    static bool validUtf8(Text t) {
        // Bounded UTF-8: reject NUL, overlong sequences, surrogates and >U+10FFFF.
        size_t i = 0;
        while (i < t.size) {
            const uint8_t c = static_cast<uint8_t>(t.data[i++]);
            if (c == 0) return false;
            if (c < 0x80) continue;
            size_t more = 0;
            uint8_t low = 0x80, high = 0xbf;
            if (c >= 0xc2 && c <= 0xdf) more = 1;
            else if (c >= 0xe0 && c <= 0xef) {
                more = 2;
                if (c == 0xe0) low = 0xa0;
                if (c == 0xed) high = 0x9f;
            } else if (c >= 0xf0 && c <= 0xf4) {
                more = 3;
                if (c == 0xf0) low = 0x90;
                if (c == 0xf4) high = 0x8f;
            } else return false;
            if (more > t.size - i) return false;
            const uint8_t first = static_cast<uint8_t>(t.data[i]);
            if (first < low || first > high) return false;
            for (size_t j = 1; j < more; ++j) {
                const uint8_t next = static_cast<uint8_t>(t.data[i + j]);
                if (next < 0x80 || next > 0xbf) return false;
            }
            i += more;
        }
        return true;
    }
    Result sourceState(size_t index) const {
        if (!_storage) return Result::NotInitialized;
        return index < _sourceCount ? Result::Ok : Result::InvalidSource;
    }
    Result transactionState(size_t index) const {
        const Result state = sourceState(index);
        if (state != Result::Ok) return state;
        return index == _active ? Result::Ok : Result::InvalidState;
    }
    Result abort(Result result, Reason reason) { rollback(reason); return result; }
    void rollback(Reason reason) {
        _recordCursor = _publishedRecords;
        _textCursor = _publishedText;
        auto* source = sourceAt(_active);
        source->firstOccurrence = 0;
        source->occurrenceCount = 0;
        source->outcome = Outcome::Incomplete;
        source->reason = reason;
        _active = NoSource;
    }
    Source* sourceAt(size_t index) const {
        return reinterpret_cast<Source*>(_storage + _sourcesOffset + index * sizeof(Source));
    }
    Occurrence* recordAt(size_t index) const {
        return reinterpret_cast<Occurrence*>(_storage + _recordsOffset + index * sizeof(Occurrence));
    }
    Text textAt(size_t offset) const {
        if (offset >= _publishedText) return {nullptr, 0};
        const char* p = reinterpret_cast<const char*>(_storage + _textOffset + offset);
        const size_t remaining = _publishedText - offset;
        const void* end = std::memchr(p, 0, remaining);
        return end ? Text{p, static_cast<size_t>(static_cast<const char*>(end) - p)} : Text{nullptr, 0};
    }
};

static_assert(sizeof(FiveDaySnapshot::Occurrence) == 24, "review changed snapshot record layout");
static_assert(sizeof(FiveDaySnapshot::Source) == 16, "review changed snapshot source layout");
static_assert(sizeof(FiveDaySnapshot::Window) == 56, "review changed snapshot window layout");
static_assert(std::is_trivially_copyable<FiveDaySnapshot::Occurrence>::value, "snapshot owns plain records");

#endif
