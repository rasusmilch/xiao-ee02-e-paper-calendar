#ifndef EE02_ICS_LOGICAL_LINES_H
#define EE02_ICS_LOGICAL_LINES_H

#include <cstddef>
#include <cstdint>

namespace ee02_ics_detail {
// Arithmetic checks do not prove that a caller's storage is readable/writable.
inline bool span(const void* p, size_t n) {
    return n <= static_cast<size_t>(PTRDIFF_MAX) && (p || n == 0) &&
        (!p || n <= UINTPTR_MAX - reinterpret_cast<uintptr_t>(p));
}
inline bool overlap(const void* a, size_t an, const void* b, size_t bn) {
    if (!an || !bn) return false; // Both spans must already be validated.
    const uintptr_t x = reinterpret_cast<uintptr_t>(a), y = reinterpret_cast<uintptr_t>(b);
    return x < y + bn && y < x + an;
}
}

// RFC 5545 section 3.1 octet unfolding only, not content/property validation.
// Caller owns storage. Sink views last only for the synchronous callback; no
// escaped aliases, concurrency, throwing, owner destruction or reentrant use.
class IcsLogicalLines {
public:
    enum class Status { NotInitialized, InProgress, Complete, InvalidArgument,
        InvalidState, MalformedLine, IncompleteLine, ResourceLimit, CallbackFailure, Cancelled };
    enum class Limit { None, PhysicalLine, LogicalLine, LineCount };
    using Sink = bool (*)(const uint8_t*, size_t, void*);

    IcsLogicalLines() = default;
    IcsLogicalLines(const IcsLogicalLines&) = delete;
    IcsLogicalLines& operator=(const IcsLogicalLines&) = delete;
    ~IcsLogicalLines() { release(); }

    Status initialize(uint8_t* buffer, size_t capacity, size_t physicalLimit,
                      size_t logicalLimit, Sink sink, void* context = nullptr) {
        if (_used) return fail(Status::InvalidState);
        _used = true;
        if (!buffer || !ee02_ics_detail::span(buffer, capacity) || !sink ||
            !physicalLimit || !logicalLimit || logicalLimit > capacity)
            return _status = Status::InvalidArgument;
        _buffer = buffer; _capacity = capacity; _physicalLimit = physicalLimit;
        _logicalLimit = logicalLimit; _sink = sink; _context = context;
        return _status = Status::InProgress;
    }

    Status feed(const uint8_t* bytes, size_t count) {
        if (_busy) return fail(Status::InvalidState);
        if (_status == Status::Complete && count) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        if (!ee02_ics_detail::span(bytes, count) ||
            ee02_ics_detail::overlap(bytes, count, _buffer, _capacity))
            return fail(Status::InvalidArgument);
        Call call(_busy);
        for (size_t i = 0; i < count && _status == Status::InProgress; ++i) {
            const uint8_t b = bytes[i]; _hasInput = true;
            if (_state == State::AwaitLF) {
                if (b != '\n') return fail(Status::MalformedLine);
                _state = State::AfterCRLF; _physical = 0;
                continue;
            }
            if (_state == State::AfterCRLF) {
                if (b == ' ' || b == '\t') {
                    _state = State::Collect;
                    if (!physicalByte()) return _status;
                    continue; // Remove exactly one marker; preserve subsequent WSP.
                }
                if (!emit()) return _status;
                _logical = 0; _state = State::Collect;
            }
            if (b == '\r') _state = State::AwaitLF;
            else if (b == '\n') return fail(Status::MalformedLine);
            else {
                if (!physicalByte()) return _status;
                if (_logical == _logicalLimit) {
                    _limit = Limit::LogicalLine; return fail(Status::ResourceLimit);
                }
                _buffer[_logical++] = b;
            }
        }
        return _status;
    }

    // Call only after independent transport/body completeness is established.
    Status finish() {
        if (_busy) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        Call call(_busy);
        if (_hasInput && _state != State::AfterCRLF) return fail(Status::IncompleteLine);
        if (_hasInput && !emit()) return _status;
        return _status = Status::Complete; // Empty body is zero lines, not valid ICS.
    }
    Status cancel() { return _status == Status::InProgress ? fail(Status::Cancelled) : _status; }
    Status status() const { return _status; }
    Limit limit() const { return _limit; }
    uint64_t lineCount() const { return _lines; }
    void release() {
        if (_busy) { fail(Status::InvalidState); return; }
        cancel(); _buffer = nullptr; _capacity = 0; _sink = nullptr; _context = nullptr;
    }

private:
    enum class State { Collect, AwaitLF, AfterCRLF };
    struct Call { bool& busy; explicit Call(bool& b) : busy(b) { busy = true; }
        ~Call() { busy = false; } };
    uint8_t* _buffer = nullptr;
    size_t _capacity = 0, _physicalLimit = 0, _logicalLimit = 0, _physical = 0, _logical = 0;
    Sink _sink = nullptr; void* _context = nullptr;
    uint64_t _lines = 0;
    State _state = State::Collect;
    Status _status = Status::NotInitialized;
    Limit _limit = Limit::None;
    bool _used = false, _hasInput = false, _busy = false;

    Status fail(Status s) {
        if (_status == Status::InProgress || _status == Status::Complete) _status = s;
        return _status;
    }
    bool physicalByte() {
        if (_physical == _physicalLimit) {
            _limit = Limit::PhysicalLine; fail(Status::ResourceLimit); return false;
        }
        ++_physical; return true;
    }
    bool emit() {
        if (_lines == UINT64_MAX) {
            _limit = Limit::LineCount; fail(Status::ResourceLimit); return false;
        }
        if (!_sink(_buffer, _logical, _context)) fail(Status::CallbackFailure);
        if (_status != Status::InProgress) return false;
        ++_lines; return true;
    }
};

#endif
