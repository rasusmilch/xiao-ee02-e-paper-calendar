#include "../application/ics_typed_values.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <type_traits>

namespace { bool auditing = false; size_t allocations = 0, assertions = 0; }
// Keep test replacement allocation entry points out of line. GCC 14 otherwise
// inlines their legitimate malloc/free implementation into STL callers and emits
// mismatched-new-delete at -O2. Do not suppress that warning on production code.
__attribute__((noinline))
void* operator new(size_t n) {
    if (auditing) ++allocations;
    void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline))
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
namespace {
namespace T = IcsTypedValues;
using S = T::Status;
using Stage = IcsSourceStage;
using Code = Stage::Code;
T::View literal(const char* s) { return {reinterpret_cast<const uint8_t*>(s), std::strlen(s)}; }
void expect(T::Result r, S s) {
    ++assertions;
    if (r.status != s) std::fprintf(stderr, "assertion %zu: expected %u got %u offset %zu\n",
        assertions, unsigned(s), unsigned(r.status), r.offset);
    assert(r.status == s);
}
struct Audit {
    size_t before;
    Audit() : before(allocations) { assert(!auditing); auditing = true; }
    ~Audit() { auditing = false; assert(allocations == before); }
};
struct Guard {
    std::vector<uint8_t> bytes; size_t capacity;
    explicit Guard(size_t n) : bytes(n + 32, 0xa5), capacity(n) {}
    uint8_t* data() { return bytes.data() + 16; }
    void check() const {
        for (size_t i = 0; i < 16; ++i) assert(bytes[i] == 0xa5);
        for (size_t i = 16 + capacity; i < bytes.size(); ++i) assert(bytes[i] == 0xa5);
    }
};
void dates() {
    T::Date d = {}; T::DateTime dt = {};
    Audit a;
    for (const char* s : {"20261012","20000229","20240229","00000229","99991231"})
        expect(T::decodeDate(literal(s),d),S::Ok);
    expect(T::decodeDate(literal("00000101"),d),S::Ok); assert(d.year == 0);
    for (const char* s : {"19000229","20230229","20260431","20260001","20261301","20260100",
                          "2026a101","2026011","202601011","2026-01-01","", " 20260101"})
        expect(T::decodeDate(literal(s),d),S::MalformedValue);
    expect(T::decodeDateTime(literal("20261012T090000Z"),dt),S::Ok);
    assert(dt.mode == T::TimeMode::Utc && dt.hour == 9);
    expect(T::decodeDateTime(literal("20261012t090000z"),dt),S::Ok);
    for (const char* s : {"20260308T023000","20261101T013000"}) {
        expect(T::decodeDateTime(literal(s),dt),S::Ok); assert(dt.mode == T::TimeMode::Floating);
    }
    expect(T::decodeDateTime(literal("20161231T235960Z"),dt),S::NeedsContext); assert(dt.second == 60);
    for (const char* s : {"20261012T240000","20261012T096000","20261012T090061","20261012T0900",
                          "20261012T090000-0500","20261012T090000.0Z","20261012","20261012T090000ZZ",
                          "20260229T090000","20261012X090000", ""})
        expect(T::decodeDateTime(literal(s),dt),S::MalformedValue);
    expect(T::decodeDate({nullptr,8},d),S::InvalidArgument);
    expect(T::decodeDateTime({reinterpret_cast<const uint8_t*>(UINTPTR_MAX - 2),16},dt),S::InvalidArgument);
    expect(T::decodeDate({reinterpret_cast<const uint8_t*>(1),SIZE_MAX},d),S::InvalidArgument);
}
void durationsAndPeriods() {
    T::Duration d = {}; T::Period p = {}; Audit a;
    for (const char* s : {"P1W","+P2W","-P2D","P0D","PT0S","P3DT4H5M6S","PT1H0M2S","PT2M3S",
                          "PT3S","PT99H","pt1h","P18446744073709551615D"})
        expect(T::decodeDuration(literal(s),d),S::Ok);
    expect(T::decodeDuration(literal("-P2W"),d),S::Ok); assert(d.negative && d.weekForm && d.weeks == 2);
    expect(T::decodeDuration(literal("P1DT2H"),d),S::Ok); assert(d.days == 1 && d.hours == 2);
    expect(T::decodeDuration(literal("P18446744073709551616D"),d),S::ResourceLimit);
    for (const char* s : {"P","PT","P1Y","P1M","P1W1D","P1DT","PT1H2S","PT1S1M","PT1H1H",
                          "PT1M1H","P1D1D","PT+1H","P-1D","P1DT1D","1D","P1DX",""})
        expect(T::decodeDuration(literal(s),d),S::MalformedValue);
    expect(T::decodePeriod(literal("20261012T090000Z/20261012T100000Z"),p),S::Ok);
    expect(T::decodePeriod(literal("20261012T090000Z/PT1H"),p),S::Ok); assert(p.durationForm && p.duration.hours == 1);
    expect(T::decodePeriod(literal("20261012T090000/PT1H"),p),S::Ok);
    expect(T::decodePeriod(literal("20261012T090000/20261012T100000"),p),S::NeedsContext);
    expect(T::decodePeriod(literal("20260308T025500/20260308T030000"),p),S::NeedsContext);
    expect(T::decodePeriod(literal("20261012T100000/20261012T090000"),p),S::NeedsContext);
    expect(T::decodePeriod(literal("20261012T090000/20261012T100000Z"),p),S::NeedsContext);
    expect(T::decodePeriod(literal("20161231T235960Z/20170101T000000Z"),p),S::NeedsContext);
    for (const char* s : {"20261012T090000Z/20261012T090000Z","20261012T100000Z/20261012T090000Z",
                          "20261012T090000Z/PT0S","20261012T090000Z/-PT1S","20261012T090000Z/P0W",
                          "20261012/20261013","20261012T090000Z/","/20261012T090000Z",
                          "20261012T090000Z/PT1H/PT2H","20261012T090000Z"})
        expect(T::decodePeriod(literal(s),p),S::MalformedValue);
}
void numbers() {
    int32_t n = 0; Audit a;
    expect(T::decodeInteger(literal("2147483647"),n),S::Ok); assert(n == INT32_MAX);
    expect(T::decodeInteger(literal("-2147483648"),n),S::Ok); assert(n == INT32_MIN);
    expect(T::decodeInteger(literal("+00001"),n),S::Ok); assert(n == 1);
    for (const char* s : {"-0","0","-1"}) expect(T::decodeInteger(literal(s),n),S::Ok);
    for (const char* s : {"2147483648","-2147483649","99999999999999999999","1.0","0x1","1x","+",""," 1"})
        expect(T::decodeInteger(literal(s),n),S::MalformedValue);
    expect(T::decodeUtcOffset(literal("-0530"),n),S::Ok); assert(n == -19800);
    expect(T::decodeUtcOffset(literal("+235959"),n),S::Ok); assert(n == 86399);
    for (const char* s : {"+0000","+000000","-235959","+010001"}) expect(T::decodeUtcOffset(literal(s),n),S::Ok);
    for (const char* s : {"-0000","-000000","+2400","+0060","+010060","0100","+01:00","+01000","+010000Z",""})
        expect(T::decodeUtcOffset(literal(s),n),S::MalformedValue);
}
void writing() {
    struct Example { const char* encoded; const char* decoded; bool text; };
    const Example cases[] = {{"a\\,b\\;c\\\\d\\nE\\NF","a,b;c\\d\nE\nF",true},
        {"UTF-8: caf\xc3\xa9 \xf0\x9f\x93\x85\"", "UTF-8: caf\xc3\xa9 \xf0\x9f\x93\x85\"",true},
        {"","",true},{"^n^^^' ^N ^z ^ \\n","\n^\" ^N ^z ^ \\n",false},
        {"caf\xc3\xa9^","caf\xc3\xa9^",false}};
    for (const auto& c : cases) {
        const size_t expected = std::strlen(c.decoded); Guard g(expected + 1); size_t bytes = SIZE_MAX;
        for (bool nul : {false,true}) { Audit a;
            expect(c.text ? T::measureText(literal(c.encoded),bytes) :
                T::measureParameter({literal(c.encoded),true},bytes),S::Ok); assert(bytes == expected);
            std::memset(g.data(),0xa5,g.capacity);
            expect(c.text ? T::writeText(literal(c.encoded),g.data(),expected + size_t(nul),nul,bytes) :
                T::writeParameter({literal(c.encoded),true},g.data(),expected + size_t(nul),nul,bytes),S::Ok);
            assert(bytes == expected && !std::memcmp(g.data(),c.decoded,expected));
            assert(g.data()[expected] == (nul ? 0 : 0xa5));
            if (expected || nul) {
                std::memset(g.data(),0xa5,g.capacity);
                expect(c.text ? T::writeText(literal(c.encoded),g.data(),expected + size_t(nul) - 1,nul,bytes) :
                    T::writeParameter({literal(c.encoded),false},g.data(),expected + size_t(nul) - 1,nul,bytes),S::ResourceLimit);
                assert(!bytes); for (size_t i = 0; i < g.capacity; ++i) assert(g.data()[i] == 0xa5);
            }
        }
        g.check();
    }
    const char* const malformed[] = {"bad\\q","bad\\:","dangling\\","raw,comma","raw;semicolon",
        "line\nfeed","\x01","\x7f","\xc0\xaf","\xed\xa0\x80","\xf4\x90\x80\x80","\xe2\x82"};
    for (const char* s : malformed) {
        uint8_t out[32]; std::memset(out,0xa5,sizeof(out)); size_t n = 7; Audit a;
        expect(T::writeText(literal(s),out,sizeof(out),true,n),S::MalformedValue);
        assert(n == 0); for (uint8_t b : out) assert(b == 0xa5);
    }
    uint8_t embedded[] = {'a',0,'b'}, alias[] = {'a','b','c',0}; size_t n = 0; Audit a;
    expect(T::measureText({embedded,3},n),S::MalformedValue);
    expect(T::writeText({alias,3},alias,4,true,n),S::InvalidArgument);
    expect(T::writeParameter({{alias,3},false},alias+1,3,false,n),S::InvalidArgument);
    expect(T::writeText(literal("a"),nullptr,1,false,n),S::InvalidArgument);
    expect(T::writeText(literal("a"),reinterpret_cast<uint8_t*>(UINTPTR_MAX),2,false,n),S::InvalidArgument);
    expect(T::writeText({nullptr,0},nullptr,0,false,n),S::Ok);
    expect(T::writeText({nullptr,0},nullptr,0,true,n),S::ResourceLimit);
}

uint64_t clock(void*) { return 0; }
size_t stageBytes() { size_t n = 0; assert(Stage::requiredBytes({64,128,32768,32,8192},n) == Stage::Status::InProgress); return n; }
IcsStructure::Limits syntax() { return {4096,32,32,64,8,64,1024,4096,128,4}; }
size_t parserBytes() { size_t n = 0; assert(IcsStructure::scratchBytes(syntax(),n) == IcsStructure::Status::InProgress); return n; }
struct Session {
    Guard storage, parserStorage, httpStorage;
    Stage stage; IcsStructure parser; IcsIngestion http;
    Session() : storage(stageBytes()),parserStorage(parserBytes()),httpStorage(6144) {
        Audit a;
        assert(stage.initialize(storage.data(),storage.capacity,{64,128,32768,32,8192}) == Stage::Status::InProgress);
        assert(parser.initialize(parserStorage.data(),parserStorage.capacity,syntax(),Stage::sink,&stage) == IcsStructure::Status::InProgress);
        const IcsIngestion::Limits limits = {131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
        assert(http.initialize(httpStorage.data(),httpStorage.capacity,limits,IcsStructure::lineSink,&parser,clock,nullptr) == IcsIngestion::Status::InProgress);
    }
    ~Session() { Audit a; stage.release(); parser.release(); http.release(); storage.check(); parserStorage.check(); httpStorage.check(); }
};
std::string event(const std::string& properties) { return "BEGIN:VEVENT\r\n"+properties+"END:VEVENT\r\n"; }
std::string calendar(const std::string& contents) {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\n"+contents+"END:VCALENDAR\r\n";
}
std::string response(const std::string& body, bool chunked) {
    if (!chunked) return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    std::string raw = "HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for (size_t i = 0; i < body.size();) {
        const size_t n = std::min<size_t>(7,body.size()-i); char size[24];
        std::snprintf(size,sizeof(size),"%zx",n); raw += size; raw += "\r\n"; raw.append(body,i,n); raw += "\r\n"; i += n;
    }
    return raw + "0\r\n\r\n";
}
void stage(Session& s, const std::string& body, bool chunked = false, size_t step = SIZE_MAX) {
    std::string raw = response(body,chunked); Audit a;
    for (size_t i = 0; i < raw.size();) {
        const size_t n = std::min(step,raw.size()-i);
        assert(s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()+i),n) == IcsIngestion::Status::InProgress); i += n;
    }
    assert(s.http.finish() == IcsIngestion::Status::Complete);
    assert(s.parser.finish() == IcsStructure::Status::StructurallyComplete);
    assert(s.stage.finish(s.http,s.parser) == Stage::Status::StagedSuccessfully);
    // Destroy/poison transient parser input while retaining the independent stage slice.
    s.http.release(); s.parser.release(); std::memset(s.httpStorage.data(),0xdd,s.httpStorage.capacity);
    std::memset(s.parserStorage.data(),0xdd,s.parserStorage.capacity);
    std::fill(raw.begin(),raw.end(),'!');
}
size_t find(const Stage& s, Code code, size_t start = 0) {
    for (size_t i = start; i < s.counts().storedProperties; ++i) if (s.property(i)->code == code) return i;
    return SIZE_MAX;
}
void selected(const std::string& property, S expected, const char* component = "VEVENT") {
    Session s;
    const std::string body = std::string(component) == "STANDARD" ?
        calendar("BEGIN:VTIMEZONE\r\nTZID:zone\r\nBEGIN:STANDARD\r\n"+property+"\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n") :
        calendar("BEGIN:"+std::string(component)+"\r\n"+property+"\r\nEND:"+component+"\r\n");
    stage(s,body);
    T::TemporalValue value = {}; T::Selection selection = {}; Audit a;
    const size_t index = s.stage.counts().storedProperties - 1;
    expect(T::decodeTemporal(s.stage,index,value,selection),expected);
}
void propertyRules() {
    selected("DTSTART:20261012T090000",S::Ok);
    selected("dtstart;value=\"date\":20261012",S::Ok);
    selected("DTSTART;VALUE=DATE-TIME;TZID=Late-Zone:20261012T090000",S::Ok);
    selected("DTSTART;TZID=\"\":20261012T090000",S::NeedsContext);
    selected("DTSTART;VALUE=DATE;VALUE=DATE:20261012",S::InvalidCardinality);
    selected("DTSTART;TZID=a;TZID=b:20261012T090000",S::InvalidCardinality);
    selected("DTSTART;VALUE=DATE,DATE-TIME:20261012",S::InvalidCardinality);
    selected("DTSTART;TZID=a,b:20261012T090000",S::InvalidCardinality);
    selected("DTSTART;VALUE=:20261012",S::InvalidValueType);
    selected("DTSTART;VALUE=X-OTHER:opaque",S::UnsupportedFeature);
    selected("DTSTART;VALUE=REGISTERED-OTHER:opaque",S::UnsupportedFeature);
    selected("DTSTART;VALUE=\"bad value\":opaque",S::InvalidValueType);
    selected("DTSTART;VALUE=INTEGER:1",S::InvalidValueType);
    selected("DTSTART;VALUE=DATE:20261012T090000",S::MalformedValue);
    selected("DTSTART;VALUE=PERIOD:20261012T090000Z/PT1H",S::InvalidValueType);
    selected("DTSTART;VALUE=DATE;TZID=a:20261012",S::InvalidParameter);
    selected("DTSTART;TZID=a:20261012T090000Z",S::InvalidParameter);
    selected("DTSTART;LANGUAGE=en:20261012T090000",S::InvalidParameter);
    selected("DTSTART;RANGE=THISANDFUTURE:20261012T090000",S::InvalidParameter);
    selected("DTSTART;X-UNREVIEWED=a:20261012T090000",S::Ok);
    selected("CREATED:20261012T090000Z",S::Ok);
    selected("DTSTAMP:20261012T090000",S::MalformedValue);
    selected("LAST-MODIFIED;TZID=a:20261012T090000",S::InvalidParameter);
    selected("DTSTAMP;VALUE=DATE-TIME:20261012T090000Z",S::Ok);
    selected("DTSTAMP;VALUE=DATE:20261012",S::InvalidValueType);
    selected("DTSTAMP;VALUE=X-OTHER:opaque",S::UnsupportedFeature);
    selected("DURATION;VALUE=DURATION:PT1H",S::Ok);
    selected("DURATION;VALUE=DATE-TIME:20261012T090000Z",S::InvalidValueType);
    selected("DTSTART;VALUE=DURATION:PT1H",S::InvalidValueType);
    selected("DURATION:-PT1H",S::Ok);
    selected("DTSTART:20261012T090000",S::Ok,"STANDARD");
    selected("DTSTART:20261012T090000Z",S::MalformedValue,"STANDARD");
    selected("DTSTART;VALUE=DATE:20261012",S::InvalidValueType,"STANDARD");
    selected("DTSTART;TZID=a:20261012T090000",S::InvalidParameter,"STANDARD");
}
void lists() {
    struct Fixture { const char* property; S status; size_t members; };
    const Fixture cases[] = {
        {"RDATE;VALUE=DATE:20261012,20261013",S::Ok,2},
        {"EXDATE:20261012T090000Z,20261013T090000Z",S::Ok,2},
        {"RDATE;VALUE=PERIOD:20261012T090000Z/PT1H,20261013T090000Z/20261013T100000Z",S::Ok,2},
        {"RDATE;VALUE=PERIOD;TZID=a:20261012T090000/20261012T100000",S::NeedsContext,1},
        {"EXDATE:20161231T235960Z",S::NeedsContext,1},
        {"RDATE;VALUE=DATE:20261012,",S::MalformedValue,1},
        {"RDATE;VALUE=DATE:,20261012",S::MalformedValue,0},
        {"RDATE;VALUE=DATE:20261012,,20261013",S::MalformedValue,1},
        {"RDATE;VALUE=DATE:20261012;20261013",S::MalformedValue,0},
        {"RDATE;VALUE=DATE:20261012,20261013T090000Z",S::MalformedValue,1},
        {"EXDATE;VALUE=PERIOD:20261012T090000Z/PT1H",S::InvalidValueType,0},
        {"RDATE;VALUE=PERIOD;TZID=a:20261012T090000Z/PT1H",S::InvalidParameter,0},
        {"RDATE:",S::MalformedValue,0}};
    for (const auto& f : cases) {
        Session s; stage(s,calendar(event(std::string(f.property)+"\r\n")));
        const size_t i = s.stage.counts().storedProperties - 1; Audit a;
        T::TemporalList list(s.stage,i,{8,4096}); T::TemporalValue v = {};
        for (size_t j = 0; j < f.members; ++j) assert(T::parsed(list.next(v)));
        assert(!list.complete());
        if (f.status == S::Ok || f.status == S::NeedsContext) {
            expect(list.status(),S::InProgress); expect(list.next(v),S::End);
            assert(list.complete()); expect(list.status(),f.status); expect(list.next(v),S::End);
        } else { expect(list.next(v),f.status); expect(list.next(v),f.status); assert(!list.complete()); }
        assert(list.count() == f.members);
    }
    Session s; stage(s,calendar(event("RDATE;VALUE=DATE:20261012,20261013\r\n")));
    const size_t i = find(s.stage,Code::Rdate); const auto* p = s.stage.property(i);
    const size_t bytes = p->valueLength + p->parameterLength;
    T::TemporalValue v = {}; Audit a;
    T::TemporalList exact(s.stage,i,{2,bytes}); expect(exact.next(v),S::Ok); expect(exact.next(v),S::Ok);
    expect(exact.next(v),S::End); expect(exact.status(),S::Ok);
    T::TemporalList shortBytes(s.stage,i,{2,bytes-1}); expect(shortBytes.next(v),S::ResourceLimit);
    T::TemporalList shortItems(s.stage,i,{1,bytes}); expect(shortItems.next(v),S::Ok);
    expect(shortItems.next(v),S::ResourceLimit); assert(!shortItems.complete());
    T::TemporalList invalid(s.stage,i,{0,bytes}); expect(invalid.next(v),S::InvalidArgument);
}
void integration() {
    const std::string uid = std::string(300,'u') + "\xc3\xa9";
    const std::string body = calendar(event("UID:"+uid+"\r\nDTSTART;TZID=\"Late^'Zone\";X-FUTURE=^z:20261012T090000\r\n"
        "DURATION:PT1H\r\nSUMMARY:Title\\, caf\xc3\r\n \xa9\\nDetails\r\nDESCRIPTION:\r\nATTACH:large-unneeded\r\n"
        "RECURRENCE-ID;RANGE=THISANDFUTURE:20261012T090000\r\nRRULE:FREQ=DAILY;COUNT=2\r\n"
        "RRULE:RSCALE=CHINESE;SKIP=FORWARD;FREQ=YEARLY\r\nX-UNSEEN:preserved\r\nBEGIN:PARTICIPANT\r\nUID:child\r\nSUMMARY:Child\r\nEND:PARTICIPANT\r\n")+
        "BEGIN:X-CUSTOM\r\nUID:opaque\r\nEND:X-CUSTOM\r\n");
    for (bool chunked : {false,true}) for (size_t step : {size_t(1),size_t(7),size_t(31),SIZE_MAX}) {
        Session s; stage(s,body,chunked,step);
        const std::vector<uint8_t> original = s.storage.bytes;
        uint8_t output[64]; size_t bytes = 0; T::TemporalValue temporal = {}; T::Selection selection = {};
        T::PropertyView p = {}; const size_t dt = find(s.stage,Code::Dtstart); Audit a;
        expect(T::decodeTemporal(s.stage,dt,temporal,selection),S::Ok);
        assert(temporal.dateTime.mode == T::TimeMode::Tzid && selection.tzidPresent && selection.tzid.quoted && selection.review);
        assert(s.stage.property(dt)->flags & Stage::UnknownParameters);
        expect(T::writeParameter(selection.tzid,output,sizeof(output),true,bytes),S::Ok);
        assert(bytes == 9 && std::strcmp(reinterpret_cast<char*>(output),"Late\"Zone") == 0);
        expect(T::decodeTemporal(s.stage,find(s.stage,Code::Duration),temporal,selection),S::Ok);
        assert(temporal.duration.hours == 1);
        expect(T::readProperty(s.stage,find(s.stage,Code::Summary),p),S::Ok);
        assert(p.kind == T::Kind::Event && p.component == 2 && p.calendar == 1);
        expect(T::writeText(p.value,output,sizeof(output),true,bytes),S::Ok);
        assert(std::strcmp(reinterpret_cast<char*>(output),"Title, caf\xc3\xa9\nDetails") == 0);
        expect(T::readProperty(s.stage,find(s.stage,Code::Uid),p),S::Ok);
        assert(p.value.size == uid.size() && !std::memcmp(p.value.data,uid.data(),uid.size()));
        expect(T::readProperty(s.stage,find(s.stage,Code::Description),p),S::Ok); assert(!p.value.size);
        expect(T::writeText(p.value,output,0,false,bytes),S::Ok);
        expect(T::readProperty(s.stage,find(s.stage,Code::Attach),p),S::Unavailable);
        assert(p.disposition == Stage::Disposition::IgnoreByDeclaredProjection && p.value.data == nullptr);
        expect(T::readProperty(s.stage,SIZE_MAX,p),S::InvalidArgument);
        expect(T::readProperty(s.stage,find(s.stage,Code::Unknown),p),S::Ok);
        assert(p.disposition == Stage::Disposition::RetainUnreviewed && p.value.size == 9);
        expect(T::decodeTemporal(s.stage,find(s.stage,Code::Unknown),temporal,selection),S::UnsupportedFeature);
        for (size_t rr = find(s.stage,Code::Rrule); rr != SIZE_MAX; rr = find(s.stage,Code::Rrule,rr+1))
            expect(T::decodeTemporal(s.stage,rr,temporal,selection),S::UnsupportedFeature);
        expect(T::decodeTemporal(s.stage,find(s.stage,Code::RecurrenceId),temporal,selection),S::Ok);
        assert(selection.review); // Known parameter still has unimplemented semantics.
        const size_t child = find(s.stage,Code::Summary,find(s.stage,Code::Summary)+1);
        expect(T::readProperty(s.stage,child,p),S::Ok); assert(p.kind == T::Kind::Participant && p.parent == 2);
        assert(original == s.storage.bytes); // All decoder operations were read-only.
        T::Date copied = temporal.dateTime.date;
        s.stage.release(); assert(s.stage.status() == Stage::Status::StagedSuccessfully);
        std::memset(s.storage.data(),0xdd,s.storage.capacity);
        expect(T::readProperty(s.stage,dt,p),S::Unavailable);
        expect(T::decodeTemporal(s.stage,dt,temporal,selection),S::Unavailable);
        assert(copied.year == 2026); // Copied POD survives; borrowed spans are never dereferenced.
    }
    Session s; stage(s,calendar(event("RDATE;VALUE=DATE:20261012,20261013\r\n")));
    T::TemporalList list(s.stage,find(s.stage,Code::Rdate),{2,128}); T::TemporalValue v = {};
    expect(list.next(v),S::Ok); s.stage.release(); expect(list.next(v),S::Unavailable); assert(!list.complete());
    Stage fresh; T::PropertyView p = {}; expect(T::readProperty(fresh,0,p),S::Unavailable);
}
void additionalContexts() {
    const std::string body = calendar(event("UID:parent\r\nDTSTART:20261012T090000\r\n")+
        "BEGIN:VTIMEZONE\r\nTZID:late\r\nLAST-MODIFIED:20261001T000000Z\r\n"
        "BEGIN:DAYLIGHT\r\nDTSTART:20260308T020000\r\nRDATE:20260308T020000\r\nEND:DAYLIGHT\r\nEND:VTIMEZONE\r\n")+
        calendar(event("UID:other\r\nDTSTART;VALUE=DATE:20261013\r\n"));
    Session s; stage(s,body); Audit a;
    T::TemporalValue v = {}; T::Selection sel = {}; T::PropertyView p = {};
    expect(T::decodeTemporal(s.stage,find(s.stage,Code::LastModified),v,sel),S::Ok);
    size_t i = find(s.stage,Code::Dtstart);
    expect(T::readProperty(s.stage,i,p),S::Ok); assert(p.calendar == 1);
    i = find(s.stage,Code::Dtstart,i+1);
    expect(T::decodeTemporal(s.stage,i,v,sel),S::Ok); assert(sel.observance && v.dateTime.mode == T::TimeMode::Floating);
    i = find(s.stage,Code::Dtstart,i+1);
    expect(T::decodeTemporal(s.stage,i,v,sel),S::Ok); assert(v.type == T::Type::Date);
    expect(T::readProperty(s.stage,i,p),S::Ok); assert(p.calendar == 2);
    T::TemporalList list(s.stage,find(s.stage,Code::Rdate),{1,128});
    expect(list.next(v),S::Ok); expect(list.next(v),S::End);
    expect(T::decodeTemporal(s.stage,find(s.stage,Code::Rdate),v,sel),S::InvalidArgument);
    T::TemporalList notList(s.stage,i,{1,128}); expect(notList.next(v),S::InvalidArgument);
}
}
int main() {
    static_assert(std::is_pod<T::Date>::value && std::is_pod<T::DateTime>::value &&
        std::is_pod<T::Duration>::value && std::is_pod<T::Period>::value && std::is_pod<T::TemporalValue>::value,
        "copied typed representations are POD");
    dates(); durationsAndPeriods(); numbers(); writing(); propertyRules(); lists(); integration(); additionalContexts();
    assert(allocations == 0);
    std::printf("ICS typed values: %zu result assertions, zero audited allocations; Date=%zu DateTime=%zu Duration=%zu Period=%zu Temporal=%zu Cursor=%zu\n",
        assertions,sizeof(T::Date),sizeof(T::DateTime),sizeof(T::Duration),sizeof(T::Period),sizeof(T::TemporalValue),sizeof(T::TemporalList));
}
