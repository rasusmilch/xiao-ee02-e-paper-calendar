#include "../application/ics_structure.h"
#include "../application/ics_ingestion.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

using Parser = IcsStructure;
using Status = Parser::Status;
using Kind = Parser::Kind;
using Limit = Parser::Limit;
using Type = Parser::EventType;
namespace { bool tracking = false; size_t allocations = 0, scenarios = 0; }
void* operator new(size_t n) {
    if (tracking) ++allocations;
    void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

namespace {
struct Audit { Audit() { assert(!tracking); tracking = true; } ~Audit() { tracking = false; } };
Parser::Limits evaluation() { return {16384,32,64,128,8,64,4096,65536,4096,4}; }
size_t required(const Parser::Limits& l) {
    size_t n = 0; assert(Parser::scratchBytes(l,n)==Status::InProgress); return n;
}
const uint8_t* bytes(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }
void expect(Status actual, Status wanted) {
    if (actual!=wanted) std::fprintf(stderr,"structure status %u expected %u\n",
        static_cast<unsigned>(actual),static_cast<unsigned>(wanted));
    assert(actual==wanted);
}
struct Guarded {
    std::vector<uint8_t> storage;
    size_t prefix, size;
    explicit Guarded(size_t n,size_t shift=0) : storage(n+32+shift,0xa5),prefix(16+shift),size(n) {}
    uint8_t* data() { return storage.data()+prefix; }
    void check() const {
        for(size_t i=0;i<prefix;++i) assert(storage[i]==0xa5);
        for(size_t i=prefix+size;i<storage.size();++i) assert(storage[i]==0xa5);
    }
};
struct Text { size_t offset, size; };
struct Value { Text text; bool quoted; };
struct Parameter { Text name; size_t first, count; };
struct Event {
    Type type; Kind kind; size_t depth; uint64_t ordinal, parent, calendar;
    Text component, name, value; size_t first, count;
};
// Bounded test-only copies; no callback pointers escape, no production staging.
struct Recorder {
    std::vector<uint8_t> pool;
    std::vector<Event> events;
    std::vector<Parameter> parameters;
    std::vector<Value> values;
    size_t used=0, count=0, parameterCount=0, valueCount=0, failAt=SIZE_MAX;
    Parser* parser=nullptr;
    unsigned reenter=0;
    Recorder() : pool(262144),events(512),parameters(1024),values(2048) {}
    Text copy(Parser::View v) {
        assert(v.size<=pool.size()-used); const Text result={used,v.size};
        if(v.size) std::memcpy(pool.data()+used,v.data,v.size);
        used+=v.size; return result;
    }
    bool is(Text v,const std::string& expected) const {
        return v.size==expected.size() && (!v.size || !std::memcmp(pool.data()+v.offset,expected.data(),v.size));
    }
    static bool sink(const Parser::Event& input,void* context) {
        auto& r=*static_cast<Recorder*>(context); assert(r.count<r.events.size());
        const auto& c=input.component;
        Event e={input.type,c.kind,c.depth,c.ordinal,c.parentOrdinal,c.calendarOrdinal,
            r.copy(c.name),{0,0},{0,0},r.parameterCount,0};
        if(input.type==Type::Property) {
            e.name=r.copy(input.property.name); e.value=r.copy(input.property.value);
            auto iterator=input.property.parameters(); Parser::Parameter p;
            while(iterator.next(p)) {
                assert(r.parameterCount<r.parameters.size());
                Parameter record={r.copy(p.name),r.valueCount,0};
                auto values=p.values(); Parser::EncodedValue v;
                while(values.next(v)) {
                    assert(r.valueCount<r.values.size());
                    r.values[r.valueCount++]={r.copy(v.encoded),v.quoted}; ++record.count;
                }
                r.parameters[r.parameterCount++]=record; ++e.count;
            }
            assert(e.count==input.property.parameterCount);
        }
        r.events[r.count++]=e;
        if(r.reenter) {
            assert(r.parser);
            if(r.reenter==1) expect(r.parser->consume(nullptr,0),Status::InvalidState);
            if(r.reenter==2) expect(r.parser->finish(),Status::InvalidState);
            if(r.reenter==3) expect(r.parser->cancel(),Status::InvalidState);
            if(r.reenter==4) { r.parser->release(); expect(r.parser->status(),Status::InvalidState); }
            if(r.reenter==5) expect(r.parser->initialize(nullptr,0,evaluation(),sink,&r),Status::InvalidState);
        }
        return r.count-1!=r.failAt;
    }
};
struct Harness {
    Guarded scratch; Recorder recorder; Parser parser;
    explicit Harness(const Parser::Limits& l=evaluation(),size_t shift=0) : scratch(required(l),shift) {
        Audit audit; expect(parser.initialize(scratch.data(),scratch.size,l,Recorder::sink,&recorder),Status::InProgress);
    }
    ~Harness() { parser.release(); scratch.check(); }
};
Status line(Harness& h,const std::string& s) {
    Audit audit; return h.parser.consume(bytes(s),s.size());
}
Status finish(Harness& h) { Audit audit; return h.parser.finish(); }
void start(Harness& h,bool event=true) {
    expect(line(h,"BEGIN:VCALENDAR"),Status::InProgress);
    expect(line(h,"VERSION:2.0"),Status::InProgress);
    expect(line(h,"PRODID:host"),Status::InProgress);
    if(event) expect(line(h,"BEGIN:VEVENT"),Status::InProgress);
}
void complete(Harness& h,bool event=true) {
    if(event) expect(line(h,"END:VEVENT"),Status::InProgress);
    expect(line(h,"END:VCALENDAR"),Status::InProgress);
    expect(h.parser.status(),Status::InProgress); expect(finish(h),Status::StructurallyComplete);
}
Status lines(Harness& h,const std::string& body) {
    Status result=Status::InProgress;
    for(size_t offset=0;offset<body.size() && result==Status::InProgress;) {
        const size_t end=body.find("\r\n",offset); assert(end!=std::string::npos);
        result=line(h,body.substr(offset,end-offset)); offset=end+2;
    }
    return result;
}
void failure(Harness& h,Status expected) {
    expect(finish(h),expected); expect(line(h,"END:VCALENDAR"),expected);
    expect(h.parser.cancel(),expected); const auto d=h.parser.diagnostic(); h.parser.release();
    expect(h.parser.status(),expected); expect(finish(h),expected);
    assert(h.parser.diagnostic().line==d.line && h.parser.diagnostic().offset==d.offset);
    h.scratch.check(); ++scenarios;
}

void properties() {
    const std::vector<std::pair<std::string,std::string>> valid={
        {"SUMMARY:",""},{"SUMMARY:Design review: Rev 2; customer feedback","Design review: Rev 2; customer feedback"},
        {"DTSTART;TZID=America/Chicago:20261012T090000","20261012T090000"},
        {"ATTENDEE;CN=\"Doe, John\";ROLE=REQ-PARTICIPANT:mailto:user@example.invalid","mailto:user@example.invalid"},
        {"X-EXAMPLE;LABEL=\"a;b:c,d\":opaque-value","opaque-value"},
        {"sUmMaRy;X-P=;X-P=\"\":unchanged","unchanged"},
        {"SUMMARY;P=a,,\"\",:a\\,b\\;c\\n","a\\,b\\;c\\n"},
        {"X-NAME;P=a=b:value","value"},{"9-TOKEN:value","value"},
        {"SUMMARY;CN=George ^'Babe^' Ruth;P=^^,^n,^N,^z,^:text","text"},
        {"SUMMARY;CN=\"caf\xc3\xa9\":\xe2\x98\x83 \xf0\x9f\x98\x80","\xe2\x98\x83 \xf0\x9f\x98\x80"},
        {"SUMMARY:\tspaces remain ","\tspaces remain "}
    };
    for(const auto& test:valid) {
        Harness h; start(h); expect(line(h,test.first),Status::InProgress);
        const auto& e=h.recorder.events[h.recorder.count-1];
        assert(e.type==Type::Property && e.kind==Kind::Event && e.depth==2 && e.parent==1);
        assert(h.recorder.is(e.value,test.second)); complete(h); ++scenarios;
    }
    Harness q; start(q);
    expect(line(q,"ATTENDEE;CN=\"Doe, John\";X-LIST=a,,\"b:c;d\",:mailto:user@example.invalid"),Status::InProgress);
    const auto& e=q.recorder.events[q.recorder.count-1]; assert(e.count==2);
    const auto& first=q.recorder.parameters[e.first]; const auto& second=q.recorder.parameters[e.first+1];
    assert(q.recorder.is(first.name,"CN") && first.count==1);
    assert(q.recorder.values[first.first].quoted && q.recorder.is(q.recorder.values[first.first].text,"Doe, John"));
    assert(second.count==4 && !q.recorder.values[second.first].quoted);
    assert(q.recorder.is(q.recorder.values[second.first+1].text,""));
    assert(q.recorder.values[second.first+2].quoted && q.recorder.is(q.recorder.values[second.first+2].text,"b:c;d"));
    assert(q.recorder.is(q.recorder.values[second.first+3].text,"")); complete(q);
    Harness caret; start(caret);
    expect(line(caret,"SUMMARY;P=^n,^^,^',^N,^z,^:a\\n"),Status::InProgress);
    const auto& ce=caret.recorder.events[caret.recorder.count-1];
    const auto& cp=caret.recorder.parameters[ce.first]; assert(cp.count==6);
    const char* expected[]={"^n","^^","^'","^N","^z","^"};
    for(size_t i=0;i<6;++i) assert(caret.recorder.is(caret.recorder.values[cp.first+i].text,expected[i]));
    assert(caret.recorder.is(ce.value,"a\\n")); complete(caret); ++scenarios;

    const std::vector<std::pair<std::string,Status>> invalid={
        {"",Status::MalformedProperty},{":x",Status::MalformedProperty},{"SUMMARY",Status::MalformedProperty},
        {"SUM.MARY:x",Status::MalformedProperty},{" SUMMARY:x",Status::MalformedProperty},
        {"SUMMARY;=x:y",Status::InvalidParameters},{"SUMMARY;P:y",Status::InvalidParameters},
        {"SUMMARY;;P=x:y",Status::InvalidParameters},{"SUMMARY;P_Q=x:y",Status::InvalidParameters},
        {"SUMMARY;P=\"x:y",Status::InvalidParameters},
        {"SUMMARY;P=\"x\"tail:y",Status::InvalidParameters},{"SUMMARY;P=x\"y:z",Status::InvalidParameters},
        {"SUMMARY;P=\"x\\\"y\":z",Status::InvalidParameters},{"SUMMARY;P=x",Status::MalformedProperty}
    };
    for(const auto& test:invalid) {
        Harness h; start(h); const size_t before=h.recorder.count;
        expect(line(h,test.first),test.second); assert(h.recorder.count==before); failure(h,test.second);
    }
    const std::vector<std::string> invalidUtf={std::string(1,'\0'),"\x01","\x7f","\x80","\xc0\xaf",
        "\xe0\x80\x80","\xed\xa0\x80","\xf4\x90\x80\x80","\xf5\x80\x80\x80","\xe2\x98","\xc3X","\r","\n"};
    for(const auto& value:invalidUtf) {
        Harness h; start(h); expect(line(h,"SUMMARY:"+value),Status::InvalidText);
        assert(h.parser.diagnostic().offset>=8); failure(h,Status::InvalidText);
    }
}

void hierarchy() {
    const std::string base="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:host\r\n";
    const std::vector<std::string> bodies={
        "BEGIN:VEVENT\r\nUID:event\r\nBEGIN:VALARM\r\nACTION:DISPLAY\r\nEND:VALARM\r\nEND:VEVENT\r\n",
        "BEGIN:VTIMEZONE\r\nTZID:Z\r\nBEGIN:STANDARD\r\nDTSTART:20261001T000000\r\nEND:STANDARD\r\nX-P:between\r\nBEGIN:DAYLIGHT\r\nDTSTART:20260301T000000\r\nEND:DAYLIGHT\r\nEND:VTIMEZONE\r\n",
        "BEGIN:VTODO\r\nUID:todo\r\nEND:VTODO\r\n",
        "BEGIN:VJOURNAL\r\nUID:journal\r\nEND:VJOURNAL\r\n",
        "BEGIN:VFREEBUSY\r\nUID:freebusy\r\nEND:VFREEBUSY\r\n",
        "BEGIN:X-OPAQUE\r\nX-P:raw\r\nBEGIN:UNREGISTERED-NAME\r\nX-P:nested\r\nEND:UNREGISTERED-NAME\r\nEND:X-OPAQUE\r\n",
        "BEGIN:UNREGISTERED-NAME\r\nX-P:raw\r\nEND:UNREGISTERED-NAME\r\n"
    };
    for(const auto& body:bodies) {
        Harness h; expect(lines(h,base+body+"END:VCALENDAR\r\n"),Status::InProgress);
        expect(finish(h),Status::StructurallyComplete); ++scenarios;
    }
    for(const char* parent:{"VEVENT","VTODO","VJOURNAL","VFREEBUSY"}) {
        Harness h;
        const std::string body=base+"BEGIN:"+parent+"\r\nUID:event\r\nSUMMARY:event-title\r\n"+
            "BEGIN:PARTICIPANT\r\nUID:person\r\nSUMMARY:person-title\r\nPARTICIPANT-TYPE:PERFORMER\r\n"+
            "BEGIN:VLOCATION\r\nUID:place\r\nEND:VLOCATION\r\n"+
            "BEGIN:VRESOURCE\r\nUID:equipment\r\nEND:VRESOURCE\r\nEND:PARTICIPANT\r\n"+
            "BEGIN:VLOCATION\r\nUID:venue\r\nEND:VLOCATION\r\n"+
            "BEGIN:VRESOURCE\r\nUID:projector\r\nEND:VRESOURCE\r\nEND:"+parent+"\r\nEND:VCALENDAR\r\n";
        expect(lines(h,body),Status::InProgress); expect(finish(h),Status::StructurallyComplete);
        uint64_t eventId=0, participantId=0;
        for(size_t i=0;i<h.recorder.count;++i) {
            const auto& e=h.recorder.events[i];
            if(e.type!=Type::Property || !h.recorder.is(e.name,"UID")) continue;
            if(h.recorder.is(e.value,"event")) { eventId=e.ordinal; assert(e.depth==2 && e.parent==1); }
            if(h.recorder.is(e.value,"person")) { participantId=e.ordinal; assert(e.kind==Kind::Participant && e.parent==eventId); }
            if(h.recorder.is(e.value,"place")) assert(e.kind==Kind::Location && e.parent==participantId && e.depth==4);
            if(h.recorder.is(e.value,"equipment")) assert(e.kind==Kind::Resource && e.parent==participantId);
            if(h.recorder.is(e.value,"projector")) assert(e.kind==Kind::Resource && e.parent==eventId);
        }
        assert(eventId && participantId); ++scenarios;
    }
    const std::vector<std::pair<std::string,Status>> bad={
        {"END:VCALENDAR",Status::InvalidNesting},{"BEGIN:VEVENT",Status::MissingRoot},
        {"BEGIN:VCALENDAR\r\nBEGIN:VCALENDAR\r\n",Status::InvalidNesting},
        {base+"BEGIN:VEVENT\r\nBEGIN:VEVENT\r\n",Status::InvalidNesting},
        {base+"BEGIN:VEVENT\r\nEND:VTODO\r\n",Status::InvalidNesting},
        {base+"BEGIN:VEVENT\r\nBEGIN:PARTICIPANT\r\nBEGIN:PARTICIPANT\r\n",Status::InvalidNesting},
        {base+"BEGIN:VTIMEZONE\r\nBEGIN:VRESOURCE\r\n",Status::InvalidNesting},
        {base+"BEGIN:VEVENT\r\nBEGIN:VRESOURCE\r\nEND:VRESOURCE\r\nBEGIN:VLOCATION\r\n",Status::InvalidNesting},
        {base+"BEGIN:VEVENT\r\nBEGIN:PARTICIPANT\r\nEND:PARTICIPANT\r\nBEGIN:VALARM\r\n",Status::InvalidNesting},
        {base+"BEGIN:VEVENT\r\nBEGIN:PARTICIPANT\r\nEND:PARTICIPANT\r\nSUMMARY:late\r\n",Status::InvalidNesting},
        {base+"BEGIN:PARTICIPANT\r\n",Status::InvalidNesting},
        {base+"BEGIN:VAVAILABILITY\r\n",Status::UnsupportedProfile},
        {base+"BEGIN:AVAILABLE\r\n",Status::UnsupportedProfile},
        {base+"BEGIN:X-WRAPPER\r\nBEGIN:VEVENT\r\n",Status::UnsupportedProfile},
        {base+"BEGIN:VEVENT\r\nBEGIN:X-UNKNOWN\r\n",Status::UnsupportedProfile},
        {base+"BEGIN:OPAQUE\r\nEND:OPAQUE\r\n",Status::InvalidNesting},
        {base+"BEGIN:VTIMEZONE\r\nTZID:Z\r\nEND:VTIMEZONE\r\n",Status::InvalidNesting}
    };
    for(const auto& test:bad) {
        Harness h;
        const Status result=test.first.find("\r\n")==std::string::npos ? line(h,test.first) : lines(h,test.first);
        expect(result,test.second); failure(h,test.second);
    }
    Harness mixed;
    expect(lines(mixed,"begin:vcalendar\r\nversion:2.0;2.0\r\nprodid:\r\nbegin:vevent\r\nend:VeVeNt\r\nend:VCALENDAR\r\n"),Status::InProgress);
    expect(finish(mixed),Status::StructurallyComplete); ++scenarios;
    Harness multiple;
    expect(lines(multiple,base+bodies[0]+"END:VCALENDAR\r\n"+base+bodies[2]+"END:VCALENDAR\r\n"),Status::InProgress);
    assert(multiple.parser.calendarCount()==2); expect(finish(multiple),Status::StructurallyComplete);
    assert(multiple.recorder.events[multiple.recorder.count-1].calendar==2); ++scenarios;
    Harness partial; start(partial); failure(partial,Status::IncompleteStructure);
    Harness empty; failure(empty,Status::MissingRoot);
    Harness trailing; start(trailing); expect(line(trailing,"END:VEVENT"),Status::InProgress);
    expect(line(trailing,"END:VCALENDAR"),Status::InProgress);
    expect(line(trailing,"SUMMARY:outside"),Status::InvalidNesting); failure(trailing,Status::InvalidNesting);
    Harness unsupportedSemantics; start(unsupportedSemantics);
    expect(line(unsupportedSemantics,"BEGIN:PARTICIPANT"),Status::InProgress);
    expect(line(unsupportedSemantics,"STRUCTURED-DATA;VALUE=BINARY;ENCODING=BASE64:SGVsbG8="),Status::InProgress);
    const auto& delivered=unsupportedSemantics.recorder.events[unsupportedSemantics.recorder.count-1];
    assert(delivered.kind==Kind::Participant && unsupportedSemantics.recorder.is(delivered.value,"SGVsbG8="));
    expect(line(unsupportedSemantics,"END:PARTICIPANT"),Status::InProgress); complete(unsupportedSemantics); ++scenarios;
    const std::vector<std::pair<std::string,Status>> metadata={
        {"BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:host\r\nEND:VCALENDAR\r\n",Status::InvalidMetadata},
        {"BEGIN:VCALENDAR\r\nBEGIN:VEVENT\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n",Status::InvalidMetadata},
        {"BEGIN:VCALENDAR\r\nVERSION:3.0\r\n",Status::UnsupportedProfile},
        {base+"VERSION:2.0\r\n",Status::InvalidMetadata},
        {base+"PRODID:other\r\n",Status::InvalidMetadata},
        {base+"METHOD:PUBLISH\r\nMETHOD:PUBLISH\r\n",Status::InvalidMetadata},
        {base+"CALSCALE:GREGORIAN\r\nCALSCALE:GREGORIAN\r\n",Status::InvalidMetadata},
        {base+"BEGIN:VEVENT\r\nEND:VEVENT\r\nVERSION:2.0\r\n",Status::InvalidNesting}
    };
    for(const auto& test:metadata) { Harness h; expect(lines(h,test.first),test.second); failure(h,test.second); }
}

void budgets() {
    auto l=evaluation(); l.logicalLine=16;
    { Harness h(l); start(h); expect(line(h,"SUMMARY:12345678"),Status::InProgress); complete(h); ++scenarios; }
    { Harness h(l); start(h); expect(line(h,"SUMMARY:123456789"),Status::ResourceLimit);
      assert(h.parser.diagnostic().limit==Limit::LogicalLine); failure(h,Status::ResourceLimit); }
    for(unsigned kind=0;kind<3;++kind) {
        l=evaluation(); std::string exact,over; Limit wanted=Limit::None;
        if(kind==0) { l.parameters=2; exact="SUMMARY;P=a;Q=b:x"; over="SUMMARY;P=a;Q=b;R=c:x"; wanted=Limit::Parameters; }
        if(kind==1) { l.valuesPerParameter=2; exact="SUMMARY;P=a,b:x"; over="SUMMARY;P=a,b,c:x"; wanted=Limit::ValuesPerParameter; }
        if(kind==2) { l.valuesPerProperty=3; exact="SUMMARY;P=a,b;Q=c:x"; over="SUMMARY;P=a,b;Q=c,d:x"; wanted=Limit::ValuesPerProperty; }
        Harness h(l); start(h); expect(line(h,exact),Status::InProgress); const size_t seen=h.recorder.count;
        expect(line(h,over),Status::ResourceLimit); assert(h.recorder.count==seen && h.parser.diagnostic().limit==wanted);
        failure(h,Status::ResourceLimit);
    }
    l=evaluation(); l.depth=2;
    { Harness h(l); start(h); expect(line(h,"BEGIN:VALARM"),Status::ResourceLimit);
      assert(h.parser.diagnostic().limit==Limit::Depth); failure(h,Status::ResourceLimit); }
    l=evaluation(); l.componentName=9;
    { Harness h(l); start(h,false); expect(line(h,"BEGIN:123456789"),Status::InProgress);
      expect(line(h,"X-P:x"),Status::InProgress); expect(line(h,"END:123456789"),Status::InProgress);
      expect(line(h,"BEGIN:1234567890"),Status::ResourceLimit); failure(h,Status::ResourceLimit); }
    l=evaluation(); l.propertiesPerComponent=2;
    { Harness h(l); start(h); expect(line(h,"P:x"),Status::InProgress); expect(line(h,"Q:y"),Status::InProgress);
      expect(line(h,"R:z"),Status::ResourceLimit); assert(h.parser.diagnostic().limit==Limit::PropertiesPerComponent); failure(h,Status::ResourceLimit); }
    l=evaluation(); l.propertiesPerSource=3;
    { Harness h(l); start(h); expect(line(h,"P:x"),Status::InProgress);
      expect(line(h,"Q:y"),Status::ResourceLimit); assert(h.parser.diagnostic().limit==Limit::PropertiesPerSource); failure(h,Status::ResourceLimit); }
    l=evaluation(); l.components=2;
    { Harness h(l); start(h); expect(line(h,"END:VEVENT"),Status::InProgress);
      expect(line(h,"BEGIN:VEVENT"),Status::ResourceLimit); assert(h.parser.diagnostic().limit==Limit::Components); failure(h,Status::ResourceLimit); }
    l=evaluation(); l.calendars=1;
    { Harness h(l); start(h); expect(line(h,"END:VEVENT"),Status::InProgress); expect(line(h,"END:VCALENDAR"),Status::InProgress);
      expect(line(h,"BEGIN:VCALENDAR"),Status::ResourceLimit); assert(h.parser.diagnostic().limit==Limit::Calendars); failure(h,Status::ResourceLimit); }
    // Exact reviewed parameter/member counts; failures emit no partial property.
    Harness maximum; start(maximum); std::string many="SUMMARY";
    for(size_t i=0;i<32;++i) many+=";P=x";
    expect(line(maximum,many+":x"),Status::InProgress); expect(line(maximum,many+";P=x:x"),Status::ResourceLimit);
    failure(maximum,Status::ResourceLimit);
    Harness values; start(values); std::string list="x";
    for(size_t i=1;i<64;++i) list+=",x";
    expect(line(values,"P;A="+list+";B="+list+":x"),Status::InProgress);
    expect(line(values,"P;A="+list+";B="+list+";C=x:x"),Status::ResourceLimit); failure(values,Status::ResourceLimit);
}

void lifecycleAndOwnership() {
    for(size_t shift=0;shift<16;++shift) {
        Harness h(evaluation(),shift); start(h); complete(h);
        const size_t seen=h.recorder.count; h.parser.release(); std::memset(h.scratch.data(),0xbd,h.scratch.size);
        expect(finish(h),Status::StructurallyComplete); assert(h.recorder.count==seen); h.scratch.check(); ++scenarios;
    }
    Guarded g(required(evaluation())); Recorder r;
    for(unsigned fault=0;fault<5;++fault) {
        Parser p; auto l=evaluation(); uint8_t* storage=g.data(); size_t size=g.size;
        if(fault==0) storage=nullptr;
        if(fault==1) --size;
        if(fault==2) storage=reinterpret_cast<uint8_t*>(UINTPTR_MAX-2);
        if(fault==3) l.componentName=0;
        if(fault==4) l.depth=SIZE_MAX;
        Audit audit; expect(p.initialize(storage,size,l,Recorder::sink,&r),Status::InvalidArgument);
        expect(p.finish(),Status::InvalidArgument); ++scenarios;
    }
    size_t size=99; auto l=evaluation(); l.componentName=SIZE_MAX;
    expect(Parser::scratchBytes(l,size),Status::InvalidArgument); assert(size==0);
    Parser fresh; expect(fresh.finish(),Status::NotInitialized);
    { Harness h; start(h); Audit audit;
      expect(h.parser.consume(reinterpret_cast<const uint8_t*>(UINTPTR_MAX-2),4),Status::InvalidArgument); }
    { Harness h; start(h); Audit audit; expect(h.parser.consume(nullptr,1),Status::InvalidArgument); }
    { Harness h; start(h); Audit audit; expect(h.parser.consume(g.data(),SIZE_MAX),Status::InvalidArgument); }
    { Harness h; start(h); Audit audit; expect(h.parser.consume(h.scratch.data(),1),Status::InvalidArgument); }
    { Harness h; start(h); expect(h.parser.cancel(),Status::Cancelled); failure(h,Status::Cancelled); }
    { Harness h; start(h); h.recorder.failAt=h.recorder.count;
      expect(line(h,"SUMMARY:x"),Status::ConsumerFailure); failure(h,Status::ConsumerFailure); }
    for(unsigned action=1;action<=5;++action) {
        Harness h; h.recorder.parser=&h.parser; h.recorder.reenter=action;
        expect(line(h,"BEGIN:VCALENDAR"),Status::InvalidState); failure(h,Status::InvalidState);
    }
    { Harness h; start(h); complete(h); expect(line(h,"BEGIN:VCALENDAR"),Status::InvalidState); failure(h,Status::InvalidState); }
    { Harness h; start(h); h.parser.release(); expect(finish(h),Status::Cancelled);
      expect(h.parser.initialize(g.data(),g.size,evaluation(),Recorder::sink,&r),Status::Cancelled); }
    // Mutate/destroy every input; names needed for END came from parser scratch.
    { Harness h;
      { std::string input="BEGIN:VCALENDAR"; expect(line(h,input),Status::InProgress); std::fill(input.begin(),input.end(),'!'); }
      expect(line(h,"VERSION:2.0"),Status::InProgress); expect(line(h,"PRODID:host"),Status::InProgress);
      { std::string input="BEGIN:VEVENT"; expect(line(h,input),Status::InProgress); std::fill(input.begin(),input.end(),'!'); }
      { std::string input="UID:full-identity\\,unmodified"; expect(line(h,input),Status::InProgress); std::fill(input.begin(),input.end(),'!'); }
      complete(h); assert(h.recorder.is(h.recorder.events[4].value,"full-identity\\,unmodified")); ++scenarios; }
}

uint64_t clock(void*) { return 0; }
IcsIngestion::Limits transportLimits() { return {16777216,8388608,16384,64,2048,256,8192,32,2048,4096,16384,120000,10000}; }
std::string response(const std::string& body,bool chunks) {
    if(!chunks) return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    std::string wire="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for(size_t i=0;i<body.size();i+=7) {
        const size_t n=std::min(size_t(7),body.size()-i); char length[24];
        std::snprintf(length,sizeof(length),"%zx",n); wire+=std::string(length)+"\r\n"+body.substr(i,n)+"\r\n";
    }
    return wire+"0\r\nX-Trailer: valid\r\n\r\n";
}
void integrationCase(const std::string& body,bool chunks,size_t split,bool oneByte,
                     IcsIngestion::Status httpExpected,Status parserExpected,bool refuse=false) {
    Harness h; const auto limits=transportLimits(); Guarded scratch(limits.metadataLine+limits.location+limits.logicalLine);
    if(refuse) h.recorder.failAt=4;
    IcsIngestion http;
    { Audit audit; assert(http.initialize(scratch.data(),scratch.size,limits,Parser::lineSink,&h.parser,clock,nullptr)==IcsIngestion::Status::InProgress); }
    const std::string wire=response(body,chunks); auto result=IcsIngestion::Status::InProgress;
    if(oneByte) {
        for(size_t i=0;i<wire.size() && result==IcsIngestion::Status::InProgress;++i) {
            Audit audit; result=http.feed(bytes(wire)+i,1);
        }
    } else {
        assert(split<=wire.size());
        { Audit audit; result=http.feed(bytes(wire),split); }
        if(result==IcsIngestion::Status::InProgress) { Audit audit; result=http.feed(bytes(wire)+split,wire.size()-split); }
    }
    if(result==IcsIngestion::Status::InProgress) { Audit audit; result=http.finish(); }
    assert(result==httpExpected);
    if(httpExpected==IcsIngestion::Status::Complete) expect(finish(h),parserExpected);
    else { expect(h.parser.status(),parserExpected); expect(finish(h),parserExpected); }
    if(parserExpected==Status::StructurallyComplete) {
        bool summary=false;
        for(size_t i=0;i<h.recorder.count;++i) {
            const auto& e=h.recorder.events[i];
            if(e.type==Type::Property && h.recorder.is(e.name,"SUMMARY")) {
                assert(h.recorder.is(e.value,"caf\xc3\xa9: value; intact")); summary=true;
            }
        }
        assert(summary);
    }
    http.release(); scratch.check(); h.scratch.check(); ++scenarios;
}
void integrationAndDependencies() {
    const std::string body="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:host\r\nBEGIN:VEVENT\r\n"+
        std::string("SUMMARY:caf\xc3\r\n \xa9: value; intact\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n");
    for(bool chunked:{false,true}) {
        const size_t n=response(body,chunked).size();
        for(size_t split=0;split<=n;++split) integrationCase(body,chunked,split,false,IcsIngestion::Status::Complete,Status::StructurallyComplete);
        integrationCase(body,chunked,0,true,IcsIngestion::Status::Complete,Status::StructurallyComplete);
    }
    const std::string invalid="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:h\r\nBEGIN:VEVENT\r\nSUMMARY;P=\"unterminated:x\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    integrationCase(invalid,false,0,true,IcsIngestion::Status::CallbackFailure,Status::InvalidParameters);
    integrationCase(body,true,0,true,IcsIngestion::Status::CallbackFailure,Status::ConsumerFailure,true);
    integrationCase("",false,0,true,IcsIngestion::Status::Complete,Status::MissingRoot);
    integrationCase("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:h\r\nBEGIN:VEVENT\r\n",true,0,true,IcsIngestion::Status::Complete,Status::IncompleteStructure);
    const std::string root="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:host\r\n";
    const std::string master="BEGIN:VEVENT\r\nUID:full-series-id\r\nDTSTART;TZID=Late-Zone:20200101T090000\r\nRRULE:FREQ=DAILY\r\nEXDATE;TZID=Late-Zone:20261013T090000\r\nRDATE;TZID=Late-Zone:20261014T100000\r\nEND:VEVENT\r\n";
    const std::string moved="BEGIN:VEVENT\r\nUID:full-series-id\r\nRECURRENCE-ID;RANGE=THISANDFUTURE;TZID=Late-Zone:20261012T090000\r\nDTSTART;TZID=Late-Zone:20261018T110000\r\nSTATUS:CANCELLED\r\nEND:VEVENT\r\n";
    const std::string timezone="BEGIN:VTIMEZONE\r\nTZID:Late-Zone\r\nBEGIN:STANDARD\r\nDTSTART:20200101T000000\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0600\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n";
    for(bool reverse:{false,true}) {
        Harness h; expect(lines(h,root+(reverse?moved+master:master+moved)+timezone+"END:VCALENDAR\r\n"),Status::InProgress);
        expect(finish(h),Status::StructurallyComplete); size_t identities=0; bool recurrence=false,zone=false;
        for(size_t i=0;i<h.recorder.count;++i) {
            const auto& e=h.recorder.events[i];
            if(e.type!=Type::Property) continue;
            if(h.recorder.is(e.name,"UID")) { assert(h.recorder.is(e.value,"full-series-id")); ++identities; }
            if(h.recorder.is(e.name,"RECURRENCE-ID")) { assert(e.count==2 && h.recorder.is(e.value,"20261012T090000")); recurrence=true; }
            if(h.recorder.is(e.name,"TZID")) { assert(e.kind==Kind::Timezone); zone=true; }
        }
        assert(identities==2 && recurrence && zone); ++scenarios;
    }
    Harness distinct;
    expect(lines(distinct,root+"BEGIN:VEVENT\r\nUID:A\r\nSUMMARY:same\r\nEND:VEVENT\r\n"
        "BEGIN:VEVENT\r\nUID:B\r\nSUMMARY:same\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n"),Status::InProgress);
    expect(finish(distinct),Status::StructurallyComplete);
    size_t uidCount=0; uint64_t previous=0;
    for(size_t i=0;i<distinct.recorder.count;++i) {
        const auto& e=distinct.recorder.events[i];
        if(e.type==Type::Property && distinct.recorder.is(e.name,"UID")) {
            assert(distinct.recorder.is(e.value,uidCount?"B":"A") && e.ordinal!=previous);
            previous=e.ordinal; ++uidCount;
        }
    }
    assert(uidCount==2); ++scenarios;
}
}
int main() {
    static_assert(!std::is_copy_constructible<Parser>::value && !std::is_move_constructible<Parser>::value,"scratch-bound owner");
    properties(); hierarchy(); budgets(); lifecycleAndOwnership(); integrationAndDependencies();
    assert(allocations==0);
    std::printf("PASS: %zu structure/property/fragmentation scenarios; zero parser allocations; scratch=%zu sizeof parser=%zu\n",
        scenarios,required(evaluation()),sizeof(Parser));
}
