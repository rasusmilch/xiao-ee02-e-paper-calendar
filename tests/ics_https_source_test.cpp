#include "../application/ics_https_source.h"
#include "../application/five_day_posix_time.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
namespace { bool audit=false; size_t allocated=0,cases=0; }
__attribute__((noinline)) void* operator new(size_t n) { if(audit) ++allocated; void* p=std::malloc(n?n:1);
        if(!p) throw std::bad_alloc();
    return p; }
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
namespace {
struct Allocation { uint8_t* raw; uint8_t* data; size_t size; bool live; };
std::vector<Allocation> allocations;
bool psramAvailable=true,failAllocation=false;
}
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p) {
    for(auto it=allocations.rbegin();it!=allocations.rend();++it) if(it->data==p && it->live) {
        for(size_t i=0;i<16;++i) assert(it->raw[i]==0xa5 && it->data[it->size+i]==0xa5);
        it->live=false; __real_free(it->raw); return;
    }
    __real_free(p);
}
bool psramFound() { return psramAvailable; }
void* ps_malloc(size_t n) {
    if(failAllocation) return nullptr;
    auto* p=static_cast<uint8_t*>(std::malloc(n+32)); assert(p);
    std::memset(p,0xa5,n+32); allocations.push_back({p,p+16,n,true}); return p+16;
}
namespace {
namespace N=IcsHttpsSource;
using S=N::Status; using Snap=FiveDaySnapshot;
struct Audit { size_t before; Audit():before(allocated) { assert(!audit); audit=true; } ~Audit() { audit=false; assert(before==allocated); } };
N::View view(const std::string& s) { return {reinterpret_cast<const uint8_t*>(s.data()),s.size()}; }
const std::string url="https://calendar.example.invalid/public%2Ffeed?key=synthetic%2bfixture&v=1";
const char ca[]="synthetic-test-CA";
N::Limits limits() {
    return {2048,253,2048,4096,512,{1000,5,20},
        {131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,0,100},
        {4096,32,32,64,8,64,4096,8192,128,8}, {128,1024,65536,128,16384},
        {{128,8,64,32,64,128,{128,128,4096},{128,128,4096,64,4096}},4096,4096}};
}
std::string body(const std::string& fields="",const std::string& start="20261023T090000Z") {
    return "BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:Synthetic export\r\nMETHOD:PUBLISH\r\nX-WR-CALNAME:Example\r\n"
        "BEGIN:VEVENT\r\nUID:synthetic\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:"+start+
        "\r\nDTEND:20261023T100000Z\r\nSUMMARY:Folded "+"\r\n title\\, UTF-8 Café\r\n"+fields+"END:VEVENT\r\nEND:VCALENDAR\r\n";
}
std::string length(const std::string& b,const std::string& extra="") { return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(b.size())+"\r\n"+extra+"\r\n"+b; }
std::string chunked(const std::string& b) {
    std::string s="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
    for(size_t i=0;i<b.size();) { size_t n=std::min<size_t>(7,b.size()-i); char hex[20];std::snprintf(hex,sizeof(hex),"%zx",n);s+=std::string(hex)+"\r\n"+b.substr(i,n)+"\r\n";i+=n; }
    return s+"0\r\n\r\n";
}
struct Fake {
    std::string wire,request,host,tlsHost;
    size_t position=0,reads=0,writes=0,resolves=0,connects=0,closes=0,idles=0;
    size_t readSize=SIZE_MAX,writeSize=SIZE_MAX,readFailAt=SIZE_MAX,emptyEvery=0;
    uint64_t now=0,dnsDuration=0,tlsDuration=0,readDuration=0,writeDuration=0,idleDuration=1;
    bool dnsOk=true,tlsOk=true,writeOk=true,stall=false,writeStall=false,regress=false,closed=false,holdOpen=false;
    uint16_t port=0;
    explicit Fake(std::string w=length(body())):wire(std::move(w)) { request.reserve(8192);host.reserve(254);tlsHost.reserve(254); }
    static uint64_t clock(void* p) { return static_cast<Fake*>(p)->now; }
    static bool resolve(void* p,const char* h) { auto& f=*static_cast<Fake*>(p); ++f.resolves;f.host=h;f.now+=f.dnsDuration;return f.dnsOk; }
    static bool connect(void* p,const char* h,uint16_t port,const char* trust,bool time,const N::Timeouts& t) {
        auto& f=*static_cast<Fake*>(p); assert(f.resolves==1 && f.host==h && trust && *trust && time && t.socketMs && t.handshakeSeconds);
        ++f.connects;f.tlsHost=h;f.port=port;f.now+=f.tlsDuration;
        if(f.regress)f.now=0;
        return f.tlsOk;
    }
    static N::Io write(void* p,const uint8_t* data,size_t n) {
        auto& f=*static_cast<Fake*>(p); ++f.writes;f.now+=f.writeDuration;
        if(!f.writeOk)return {N::IoStatus::Error,0};
        if(f.writeStall)return {N::IoStatus::WouldBlock,0};
        n=std::min(n,f.writeSize); f.request.append(reinterpret_cast<const char*>(data),n);return {N::IoStatus::Data,n};
    }
    static N::Io read(void* p,uint8_t* bytes,size_t n) {
        auto& f=*static_cast<Fake*>(p); ++f.reads;f.now+=f.readDuration;
        if(f.position>=f.readFailAt)return {N::IoStatus::Error,0};
        if(f.stall || (f.emptyEvery && f.reads%f.emptyEvery==0))return {N::IoStatus::WouldBlock,0};
        if(f.position==f.wire.size())return {f.holdOpen?N::IoStatus::WouldBlock:N::IoStatus::Closed,0};
        n=std::min(n,std::min(f.readSize,f.wire.size()-f.position));std::memcpy(bytes,f.wire.data()+f.position,n);f.position+=n;return {N::IoStatus::Data,n};
    }
    static void idle(void* p) { auto& f=*static_cast<Fake*>(p);++f.idles;f.now+=f.idleDuration; }
    static void close(void* p) { auto& f=*static_cast<Fake*>(p);++f.closes;f.closed=true; }
    N::Transport transport() { return {this,resolve,connect,write,read,idle,close}; }
};
void init(Snap& s,size_t records=32,size_t text=4096) {
    const Snap::Window w={{1792706400,1792792800,1792879200,1792969200,1793055600,1793142000},20749,0};
    const Snap::Text labels[]={{"Personal",8},{"Team",4}}; assert(s.initialize(w,records,text,labels,2)==Snap::Result::Ok);
}
N::Prerequisites ready() { return {true,true,{reinterpret_cast<const uint8_t*>(ca),sizeof(ca)}}; }
N::Report run(Fake& fake,Snap& s,Ee02PsramWorkspace& w,const std::string& address=url,N::Limits l=limits(),N::Prerequisites p=ready(),size_t source=0) {
    const auto t=fake.transport(); N::Report r;
    { Audit audit; r=N::acquire(view(address),s,source,w,l,t,Fake::clock,&fake,p); }
    ++cases; assert(w.state()==Ee02PsramWorkspace::State::Scratch);
    if(r.status!=S::Published) assert(!r.publishedOccurrences && !r.outsideWindowOccurrences);
    if(fake.connects)assert(fake.closes && fake.closed);
    return r;
}
void expect(const N::Report& r,S status) { if(r.status!=status) std::fprintf(stderr,"case %zu: got %u expected %u HTTP=%u ingestion=%u structure=%u stage=%u producer=%u category=%u property=%zu\n",cases,unsigned(r.status),unsigned(status),r.httpStatus,unsigned(r.ingestionStatus),unsigned(r.structureStatus),unsigned(r.stageStatus),unsigned(r.producerReport.status),unsigned(r.producerReport.failureCategory),r.producerReport.propertyIndex);assert(r.status==status); }
void failed(Snap& s,size_t source=0,size_t keep=0) { assert(s.source(source)->outcome==Snap::Outcome::Incomplete && s.occurrenceCount()==keep); }
std::string title(const Snap& s) { const auto t=s.title(0);return {t.data,t.size}; }
void happy() {
    for(bool chunks:{false,true}) for(size_t n:{size_t(1),size_t(17),SIZE_MAX}) {
        Fake fake(chunks?chunked(body()):length(body()));fake.readSize=n;fake.writeSize=3;fake.emptyEvery=3;fake.holdOpen=true;
        Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok()); const auto r=run(fake,s,w);expect(r,S::Published);
        assert(r.httpStatus==200 && r.ingestionStatus==IcsIngestion::Status::Complete && r.structureStatus==IcsStructure::Status::StructurallyComplete && r.stageStatus==IcsSourceStage::Status::StagedSuccessfully);
        assert(r.publishedOccurrences==1 && s.source(0)->outcome==Snap::Outcome::Complete && fake.resolves==1 && fake.connects==1 && fake.closes==1 && fake.port==443);
        assert(s.occurrence(0)->range.timed.start==1792746000 && s.occurrence(0)->range.timed.end==1792749600 && s.occurrence(0)->sourceIndex==0);
        assert(title(s)=="Folded title, UTF-8 Café" && fake.host=="calendar.example.invalid" && fake.tlsHost==fake.host);
        assert(fake.request=="GET /public%2Ffeed?key=synthetic%2bfixture&v=1 HTTP/1.1\r\nHost: calendar.example.invalid\r\nAccept: text/calendar\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n");
        assert(w.withScratch(0,w.capacity(),[](uint8_t* p,size_t n,void*){std::memset(p,0xdd,n);return GfxResult::success();}).ok());assert(title(s)=="Folded title, UTF-8 Café" && s.occurrence(0)->range.timed.start==1792746000);assert(w.release().ok());
    }
    { auto b=body(); const auto start=b.find("DTSTART:"), end=b.find("SUMMARY:");
      b.replace(start,end-start,"DTSTART;VALUE=DATE:20261023\r\nDTEND;VALUE=DATE:20261026\r\n");
      Fake f(chunked(b)); Snap s; init(s); Ee02PsramWorkspace w; assert(w.allocate(2097152).ok());
      expect(run(f,s,w),S::Published); assert(s.occurrence(0)->kind==Snap::Kind::AllDay);
      assert(s.occurrence(0)->range.civil.start==20749 && s.occurrence(0)->range.civil.end==20752); }
    { Fake f;f.dnsDuration=1000000;f.tlsDuration=1000000;f.readDuration=9;f.readSize=1;f.writeSize=8;f.writeDuration=5;f.emptyEvery=3;
      Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());const auto r=run(f,s,w);expect(r,S::Published);
      assert(f.now>2000000 && r.producerReport.status==IcsNonrecurringSnapshot::Status::SourceCommitted && s.source(0)->outcome==Snap::Outcome::Complete); }
}
void urls() {
    for(const std::string u:{"https://example.invalid","HTTPS://Example.invalid?x=%2B&y=%2f","https://example.invalid:8443/a?x=1","https://example.invalid:443/"}) {
        Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w,u),S::Published);
        const auto begin=u.find("://")+3, end=u.find_first_of("/?",begin); const auto authority=u.substr(begin,end-begin);
        const auto resource=end==std::string::npos?"/":u[end]=='?'?"/"+u.substr(end):u.substr(end);
        assert(f.request.find("GET "+resource+" HTTP/1.1\r\nHost: "+authority+"\r\n")==0);
        assert(f.request.find("Authorization")==std::string::npos && f.request.find("Cookie")==std::string::npos && f.request.find("Referer")==std::string::npos);
    }
    for(const char* bad:{"http://example.invalid/a","https:///a","https://user:pass@example.invalid/a","https://example.invalid:0/a","https://example.invalid:65536/a","https://example.invalid:abc/a","https://example.invalid:/a",
        "https://example.invalid/a#frag","https://example.invalid/a\r\nInjected:x","https://example.invalid/a\t","https://example.invalid/a b","https://example.invalid/\\x","https://example.invalid/%0g","https://example.invalid/%a",
        "https://[::1]/a","https://127.0.0.1/a","https://bad..host/a","https://-bad.host/a","https://bad_.host/a","https://example.invalid./a"}) {
        Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w,bad),S::InvalidUrl);failed(s);assert(!f.resolves && f.closes==1);
    }
    for(int kind=0;kind<4;++kind) {
        Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto l=limits();
        if(kind==0)l.maxUrlBytes=url.size()-1;
        if(kind==1)l.maxTargetBytes=2;
        if(kind==2)l.maxHostBytes=2;
        if(kind==3)l.maxRequestBytes=10;
        expect(run(f,s,w,url,l),S::ResourceLimit);failed(s);assert(!f.resolves);
    }
}
void transportFailures() {
    for(int k=0;k<9;++k) {
        Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto p=ready();
        const S expected[]={S::WifiUnavailable,S::UntrustedTime,S::MissingTrust,S::DnsFailure,S::TlsFailure,S::WriteFailure,S::WriteTimeout,S::ReadTimeout,S::ReadFailure};
        if(k==0)p.wifiReady=false;
        if(k==1)p.trustedWallClock=false;
        if(k==2)p.caPem={nullptr,0};
        if(k==3)f.dnsOk=false;
        if(k==4)f.tlsOk=false;
        if(k==5)f.writeOk=false;
        if(k==6)f.writeStall=true;
        if(k==7)f.stall=true;
        if(k==8)f.readFailAt=0;
        expect(run(f,s,w,url,limits(),p),expected[k]);failed(s);assert(f.closes==1);
        if(k<3)assert(!f.resolves && !f.connects);
    }
    { Fake f;f.now=10;f.regress=true;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w),S::InvalidClock);failed(s); }
}
void responseFailures() {
    const auto wire=length(body());
    const std::vector<std::string> bad={wire.substr(0,wire.size()-2),chunked(body()).substr(0,chunked(body()).size()-4),"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\nZZ\r\n",
        length(body(),"Content-Encoding: gzip\r\n"),length(body(),"BrokenHeader\r\n"),length(body(),"Transfer-Encoding: chunked\r\n"),"HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n",
        "HTTP/1.1 304 Not Modified\r\n\r\n",wire+"surplus"};
    for(const auto& raw:bad) {
        Fake f(raw);Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto l=limits();l.receiveBytes=4096;
        expect(run(f,s,w,url,l),S::HttpFailure);failed(s);assert(f.closes==1);
    }
    { Fake f("HTTP/1.1 302 Found\r\nLocation: https://redirect.example.invalid/synthetic\r\nContent-Length: 0\r\n\r\n");Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());
      const auto r=run(f,s,w);expect(r,S::RedirectRequired);assert(r.httpStatus==302 && r.ingestionStatus==IcsIngestion::Status::RedirectRequired && f.connects==1);failed(s); }
    for(int kind=0;kind<3;++kind) {
        Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto l=limits();
        if(kind==0)l.ingestion.receivedBytes=32;
        if(kind==1)l.ingestion.bodyBytes=32;
        if(kind==2)l.ingestion.headerBytes=10;
        expect(run(f,s,w,url,l),S::ResourceLimit);failed(s);
    }
    { Fake f;f.readSize=1;f.emptyEvery=2;f.idleDuration=101;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w),S::ReadTimeout);failed(s); }
}
void semantics() {
    for(const char* fields:{"RRULE:FREQ=DAILY\r\n","STATUS:CANCELLED\r\n","RECURRENCE-ID:20261023T090000Z\r\n"}) {
        Fake f(length(body(fields)));Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w),S::ProducerFailure);failed(s);
    }
    { auto b=body();b.replace(b.find("DTSTART:"),8,"DTSTART;TZID=Example:");Fake f(length(b));Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w),S::ProducerFailure);failed(s); }
    { Fake f(length("BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:Example\r\n"));Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());expect(run(f,s,w),S::StructureFailure);failed(s); }
    { auto b=body();size_t i=b.find("20261023T090000Z");b.replace(i,16,"20200101T090000Z");i=b.find("20261023T100000Z");b.replace(i,16,"20200101T100000Z");Fake f(length(b));
      Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto r=run(f,s,w);expect(r,S::Published);assert(!r.publishedOccurrences && r.outsideWindowOccurrences==1 && s.source(0)->outcome==Snap::Outcome::Complete); }
    for(bool text:{false,true}) {
        Fake f;Snap s;init(s,text?32:0,text?16:4096);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());const auto r=run(f,s,w);expect(r,S::ProducerFailure);failed(s);
        assert(r.domain==N::Domain::Snapshot && r.producerReport.status==IcsNonrecurringSnapshot::Status::ResourceLimit);
    }
}
void exactLimits() {
    Fake first; Snap baseline; init(baseline); Ee02PsramWorkspace w; assert(w.allocate(2097152).ok());
    expect(run(first,baseline,w),S::Published);
    auto l=limits(); l.maxUrlBytes=url.size(); l.maxHostBytes=first.host.size();
    l.maxTargetBytes=url.size()-8-first.host.size(); l.maxRequestBytes=first.request.size();
    l.ingestion.receivedBytes=first.wire.size(); l.ingestion.bodyBytes=body().size();
    { Fake f; Snap s; init(s); expect(run(f,s,w,url,l),S::Published); }
    for(int kind=0;kind<5;++kind) {
        Fake f; Snap s; init(s); auto bounded=l;
        if(kind==0)--bounded.maxRequestBytes;
        if(kind==1)--bounded.ingestion.receivedBytes;
        if(kind==2)--bounded.ingestion.bodyBytes;
        if(kind==3)bounded.stage.properties=1;
        if(kind==4)bounded.structure.components=1;
        const auto r=run(f,s,w,url,bounded);
        assert(r.status!=S::Published && r.limit!=N::Limit::None); failed(s);
    }
    { Fake f; f.now=UINT64_MAX; Snap s; init(s); expect(run(f,s,w),S::InvalidClock); failed(s); assert(!f.resolves); }
}
void ownership() {
    { auto b=body(); const auto start=b.find("BEGIN:VEVENT"), end=b.find("END:VCALENDAR");
      auto second=b.substr(start,end-start); second.replace(second.find("UID:synthetic"),13,"UID:other"); b.insert(end,second);
      Fake f(length(b)); Snap s; init(s,1); Ee02PsramWorkspace w; assert(w.allocate(2097152).ok());
      expect(run(f,s,w),S::ProducerFailure); failed(s); assert(s.source(0)->reason==Snap::Reason::RecordCapacity); }

    { Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto l=limits();l.stage.poolBytes=2097152;expect(run(f,s,w,url,l),S::ResourceLimit);failed(s);assert(!f.connects); }
    { Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());auto l=limits();l.ingestion.processingMs=1;
      expect(run(f,s,w,url,l),S::InvalidArgument);assert(s.source(0)->outcome==Snap::Outcome::Pending && !f.resolves); }
    { Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());assert(s.beginSource(1)==Snap::Result::Ok);
      expect(run(f,s,w),S::InvalidState);assert(s.source(0)->outcome==Snap::Outcome::Pending && s.completeSource(1)==Snap::Result::Ok); }
    { Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());const auto t=f.transport();
      auto r=N::acquire(view(url),s,9,w,limits(),t,Fake::clock,&f,ready());expect(r,S::InvalidArgument);assert(!f.resolves); }
    { Fake f;Snap s;init(s);Ee02PsramWorkspace w;failAllocation=true;assert(!w.allocate(2097152).ok());failAllocation=false;
      const auto t=f.transport();expect(N::acquire(view(url),s,0,w,limits(),t,Fake::clock,&f,ready()),S::InvalidState); }
    { Fake f;Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());uint8_t* escaped=nullptr;
      assert(w.withScratch(0,w.capacity(),[](uint8_t* p,size_t,void* v){*static_cast<uint8_t**>(v)=p;std::memcpy(p,"https://example.invalid",23);return GfxResult::success();},&escaped).ok());
      const auto t=f.transport();const auto r=N::acquire({escaped,23},s,0,w,limits(),t,Fake::clock,&f,ready());expect(r,S::InvalidArgument);assert(!f.resolves && s.source(0)->outcome==Snap::Outcome::Pending); }
}
std::string row(const Seeed_GFX& gfx,int32_t y) { std::vector<Seeed_GFX::Glyph> g;for(const auto& c:gfx.glyphs)if(c.y==y)g.push_back(c);std::sort(g.begin(),g.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});std::string s;for(const auto& c:g)s+=char(c.code);return s; }
bool has(const Seeed_GFX& gfx,const std::string& value) { for(const auto& g:gfx.glyphs)if(row(gfx,g.y).find(value)!=std::string::npos)return true;return false; }
void multiple() {
    for(int failure=0;failure<4;++failure) {
        Snap s;init(s);Ee02PsramWorkspace w;assert(w.allocate(2097152).ok());Fake first;expect(run(first,s,w),S::Published);
        Fake second(failure==3?length(body("RRULE:FREQ=DAILY\r\n")):length(body()));
        if(failure==0)second.dnsOk=false;
        if(failure==1)second.tlsOk=false;
        if(failure==2)second.readFailAt=0;
        const auto r=run(second,s,w,url,limits(),ready(),1);assert(r.status!=S::Published);failed(s,1,1);
        assert(s.source(0)->outcome==Snap::Outcome::Complete && title(s)=="Folded title, UTF-8 Café");
        const char* tz="CET-1CEST,M3.5.0,M10.5.0/3";assert(!setenv("TZ",tz,1));tzset();FiveDayPosixTime::Context zone={tz};Seeed_GFX gfx;
        const auto drawn=FiveDayRenderer::render(gfx,s,{10,10,1025,1025},{FiveDayPosixTime::convert,&zone});
        assert(drawn.status==FiveDayRenderer::Status::Drawn && has(gfx,"Incomplete schedule") && has(gfx,"Folded title") && has(gfx,"Team"));
    }
}
}
int main() { happy();urls();transportFailures();responseFailures();semantics();exactLimits();ownership();multiple();for(const auto& a:allocations)assert(!a.live);std::printf("HTTPS ICS acquisition: %zu composed cases passed; no audited allocations\n",cases); }
