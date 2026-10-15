#ifndef EE02_ICS_SOURCE_STAGE_H
#define EE02_ICS_SOURCE_STAGE_H

#include <initializer_list>
#include "ics_ingestion.h"
#include "ics_structure.h"

// Selective VEVENT dependency storage, not a calendar normalizer or document.
// Consume only validated IcsStructure callbacks. Construct/use/destroy inside ONE
// withScratch() lifetime, with disjoint ingestion/structure/stage slices. No
// throwing, reentrancy, concurrent access or escaped views. No allocation/free.
class IcsSourceStage {
public:
    using Kind = IcsStructure::Kind;
    using View = IcsStructure::View;
    enum class Status { NotInitialized, InProgress, StagedSuccessfully,
        InvalidArgument, InvalidState, InvalidAssociation, UnsupportedProfile,
        ResourceFailure, UpstreamFailure, Cancelled };
    enum class Disposition : uint8_t { RetainRequired, RetainConditional,
        RetainUnreviewed, IgnoreByDeclaredProjection, RejectUnsupported, ResourceFailure };
    enum class Limit { None, Components, Properties, Pool, ReviewItems, ReviewBytes, Counter };
    enum class Read { Ok, InvalidState, InvalidIndex, ValueOmitted, PayloadProjectedOut };
    enum class Code : uint16_t { Unknown, Uid, Dtstart, Dtend, Duration, RecurrenceId,
        Rrule, Rdate, Exdate, Status, Sequence, Dtstamp, LastModified, Summary,
        Created, Description, Location, Comment, Organizer, Attendee, RequestStatus,
        RelatedTo, Transp, Class, Priority, Categories, Contact, Resources, Geo, Url,
        Version, Prodid, Method, Calscale, Name, Tzid, Tzurl, Tzoffsetfrom, Tzoffsetto,
        Tzname, ParticipantType, CalendarAddress, LocationType, ResourceType,
        Attach, Image, Color, Source, RefreshInterval, StyledDescription,
        StructuredData, Conference, Due, Completed, PercentComplete, Freebusy,
        Action, Trigger, Repeat };
    enum Flag : uint16_t { ProjectedOut = 1, OpaqueProjectedOut = 2, HasReview = 4 };
    enum PropertyFlag : uint8_t { ValueRetained = 1, UnknownParameters = 2 };
    struct Limits { size_t components, properties, poolBytes, reviewItems, reviewBytes; };
    static constexpr uint32_t None = UINT32_MAX;
    struct Component {
        uint64_t ordinal, parentOrdinal, calendarOrdinal;
        uint32_t nameOffset, nameLength, firstProperty, lastProperty, observedProperties;
        uint16_t flags;
        Kind kind;
        uint8_t closed;
    };
    struct Property {
        uint32_t owner, valueOffset, valueLength, parameterOffset, parameterLength;
        uint32_t nameOffset, nameLength, next, parameterCount;
        Code code;
        Disposition disposition;
        uint8_t flags;
    };
    struct Counts {
        uint64_t components, calendars, observedProperties, ignoredOpaqueProperties, reviewItems;
        uint32_t storedProperties, poolBytes, reviewBytes;
    };
    struct Diagnostic {
        uint64_t component;
        Limit limit;
        Disposition disposition;
        IcsIngestion::Status ingestion;
        IcsStructure::Status structure;
    };

    IcsSourceStage() = default;
    IcsSourceStage(const IcsSourceStage&) = delete;
    IcsSourceStage& operator=(const IcsSourceStage&) = delete;
    IcsSourceStage(IcsSourceStage&&) = delete;
    IcsSourceStage& operator=(IcsSourceStage&&) = delete;
    ~IcsSourceStage() { release(); }

    static Status requiredBytes(const Limits& l, size_t& bytes) {
        bytes = 0;
        const size_t maximum = static_cast<size_t>(PTRDIFF_MAX);
        if (!l.components || l.components > UINT32_MAX || l.properties > UINT32_MAX ||
            l.poolBytes > UINT32_MAX || l.reviewItems > UINT32_MAX || l.reviewBytes > l.poolBytes ||
            l.components > maximum / sizeof(Component) || l.properties > maximum / sizeof(Property))
            return Status::InvalidArgument;
        const size_t c = l.components * sizeof(Component), p = l.properties * sizeof(Property);
        const size_t pad = alignof(Component) - 1 + alignof(Property) - 1;
        if (p > maximum - c || l.poolBytes > maximum - c - p ||
            pad > maximum - c - p - l.poolBytes) return Status::InvalidArgument;
        bytes = c + p + l.poolBytes + pad;
        return Status::InProgress;
    }
    Status initialize(uint8_t* storage, size_t capacity, const Limits& limits) {
        if (_busy || _used) return fail(Status::InvalidState);
        _used = true;
        size_t required = 0;
        if (!storage || !ee02_ics_detail::span(storage, capacity) ||
            requiredBytes(limits, required) != Status::InProgress || capacity < required)
            return _status = Status::InvalidArgument;
        _storage = storage; _capacity = capacity; _limits = limits;
        uint8_t* p = aligned(storage, alignof(Component));
        _components = p;
        p += limits.components * sizeof(Component);
        p = aligned(p, alignof(Property)); _properties = p;
        _pool = p + limits.properties * sizeof(Property);
        return _status = Status::InProgress;
    }
    Status consume(const IcsStructure::Event& event) {
        if (_busy || _status == Status::StagedSuccessfully) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        Call call(_busy);
        _diagnostic.component = event.component.ordinal;
        if (!input(event.component.name)) return fail(Status::InvalidAssociation);
        if (event.type == IcsStructure::EventType::BeginComponent) return begin(event.component);
        if (!association(event.component)) return fail(Status::InvalidAssociation);
        if (event.type == IcsStructure::EventType::EndComponent) {
            Component& c = componentAt(_current); c.closed = 1;
            _current = c.parentOrdinal ? static_cast<uint32_t>(c.parentOrdinal - 1) : None;
            --_depth; return _status;
        }
        if (event.type != IcsStructure::EventType::Property) return fail(Status::InvalidAssociation);
        return property(event.property);
    }
    static bool sink(const IcsStructure::Event& event, void* context) {
        return context && static_cast<IcsSourceStage*>(context)->consume(event) == Status::InProgress;
    }
    // Pass the corresponding source pipeline, after both explicit finalizers.
    // Counts check association, not authenticity of arbitrarily substituted objects.
    Status finish(const IcsIngestion& ingestion, const IcsStructure& structure) {
        if (_busy) return fail(Status::InvalidState);
        if (_status != Status::InProgress) return _status;
        _diagnostic.ingestion = ingestion.status(); _diagnostic.structure = structure.status();
        if (ingestion.status() != IcsIngestion::Status::Complete ||
            structure.status() != IcsStructure::Status::StructurallyComplete)
            return fail(Status::UpstreamFailure);
        if (_current != None || _depth || !_counts.calendars ||
            _counts.components != structure.componentCount() || _counts.calendars != structure.calendarCount() ||
            _counts.observedProperties != structure.propertyCount() ||
            _counts.ignoredOpaqueProperties > _counts.observedProperties ||
            _counts.observedProperties - _counts.ignoredOpaqueProperties != _counts.storedProperties)
            return fail(Status::InvalidAssociation);
        return _status = Status::StagedSuccessfully;
    }
    Status cancel() {
        if (_busy) return fail(Status::InvalidState);
        return _status == Status::InProgress ? fail(Status::Cancelled) : _status;
    }
    void release() {
        if (_busy) { fail(Status::InvalidState); return; }
        cancel();
        if (_storage) {
            for (uint64_t i = 0; i < _counts.components; ++i) componentAt(i).~Component();
            for (uint32_t i = 0; i < _counts.storedProperties; ++i) propertyAt(i).~Property();
        }
        _storage = _pool = nullptr; _components = nullptr; _properties = nullptr;
        _capacity = _depth = 0; _current = None;
    }
    Status status() const { return _status; }
    Diagnostic diagnostic() const { return _diagnostic; }
    Counts counts() const { return _counts; }
    const Component* component(size_t index) const {
        return readable() && index < _counts.components ? &componentAt(index) : nullptr;
    }
    const Property* property(size_t index) const {
        return readable() && index < _counts.storedProperties ? &propertyAt(index) : nullptr;
    }
    View componentName(size_t index) const {
        const Component* c = component(index);
        return c ? View{_pool + c->nameOffset, c->nameLength} : View{nullptr, 0};
    }
    Read payload(size_t index) const {
        const Component* c = component(index);
        if (!c) return readable() ? Read::InvalidIndex : Read::InvalidState;
        return c->flags & OpaqueProjectedOut ? Read::PayloadProjectedOut : Read::Ok;
    }
    View propertyName(size_t index) const {
        const Property* p = property(index);
        if (!p) return {nullptr, 0};
        if (p->code == Code::Unknown) return {_pool + p->nameOffset, p->nameLength};
        const char* n = name(p->code); // Canonical identifier has program lifetime.
        return {reinterpret_cast<const uint8_t*>(n), std::strlen(n)};
    }
    View parameters(size_t index) const {
        const Property* p = property(index);
        return p ? View{_pool + p->parameterOffset, p->parameterLength} : View{nullptr, 0};
    }
    Read value(size_t index, View& out) const {
        out = {nullptr, 0}; const Property* p = property(index);
        if (!p) return readable() ? Read::InvalidIndex : Read::InvalidState;
        if (!(p->flags & ValueRetained)) return Read::ValueOmitted;
        out = {_pool + p->valueOffset, p->valueLength}; return Read::Ok;
    }

private:
    struct Call { bool& busy; explicit Call(bool& b) : busy(b) { busy = true; }
        ~Call() { busy = false; } };
    Limits _limits = {};
    Counts _counts = {};
    Diagnostic _diagnostic = {0, Limit::None, Disposition::RetainRequired,
        IcsIngestion::Status::NotInitialized, IcsStructure::Status::NotInitialized};
    uint8_t* _storage = nullptr; uint8_t* _pool = nullptr;
    uint8_t* _components = nullptr; uint8_t* _properties = nullptr;
    size_t _capacity = 0, _depth = 0;
    uint32_t _current = None;
    Status _status = Status::NotInitialized;
    bool _busy = false, _used = false;

    // Byte offsets avoid pointer arithmetic over an array whose objects have
    // not yet been placement-constructed (important for the C++11 baseline).
    Component& componentAt(size_t index) const {
        return *reinterpret_cast<Component*>(_components + index * sizeof(Component));
    }
    Property& propertyAt(size_t index) const {
        return *reinterpret_cast<Property*>(_properties + index * sizeof(Property));
    }
    bool readable() const { return _storage && _status == Status::StagedSuccessfully; }
    static uint8_t* aligned(uint8_t* p, size_t alignment) {
        const uintptr_t a = reinterpret_cast<uintptr_t>(p);
        return p + (alignment - a % alignment) % alignment;
    }
    Status fail(Status s) {
        if (_status == Status::InProgress || _status == Status::StagedSuccessfully) _status = s;
        return _status;
    }
    Status exhausted(Limit l) {
        _diagnostic.limit = l; _diagnostic.disposition = Disposition::ResourceFailure;
        return fail(Status::ResourceFailure);
    }
    bool input(View v) const {
        return ee02_ics_detail::span(v.data, v.size) && v.size <= UINT32_MAX &&
            !ee02_ics_detail::overlap(v.data, v.size, _storage, _capacity);
    }
    static bool equal(View a, View b) {
        if (a.size != b.size) return false;
        for (size_t i = 0; i < a.size; ++i) {
            uint8_t x = a.data[i], y = b.data[i];
            if (x >= 'a' && x <= 'z') x -= 'a' - 'A';
            if (y >= 'a' && y <= 'z') y -= 'a' - 'A';
            if (x != y) return false;
        }
        return true;
    }
    bool association(const IcsStructure::Component& v) const {
        if (_current == None || v.depth != _depth) return false;
        const Component& c = componentAt(_current);
        return !c.closed && c.ordinal == v.ordinal && c.parentOrdinal == v.parentOrdinal &&
            c.calendarOrdinal == v.calendarOrdinal && c.kind == v.kind &&
            equal(v.name, {_pool + c.nameOffset, c.nameLength});
    }
    uint32_t copy(View v) {
        const uint32_t offset = _counts.poolBytes;
        if (v.size) std::memcpy(_pool + offset, v.data, v.size);
        _counts.poolBytes += static_cast<uint32_t>(v.size); return offset;
    }
    Status begin(const IcsStructure::Component& v) {
        if (v.kind == Kind::Unsupported) {
            _diagnostic.disposition = Disposition::RejectUnsupported;
            return fail(Status::UnsupportedProfile);
        }
        if (static_cast<uint8_t>(v.kind) > static_cast<uint8_t>(Kind::Opaque) ||
            v.ordinal != _counts.components + 1 || v.depth != _depth + 1 || !v.name.size)
            return fail(Status::InvalidAssociation);
        uint16_t flags = 0;
        if (_current == None) {
            if (v.kind != Kind::Calendar || v.parentOrdinal || v.calendarOrdinal != _counts.calendars + 1)
                return fail(Status::InvalidAssociation);
        } else {
            const Component& parent = componentAt(_current);
            if (v.parentOrdinal != parent.ordinal || v.calendarOrdinal != parent.calendarOrdinal || v.kind == Kind::Calendar)
                return fail(Status::InvalidAssociation);
            if ((parent.flags & OpaqueProjectedOut) && v.kind != Kind::Opaque) {
                _diagnostic.disposition = Disposition::RejectUnsupported;
                return fail(Status::UnsupportedProfile);
            }
            flags = parent.flags & ProjectedOut;
        }
        if (_counts.components == _limits.components) return exhausted(Limit::Components);
        if (v.name.size > _limits.poolBytes - _counts.poolBytes) return exhausted(Limit::Pool);
        if (v.kind == Kind::Todo || v.kind == Kind::Journal || v.kind == Kind::FreeBusy || v.kind == Kind::Alarm)
            flags |= ProjectedOut;
        if (v.kind == Kind::Opaque) flags |= ProjectedOut | OpaqueProjectedOut;
        const uint32_t offset = copy(v.name);
        _current = static_cast<uint32_t>(_counts.components);
        new (_components + _current * sizeof(Component)) Component{v.ordinal, v.parentOrdinal, v.calendarOrdinal,
            offset, static_cast<uint32_t>(v.name.size), None, None, 0, flags, v.kind, 0};
        ++_counts.components; ++_depth;
        if (v.kind == Kind::Calendar) ++_counts.calendars;
        return _status;
    }
    static const char* name(Code c) {
        static const char* const names[] = {"", "UID", "DTSTART", "DTEND", "DURATION", "RECURRENCE-ID",
            "RRULE", "RDATE", "EXDATE", "STATUS", "SEQUENCE", "DTSTAMP", "LAST-MODIFIED", "SUMMARY",
            "CREATED", "DESCRIPTION", "LOCATION", "COMMENT", "ORGANIZER", "ATTENDEE", "REQUEST-STATUS",
            "RELATED-TO", "TRANSP", "CLASS", "PRIORITY", "CATEGORIES", "CONTACT", "RESOURCES", "GEO", "URL",
            "VERSION", "PRODID", "METHOD", "CALSCALE", "NAME", "TZID", "TZURL", "TZOFFSETFROM", "TZOFFSETTO",
            "TZNAME", "PARTICIPANT-TYPE", "CALENDAR-ADDRESS", "LOCATION-TYPE", "RESOURCE-TYPE",
            "ATTACH", "IMAGE", "COLOR", "SOURCE", "REFRESH-INTERVAL", "STYLED-DESCRIPTION",
            "STRUCTURED-DATA", "CONFERENCE", "DUE", "COMPLETED", "PERCENT-COMPLETE", "FREEBUSY",
            "ACTION", "TRIGGER", "REPEAT"};
        static_assert(sizeof(names) / sizeof(names[0]) == static_cast<size_t>(Code::Repeat) + 1, "property codes");
        return names[static_cast<size_t>(c)];
    }
    static Code code(View v) {
        for (size_t i = 1; i <= static_cast<size_t>(Code::Repeat); ++i)
            if (IcsStructure::identifier(v, name(static_cast<Code>(i)))) return static_cast<Code>(i);
        return Code::Unknown;
    }
    static bool one(Code c, std::initializer_list<Code> set) {
        for (Code x : set) if (c == x) return true;
        return false;
    }
    static bool standard(Kind k, Code c) {
        // Context-specific RFC 5545 core fields; names alone never discard data.
        switch (k) {
        case Kind::Todo:
            return one(c, {Code::Uid,Code::Dtstamp,Code::Class,Code::Completed,Code::Created,Code::Description,
                Code::Dtstart,Code::Geo,Code::LastModified,Code::Location,Code::Organizer,Code::PercentComplete,
                Code::Priority,Code::RecurrenceId,Code::Sequence,Code::Status,Code::Summary,Code::Url,Code::Rrule,
                Code::Due,Code::Duration,Code::Attach,Code::Attendee,Code::Categories,Code::Comment,Code::Contact,
                Code::Exdate,Code::RequestStatus,Code::RelatedTo,Code::Resources,Code::Rdate});
        case Kind::Journal:
            return one(c, {Code::Uid,Code::Dtstamp,Code::Class,Code::Created,Code::Dtstart,Code::LastModified,
                Code::Organizer,Code::RecurrenceId,Code::Sequence,Code::Status,Code::Summary,Code::Url,Code::Rrule,
                Code::Attach,Code::Attendee,Code::Categories,Code::Comment,Code::Contact,Code::Description,
                Code::Exdate,Code::RelatedTo,Code::Rdate,Code::RequestStatus});
        case Kind::FreeBusy:
            return one(c, {Code::Uid,Code::Dtstamp,Code::Contact,Code::Dtstart,Code::Dtend,Code::Organizer,
                Code::Url,Code::Attendee,Code::Comment,Code::Freebusy,Code::RequestStatus});
        case Kind::Alarm:
            return one(c, {Code::Action,Code::Trigger,Code::Duration,Code::Repeat,Code::Description,
                Code::Summary,Code::Attendee,Code::Attach});
        default: return false;
        }
    }
    static Disposition selected(Kind k, Code c, bool projected) {
        using D = Disposition;
        if (standard(k, c)) {
            if (c == Code::Uid || c == Code::RecurrenceId) return D::RetainConditional;
            return D::IgnoreByDeclaredProjection;
        }
        if (k == Kind::Event) {
            if (one(c, {Code::Uid,Code::Dtstart,Code::Dtend,Code::Duration,Code::RecurrenceId,Code::Rrule,
                Code::Rdate,Code::Exdate,Code::Status,Code::Sequence,Code::Dtstamp,Code::LastModified,Code::Summary}))
                return D::RetainRequired;
            if (one(c, {Code::Created,Code::Description,Code::Location,Code::Comment,Code::Organizer,Code::Attendee,
                Code::RequestStatus,Code::RelatedTo,Code::Transp,Code::Class,Code::Priority,Code::Categories,
                Code::Contact,Code::Resources,Code::Geo,Code::Url})) return D::RetainConditional;
        }
        if (k == Kind::Calendar) {
            if (one(c, {Code::Version,Code::Prodid,Code::Method,Code::Calscale})) return D::RetainRequired;
            if (one(c, {Code::Uid,Code::Name,Code::Description})) return D::RetainConditional;
            if (one(c, {Code::Color,Code::Source,Code::RefreshInterval})) return D::IgnoreByDeclaredProjection;
        }
        if (k == Kind::Timezone) {
            if (c == Code::Tzid || c == Code::LastModified) return D::RetainRequired;
            if (c == Code::Tzurl) return D::RetainConditional;
        }
        if (k == Kind::Standard || k == Kind::Daylight) {
            if (one(c, {Code::Dtstart,Code::Tzoffsetfrom,Code::Tzoffsetto,Code::Rrule,Code::Rdate,Code::Exdate}))
                return D::RetainRequired;
            if (c == Code::Tzname || c == Code::Comment) return D::IgnoreByDeclaredProjection;
        }
        bool child = false;
        if (k == Kind::Participant)
            child = one(c, {Code::Uid,Code::ParticipantType,Code::CalendarAddress,Code::Summary,Code::Description,
                Code::Location,Code::Geo,Code::Created,Code::Dtstamp,Code::LastModified,Code::Sequence,Code::Status,
                Code::Priority,Code::Url,Code::Categories,Code::Comment,Code::Contact,Code::RelatedTo,
                Code::Resources,Code::RequestStatus});
        if (k == Kind::Location) child = one(c, {Code::Uid,Code::Name,Code::Description,Code::Geo,Code::LocationType});
        if (k == Kind::Resource) child = one(c, {Code::Uid,Code::Name,Code::Description,Code::Geo,Code::ResourceType});
        if (child) return projected && c != Code::Uid ? D::IgnoreByDeclaredProjection :
            (c == Code::Uid && !projected ? D::RetainRequired : D::RetainConditional);
        return D::RetainUnreviewed;
    }
    static bool knownParameter(Code c, View n) {
        const auto is = [n](const char* x) { return IcsStructure::identifier(n, x); };
        if (is("VALUE")) return true; // Typed legality of explicit values is later validation.
        if (one(c, {Code::Dtstart,Code::Dtend,Code::Rdate,Code::Exdate,Code::RecurrenceId}) && is("TZID")) return true;
        if (c == Code::RecurrenceId && is("RANGE")) return true;
        if (one(c, {Code::Summary,Code::Description,Code::Location,Code::Comment,Code::Contact,
            Code::Resources,Code::Categories,Code::Name,Code::Tzname}) && is("LANGUAGE")) return true;
        if (one(c, {Code::Summary,Code::Description,Code::Location,Code::Comment,Code::Contact,
            Code::Resources}) && is("ALTREP")) return true;
        if (c == Code::Attach || c == Code::Image)
            return is("ENCODING") || is("FMTTYPE") || (c == Code::Image && (is("DISPLAY") || is("ALTREP")));
        if ((c == Code::Organizer || c == Code::Attendee) &&
            (is("CN") || is("DIR") || is("SENT-BY") || is("LANGUAGE"))) return true;
        if (c == Code::Attendee)
            return is("CUTYPE") || is("MEMBER") || is("ROLE") || is("PARTSTAT") || is("RSVP") ||
                is("DELEGATED-TO") || is("DELEGATED-FROM");
        if (c == Code::RelatedTo && is("RELTYPE")) return true;
        if (c == Code::Trigger && is("RELATED")) return true;
        if (c == Code::Freebusy && is("FBTYPE")) return true;
        return false;
    }
    static bool attachmentScope(Kind k, Code c) {
        return c == Code::Attach ? (k == Kind::Event || k == Kind::Todo || k == Kind::Journal || k == Kind::Alarm) :
            c == Code::Image && (k == Kind::Calendar || k == Kind::Event || k == Kind::Todo || k == Kind::Journal);
    }
    static bool omitAttachment(Code c, const IcsStructure::Property& p) {
        if (!p.parameterCount) return c == Code::Attach;
        bool uri = false, binary = false, base64 = false;
        auto parameters = p.parameters(); IcsStructure::Parameter parameter;
        size_t count = 0;
        while (parameters.next(parameter)) {
            ++count; auto values = parameter.values(); IcsStructure::EncodedValue v, extra;
            if (!values.next(v) || values.next(extra)) return false;
            if (IcsStructure::identifier(parameter.name, "VALUE")) {
                if (uri || binary) return false;
                uri = IcsStructure::identifier(v.encoded, "URI");
                binary = IcsStructure::identifier(v.encoded, "BINARY");
                if (!uri && !binary) return false;
            } else if (IcsStructure::identifier(parameter.name, "ENCODING")) {
                if (base64 || !IcsStructure::identifier(v.encoded, "BASE64")) return false;
                base64 = true;
            } else return false;
        }
        return count == p.parameterCount && ((uri && !base64 && count == 1) || (binary && base64 && count == 2));
    }
    Status property(const IcsStructure::Property& p) {
        Component& c = componentAt(_current);
        if (c.observedProperties == UINT32_MAX || _counts.observedProperties == UINT64_MAX)
            return exhausted(Limit::Counter);
        if (c.flags & OpaqueProjectedOut) {
            if (_counts.ignoredOpaqueProperties == UINT64_MAX) return exhausted(Limit::Counter);
            ++c.observedProperties; ++_counts.observedProperties; ++_counts.ignoredOpaqueProperties;
            return _status; // Entire validated opaque payload is projected out, not parsed again.
        }
        if (!p.name.size || !input(p.name) || !input(p.value) || !input(p.encodedParameters) || p.parameterCount > UINT32_MAX)
            return fail(Status::InvalidAssociation);
        const Code id = code(p.name);
        Disposition d = selected(c.kind, id, c.flags & ProjectedOut);
        if (attachmentScope(c.kind, id) && !standard(c.kind, id))
            d = omitAttachment(id, p) ? Disposition::IgnoreByDeclaredProjection : Disposition::RetainConditional;
        bool unknownParameters = false; size_t n = 0;
        auto iterator = p.parameters(); IcsStructure::Parameter parameter;
        while (iterator.next(parameter)) {
            if (++n > p.parameterCount) return fail(Status::InvalidAssociation);
            if (!knownParameter(id, parameter.name)) unknownParameters = true;
        }
        if (n != p.parameterCount) return fail(Status::InvalidAssociation);
        if (unknownParameters && d == Disposition::IgnoreByDeclaredProjection) d = Disposition::RetainUnreviewed;
        _diagnostic.disposition = d;
        const bool retained = d != Disposition::IgnoreByDeclaredProjection;
        const bool reviewed = d == Disposition::RetainUnreviewed || unknownParameters;
        const size_t nameBytes = id == Code::Unknown ? p.name.size : 0;
        size_t bytes = nameBytes;
        // All operands were bounded to uint32; subtraction avoids size_t overflow on ESP32.
        if (p.encodedParameters.size > UINT32_MAX - bytes) return exhausted(Limit::Pool);
        bytes += p.encodedParameters.size;
        if (retained && p.value.size > UINT32_MAX - bytes) return exhausted(Limit::Pool);
        if (retained) bytes += p.value.size;
        const size_t reviewBytes = d == Disposition::RetainUnreviewed ? bytes :
            (unknownParameters ? p.encodedParameters.size : 0);
        if (_counts.storedProperties == _limits.properties) return exhausted(Limit::Properties);
        if (bytes > _limits.poolBytes - _counts.poolBytes) return exhausted(Limit::Pool);
        if (reviewed && _counts.reviewItems == _limits.reviewItems) return exhausted(Limit::ReviewItems);
        if (reviewBytes > _limits.reviewBytes - _counts.reviewBytes) return exhausted(Limit::ReviewBytes);
        const uint32_t nameOffset = nameBytes ? copy(p.name) : None;
        const uint32_t parameterOffset = copy(p.encodedParameters);
        const uint32_t valueOffset = retained ? copy(p.value) : None;
        const uint32_t index = _counts.storedProperties;
        new (_properties + index * sizeof(Property)) Property{_current, valueOffset, static_cast<uint32_t>(p.value.size),
            parameterOffset, static_cast<uint32_t>(p.encodedParameters.size), nameOffset,
            static_cast<uint32_t>(nameBytes), None, static_cast<uint32_t>(p.parameterCount), id, d,
            static_cast<uint8_t>((retained ? ValueRetained : 0) | (unknownParameters ? UnknownParameters : 0))};
        if (c.lastProperty != None) propertyAt(c.lastProperty).next = index;
        else c.firstProperty = index;
        c.lastProperty = index;
        ++_counts.storedProperties; ++c.observedProperties; ++_counts.observedProperties;
        if (reviewed) { ++_counts.reviewItems; c.flags |= HasReview; }
        _counts.reviewBytes += static_cast<uint32_t>(reviewBytes);
        return _status;
    }
};

#endif
