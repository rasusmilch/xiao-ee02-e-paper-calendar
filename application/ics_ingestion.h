#ifndef EE02_ICS_INGESTION_H
#define EE02_ICS_INGESTION_H

#include <cstring>
#include "ics_logical_lines.h"

// One raw response, no socket/request/redirect following. Borrow this object's
// scratch for its whole lifetime inside withScratch(). No heap/PSRAM allocation.
// Input spans must not overlap scratch. No concurrent/reentrant calls or escaped
// sink/Location views. Clocks and sinks must return normally and promptly.
class IcsIngestion {
public:
    enum class Status { NotInitialized, InProgress, Complete, RedirectRequired,
        InvalidArgument, InvalidState, HttpStatusFailure, TransportFailure,
        MalformedFraming, IncompleteFraming, UnsupportedFraming, UnsupportedEncoding,
        UnexpectedBytes, MalformedLine, IncompleteLine, ResourceLimit,
        CallbackFailure, Cancelled, InvalidClock };
    enum class Limit { None, ReceivedBytes, BodyBytes, Headers, HeaderFields,
        MetadataLine, ChunkMetadata, Trailers, TrailerFields, Location,
        PhysicalLine, LogicalLine, ProcessingTime, StallTime };
    enum class Transport { WouldBlock, Closed, Error, Cancelled };
    struct Limits {
        // Bytes include CRLF; physical/logical limits count content octets only
        // (a removed fold marker still counts toward its physical line).
        uint64_t receivedBytes, bodyBytes;
        size_t headerBytes, headerFields, metadataLine, chunkMetadata;
        size_t trailerBytes, trailerFields, location, physicalLine, logicalLine;
        // Zero processingMs explicitly disables only the total elapsed budget.
        // stalledMs remains mandatory; progress/clock/framing checks still run.
        uint64_t processingMs, stalledMs;
    };
    struct View { const uint8_t* data; size_t size; };
    using Clock = uint64_t (*)(void*); // Monotonic milliseconds, not UTC/NTP.
    using Sink = IcsLogicalLines::Sink;

    IcsIngestion() = default;
    IcsIngestion(const IcsIngestion&) = delete;
    IcsIngestion& operator=(const IcsIngestion&) = delete;
    ~IcsIngestion() { release(); }

    // Clock-free storage/limit admission shared with acquisition. InProgress
    // means the configuration fits, not that a session or timer has started.
    // The positive logical-line limit is also the unfolder's slice capacity;
    // whole-span admission below proves that nested slice is a valid subspan.
    static Status validateConfiguration(const uint8_t* scratch, size_t capacity,
                                        const Limits& limits, Sink sink, Clock clock) {
        if (!scratch || !ee02_ics_detail::span(scratch, capacity) || !sink || !clock ||
            !limits.receivedBytes || !limits.headerBytes || !limits.headerFields ||
            !limits.metadataLine || !limits.chunkMetadata || !limits.trailerBytes ||
            !limits.trailerFields || !limits.location || !limits.physicalLine ||
            !limits.logicalLine || !limits.stalledMs ||
            limits.chunkMetadata > limits.metadataLine || limits.metadataLine > capacity ||
            limits.location > capacity - limits.metadataLine ||
            limits.logicalLine > capacity - limits.metadataLine - limits.location)
            return Status::InvalidArgument;
        return Status::InProgress;
    }

    // Layout is [metadata][Location][logical line], all byte-aligned. No defaults.
    Status initialize(uint8_t* scratch, size_t capacity, const Limits& limits,
                      Sink sink, void* sinkContext, Clock clock, void* clockContext) {
        if (_used) return fail(Status::InvalidState);
        _used = true;
        if (validateConfiguration(scratch, capacity, limits, sink, clock) != Status::InProgress)
            return _status = Status::InvalidArgument;
        _scratch = scratch; _capacity = capacity; _limits = limits;
        _meta = scratch; _location = scratch + limits.metadataLine;
        const auto initialized = _lines.initialize(_location + limits.location,
            limits.logicalLine, limits.physicalLine, limits.logicalLine, sink, sinkContext);
        if (initialized != IcsLogicalLines::Status::InProgress) return _status = Status::InvalidArgument;
        _clock = clock; _clockContext = clockContext;
        _started = _lastClock = _lastData = clock(clockContext);
        return _status = Status::InProgress;
    }

    Status feed(const uint8_t* bytes, size_t count) {
        if (_busy) return fail(Status::InvalidState);
        if (_status == Status::Complete && count) return fail(Status::UnexpectedBytes);
        if (_status != Status::InProgress) return _status;
        if (!ee02_ics_detail::span(bytes, count) ||
            ee02_ics_detail::overlap(bytes, count, _scratch, _capacity))
            return fail(Status::InvalidArgument);
        Call call(_busy);
        if (!checkTime(true)) return _status;
        if (count > _limits.receivedBytes - _received) return exhausted(Limit::ReceivedBytes);
        _received += count; // Includes all supplied bytes, even discarded redirect body.
        if (count) _lastData = _lastClock;
        for (size_t i = 0; i < count && _status == Status::InProgress; ++i) {
            if ((i & 255U) == 0 && !checkTime()) return _status;
            const uint8_t b = bytes[i];
            if (_phase == Phase::Redirect) break; // Discard body in this read; caller closes source.
            if (_phase == Phase::Boundary) return fail(Status::UnexpectedBytes);
            if (_phase == Phase::LengthBody || _phase == Phase::ChunkBody) {
                if (_decoded == _limits.bodyBytes) return exhausted(Limit::BodyBytes);
                ++_decoded;
                if (!logical(_lines.feed(&b, 1)) || !checkTime()) return _status;
                if (--_remaining == 0)
                    _phase = _phase == Phase::LengthBody ? Phase::Boundary : Phase::ChunkCR;
            } else if (_phase == Phase::ChunkCR) {
                if (b != '\r') return fail(Status::MalformedFraming);
                _phase = Phase::ChunkLF;
            } else if (_phase == Phase::ChunkLF) {
                if (b != '\n') return fail(Status::MalformedFraming);
                _phase = Phase::ChunkSize;
            } else if (!metadataByte(b)) return _status;
        }
        if (_status == Status::InProgress && checkTime()) {
            if (count) _lastData = _lastClock; // Read waiting begins after synchronous processing.
            if (_phase == Phase::Redirect) _status = Status::RedirectRequired;
        }
        return _status; // Boundary is not success until explicit finish().
    }

    // Ordinary success requires the known body boundary plus unfolded EOF.
    // A caller must submit observed surplus bytes before this finalization.
    Status finish() {
        if (_busy) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        Call call(_busy);
        if (!checkTime()) return _status;
        if (_phase != Phase::Boundary) return fail(Status::IncompleteFraming);
        if (!logical(_lines.finish()) || !checkTime()) return _status;
        return _status = Status::Complete; // Not an assertion of valid/empty ICS.
    }
    Status transport(Transport event) {
        if (_busy) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        if (event == Transport::Cancelled) return fail(Status::Cancelled);
        if (event == Transport::Error) return fail(Status::TransportFailure);
        if (event == Transport::Closed) return finish(); // Only known framing can complete.
        if (event != Transport::WouldBlock) return fail(Status::InvalidArgument);
        Call call(_busy); checkTime(true); return _status;
    }
    Status status() const { return _status; }
    // Observed declared framing boundary only. finish() must still finalize
    // unfolded lines; received surplus bytes must still be submitted to feed().
    bool bodyBoundaryReached() const {
        return _scratch && (_status == Status::InProgress || _status == Status::Complete) && _phase == Phase::Boundary;
    }
    Limit limit() const { return _limit; }
    uint16_t httpStatus() const { return _httpStatus; }
    uint64_t receivedBytes() const { return _received; }
    uint64_t decodedBytes() const { return _decoded; }
    uint64_t lineCount() const { return _lines.lineCount(); }
    View redirectLocation() const {
        return _status == Status::RedirectRequired && _scratch ? View{_location, _locationSize} : View{nullptr, 0};
    }
    void release() {
        if (_busy) { fail(Status::InvalidState); return; }
        if (_status == Status::InProgress) fail(Status::Cancelled);
        _lines.release(); _scratch = _meta = _location = nullptr;
        _capacity = 0; _clock = nullptr; _clockContext = nullptr;
    }

private:
    enum class Phase { StatusLine, Headers, LengthBody, ChunkSize, ChunkBody,
        ChunkCR, ChunkLF, Trailers, Boundary, Redirect };
    struct Call { bool& busy; explicit Call(bool& b) : busy(b) { busy = true; }
        ~Call() { busy = false; } };
    Limits _limits = {};
    IcsLogicalLines _lines;
    uint8_t* _scratch = nullptr; uint8_t* _meta = nullptr; uint8_t* _location = nullptr;
    size_t _capacity = 0, _metaSize = 0, _locationSize = 0;
    size_t _headers = 0, _headerFields = 0, _trailers = 0, _trailerFields = 0;
    uint64_t _received = 0, _decoded = 0, _length = 0, _remaining = 0;
    uint64_t _started = 0, _lastClock = 0, _lastData = 0;
    Clock _clock = nullptr; void* _clockContext = nullptr;
    uint16_t _httpStatus = 0;
    Phase _phase = Phase::StatusLine;
    Status _status = Status::NotInitialized;
    Limit _limit = Limit::None;
    bool _used = false, _busy = false, _cr = false, _http11 = false;
    bool _hasLength = false, _hasTransfer = false, _hasEncoding = false, _hasLocation = false;

    Status fail(Status s) {
        if (_status == Status::InProgress || _status == Status::Complete) _status = s;
        return _status;
    }
    Status exhausted(Limit l) { _limit = l; return fail(Status::ResourceLimit); }
    bool checkTime(bool checkStall = false) {
        const uint64_t now = _clock(_clockContext);
        if (now < _lastClock) { fail(Status::InvalidClock); return false; }
        _lastClock = now;
        if (_limits.processingMs && now - _started > _limits.processingMs) { exhausted(Limit::ProcessingTime); return false; }
        if (checkStall && now - _lastData > _limits.stalledMs) { exhausted(Limit::StallTime); return false; }
        return _status == Status::InProgress;
    }
    bool logical(IcsLogicalLines::Status s) {
        using LS = IcsLogicalLines::Status;
        if (_status != Status::InProgress) return false;
        if (s == LS::InProgress || s == LS::Complete) return true;
        if (s == LS::ResourceLimit) {
            exhausted(_lines.limit() == IcsLogicalLines::Limit::PhysicalLine ? Limit::PhysicalLine : Limit::LogicalLine);
        } else fail(s == LS::MalformedLine ? Status::MalformedLine :
                    s == LS::IncompleteLine ? Status::IncompleteLine :
                    s == LS::CallbackFailure ? Status::CallbackFailure : Status::InvalidState);
        return false;
    }
    static bool token(uint8_t b) {
        return (b >= '0' && b <= '9') || (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
            (b != 0 && std::strchr("!#$%&'*+-.^_`|~", b) != nullptr);
    }
    static bool equal(const uint8_t* p, size_t n, const char* literal) {
        if (n != std::strlen(literal)) return false;
        for (size_t i = 0; i < n; ++i) {
            const uint8_t b = p[i] >= 'A' && p[i] <= 'Z' ? p[i] + ('a' - 'A') : p[i];
            if (b != static_cast<uint8_t>(literal[i])) return false;
        }
        return true;
    }
    static unsigned hex(uint8_t b) {
        if (b >= '0' && b <= '9') return b - '0';
        if (b >= 'a' && b <= 'f') return b - 'a' + 10;
        if (b >= 'A' && b <= 'F') return b - 'A' + 10;
        return 16;
    }
    static bool decimal(const uint8_t* p, size_t n, uint64_t& value) {
        if (!n) return false;
        value = 0;
        for (size_t i = 0; i < n; ++i) {
            if (p[i] < '0' || p[i] > '9') return false;
            const unsigned digit = p[i] - '0';
            if (value > (UINT64_MAX - digit) / 10) return false;
            value = value * 10 + digit;
        }
        return true;
    }
    bool metadataByte(uint8_t b) {
        if (_phase == Phase::StatusLine || _phase == Phase::Headers) {
            if (_headers == _limits.headerBytes) { exhausted(Limit::Headers); return false; }
            ++_headers;
        } else if (_phase == Phase::Trailers) {
            if (_trailers == _limits.trailerBytes) { exhausted(Limit::Trailers); return false; }
            ++_trailers;
        }
        if (_cr) {
            if (b != '\n') { fail(Status::MalformedFraming); return false; }
            _cr = false;
            const bool ok = metadataLine(); _metaSize = 0;
            return ok;
        }
        if (b == '\r') { _cr = true; return true; }
        if (b == '\n' || b == 0 || (b < 32 && b != '\t') || b == 127) {
            fail(Status::MalformedFraming); return false;
        }
        const size_t bound = _phase == Phase::ChunkSize ? _limits.chunkMetadata : _limits.metadataLine;
        if (_metaSize == bound) {
            exhausted(_phase == Phase::ChunkSize ? Limit::ChunkMetadata : Limit::MetadataLine); return false;
        }
        _meta[_metaSize++] = b; return true;
    }
    bool metadataLine() {
        if (_phase == Phase::StatusLine) {
            if (_metaSize < 13 || std::memcmp(_meta, "HTTP/1.", 7) ||
                (_meta[7] != '0' && _meta[7] != '1') || _meta[8] != ' ' || _meta[12] != ' ' ||
                _meta[9] < '1' || _meta[9] > '5' || _meta[10] < '0' || _meta[10] > '9' ||
                _meta[11] < '0' || _meta[11] > '9') {
                fail(Status::MalformedFraming); return false;
            }
            _http11 = _meta[7] == '1';
            _httpStatus = static_cast<uint16_t>((_meta[9]-'0')*100 + (_meta[10]-'0')*10 + _meta[11]-'0');
            _phase = Phase::Headers; return true;
        }
        if (_phase == Phase::ChunkSize) return chunkSize();
        if (!_metaSize) {
            if (_phase == Phase::Trailers) { _phase = Phase::Boundary; return true; }
            return headersComplete();
        }
        const bool trailer = _phase == Phase::Trailers;
        size_t& fields = trailer ? _trailerFields : _headerFields;
        if (fields == (trailer ? _limits.trailerFields : _limits.headerFields)) {
            exhausted(trailer ? Limit::TrailerFields : Limit::HeaderFields); return false;
        }
        ++fields;
        size_t colon = 0;
        while (colon < _metaSize && _meta[colon] != ':') {
            if (!token(_meta[colon])) { fail(Status::MalformedFraming); return false; }
            ++colon;
        }
        if (!colon || colon == _metaSize) { fail(Status::MalformedFraming); return false; }
        size_t start = colon + 1, end = _metaSize;
        while (start < end && (_meta[start] == ' ' || _meta[start] == '\t')) ++start;
        while (end > start && (_meta[end-1] == ' ' || _meta[end-1] == '\t')) --end;
        const uint8_t* value = _meta + start; const size_t n = end - start;
        if (trailer) {
            // Trailer fields must not change framing, routing, authentication or
            // payload processing. Optional unknown metadata is syntax-checked only.
            static const char* const forbidden[] = {"content-length","transfer-encoding","content-encoding",
                "content-type","content-range","host","connection","trailer","te","location",
                "authorization","proxy-authorization","www-authenticate","proxy-authenticate",
                "authentication-info","proxy-authentication-info","cookie","set-cookie",
                "cache-control","max-forwards","range"};
            for (const char* name : forbidden) if (equal(_meta, colon, name)) {
                fail(Status::MalformedFraming); return false;
            }
        } else if (equal(_meta, colon, "content-length")) {
            if (_hasLength || !decimal(value, n, _length)) { fail(Status::MalformedFraming); return false; }
            _hasLength = true;
        } else if (equal(_meta, colon, "transfer-encoding")) {
            if (_hasTransfer) { fail(Status::MalformedFraming); return false; }
            _hasTransfer = true;
            if (!_http11 || !equal(value, n, "chunked")) { fail(Status::UnsupportedFraming); return false; }
        } else if (equal(_meta, colon, "content-encoding")) {
            if (_hasEncoding) { fail(Status::MalformedFraming); return false; }
            _hasEncoding = true;
            if (!equal(value, n, "identity")) { fail(Status::UnsupportedEncoding); return false; }
        } else if (equal(_meta, colon, "location")) {
            if (_hasLocation || !n) { fail(Status::MalformedFraming); return false; }
            if (n > _limits.location) { exhausted(Limit::Location); return false; }
            // Opaque untrusted URI-reference octets, not URL resolution/trust.
            for (size_t i = 0; i < n; ++i) {
                if (value[i] <= 32 || value[i] >= 127) { fail(Status::MalformedFraming); return false; }
                if (value[i] == '%') {
                    if (n-i < 3 || hex(value[i+1]) == 16 || hex(value[i+2]) == 16) {
                        fail(Status::MalformedFraming); return false;
                    }
                    i += 2;
                }
            }
            std::memcpy(_location, value, n); _locationSize = n; _hasLocation = true;
        }
        return true;
    }
    bool headersComplete() {
        if (_hasLength && _hasTransfer) { fail(Status::MalformedFraming); return false; }
        if (_httpStatus == 301 || _httpStatus == 302 || _httpStatus == 303 ||
            _httpStatus == 307 || _httpStatus == 308) {
            if (!_hasLocation) { fail(Status::MalformedFraming); return false; }
            _phase = Phase::Redirect; return true; // Handoff only; no URL/trust policy.
        }
        if (_httpStatus != 200) { fail(Status::HttpStatusFailure); return false; }
        if (_hasTransfer) _phase = Phase::ChunkSize;
        else if (_hasLength) {
            if (_length > _limits.bodyBytes) { exhausted(Limit::BodyBytes); return false; }
            _remaining = _length; _phase = _length ? Phase::LengthBody : Phase::Boundary;
        } else { fail(Status::UnsupportedFraming); return false; }
        return true;
    }
    bool chunkSize() {
        size_t i = 0; uint64_t size = 0;
        while (i < _metaSize && hex(_meta[i]) != 16) {
            const unsigned digit = hex(_meta[i++]);
            if (size > (UINT64_MAX-digit)/16) { fail(Status::MalformedFraming); return false; }
            size = size*16+digit;
        }
        if (!i || !extensions(i)) { fail(Status::MalformedFraming); return false; }
        if (size > _limits.bodyBytes - _decoded) { exhausted(Limit::BodyBytes); return false; }
        _remaining = size; _phase = size ? Phase::ChunkBody : Phase::Trailers;
        return true;
    }
    bool extensions(size_t i) const {
        // RFC 9112 chunk-ext: BWS ; BWS token [ BWS = BWS (token / quoted-string) ].
        while (i < _metaSize) {
            while (i < _metaSize && (_meta[i]==' ' || _meta[i]=='\t')) ++i;
            if (i == _metaSize || _meta[i++] != ';') return false;
            while (i < _metaSize && (_meta[i]==' ' || _meta[i]=='\t')) ++i;
            const size_t name = i;
            while (i < _metaSize && token(_meta[i])) ++i;
            if (i == name) return false;
            const size_t afterName = i;
            while (i < _metaSize && (_meta[i]==' ' || _meta[i]=='\t')) ++i;
            if (i < _metaSize && _meta[i]=='=') {
                ++i; while (i < _metaSize && (_meta[i]==' ' || _meta[i]=='\t')) ++i;
                if (i == _metaSize) return false;
                if (_meta[i]=='"') {
                    ++i; bool closed = false;
                    while (i < _metaSize) {
                        const uint8_t b = _meta[i++];
                        if (b=='"') { closed = true; break; }
                        if (b=='\\') { if (i == _metaSize) return false; ++i; }
                    }
                    if (!closed) return false;
                } else {
                    const size_t value = i;
                    while (i < _metaSize && token(_meta[i])) ++i;
                    if (i == value) return false;
                }
            } else i = afterName; // BWS belongs to a following extension, not a suffix.
        }
        return true;
    }
};

#endif
