#include "../application/ics_ingestion.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <type_traits>
#include <vector>

using HTTP = IcsIngestion;
using Status = HTTP::Status;
using Lines = IcsLogicalLines;
using LS = Lines::Status;
namespace {
bool tracking = false;
size_t allocations = 0, scenarios = 0;
}
// Test-local allocation audit. Recorders are preallocated before feed/finalize.
void* operator new(size_t n) {
    if (tracking) ++allocations;
    void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

namespace {
struct Audit { Audit() { assert(!tracking); tracking = true; }
    ~Audit() { tracking = false; } };
struct Guarded {
    std::vector<uint8_t> storage;
    size_t bytes;
    explicit Guarded(size_t n) : storage(n + 32, 0xa5), bytes(n) {}
    uint8_t* data() { return storage.data() + 16; }
    void check() const {
        for (size_t i=0;i<16;++i) assert(storage[i]==0xa5 && storage[16+bytes+i]==0xa5);
    }
};
struct Recorder {
    std::vector<uint8_t> output;
    std::vector<size_t> offsets, lengths;
    size_t used=0, count=0, failAt=SIZE_MAX;
    HTTP* reenter=nullptr;
    Lines* reenterLines=nullptr;
    uint64_t* advanceClock=nullptr;
    uint64_t clockStep=0;
    Recorder() : output(262144), offsets(16384), lengths(16384) {}
    static bool sink(const uint8_t* p,size_t n,void* context) {
        auto& r=*static_cast<Recorder*>(context);
        assert(r.count<r.offsets.size() && n<=r.output.size()-r.used);
        r.offsets[r.count]=r.used; r.lengths[r.count]=n;
        if(n) std::memcpy(r.output.data()+r.used,p,n);
        r.used+=n;
        if(r.advanceClock) *r.advanceClock+=r.clockStep;
        if(r.reenter) { assert(r.reenter->finish()==Status::InvalidState); }
        if(r.reenterLines) { assert(r.reenterLines->finish()==LS::InvalidState); }
        return r.count++ != r.failAt;
    }
    void equals(const std::vector<std::string>& expected) const {
        assert(count==expected.size());
        for(size_t i=0;i<count;++i) {
            assert(lengths[i]==expected[i].size());
            assert(!lengths[i] || !std::memcmp(output.data()+offsets[i],expected[i].data(),lengths[i]));
        }
    }
};
struct Time {
    uint64_t now=0, step=0;
    static uint64_t clock(void* context) {
        auto& t=*static_cast<Time*>(context); const uint64_t result=t.now;
        assert(t.step<=UINT64_MAX-t.now); t.now+=t.step; return result;
    }
};
HTTP::Limits evaluation() {
    // Reviewed planning candidates only. No defaults exist in the implementation.
    return {16U*1024U*1024U,8U*1024U*1024U,16384,64,2048,256,8192,32,2048,4096,16384,120000,10000};
}
size_t scratchBytes(const HTTP::Limits& l) { return l.metadataLine+l.location+l.logicalLine; }
const uint8_t* bytes(const std::string& s) { return reinterpret_cast<const uint8_t*>(s.data()); }
std::string lengthResponse(const std::string& body,const std::string& extra="",const std::string& version="1.1") {
    return "HTTP/"+version+" 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n"+extra+"\r\n"+body;
}
std::string chunked(const std::string& body,size_t width,const std::string& extensions="",const std::string& trailers="") {
    std::string result="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for(size_t i=0;i<body.size();i+=width) {
        const size_t n=std::min(width,body.size()-i); char size[32];
        std::snprintf(size,sizeof(size),"%zx",n);
        result+=std::string(size)+extensions+"\r\n"+body.substr(i,n)+"\r\n";
    }
    return result+"0"+extensions+"\r\n"+trailers+"\r\n";
}
void expect(Status actual,Status expected) {
    if(actual!=expected) std::fprintf(stderr,"HTTP status %u expected %u\n",static_cast<unsigned>(actual),static_cast<unsigned>(expected));
    assert(actual==expected);
}
Status feed(HTTP& h,const std::string& s,size_t offset,size_t count) {
    Audit audit; return h.feed(bytes(s)+offset,count);
}
Status finish(HTTP& h) { Audit audit; return h.finish(); }
Status event(HTTP& h,HTTP::Transport e) { Audit audit; return h.transport(e); }

void responseCase(const std::string& wire,Status expected,const std::vector<std::string>& lines,
                  size_t partition,const HTTP::Limits& limits=evaluation()) {
    Guarded arena(scratchBytes(limits)); Time time; Recorder recorder; HTTP h;
    expect(h.initialize(arena.data(),arena.bytes,limits,Recorder::sink,&recorder,Time::clock,&time),Status::InProgress);
    Status result=Status::InProgress;
    for(size_t i=0;i<wire.size() && result==Status::InProgress;i+=partition)
        result=feed(h,wire,i,std::min(partition,wire.size()-i));
    if(result==Status::InProgress) result=finish(h);
    expect(result,expected);
    if(expected==Status::Complete) recorder.equals(lines);
    else if(expected!=Status::RedirectRequired) {
        expect(finish(h),expected); expect(event(h,HTTP::Transport::Closed),expected);
        expect(event(h,HTTP::Transport::Cancelled),expected); // First terminal failure survives cleanup.
    }
    h.release(); assert(!h.redirectLocation().data); arena.check(); ++scenarios;
}

void unfoldedCase(const std::string& wire,const std::vector<std::string>& expected,size_t split) {
    Guarded arena(1024); Recorder r; Lines lines;
    assert(lines.initialize(arena.data(),arena.bytes,512,1024,Recorder::sink,&r)==LS::InProgress);
    { Audit a;
      assert(lines.feed(bytes(wire),split)==LS::InProgress);
      assert(lines.feed(bytes(wire)+split,wire.size()-split)==LS::InProgress);
      assert(lines.finish()==LS::Complete); }
    r.equals(expected); lines.release(); arena.check(); ++scenarios;
}
void unfolding() {
    const std::vector<std::pair<std::string,std::vector<std::string>>> cases={
        {"",{}},{"A\r\n",{"A"}},{"A\r\nB\r\n",{"A","B"}},
        {"\r\n\r\n",{"",""}}, {" x\r\n",{" x"}},
        {"A\r\n B\r\n\tC\r\n",{"ABC"}},
        {"A\r\n  B\r\n\t\tC\r\n",{"A B\tC"}},
        {"A\r\n \r\n\tB\r\n",{"AB"}},
        {std::string("\xc3\r\n \xbc\xe2\x82\r\n\t\xac\r\n"),{std::string("\xc3\xbc\xe2\x82\xac")}},
        {std::string("a\0b\r\n",5),{std::string("a\0b",3)}}}; // Octets, not property/UTF-8 validity.
    for(const auto& c:cases) {
        for(size_t split=0;split<=c.first.size();++split) unfoldedCase(c.first,c.second,split);
        Guarded arena(1024); Recorder r; Lines l;
        assert(l.initialize(arena.data(),arena.bytes,512,1024,Recorder::sink,&r)==LS::InProgress);
        { Audit a; for(size_t i=0;i<c.first.size();++i) assert(l.feed(bytes(c.first)+i,1)==LS::InProgress);
          assert(l.finish()==LS::Complete); }
        r.equals(c.second); arena.check(); ++scenarios;
    }
    for(const auto& input:std::vector<std::string>{"A","A\r","A\r\n ","A\r\n\t"}) {
        Guarded arena(32); Recorder r; Lines l;
        assert(l.initialize(arena.data(),32,16,32,Recorder::sink,&r)==LS::InProgress);
        assert(l.feed(bytes(input),input.size())==LS::InProgress);
        assert(l.finish()==LS::IncompleteLine && l.finish()==LS::IncompleteLine);
        assert(l.feed(nullptr,0)==LS::IncompleteLine); arena.check(); ++scenarios;
    }
    for(const auto& input:std::vector<std::string>{"A\n","A\rB","\n","\r\r"}) {
        Guarded arena(32); Recorder r; Lines l;
        assert(l.initialize(arena.data(),32,16,32,Recorder::sink,&r)==LS::InProgress);
        assert(l.feed(bytes(input),input.size())==LS::MalformedLine && l.finish()==LS::MalformedLine);
        arena.check(); ++scenarios;
    }
    for(const auto& input:std::vector<std::string>{"abcd\r\n","abcde\r\n","ab\r\n cd\r\n","ab\r\n cde\r\n"}) {
        Guarded arena(4); Recorder r; Lines l;
        assert(l.initialize(arena.data(),4,4,4,Recorder::sink,&r)==LS::InProgress);
        auto result=l.feed(bytes(input),input.size()); if(result==LS::InProgress)result=l.finish();
        assert(result==(input=="abcd\r\n" || input=="ab\r\n cd\r\n" ? LS::Complete : LS::ResourceLimit));
        arena.check(); ++scenarios;
    }
    Guarded arena(32); Recorder r; Lines l;
    assert(l.initialize(arena.data(),32,16,32,Recorder::sink,&r)==LS::InProgress);
    const std::string terminated="A\r\n";
    assert(l.feed(bytes(terminated),terminated.size())==LS::InProgress && r.count==0);
    r.failAt=0; assert(l.finish()==LS::CallbackFailure && l.finish()==LS::CallbackFailure);
    Lines reentrant; r.failAt=SIZE_MAX; r.reenterLines=&reentrant;
    assert(reentrant.initialize(arena.data(),32,16,32,Recorder::sink,&r)==LS::InProgress);
    assert(reentrant.feed(bytes(terminated),terminated.size())==LS::InProgress);
    assert(reentrant.finish()==LS::InvalidState); r.reenterLines=nullptr;
    Lines cancelled; assert(cancelled.initialize(arena.data(),32,16,32,Recorder::sink,&r)==LS::InProgress);
    assert(cancelled.feed(reinterpret_cast<const uint8_t*>("A"),1)==LS::InProgress);
    assert(cancelled.cancel()==LS::Cancelled && cancelled.finish()==LS::Cancelled);
    Lines overlap; assert(overlap.initialize(arena.data(),32,16,32,Recorder::sink,&r)==LS::InProgress);
    assert(overlap.feed(arena.data(),1)==LS::InvalidArgument); arena.check();
}

void happyFramingAndPartitions() {
    const std::string body="BEGIN:VCALENDAR\r\nSUMMARY:A\r\n B\r\n\tC\r\n\r\nEND:VCALENDAR\r\n";
    const std::vector<std::string> expected={"BEGIN:VCALENDAR","SUMMARY:ABC","","END:VCALENDAR"};
    std::vector<std::string> wires={lengthResponse(body),lengthResponse(body,"Content-Encoding: identity\r\n","1.0"),
        chunked(body,1),chunked(body,7,";name=token;quoted=\"a\\\";b\";flag","X-Result: ok\r\nDigest: opaque\r\n")};
    for(const auto& wire:wires) {
        responseCase(wire,Status::Complete,expected,wire.size());
        responseCase(wire,Status::Complete,expected,1);
        // Every single split position, including before/after all delimiters.
        for(size_t split=0;split<=wire.size();++split) {
            Guarded arena(scratchBytes(evaluation())); Recorder r; Time time; HTTP h;
            expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&time),Status::InProgress);
            expect(feed(h,wire,0,split),Status::InProgress);
            expect(feed(h,wire,split,wire.size()-split),Status::InProgress);
            expect(finish(h),Status::Complete); r.equals(expected); arena.check(); ++scenarios;
        }
        for(size_t width:{2U,3U,17U,31U}) responseCase(wire,Status::Complete,expected,width);
    }
    responseCase(lengthResponse(""),Status::Complete,{},1);
    responseCase(chunked("",1),Status::Complete,{},1);
    responseCase(lengthResponse("A\r\n","X-Case: \tvalue \t\r\n"),Status::Complete,{"A"},1);
    // Case insensitive names/codings, and zero last-chunk extension.
    responseCase("HTTP/1.1 200 \r\ncOnTeNt-LeNgTh:\t3\t\r\n\r\nA\r\n",Status::Complete,{"A"},1);
    responseCase("HTTP/1.1 200 OK\r\nTransfer-Encoding: CHUNKED\r\n\r\n3 ; x = \"y\"\r\nA\r\n\r\n00;last\r\n\r\n",Status::Complete,{"A"},1);
}
void framingErrors() {
    const std::vector<std::pair<std::string,Status>> cases={
        {lengthResponse("A\r\n").substr(0,lengthResponse("A\r\n").size()-1),Status::IncompleteFraming},
        {"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n",Status::IncompleteFraming},
        {lengthResponse("A\r\n")+"X",Status::UnexpectedBytes},
        {lengthResponse("A"),Status::IncompleteLine},{lengthResponse("A\n"),Status::MalformedLine},
        {"HTTP/2.0 200 OK\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 20x OK\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 200\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 200 OK\n\n",Status::MalformedFraming},
        {lengthResponse("","Broken\r\n"),Status::MalformedFraming},
        {lengthResponse("","Bad Name: x\r\n"),Status::MalformedFraming},
        {lengthResponse(""," X: folded\r\n"),Status::MalformedFraming},
        {lengthResponse("","Content-Length: 0\r\n"),Status::MalformedFraming},
        {lengthResponse("","Content-Length: 1\r\n"),Status::MalformedFraming},
        {"HTTP/1.1 200 OK\r\nContent-Length: 0,0\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 200 OK\r\nContent-Length: -1\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 200 OK\r\nContent-Length: +1\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 200 OK\r\nContent-Length: 1x\r\n\r\n",Status::MalformedFraming},
        {"HTTP/1.1 200 OK\r\nContent-Length: 18446744073709551616\r\n\r\n",Status::MalformedFraming},
        {lengthResponse("","Transfer-Encoding: chunked\r\n"),Status::MalformedFraming},
        {"HTTP/1.0 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n",Status::UnsupportedFraming},
        {"HTTP/1.1 200 OK\r\nTransfer-Encoding: gzip, chunked\r\n\r\n",Status::UnsupportedFraming},
        {"HTTP/1.1 200 OK\r\nTransfer-Encoding: identity\r\n\r\n",Status::UnsupportedFraming},
        {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\nTransfer-Encoding: chunked\r\n\r\n",Status::MalformedFraming},
        {lengthResponse("","Content-Encoding: gzip\r\n"),Status::UnsupportedEncoding},
        {lengthResponse("","Content-Encoding: br\r\n"),Status::UnsupportedEncoding},
        {lengthResponse("","Content-Encoding: identity\r\nContent-Encoding: identity\r\n"),Status::MalformedFraming},
        {"HTTP/1.1 200 OK\r\n\r\nA\r\n",Status::UnsupportedFraming},
        {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nA\r\n\r\n",Status::IncompleteFraming},
        {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n",Status::IncompleteFraming},
        {"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n0\r\nX: y\r\n",Status::IncompleteFraming},
        {chunked("A\r\n",3,"","Content-Length: 3\r\n"),Status::MalformedFraming},
        {chunked("A\r\n",3,"","Broken\r\n"),Status::MalformedFraming},
        {chunked("A\r\n",3)+"X",Status::UnexpectedBytes}};
    for(const auto& c:cases) {
        responseCase(c.first,c.second,{},1); responseCase(c.first,c.second,{},c.first.size());
    }
    const std::string head="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for(const auto& size:std::vector<std::string>{"z","-1","+1","3junk","10000000000000000","3;","3;x=","3;x=\"unterminated","3 ","3;x ","3;x=y ","3;x=\"x\"junk"}) {
        responseCase(head+size+"\r\nA\r\n\r\n0\r\n\r\n",Status::MalformedFraming,{},1);
    }
    responseCase(head+"3\r\nA\r\nXX0\r\n\r\n",Status::MalformedFraming,{},1);
    for(int status:{100,101,103,204,206,304,400,401,403,404,500,503})
        responseCase("HTTP/1.1 "+std::to_string(status)+" status\r\nContent-Length: 0\r\n\r\n",Status::HttpStatusFailure,{},1);
}
void redirects() {
    for(int status:{301,302,303,307,308}) {
        const std::string wire="HTTP/1.1 "+std::to_string(status)+" Redirect\r\nLocation: https://example.invalid/a%20b?synthetic=x\r\n\r\nnot ICS";
        for(size_t partition:{1U,7U,128U}) {
            Guarded arena(scratchBytes(evaluation())); Time time; Recorder r; HTTP h;
            expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&time),Status::InProgress);
            Status result=Status::InProgress;
            for(size_t i=0;i<wire.size() && result==Status::InProgress;i+=partition)
                result=feed(h,wire,i,std::min(partition,wire.size()-i));
            expect(result,Status::RedirectRequired); assert(r.count==0 && h.httpStatus()==status && h.decodedBytes()==0);
            const auto location=h.redirectLocation();
            const std::string expected="https://example.invalid/a%20b?synthetic=x";
            assert(location.size==expected.size() && !std::memcmp(location.data,expected.data(),location.size));
            expect(finish(h),Status::RedirectRequired); h.release(); assert(!h.redirectLocation().data);
            std::memset(arena.data(),0xbd,arena.bytes); expect(finish(h),Status::RedirectRequired);
            arena.check(); ++scenarios;
        }
    }
    for(const auto& headers:std::vector<std::string>{"","Location: \r\n","Location: /a\r\nLocation: /b\r\n",
        "Location: bad space\r\n","Location: /%xx\r\n","Location: /%\r\n"})
        responseCase("HTTP/1.1 302 Redirect\r\n"+headers+"\r\n",Status::MalformedFraming,{},1);
    auto l=evaluation(); l.location=3;
    responseCase("HTTP/1.1 302 Redirect\r\nLocation: /abc\r\n\r\n",Status::ResourceLimit,{},1,l);
    responseCase("HTTP/1.1 302 Redirect\r\nLocation: /ab\r\n\r\n",Status::RedirectRequired,{},1,l);
    // No URI/provider/trust policy is selected here: insecure/cross-origin
    // Locations remain untrusted handoff data, never a followed request.
    responseCase("HTTP/1.1 302 Redirect\r\nLocation: http://other.invalid/x\r\n\r\n",Status::RedirectRequired,{},1);
}

void budgetsAndTiming() {
    const std::string empty=lengthResponse(""), wire=lengthResponse("abc\r\n");
    auto l=evaluation(); l.receivedBytes=empty.size();
    responseCase(empty,Status::Complete,{},1,l);
    l.receivedBytes=empty.size()-1; responseCase(empty,Status::ResourceLimit,{},1,l);
    l=evaluation(); l.bodyBytes=5; responseCase(wire,Status::Complete,{"abc"},1,l);
    l.bodyBytes=4; responseCase(wire,Status::ResourceLimit,{},1,l);
    responseCase(chunked("abc\r\n",2),Status::ResourceLimit,{},1,l);
    l=evaluation(); l.headerBytes=empty.size(); responseCase(empty,Status::Complete,{},1,l);
    l.headerBytes=empty.size()-1; responseCase(empty,Status::ResourceLimit,{},1,l);
    l=evaluation(); l.headerFields=1; responseCase(lengthResponse("","X: y\r\n"),Status::ResourceLimit,{},1,l);
    l=evaluation(); l.metadataLine=17; l.chunkMetadata=17;
    responseCase(empty,Status::Complete,{},1,l); // Content-Length: 0 is 17 bytes.
    l.metadataLine=16; l.chunkMetadata=16; responseCase(empty,Status::ResourceLimit,{},1,l);
    l=evaluation(); l.chunkMetadata=3;
    responseCase(chunked("abc\r\n",1,";x"),Status::Complete,{"abc"},1,l);
    responseCase(chunked("abc\r\n",1,";xx"),Status::ResourceLimit,{},1,l);
    l=evaluation(); l.trailerBytes=8;
    responseCase(chunked("",1,"","X: y\r\n"),Status::Complete,{},1,l);
    l.trailerBytes=7; responseCase(chunked("",1,"","X: y\r\n"),Status::ResourceLimit,{},1,l);
    l=evaluation(); l.trailerFields=1;
    responseCase(chunked("",1,"","X: y\r\nY: z\r\n"),Status::ResourceLimit,{},1,l);
    l=evaluation(); l.physicalLine=3; l.logicalLine=3;
    responseCase(wire,Status::Complete,{"abc"},1,l);
    responseCase(lengthResponse("abcd\r\n"),Status::ResourceLimit,{},1,l);
    responseCase(lengthResponse("ab\r\n cd\r\n"),Status::ResourceLimit,{},1,l);
    l=evaluation(); l.bodyBytes=0; responseCase(empty,Status::Complete,{},1,l);
    responseCase(lengthResponse("A\r\n"),Status::ResourceLimit,{},1,l);

    for(bool stall:{true,false}) {
        l=evaluation(); l.processingMs=stall ? 100 : 10; l.stalledMs=stall ? 10 : 100;
        Guarded arena(scratchBytes(l)); Time time; Recorder r; HTTP h;
        expect(h.initialize(arena.data(),arena.bytes,l,Recorder::sink,&r,Time::clock,&time),Status::InProgress);
        const std::string partial=wire.substr(0,wire.size()-1);
        expect(feed(h,partial,0,partial.size()),Status::InProgress);
        time.now=10; expect(event(h,HTTP::Transport::WouldBlock),Status::InProgress);
        time.now=11; expect(event(h,HTTP::Transport::WouldBlock),Status::ResourceLimit);
        assert(h.limit()==(stall ? HTTP::Limit::StallTime : HTTP::Limit::ProcessingTime));
        expect(finish(h),Status::ResourceLimit); arena.check(); ++scenarios;
    }
    // Time spent in callback cannot be hidden by successful logical finalization.
    l=evaluation(); l.processingMs=10; l.stalledMs=100;
    Guarded arena(scratchBytes(l)); Time time; Recorder r; HTTP h;
    r.advanceClock=&time.now; r.clockStep=11;
    expect(h.initialize(arena.data(),arena.bytes,l,Recorder::sink,&r,Time::clock,&time),Status::InProgress);
    expect(feed(h,wire,0,wire.size()),Status::InProgress);
    expect(finish(h),Status::ResourceLimit); assert(h.limit()==HTTP::Limit::ProcessingTime);
    HTTP periodic; time.now=0; time.step=1;
    l.processingMs=2; l.stalledMs=100;
    expect(periodic.initialize(arena.data(),arena.bytes,l,Recorder::sink,&r,Time::clock,&time),Status::InProgress);
    expect(feed(periodic,wire,0,wire.size()),Status::ResourceLimit); // Within feed checkpoints.
    HTTP backward; time.now=5; time.step=0;
    expect(backward.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&time),Status::InProgress);
    time.now=4; expect(event(backward,HTTP::Transport::WouldBlock),Status::InvalidClock);
    HTTP nearMax; time.now=UINT64_MAX-5;
    expect(nearMax.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&time),Status::InProgress);
    time.now=UINT64_MAX; expect(feed(nearMax,empty,0,empty.size()),Status::InProgress);
    expect(finish(nearMax),Status::Complete); arena.check();
}

void lifecycleAndTransport() {
    const std::string wire=lengthResponse("A\r\nB\r\n");
    for(HTTP::Transport e:{HTTP::Transport::Closed,HTTP::Transport::Error,HTTP::Transport::Cancelled}) {
        Guarded arena(scratchBytes(evaluation())); Recorder r; Time t; HTTP h;
        expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
        expect(feed(h,wire,0,wire.size()-1),Status::InProgress);
        expect(feed(h,wire,0,0),Status::InProgress); // Temporary zero read is not EOF.
        expect(event(h,HTTP::Transport::WouldBlock),Status::InProgress);
        const auto expected=e==HTTP::Transport::Closed ? Status::IncompleteFraming :
                            e==HTTP::Transport::Error ? Status::TransportFailure : Status::Cancelled;
        expect(event(h,e),expected); const size_t callbacks=r.count;
        expect(feed(h,wire,0,wire.size()),expected); expect(finish(h),expected); assert(r.count==callbacks);
        h.release(); std::memset(arena.data(),0xbd,arena.bytes);
        expect(finish(h),expected); arena.check(); ++scenarios;
    }
    for(int cycle=0;cycle<3;++cycle) {
        Guarded arena(scratchBytes(evaluation())); Recorder r; Time t; HTTP h;
        expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
        expect(feed(h,wire,0,wire.size()),Status::InProgress); assert(r.count==1);
        expect(event(h,HTTP::Transport::Closed),Status::Complete); r.equals({"A","B"});
        h.release(); std::memset(arena.data(),0xbd,arena.bytes); expect(finish(h),Status::Complete);
        arena.check(); ++scenarios;
    }
    Guarded arena(scratchBytes(evaluation())); Recorder r; Time t; HTTP h;
    r.failAt=0;
    expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    expect(feed(h,wire,0,wire.size()),Status::CallbackFailure);
    const size_t callbacks=r.count; expect(finish(h),Status::CallbackFailure); assert(r.count==callbacks);
    HTTP reentrant; r.failAt=SIZE_MAX; r.reenter=&reentrant;
    expect(reentrant.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    expect(feed(reentrant,wire,0,wire.size()),Status::InvalidState); r.reenter=nullptr;
    HTTP extra;
    expect(extra.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    expect(feed(extra,wire,0,wire.size()),Status::InProgress); expect(finish(extra),Status::Complete);
    expect(feed(extra,wire,0,1),Status::UnexpectedBytes); expect(finish(extra),Status::UnexpectedBytes);
}
void scriptedShortReads() {
    // Deterministic reader stand-in: requested capacity is 17, delivered Data
    // lengths may be smaller or zero. No inference of EOF from that count.
    struct Read { enum Kind { Data, WouldBlock, Closed, Error, Cancelled } kind; size_t count; uint64_t now; };
    const std::vector<Read> reads={{Read::Data,1,0},{Read::Data,0,1},{Read::WouldBlock,0,2},
        {Read::Data,2,3},{Read::Data,5,4},{Read::WouldBlock,0,5}};
    const std::string wire=chunked("A\r\n B\r\n",1,";x=\"quoted\"");
    Guarded arena(scratchBytes(evaluation())); Time time; Recorder r; HTTP h;
    expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&time),Status::InProgress);
    size_t offset=0;
    for(const auto& read:reads) {
        time.now=read.now;
        if(read.kind==Read::Data) { assert(read.count<=17); expect(feed(h,wire,offset,read.count),Status::InProgress); offset+=read.count; }
        else expect(event(h,HTTP::Transport::WouldBlock),Status::InProgress);
        assert(h.status()==Status::InProgress);
    }
    uint32_t seed=0x12345678;
    while(offset<wire.size()) {
        seed=seed*1664525U+1013904223U;
        const size_t count=std::min(static_cast<size_t>(seed%17U+1U),wire.size()-offset);
        expect(feed(h,wire,offset,count),Status::InProgress); offset+=count;
    }
    assert(r.count==0); expect(event(h,HTTP::Transport::Closed),Status::Complete);
    r.equals({"AB"}); arena.check(); ++scenarios;
}
void invalidArgumentsAndBounds() {
    Guarded arena(scratchBytes(evaluation())); Recorder r; Time t;
    HTTP fresh; expect(fresh.finish(),Status::NotInitialized);
    for(int fault=0;fault<9;++fault) {
        HTTP h; auto l=evaluation(); uint8_t* p=arena.data(); size_t n=arena.bytes;
        auto sink=Recorder::sink; auto clock=Time::clock;
        if(fault==0)p=nullptr;
        if(fault==1)n=arena.bytes-1;
        if(fault==2)l.metadataLine=SIZE_MAX;
        if(fault==3)l.logicalLine=0;
        if(fault==4)l.chunkMetadata=l.metadataLine+1;
        if(fault==5)sink=nullptr;
        if(fault==6)clock=nullptr;
        if(fault==7)p=reinterpret_cast<uint8_t*>(UINTPTR_MAX-8);
        if(fault==8)n=SIZE_MAX;
        expect(h.initialize(p,n,l,sink,&r,clock,&t),Status::InvalidArgument);
        expect(h.finish(),Status::InvalidArgument); h.release(); ++scenarios;
    }
    HTTP h;
    expect(h.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    { Audit a; expect(h.feed(reinterpret_cast<const uint8_t*>(UINTPTR_MAX-1),4),Status::InvalidArgument); }
    HTTP overlap;
    expect(overlap.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    { Audit a; expect(overlap.feed(arena.data()+arena.bytes-1,1),Status::InvalidArgument); }
    HTTP nullInput;
    expect(nullInput.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    { Audit a; expect(nullInput.feed(nullptr,1),Status::InvalidArgument); }
    HTTP releaseActive;
    expect(releaseActive.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::InProgress);
    releaseActive.release(); expect(releaseActive.finish(),Status::Cancelled);
    expect(releaseActive.initialize(arena.data(),arena.bytes,evaluation(),Recorder::sink,&r,Time::clock,&t),Status::Cancelled);
    Lines badLines; assert(badLines.initialize(nullptr,0,1,1,Recorder::sink,&r)==LS::InvalidArgument);
    Lines overflow; assert(overflow.initialize(reinterpret_cast<uint8_t*>(UINTPTR_MAX-1),4,1,1,Recorder::sink,&r)==LS::InvalidArgument);
    Lines bounds; assert(bounds.initialize(arena.data(),4,4,5,Recorder::sink,&r)==LS::InvalidArgument);
    Lines zero; assert(zero.initialize(arena.data(),4,0,4,Recorder::sink,&r)==LS::InvalidArgument);
    Lines finalized; assert(finalized.initialize(arena.data(),4,4,4,Recorder::sink,&r)==LS::InProgress);
    assert(finalized.finish()==LS::Complete);
    assert(finalized.feed(reinterpret_cast<const uint8_t*>("X"),1)==LS::InvalidState);
    arena.check();
}
}
int main() {
    static_assert(!std::is_copy_constructible<HTTP>::value,"scratch-bound session");
    static_assert(!std::is_copy_constructible<Lines>::value,"scratch-bound unfolder");
    unfolding(); happyFramingAndPartitions(); framingErrors(); redirects();
    budgetsAndTiming(); lifecycleAndTransport(); scriptedShortReads(); invalidArgumentsAndBounds();
    assert(allocations==0);
    std::printf("PASS: %zu partition/behavior scenarios; framing, unfolding, bounds, ownership, errors; zero ingestion allocations; sizeof session/unfolder=%zu/%zu\n",
        scenarios,sizeof(HTTP),sizeof(Lines));
}
