#ifndef EE02_ICS_STRUCTURE_H
#define EE02_ICS_STRUCTURE_H

#include <cstring>
#include <new>
#include "ics_logical_lines.h"

// Syntax/structure only. All callback views/iterators are borrowed and expire on
// callback return. No aliases, throwing, concurrency, reentrancy or destruction
// in callbacks. Construct inside one workspace withScratch() scope; release or
// destroy before scratch reuse. Span checks cannot prove readable/writable memory.
class IcsStructure {
public:
    enum class Status { NotInitialized, InProgress, StructurallyComplete,
        InvalidArgument, InvalidState, MalformedProperty, InvalidParameters,
        InvalidText, InvalidMetadata, InvalidNesting, MissingRoot,
        IncompleteStructure, UnsupportedProfile, ResourceLimit, Cancelled,
        ConsumerFailure };
    enum class Limit { None, LogicalLine, Parameters, ValuesPerParameter,
        ValuesPerProperty, Depth, ComponentName, PropertiesPerComponent,
        PropertiesPerSource, Components, Calendars, Lines, Scratch };
    enum class Kind : uint8_t { Calendar, Event, Todo, Journal, FreeBusy,
        Timezone, Alarm, Standard, Daylight, Participant, Location, Resource,
        Opaque, Unsupported };
    enum class EventType { BeginComponent, Property, EndComponent };
    struct Limits {
        size_t logicalLine, parameters, valuesPerParameter, valuesPerProperty;
        size_t depth, componentName;
        uint64_t propertiesPerComponent, propertiesPerSource, components, calendars;
    };
    struct View { const uint8_t* data; size_t size; };
    struct EncodedValue { View encoded; bool quoted; };

    // Iterators traverse already validated spans. They interpret delimiters,
    // not RFC 6868 caret encoding or property-specific TEXT escaping.
    class ValueIterator {
    public:
        explicit ValueIterator(View v) : _view(v) {}
        bool next(EncodedValue& out) {
            if (_done) return false;
            size_t i = _next, start = i;
            const bool quoted = i < _view.size && _view.data[i] == '"';
            if (quoted) {
                start = ++i;
                while (i < _view.size && _view.data[i] != '"') ++i;
                out = {{_view.data + start, i - start}, true}; ++i;
            } else {
                while (i < _view.size && _view.data[i] != ',') ++i;
                out = {{_view.data + start, i - start}, false};
            }
            _done = i == _view.size;
            if (!_done) _next = i + 1;
            return true;
        }
    private:
        View _view;
        size_t _next = 0;
        bool _done = false;
    };
    struct Parameter {
        View name, encodedList;
        ValueIterator values() const { return ValueIterator(encodedList); }
    };
    class ParameterIterator {
    public:
        explicit ParameterIterator(View v) : _view(v) {}
        bool next(Parameter& out) {
            if (_next == _view.size) return false;
            size_t i = _next + 1, start = i; // Skip the validated semicolon.
            while (i < _view.size && _view.data[i] != '=') ++i;
            out.name = {_view.data + start, i - start}; start = ++i;
            bool quoted = false;
            while (i < _view.size) {
                const uint8_t b = _view.data[i];
                if (b == '"') quoted = !quoted;
                if (b == ';' && !quoted) break;
                ++i;
            }
            out.encodedList = {_view.data + start, i - start}; _next = i;
            return true;
        }
    private:
        View _view;
        size_t _next = 0;
    };
    struct Property {
        View name, value, encodedParameters;
        size_t parameterCount;
        ParameterIterator parameters() const { return ParameterIterator(encodedParameters); }
    };
    struct Component {
        View name;
        Kind kind;
        size_t depth; // Root depth is 1.
        uint64_t ordinal, parentOrdinal, calendarOrdinal; // Source-local structure, not UID.
    };
    struct Event { EventType type; Component component; Property property; };
    struct Diagnostic { uint64_t line; size_t offset; Limit limit; };
    using Sink = bool (*)(const Event&, void*);

    IcsStructure() = default;
    IcsStructure(const IcsStructure&) = delete;
    IcsStructure& operator=(const IcsStructure&) = delete;
    IcsStructure(IcsStructure&&) = delete;
    IcsStructure& operator=(IcsStructure&&) = delete;
    ~IcsStructure() { release(); }

    // Includes worst-case natural-alignment padding; no default limits or owner.
    static Status scratchBytes(const Limits& l, size_t& bytes) {
        bytes = 0;
        if (!l.logicalLine || !l.parameters || !l.valuesPerParameter ||
            !l.valuesPerProperty || !l.depth || !l.componentName ||
            !l.propertiesPerComponent || !l.propertiesPerSource ||
            !l.components || !l.calendars || l.componentName > UINT32_MAX ||
            l.depth > UINT32_MAX / l.componentName ||
            l.depth > static_cast<size_t>(PTRDIFF_MAX) / sizeof(Frame))
            return Status::InvalidArgument;
        const size_t frames = l.depth * sizeof(Frame), names = l.depth * l.componentName;
        const size_t maximum = static_cast<size_t>(PTRDIFF_MAX);
        if (names > maximum - frames || alignof(Frame) - 1 > maximum - frames - names)
            return Status::InvalidArgument;
        bytes = frames + names + alignof(Frame) - 1;
        return Status::InProgress;
    }

    Status initialize(uint8_t* scratch, size_t capacity, const Limits& limits,
                      Sink sink, void* context = nullptr) {
        if (_busy || _used) return fail(Status::InvalidState);
        _used = true;
        size_t required = 0;
        if (!scratch || !sink || !ee02_ics_detail::span(scratch, capacity) ||
            scratchBytes(limits, required) != Status::InProgress || capacity < required)
            return _status = Status::InvalidArgument;
        const uintptr_t address = reinterpret_cast<uintptr_t>(scratch);
        const size_t padding = (alignof(Frame) - address % alignof(Frame)) % alignof(Frame);
        _storage = scratch; _capacity = capacity; _limits = limits;
        _frames = scratch + padding;
        _names = _frames + limits.depth * sizeof(Frame);
        _sink = sink; _context = context;
        return _status = Status::InProgress;
    }

    Status consume(const uint8_t* line, size_t size) {
        if (_busy) return fail(Status::InvalidState);
        if (_status == Status::StructurallyComplete) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        Call call(_busy);
        if (_lines == UINT64_MAX) return exhausted(Limit::Lines);
        ++_lines; _diagnostic = {_lines, 0, Limit::None};
        if (!ee02_ics_detail::span(line, size) ||
            ee02_ics_detail::overlap(line, size, _storage, _capacity))
            return fail(Status::InvalidArgument);
        if (size > _limits.logicalLine) return exhausted(Limit::LogicalLine);
        Property p = {};
        if (!parse(line, size, p)) return _status;
        if (identifier(p.name, "BEGIN") || identifier(p.name, "END")) {
            if (p.parameterCount || !token(p.value)) return fail(Status::InvalidNesting);
            return identifier(p.name, "BEGIN") ? begin(p.value) : end(p.value);
        }
        if (!_depth) return fail(_calendars ? Status::InvalidNesting : Status::MissingRoot);
        Frame& f = frame(_depth - 1);
        if (f.children && f.kind != Kind::Timezone && f.kind != Kind::Opaque)
            return fail(Status::InvalidNesting);
        if (f.properties == _limits.propertiesPerComponent)
            return exhausted(Limit::PropertiesPerComponent);
        if (_properties == _limits.propertiesPerSource)
            return exhausted(Limit::PropertiesPerSource);
        if (f.kind == Kind::Calendar && !metadata(f, p)) return _status;
        ++f.properties; ++_properties;
        emit(EventType::Property, p);
        return _status;
    }

    // Caller must first establish ingestion Complete. No semantic assertion.
    Status finish() {
        if (_busy) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        if (_depth) return fail(Status::IncompleteStructure);
        if (!_calendars) return fail(Status::MissingRoot);
        return _status = Status::StructurallyComplete;
    }
    Status cancel() {
        if (_busy) return fail(Status::InvalidState);
        return _status == Status::InProgress ? fail(Status::Cancelled) : _status;
    }
    Status status() const { return _status; }
    Diagnostic diagnostic() const { return _diagnostic; }
    uint64_t lineCount() const { return _lines; }
    uint64_t propertyCount() const { return _properties; }
    uint64_t componentCount() const { return _components; }
    uint64_t calendarCount() const { return _calendars; }
    void release() {
        if (_busy) { fail(Status::InvalidState); return; }
        cancel();
        while (_depth) frame(--_depth).~Frame();
        _storage = _frames = _names = nullptr; _capacity = _nameUsed = 0;
        _sink = nullptr; _context = nullptr;
    }
    static bool lineSink(const uint8_t* p, size_t n, void* context) {
        return context && static_cast<IcsStructure*>(context)->consume(p, n) == Status::InProgress;
    }
    static bool identifier(View v, const char* literal) {
        if (!literal || !ee02_ics_detail::span(v.data, v.size)) return false;
        if (v.size != std::strlen(literal)) return false;
        for (size_t i = 0; i < v.size; ++i)
            if (upper(v.data[i]) != upper(static_cast<uint8_t>(literal[i]))) return false;
        return true;
    }

private:
    struct Frame {
        uint64_t properties, children, ordinal;
        uint32_t nameOffset, nameSize;
        Kind kind;
        uint8_t flags, group;
    };
    struct Call { bool& busy; explicit Call(bool& b) : busy(b) { busy = true; }
        ~Call() { busy = false; } };
    Limits _limits = {};
    Diagnostic _diagnostic = {0, 0, Limit::None};
    uint8_t* _storage = nullptr; uint8_t* _frames = nullptr; uint8_t* _names = nullptr;
    size_t _capacity = 0, _depth = 0;
    uint32_t _nameUsed = 0;
    uint64_t _lines = 0, _properties = 0, _components = 0, _calendars = 0;
    Sink _sink = nullptr; void* _context = nullptr;
    Status _status = Status::NotInitialized;
    bool _busy = false, _used = false;

    Frame& frame(size_t index) { return *reinterpret_cast<Frame*>(_frames + index * sizeof(Frame)); }
    View name(const Frame& f) const { return {_names + f.nameOffset, f.nameSize}; }
    static uint8_t upper(uint8_t b) { return b >= 'a' && b <= 'z' ? b - ('a' - 'A') : b; }
    static bool letter(uint8_t b) {
        return (b >= 'A' && b <= 'Z') || (b >= 'a' && b <= 'z') ||
            (b >= '0' && b <= '9') || b == '-';
    }
    static bool token(View v) {
        if (!v.size) return false;
        for (size_t i = 0; i < v.size; ++i) if (!letter(v.data[i])) return false;
        return true;
    }
    Status fail(Status s) {
        if (_status == Status::InProgress || _status == Status::StructurallyComplete)
            _status = s;
        return _status;
    }
    Status exhausted(Limit l) { _diagnostic.limit = l; return fail(Status::ResourceLimit); }
    bool at(Status s, size_t offset) { _diagnostic.offset = offset; fail(s); return false; }

    bool text(const uint8_t* p, size_t n) {
        for (size_t i = 0; i < n;) {
            const size_t start = i; const uint8_t b = p[i++];
            if (b < 0x80) {
                if ((b < 0x20 && b != '\t') || b == 0x7f) return at(Status::InvalidText, start);
                continue;
            }
            size_t extra = 0; uint32_t cp = 0, minimum = 0;
            if (b >= 0xc2 && b <= 0xdf) { extra = 1; cp = b & 0x1f; minimum = 0x80; }
            else if (b >= 0xe0 && b <= 0xef) { extra = 2; cp = b & 0x0f; minimum = 0x800; }
            else if (b >= 0xf0 && b <= 0xf4) { extra = 3; cp = b & 0x07; minimum = 0x10000; }
            else return at(Status::InvalidText, start);
            if (extra > n - i) return at(Status::InvalidText, start);
            while (extra--) {
                const uint8_t c = p[i++];
                if ((c & 0xc0) != 0x80) return at(Status::InvalidText, i - 1);
                cp = (cp << 6) | (c & 0x3f);
            }
            if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff))
                return at(Status::InvalidText, start);
        }
        return true;
    }
    bool parse(const uint8_t* p, size_t n, Property& out) {
        if (!text(p, n)) return false;
        size_t i = 0;
        while (i < n && letter(p[i])) ++i;
        if (!i || i == n || (p[i] != ';' && p[i] != ':'))
            return at(Status::MalformedProperty, i);
        out.name = {p, i}; const size_t header = i;
        size_t totalValues = 0;
        while (i < n && p[i] == ';') {
            if (out.parameterCount == _limits.parameters) { exhausted(Limit::Parameters); return false; }
            ++out.parameterCount; size_t start = ++i;
            while (i < n && letter(p[i])) ++i;
            if (start == i || i == n || p[i] != '=') return at(Status::InvalidParameters, i);
            ++i; size_t values = 0;
            for (;;) {
                if (values == _limits.valuesPerParameter) { exhausted(Limit::ValuesPerParameter); return false; }
                if (totalValues == _limits.valuesPerProperty) { exhausted(Limit::ValuesPerProperty); return false; }
                ++values; ++totalValues;
                if (i < n && p[i] == '"') {
                    ++i;
                    while (i < n && p[i] != '"') ++i;
                    if (i == n) return at(Status::InvalidParameters, i);
                    ++i;
                    if (i < n && p[i] != ',' && p[i] != ';' && p[i] != ':')
                        return at(Status::InvalidParameters, i);
                } else {
                    while (i < n && p[i] != ',' && p[i] != ';' && p[i] != ':') {
                        if (p[i] == '"') return at(Status::InvalidParameters, i);
                        ++i;
                    }
                }
                if (i == n || p[i] != ',') break;
                ++i;
            }
        }
        if (i == n || p[i] != ':') return at(Status::MalformedProperty, i);
        out.encodedParameters = {p + header, i - header};
        out.value = {p + i + 1, n - i - 1}; return true;
    }
    static Kind classify(View v) {
        const char* const names[] = {"VCALENDAR", "VEVENT", "VTODO", "VJOURNAL", "VFREEBUSY",
            "VTIMEZONE", "VALARM", "STANDARD", "DAYLIGHT", "PARTICIPANT", "VLOCATION", "VRESOURCE"};
        for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
            if (identifier(v, names[i])) return static_cast<Kind>(i);
        if (identifier(v, "VAVAILABILITY") || identifier(v, "AVAILABLE")) return Kind::Unsupported;
        return Kind::Opaque;
    }
    // Zero means unordered; negative means a prohibited known relationship.
    static int group(Kind parent, Kind child) {
        if (parent == Kind::Calendar)
            return child <= Kind::Timezone || child == Kind::Opaque ? 0 : -1;
        if (parent == Kind::Opaque) return child == Kind::Opaque ? 0 : -1;
        if (parent == Kind::Timezone) return child == Kind::Standard || child == Kind::Daylight ? 0 : -1;
        if (parent == Kind::Event || parent == Kind::Todo) {
            if (child == Kind::Alarm) return 1;
        }
        if (parent == Kind::Event || parent == Kind::Todo || parent == Kind::Journal || parent == Kind::FreeBusy) {
            if (child == Kind::Participant) return 2;
            if (child == Kind::Location) return 3;
            if (child == Kind::Resource) return 4;
        }
        if (parent == Kind::Participant) {
            if (child == Kind::Location) return 3;
            if (child == Kind::Resource) return 4;
        }
        return -1;
    }
    Status begin(View v) {
        const Kind kind = classify(v);
        if (kind == Kind::Unsupported) return fail(Status::UnsupportedProfile);
        if (!_depth && kind != Kind::Calendar)
            return fail(_calendars ? Status::InvalidNesting : Status::MissingRoot);
        if (_depth && kind == Kind::Calendar) return fail(Status::InvalidNesting);
        int rank = 0;
        if (_depth) {
            const Frame& parent = frame(_depth - 1);
            if ((parent.kind == Kind::Opaque && kind != Kind::Opaque) ||
                (kind == Kind::Opaque && parent.kind != Kind::Opaque && parent.kind != Kind::Calendar))
                return fail(Status::UnsupportedProfile);
            rank = group(parent.kind, kind);
            if (rank < 0 || rank < parent.group) return fail(Status::InvalidNesting);
        }
        if (_depth == _limits.depth) return exhausted(Limit::Depth);
        if (v.size > _limits.componentName) return exhausted(Limit::ComponentName);
        if (_components == _limits.components) return exhausted(Limit::Components);
        if (!_depth && _calendars == _limits.calendars) return exhausted(Limit::Calendars);
        const size_t namesCapacity = _limits.depth * _limits.componentName;
        if (v.size > namesCapacity - _nameUsed) return exhausted(Limit::Scratch);
        if (!_depth) ++_calendars;
        else {
            Frame& parent = frame(_depth - 1);
            if (parent.children == UINT64_MAX) return exhausted(Limit::Components);
            ++parent.children; parent.group = static_cast<uint8_t>(rank);
        }
        new (_frames + _depth * sizeof(Frame)) Frame{0, 0, ++_components, _nameUsed,
            static_cast<uint32_t>(v.size), kind, 0, 0};
        std::memcpy(_names + _nameUsed, v.data, v.size);
        _nameUsed += static_cast<uint32_t>(v.size); ++_depth;
        emit(EventType::BeginComponent, Property{}); return _status;
    }
    Status end(View v) {
        if (!_depth) return fail(Status::InvalidNesting);
        Frame& f = frame(_depth - 1);
        const View current = name(f);
        if (v.size != current.size) return fail(Status::InvalidNesting);
        for (size_t i = 0; i < v.size; ++i)
            if (upper(v.data[i]) != upper(current.data[i])) return fail(Status::InvalidNesting);
        if (f.kind == Kind::Calendar && ((f.flags & 3) != 3 || !f.children))
            return fail(Status::InvalidMetadata);
        if ((f.kind == Kind::Timezone && !f.children) ||
            (f.kind == Kind::Opaque && !f.children && !f.properties))
            return fail(Status::InvalidNesting);
        if (emit(EventType::EndComponent, Property{})) {
            _nameUsed = f.nameOffset; f.~Frame(); --_depth;
        }
        return _status;
    }
    bool metadata(Frame& f, const Property& p) {
        uint8_t flag = 0;
        if (identifier(p.name, "VERSION")) {
            flag = 1;
            if (!identifier(p.value, "2.0") && !identifier(p.value, "2.0;2.0")) {
                fail(Status::UnsupportedProfile); return false;
            }
        } else if (identifier(p.name, "PRODID")) flag = 2;
        else if (identifier(p.name, "METHOD")) flag = 4;
        else if (identifier(p.name, "CALSCALE")) flag = 8;
        if (f.flags & flag) { fail(Status::InvalidMetadata); return false; }
        f.flags |= flag; return true;
    }
    bool emit(EventType type, const Property& p) {
        const Frame& f = frame(_depth - 1);
        const Component component = {name(f), f.kind, _depth, f.ordinal,
            _depth > 1 ? frame(_depth - 2).ordinal : 0, _calendars};
        const Event event = {type, component, p};
        if (!_sink(event, _context)) fail(Status::ConsumerFailure);
        return _status == Status::InProgress;
    }
};

#endif
