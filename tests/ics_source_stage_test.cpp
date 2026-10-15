#include "../application/ics_ingestion.h"
#include "../application/ics_structure.h"
#include <initializer_list>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>
// Test-only visibility to inject busy/counter states. Dependencies are already
// included; all tested methods remain the actual production implementation.
#define private public
#include "../application/ics_source_stage.h"
#undef private
using Stage = IcsSourceStage;
using S = Stage::Status;
using D = Stage::Disposition;
using C = Stage::Code;
using H = IcsIngestion::Status;
using P = IcsStructure::Status;
namespace { bool audit = false; size_t allocations = 0, scenarios = 0; }
void* operator new(size_t n) {
    if (audit) ++allocations;
    void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
namespace {
struct Audit { size_t before; Audit() : before(allocations) { assert(!audit); audit=true; }
    ~Audit() { audit=false; assert(before==allocations); } };
struct Guard {
    std::vector<uint8_t> bytes; size_t offset, capacity;
    Guard(size_t n, size_t shift=0) : bytes(n+64,0xa5), offset(16+shift), capacity(n) {}
    uint8_t* data() { return bytes.data()+offset; }
    void check() const {
        for (size_t i=0;i<offset;++i) assert(bytes[i]==0xa5);
        for (size_t i=offset+capacity;i<bytes.size();++i) assert(bytes[i]==0xa5);
    }
};
uint64_t clock(void*) { return 0; }
Stage::Limits limits() { return {128,1024,262144,256,65536}; }
IcsStructure::Limits parserLimits() { return {16384,32,64,128,8,64,4096,65536,4096,4}; }
IcsIngestion::Limits httpLimits() { return {16777216,8388608,16384,64,2048,256,8192,32,2048,4096,16384,120000,10000}; }
size_t required(Stage::Limits l) { size_t n=0; assert(Stage::requiredBytes(l,n)==S::InProgress); return n; }
size_t parserBytes(IcsStructure::Limits l) { size_t n=0; assert(IcsStructure::scratchBytes(l,n)==P::InProgress); return n; }
struct Session {
    Stage stage; IcsStructure parser; IcsIngestion http;
    Guard storage, structure, ingest;
    Session(Stage::Limits l=limits(),size_t shift=0,IcsStructure::Limits pl=parserLimits()) :
        storage(required(l),shift), structure(parserBytes(pl)), ingest(2048+2048+16384) {
        Audit a;
        assert(stage.initialize(storage.data(),storage.capacity,l)==S::InProgress);
        assert(parser.initialize(structure.data(),structure.capacity,pl,Stage::sink,&stage)==P::InProgress);
        assert(http.initialize(ingest.data(),ingest.capacity,httpLimits(),IcsStructure::lineSink,&parser,clock,nullptr)==H::InProgress);
    }
    ~Session() { Audit a; stage.release(); parser.release(); http.release(); storage.check(); structure.check(); ingest.check(); }
};
const std::string root="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:synthetic\r\n";
std::string calendar(const std::string& body) { return root+body+"END:VCALENDAR\r\n"; }
std::string event(const std::string& body) { return "BEGIN:VEVENT\r\n"+body+"END:VEVENT\r\n"; }
std::string wire(const std::string& body,bool chunked=false) {
    if (!chunked) return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    std::string out="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for (size_t i=0;i<body.size();) {
        const size_t n=std::min<size_t>(7,body.size()-i); char size[32];
        std::snprintf(size,sizeof(size),"%zx",n); out+=size; out+="\r\n"; out.append(body,i,n); out+="\r\n"; i+=n;
    }
    return out+"0\r\nX-Trace: test\r\n\r\n";
}
void parse(Session& s,const std::string& body,bool chunked=false,size_t split=SIZE_MAX,bool oneByte=false) {
    std::string raw=wire(body,chunked); H result=H::InProgress;
    { Audit a;
      if (oneByte) { for (size_t i=0;i<raw.size() && result==H::InProgress;++i)
          result=s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()+i),1); }
      else { const size_t n=std::min(split,raw.size()); result=s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),n);
          if (result==H::InProgress) result=s.http.feed(reinterpret_cast<const uint8_t*>(raw.data()+n),raw.size()-n); }
      if (result==H::InProgress) result=s.http.finish();
      if (result==H::Complete) s.parser.finish();
      s.stage.finish(s.http,s.parser);
    }
    ++scenarios;
}
bool equal(Stage::View v,const std::string& text) {
    return v.size==text.size() && (!v.size || (v.data && !std::memcmp(v.data,text.data(),v.size)));
}
size_t find(const Stage& s,C code,size_t owner=SIZE_MAX,size_t start=0) {
    for (size_t i=start;i<s.counts().storedProperties;++i) {
        const auto* p=s.property(i);
        if (p && p->code==code && (owner==SIZE_MAX || p->owner==owner)) return i;
    }
    return SIZE_MAX;
}
void value(const Stage& s,size_t i,const std::string& expected,D disposition) {
    Stage::View v; assert(s.value(i,v)==Stage::Read::Ok && equal(v,expected));
    assert(s.property(i)->disposition==disposition);
}
void successful(Session& s) {
    if (s.http.status()!=H::Complete || s.parser.status()!=P::StructurallyComplete || s.stage.status()!=S::StagedSuccessfully)
        std::fprintf(stderr,"scenario %zu: http=%u parser=%u stage=%u line=%llu limit=%u\n",scenarios,
            unsigned(s.http.status()),unsigned(s.parser.status()),unsigned(s.stage.status()),
            (unsigned long long)s.parser.diagnostic().line,unsigned(s.stage.diagnostic().limit));
    assert(s.http.status()==H::Complete && s.parser.status()==P::StructurallyComplete && s.stage.status()==S::StagedSuccessfully);
    const auto n=s.stage.counts(); assert(n.observedProperties==n.storedProperties+n.ignoredOpaqueProperties);
    for (size_t i=0;i<n.components;++i) {
        const auto* c=s.stage.component(i); assert(c && c->closed && c->ordinal==i+1);
        size_t count=0; uint32_t last=Stage::None;
        for (uint32_t p=c->firstProperty;p!=Stage::None;p=s.stage.property(p)->next) {
            assert(p<n.storedProperties && s.stage.property(p)->owner==i); last=p; ++count;
        }
        assert(last==c->lastProperty);
        if (c->flags & Stage::OpaqueProjectedOut) assert(!count && s.stage.payload(i)==Stage::Read::PayloadProjectedOut);
        else assert(count==c->observedProperties);
    }
}
void failure(Session& s,S expected) {
    assert(s.stage.status()==expected); const auto d=s.stage.diagnostic();
    assert(!s.stage.component(0) && !s.stage.property(0));
    assert(s.stage.cancel()==expected && s.stage.finish(s.http,s.parser)==expected);
    s.stage.release(); assert(s.stage.status()==expected && s.stage.diagnostic().limit==d.limit);
}
void dependencies() {
    const std::string uid=std::string(300,'u')+"\xc3\xa9";
    const std::string props="UID:"+uid+"\r\nDTSTART;TZID=Late-Zone:20200101T090000\r\nDTEND;TZID=Late-Zone:20200101T100000\r\n"
        "DURATION:PT1H\r\nRECURRENCE-ID;RANGE=THISANDFUTURE;TZID=Late-Zone:20261012T090000\r\n"
        "RRULE:FREQ=DAILY;INTERVAL=2\r\nRDATE;VALUE=PERIOD:20261014T090000Z/PT1H,20261015T090000Z/PT2H\r\n"
        "RDATE:20261016T090000Z\r\nEXDATE;TZID=Late-Zone:20261012T090000\r\nEXDATE:20261013T090000Z\r\n"
        "STATUS:CANCELLED\r\nSEQUENCE:2\r\nDTSTAMP:20261001T000000Z\r\nLAST-MODIFIED:20261002T000000Z\r\n"
        "SUMMARY:caf\xc3\xa9\\nTitle\r\nCREATED:20200101T000000Z\r\nDESCRIPTION;LANGUAGE=en:full\\nDetails\r\n"
        "LOCATION:Room: 2; East\r\nCOMMENT:comment\r\nORGANIZER:mailto:organizer@example.invalid\r\n"
        "ATTENDEE;CN=\"Doe, John\";ROLE=REQ-PARTICIPANT:mailto:person@example.invalid\r\nREQUEST-STATUS:2.0;ok\r\n"
        "RELATED-TO:another\r\nTRANSP:OPAQUE\r\nCLASS:PRIVATE\r\nPRIORITY:1\r\nCATEGORIES:work\r\nCONTACT:contact\r\n"
        "RESOURCES:room\r\nGEO:1;2\r\nURL:https://example.invalid/event\r\n";
    Session s; parse(s,root+"METHOD:CANCEL\r\nCALSCALE:GREGORIAN\r\n"+event(props)+"END:VCALENDAR\r\n"); successful(s);
    value(s.stage,find(s.stage,C::Uid),uid,D::RetainRequired);
    const auto recurrence=find(s.stage,C::RecurrenceId);
    value(s.stage,recurrence,"20261012T090000",D::RetainRequired);
    assert(equal(s.stage.parameters(recurrence),";RANGE=THISANDFUTURE;TZID=Late-Zone"));
    value(s.stage,find(s.stage,C::Description),"full\\nDetails",D::RetainConditional);
    value(s.stage,find(s.stage,C::Location),"Room: 2; East",D::RetainConditional);
    value(s.stage,find(s.stage,C::Method),"CANCEL",D::RetainRequired);
    const auto rdate=find(s.stage,C::Rdate); assert(find(s.stage,C::Rdate,SIZE_MAX,rdate+1)!=SIZE_MAX);
    const auto exdate=find(s.stage,C::Exdate); assert(find(s.stage,C::Exdate,SIZE_MAX,exdate+1)!=SIZE_MAX);
    assert(s.stage.counts().reviewItems==0);
    std::memset(s.ingest.data(),0xee,s.ingest.capacity); std::memset(s.structure.data(),0xee,s.structure.capacity);
    value(s.stage,find(s.stage,C::Uid),uid,D::RetainRequired);
    value(s.stage,find(s.stage,C::Summary),"caf\xc3\xa9\\nTitle",D::RetainRequired);
}
void disposition() {
    const std::string body=event("UID:event\r\nSUMMARY:\r\nATTACH:document\r\nIMAGE;VALUE=URI:image\r\n"
        "ATTACH;ENCODING=BASE64;VALUE=BINARY:QUJD\r\nATTACH;VALUE=URI;FMTTYPE=text/plain:document\r\n"
        "IMAGE;VALUE=URI;X-P=^n^'^^^z:image\r\nDESCRIPTION;X-P=x:complete\r\nX-CUSTOM;LABEL=\"a;b:c,d\":value\r\n"
        "STYLED-DESCRIPTION:styled\r\nCONFERENCE:conference\r\n")+
        "BEGIN:VTODO\r\nUID:task\r\nRECURRENCE-ID:20261001T000000Z\r\nDESCRIPTION:task-payload\r\n"
        "BEGIN:PARTICIPANT\r\nUID:task-person\r\nSUMMARY:task-person-payload\r\nEND:PARTICIPANT\r\nEND:VTODO\r\n"
        "BEGIN:VJOURNAL\r\nUID:journal\r\nDESCRIPTION:journal-payload\r\nEND:VJOURNAL\r\n"
        "BEGIN:VFREEBUSY\r\nUID:free\r\nFREEBUSY:20261001T000000Z/PT1H\r\nEND:VFREEBUSY\r\n";
    Session s; parse(s,calendar(body)); successful(s);
    Stage::View v; const auto attach=find(s.stage,C::Attach);
    assert(s.stage.value(attach,v)==Stage::Read::ValueOmitted && !v.data && !v.size && s.stage.property(attach)->valueLength==8);
    value(s.stage,find(s.stage,C::Summary,1),"",D::RetainRequired);
    assert(find(s.stage,C::Dtstart)==SIZE_MAX);
    const auto extra=find(s.stage,C::Attach,SIZE_MAX,find(s.stage,C::Attach,SIZE_MAX,attach+1)+1);
    value(s.stage,extra,"document",D::RetainConditional);
    size_t unknown=find(s.stage,C::Unknown); value(s.stage,unknown,"value",D::RetainUnreviewed);
    assert(equal(s.stage.propertyName(unknown),"X-CUSTOM") && equal(s.stage.parameters(unknown),";LABEL=\"a;b:c,d\""));
    auto values=IcsStructure::ParameterIterator(s.stage.parameters(unknown)); IcsStructure::Parameter p;
    assert(values.next(p) && IcsStructure::identifier(p.name,"LABEL"));
    const auto description=find(s.stage,C::Description,1);
    assert(s.stage.property(description)->flags & Stage::UnknownParameters);
    value(s.stage,description,"complete",D::RetainConditional);
    for (size_t owner:{size_t(2),size_t(3),size_t(4),size_t(5)}) {
        assert(s.stage.component(owner)->flags & Stage::ProjectedOut);
        value(s.stage,find(s.stage,C::Uid,owner),owner==2?"task":owner==3?"task-person":owner==4?"journal":"free",D::RetainConditional);
    }
    for (auto pair:{std::make_pair(C::Description,size_t(2)),std::make_pair(C::Summary,size_t(3)),
                    std::make_pair(C::Description,size_t(4)),std::make_pair(C::Freebusy,size_t(5))})
        assert(s.stage.value(find(s.stage,pair.first,pair.second),v)==Stage::Read::ValueOmitted);
}
void opaqueAndAssociation() {
    const std::string core=event("UID:event\r\nSUMMARY:event-title\r\nBEGIN:PARTICIPANT\r\nUID:person\r\nSUMMARY:person-title\r\n"
        "BEGIN:VLOCATION\r\nUID:place\r\nNAME:venue\r\nEND:VLOCATION\r\nBEGIN:VRESOURCE\r\nUID:projector\r\nEND:VRESOURCE\r\n"
        "END:PARTICIPANT\r\n");
    std::string opaque="BEGIN:X-CUSTOM\r\nUID:event\r\nSUMMARY:wrong\r\nRRULE:wrong\r\n";
    for (int i=0;i<20;++i) opaque+="X-DATA:"+std::string(2000,'z')+"\r\n";
    opaque+="BEGIN:REGISTERED-LOOKING\r\nX-P:ignored\r\nEND:REGISTERED-LOOKING\r\nEND:X-CUSTOM\r\n";
    Session baseline; parse(baseline,calendar(core)); successful(baseline);
    auto l=limits(); l.properties=baseline.stage.counts().storedProperties; l.reviewItems=l.reviewBytes=0;
    Session s(l); parse(s,calendar(core+opaque)+calendar(event("UID:event\r\nSUMMARY:second-object\r\n"))); // second object needs slots
    failure(s,S::ResourceFailure);
    Session ok(l); parse(ok,calendar(core+opaque),true,SIZE_MAX,true); successful(ok);
    const auto before=baseline.stage.counts(), after=ok.stage.counts();
    assert(after.storedProperties==before.storedProperties && after.reviewItems==0 && after.ignoredOpaqueProperties==24);
    assert(after.poolBytes-before.poolBytes==std::strlen("X-CUSTOM")+std::strlen("REGISTERED-LOOKING"));
    value(ok.stage,find(ok.stage,C::Uid,1),"event",D::RetainRequired);
    value(ok.stage,find(ok.stage,C::Uid,2),"person",D::RetainRequired);
    assert(ok.stage.component(3)->parentOrdinal==3 && ok.stage.component(4)->parentOrdinal==3);
    assert(ok.stage.component(5)->flags & Stage::OpaqueProjectedOut);
    assert(ok.stage.component(6)->parentOrdinal==6 && ok.stage.component(6)->observedProperties==1);
    Session multi; parse(multi,calendar(core+opaque)+calendar(event("UID:event\r\nSUMMARY:second-object\r\n"))); successful(multi);
    assert(multi.stage.counts().calendars==2 && multi.stage.component(7)->calendarOrdinal==2);
    Session only; parse(only,calendar("BEGIN:CUSTOM\r\nUID:ignored\r\nEND:CUSTOM\r\n")); successful(only);
    assert(only.stage.counts().storedProperties==2 && only.stage.counts().ignoredOpaqueProperties==1);
}
void timezoneAndOrder() {
    const std::string master=event("UID:series\r\nDTSTART;TZID=Late:20000101T090000\r\nRRULE:FREQ=DAILY\r\nEXDATE:20261012T090000Z\r\n");
    const std::string moved=event("UID:series\r\nRECURRENCE-ID;RANGE=THISANDFUTURE:20261012T090000Z\r\nDTSTART:20261018T110000Z\r\nSTATUS:CANCELLED\r\nSEQUENCE:4\r\n");
    const std::string zone="BEGIN:VTIMEZONE\r\nTZID:Late\r\nLAST-MODIFIED:20200101T000000Z\r\n"
        "BEGIN:STANDARD\r\nDTSTART:20000101T000000\r\nTZOFFSETFROM:-0500\r\nTZOFFSETTO:-0600\r\n"
        "RRULE:FREQ=YEARLY\r\nRDATE:20200101T000000\r\nEXDATE:20210101T000000\r\nX-ZONE;P=^n:rule\r\n"
        "TZNAME:label\r\nEND:STANDARD\r\nTZURL:https://example.invalid/zone\r\nX-TZ:opaque-rule\r\n"
        "BEGIN:DAYLIGHT\r\nDTSTART:20000301T000000\r\nTZOFFSETFROM:-0600\r\nTZOFFSETTO:-0500\r\nRDATE:20200301T000000\r\n"
        "END:DAYLIGHT\r\nEND:VTIMEZONE\r\n";
    for (bool reverse:{false,true}) for (bool late:{false,true}) {
        Session s; parse(s,calendar((late?"":zone)+(reverse?moved+master:master+moved)+(late?zone:""))); successful(s);
        size_t uid=find(s.stage,C::Uid); value(s.stage,uid,"series",D::RetainRequired);
        value(s.stage,find(s.stage,C::Uid,SIZE_MAX,uid+1),"series",D::RetainRequired);
        size_t timezone=0; for(size_t i=0;i<s.stage.counts().components;++i) if(s.stage.component(i)->kind==Stage::Kind::Timezone) timezone=i;
        assert(timezone && s.stage.component(timezone)->observedProperties==4);
        value(s.stage,find(s.stage,C::Tzid),"Late",D::RetainRequired);
        value(s.stage,find(s.stage,C::Tzurl),"https://example.invalid/zone",D::RetainConditional);
        assert(find(s.stage,C::Unknown,timezone)!=SIZE_MAX);
    }
    Session distinct; parse(distinct,calendar(event("UID:"+std::string(300,'a')+"A\r\nSUMMARY:same\r\n")+
        event("UID:"+std::string(300,'a')+"B\r\nSUMMARY:same\r\n"))); successful(distinct);
    const auto first=find(distinct.stage,C::Uid), second=find(distinct.stage,C::Uid,SIZE_MAX,first+1);
    Stage::View a,b; distinct.stage.value(first,a); distinct.stage.value(second,b); assert(a.size==301 && b.size==301 && a.data[300]!=b.data[300]);
}
void limitsAndFailures() {
    const auto body=calendar(event("UID:identity\r\nSUMMARY:title\r\nX-UNKNOWN;X-P=a:bytes\r\n"));
    Session base; parse(base,body); successful(base); const auto n=base.stage.counts();
    auto exact=limits(); exact.components=n.components; exact.properties=n.storedProperties; exact.poolBytes=n.poolBytes;
    exact.reviewItems=n.reviewItems; exact.reviewBytes=n.reviewBytes;
    for (size_t shift=0;shift<8;++shift) { Session s(exact,shift); parse(s,body); successful(s); }
    for (int kind=0;kind<5;++kind) {
        auto l=exact;
        if(kind==0)--l.components;
        if(kind==1)--l.properties;
        if(kind==2)--l.poolBytes;
        if(kind==3)--l.reviewItems;
        if(kind==4)--l.reviewBytes;
        // For pool shrink, review budget must remain representable.
        Session s(l); parse(s,body); failure(s,S::ResourceFailure);
        assert(s.parser.status()==P::ConsumerFailure && s.http.status()==H::CallbackFailure);
    }
    const std::vector<std::string> errors={
        calendar(event("UID:ok\r\n")+"BEGIN:X-WRAPPER\r\nBEGIN:VEVENT\r\n"),
        calendar(event("UID:ok\r\n")+"BEGIN:VAVAILABILITY\r\n"),
        calendar(event("UID:ok\r\n")+"BEGIN:AVAILABLE\r\n"),
        calendar(event("UID:ok\r\n")+"BEGIN:X-WRAPPER\r\nX-P:a\r\nEND:WRONG\r\n"),
        root+event("UID:ok\r\n")+"BEGIN:X-WRAPPER\r\nX-P:a\r\n",
        ""};
    for(const auto& text:errors) { Session s; parse(s,text); failure(s,S::UpstreamFailure); }
    Session truncated; const auto raw=wire(body); { Audit a;
        assert(truncated.http.feed(reinterpret_cast<const uint8_t*>(raw.data()),raw.size()-1)==H::InProgress);
        assert(truncated.http.finish()==H::IncompleteFraming);
        assert(truncated.stage.finish(truncated.http,truncated.parser)==S::UpstreamFailure); }
    failure(truncated,S::UpstreamFailure);
    Session cancelled; assert(cancelled.stage.cancel()==S::Cancelled); failure(cancelled,S::Cancelled);
    auto l=limits(); l.properties=2;
    Session huge(l); parse(huge,calendar(event("UID:"+std::string(2000,'r')+"\r\n"))); failure(huge,S::ResourceFailure);
    Session opaqueCap; auto small=parserLimits(); small.propertiesPerComponent=2;
    Session opaqueLimit(limits(),0,small); parse(opaqueLimit,calendar("BEGIN:CUSTOM\r\nX-A:a\r\nX-B:b\r\nX-C:c\r\nEND:CUSTOM\r\n"));
    failure(opaqueLimit,S::UpstreamFailure);
    Session ignored; parse(ignored,calendar(event("BEGIN:VALARM\r\nACTION:DISPLAY\r\nDESCRIPTION:"+std::string(2000,'i')+"\r\nEND:VALARM\r\n")));
    successful(ignored); assert(ignored.stage.counts().poolBytes<100);
}
void lifecycleAndArithmetic() {
    static_assert(sizeof(Stage::Component)==48 && alignof(Stage::Component)==8,"component layout");
    static_assert(sizeof(Stage::Property)==40 && alignof(Stage::Property)==4,"property layout");
    static_assert(!std::is_copy_constructible<Stage>::value && !std::is_move_constructible<Stage>::value,"one binding");
    auto l=limits(); size_t n=0; assert(Stage::requiredBytes(l,n)==S::InProgress);
    for(int kind=0;kind<6;++kind) {
        auto invalid=l;
        if(kind==0)invalid.components=0;
        if(kind==1)invalid.components=SIZE_MAX;
        if(kind==2)invalid.properties=SIZE_MAX;
        if(kind==3)invalid.poolBytes=SIZE_MAX;
        if(kind==4)invalid.reviewItems=SIZE_MAX;
        if(kind==5)invalid.reviewBytes=invalid.poolBytes+1;
        assert(Stage::requiredBytes(invalid,n)==S::InvalidArgument && n==0);
    }
    for(int kind=0;kind<3;++kind) { Stage s; Guard b(required(l)); uint8_t* p=b.data(); size_t capacity=b.capacity;
        if(kind==0)p=nullptr;
        if(kind==1)--capacity;
        if(kind==2)p=reinterpret_cast<uint8_t*>(UINTPTR_MAX-8);
        assert(s.initialize(p,capacity,l)==S::InvalidArgument); s.release(); b.check(); }
    for(int cycle=0;cycle<4;++cycle) { Session s; parse(s,calendar(event("UID:test\r\n"))); successful(s);
        s.stage.release(); std::memset(s.storage.data(),0xff,s.storage.capacity);
        assert(!s.stage.property(0) && s.stage.payload(0)==Stage::Read::InvalidState);
        assert(s.stage.initialize(s.storage.data(),s.storage.capacity,l)==S::InvalidState); }
    Stage busy; Guard b(required(l)); assert(busy.initialize(b.data(),b.capacity,l)==Stage::Status::InProgress);
    busy._busy=true; busy.release(); assert(busy.status()==Stage::Status::InvalidState); busy._busy=false; busy.release();
    for (int operation=0;operation<4;++operation) {
        Stage guarded; Guard memory(required(l)); assert(guarded.initialize(memory.data(),memory.capacity,l)==S::InProgress);
        guarded._busy=true; IcsStructure::Event call={}; IcsIngestion http; IcsStructure parser;
        if(operation==0) guarded.consume(call);
        if(operation==1) guarded.finish(http,parser);
        if(operation==2) guarded.cancel();
        if(operation==3) guarded.initialize(memory.data(),memory.capacity,l);
        assert(guarded.status()==S::InvalidState); guarded._busy=false; guarded.release(); memory.check();
    }
    Session invalid; IcsStructure::Event e={}; e.type=IcsStructure::EventType::Property;
    assert(invalid.stage.consume(e)==S::InvalidAssociation); failure(invalid,S::InvalidAssociation);
    // Direct span overlap is rejected before any copy.
    Session overlap; IcsStructure::Event begin={}; begin.type=IcsStructure::EventType::BeginComponent;
    begin.component={{overlap.storage.data(),9},Stage::Kind::Calendar,1,1,0,1};
    assert(overlap.stage.consume(begin)==S::InvalidAssociation); failure(overlap,S::InvalidAssociation);
}
void additionalEdges() {
    const char* unknownBodies[]={"X-EVENT;X-ARG=^n^'^^^z:full", "STRUCTURED-DATA;VALUE=URI:reference"};
    for(const char* field:unknownBodies) {
        Session s; parse(s,calendar(event(std::string(field)+"\r\n"))); successful(s);
        assert(s.stage.counts().reviewItems==1);
    }
    // Every relevant timezone/observance context preserves unknown content.
    for(const char* kind:{"STANDARD","DAYLIGHT"}) {
        const auto body="BEGIN:VTIMEZONE\r\nTZID:Zone\r\nX-TZ;X-P=\"^n^'^^^z\":full\r\nBEGIN:"+
            std::string(kind)+"\r\nDTSTART:20000101T000000\r\nTZOFFSETFROM:+0000\r\nTZOFFSETTO:+0100\r\n"
            "X-RULE;X-P=\"^n^'^^^z\":dependency\r\nEND:"+kind+"\r\nEND:VTIMEZONE\r\n";
        Session s; parse(s,calendar(body)); successful(s);
        assert(s.stage.counts().reviewItems==2);
        const auto unknown=find(s.stage,C::Unknown); value(s.stage,unknown,"full",D::RetainUnreviewed);
        assert(equal(s.stage.parameters(unknown),";X-P=\"^n^'^^^z\""));
    }
    for(const char* header:{"ATTACH;VALUE=URI;VALUE=URI:x", "IMAGE;VALUE=URI,BINARY:x", "IMAGE:x",
        "ATTACH;VALUE=URI;ENCODING=BASE64:x", "ATTACH;VALUE=BINARY:x"}) {
        Session s; parse(s,calendar(event(std::string(header)+"\r\n"))); successful(s);
        const auto i=s.stage.counts().storedProperties-1; value(s.stage,i,"x",D::RetainConditional);
    }
    for (const char* header:{"attach;value=\"uri\":payload", "IMAGE;encoding=\"base64\";value=binary:payload"}) {
        Session s; parse(s,calendar(event(std::string(header)+"\r\n"))); successful(s);
        Stage::View v; assert(s.stage.value(s.stage.counts().storedProperties-1,v)==Stage::Read::ValueOmitted);
    }
    Session unknownOmission;
    parse(unknownOmission,calendar("BEGIN:VTODO\r\nDESCRIPTION;X-P=x:must survive\r\nEND:VTODO\r\n")); successful(unknownOmission);
    value(unknownOmission.stage,find(unknownOmission.stage,C::Description),"must survive",D::RetainUnreviewed);
    Session largeBase; const auto large=calendar(event("DESCRIPTION:"+std::string(3000,'r')+"\r\n"));
    parse(largeBase,large); successful(largeBase);
    auto l=limits(); l.poolBytes=largeBase.stage.counts().poolBytes-1; l.reviewBytes=0;
    Session largeFailure(l); parse(largeFailure,large); failure(largeFailure,S::ResourceFailure);
    assert(largeFailure.stage.diagnostic().limit==Stage::Limit::Pool);
    Session timezoneParameter;
    parse(timezoneParameter,calendar("BEGIN:VTIMEZONE\r\nTZID:Zone\r\nBEGIN:STANDARD\r\n"
        "DTSTART:20000101T000000\r\nTZOFFSETFROM:+0000\r\nTZOFFSETTO:+0100\r\n"
        "TZNAME;ALTREP=\"https://example.invalid/name\":must survive\r\nEND:STANDARD\r\nEND:VTIMEZONE\r\n"));
    successful(timezoneParameter);
    value(timezoneParameter.stage,find(timezoneParameter.stage,C::Tzname),"must survive",D::RetainUnreviewed);
    Session namedBase; parse(namedBase,calendar("BEGIN:X-LONG-NAME\r\nX-P:a\r\nEND:X-LONG-NAME\r\n")); successful(namedBase);
    l=limits(); l.poolBytes=namedBase.stage.counts().poolBytes-1; l.reviewBytes=0;
    Session names(l); parse(names,calendar("BEGIN:X-LONG-NAME\r\nX-P:a\r\nEND:X-LONG-NAME\r\n")); failure(names,S::ResourceFailure);
    Session componentBase; parse(componentBase,calendar("BEGIN:X-CUSTOM\r\nX-P:a\r\nEND:X-CUSTOM\r\n")); successful(componentBase);
    l=limits(); l.components=1; Session components(l);
    parse(components,calendar("BEGIN:X-CUSTOM\r\nX-P:a\r\nEND:X-CUSTOM\r\n")); failure(components,S::ResourceFailure);
    // Post-success mutation invalidates readers rather than accepting a new source.
    Session terminal; parse(terminal,calendar(event("UID:a\r\n"))); successful(terminal);
    IcsStructure::Event e={}; assert(terminal.stage.consume(e)==S::InvalidState); failure(terminal,S::InvalidState);
    Session counter;
    const char* begin="BEGIN:VCALENDAR";
    assert(counter.parser.consume(reinterpret_cast<const uint8_t*>(begin),std::strlen(begin))==P::InProgress);
    counter.stage.componentAt(0).observedProperties=UINT32_MAX;
    const char* version="VERSION:2.0";
    assert(counter.parser.consume(reinterpret_cast<const uint8_t*>(version),std::strlen(version))==P::ConsumerFailure);
    assert(counter.stage.diagnostic().limit==Stage::Limit::Counter); failure(counter,S::ResourceFailure);
    Session association;
    assert(association.parser.consume(reinterpret_cast<const uint8_t*>(begin),std::strlen(begin))==P::InProgress);
    e.type=IcsStructure::EventType::EndComponent;
    e.component={{reinterpret_cast<const uint8_t*>("VCALENDAR"),9},Stage::Kind::Calendar,1,99,0,1};
    assert(association.stage.consume(e)==S::InvalidAssociation); failure(association,S::InvalidAssociation);
    Session unsupported; e.type=IcsStructure::EventType::BeginComponent;
    e.component={{reinterpret_cast<const uint8_t*>("AVAILABLE"),9},Stage::Kind::Unsupported,1,1,0,1};
    assert(unsupported.stage.consume(e)==S::UnsupportedProfile && unsupported.stage.diagnostic().disposition==D::RejectUnsupported);
    failure(unsupported,S::UnsupportedProfile);
    Session overflow; e.component={{reinterpret_cast<const uint8_t*>(UINTPTR_MAX-2),10},Stage::Kind::Calendar,1,1,0,1};
    assert(overflow.stage.consume(e)==S::InvalidAssociation); failure(overflow,S::InvalidAssociation);
    // Finalization requires the corresponding pipeline, not just staged closures.
    Session early; assert(early.stage.finish(early.http,early.parser)==S::UpstreamFailure); failure(early,S::UpstreamFailure);
}
void fragmentation() {
    const auto body=calendar(event("UID:fragment\r\nSUMMARY:caf\xc3\r\n \xa9\r\n")+
        "BEGIN:X-CUSTOM\r\nX-P:ignored\r\nEND:X-CUSTOM\r\n");
    for(bool chunked:{false,true}) {
        const size_t size=wire(body,chunked).size();
        for(size_t split=0;split<=size;++split) { Session s; parse(s,body,chunked,split); successful(s);
            value(s.stage,find(s.stage,C::Summary),"caf\xc3\xa9",D::RetainRequired); }
        Session one; parse(one,body,chunked,SIZE_MAX,true); successful(one);
    }
}
}
int main() {
    dependencies(); disposition(); opaqueAndAssociation(); timezoneAndOrder(); limitsAndFailures(); lifecycleAndArithmetic(); additionalEdges(); fragmentation();
    std::printf("PASS: ICS source stage %zu scenarios; zero staging new allocations; component 48/8, property 40/4\n",scenarios);
}
