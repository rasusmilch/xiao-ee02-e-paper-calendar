#ifndef EE02_ICS_HTTPS_SOURCE_H
#define EE02_ICS_HTTPS_SOURCE_H
#include "ics_nonrecurring_snapshot.h"
#include "psram_workspace.h"
#include <climits>
#include <cstring>

// One exclusive synchronous acquisition. Caller owns readable input spans, CA,
// transport and clock contexts OUTSIDE workspace, alive until return. Callbacks
// must return normally; DNS is deliberately uncancellable. No retained aliases,
// overall deadline, retries, redirects, graphics or permanent source state.
namespace IcsHttpsSource {
using View = IcsStructure::View;
enum class Status { Published, InvalidArgument, InvalidState, InvalidUrl, WifiUnavailable,
    UntrustedTime, MissingTrust, DnsFailure, TlsFailure, WriteFailure, WriteTimeout,
    ReadFailure, ReadTimeout, InvalidClock, HttpFailure, RedirectRequired,
    StructureFailure, StageFailure, ProducerFailure, SnapshotFailure, ResourceLimit };
enum class Domain { None, Admission, Url, Prerequisite, Dns, Tls, Write, Read, Http, Structure, Stage, Producer, Snapshot, Workspace };
enum class Limit { None, Url, Host, Target, Request, Workspace, Ingestion, Structure, Stage, Producer };
enum class IoStatus { Data, WouldBlock, Closed, Error };
struct Io { IoStatus status; size_t bytes; };
// Milliseconds shared by TCP connect and socket read/write. The pinned client's
// TLS write helper retains this connect-time timeout internally. Handshake uses
// seconds, matching NetworkClientSecure::setHandshakeTimeout exactly.
struct Timeouts { uint32_t socketMs, handshakeSeconds; uint64_t writeStalledMs; };
struct Transport {
    void* context;
    bool (*resolve)(void*, const char* host);
    bool (*connect)(void*, const char* host, uint16_t port, const char* ca, bool trustedTime, const Timeouts&);
    Io (*write)(void*, const uint8_t*, size_t);
    Io (*read)(void*, uint8_t*, size_t);
    void (*idle)(void*); // Yield on empty polls; must not suppress clock progress.
    void (*close)(void*); // Idempotent; revoke all borrowed host/CA/buffer aliases.
};
struct Limits {
    size_t maxUrlBytes, maxHostBytes, maxTargetBytes, maxRequestBytes, receiveBytes;
    Timeouts timeouts;
    IcsIngestion::Limits ingestion; // processingMs MUST be zero; stalledMs positive.
    IcsStructure::Limits structure;
    IcsSourceStage::Limits stage;
    IcsNonrecurringSnapshot::Limits producer;
};
struct Prerequisites { bool wifiReady, trustedWallClock; View caPem; }; // Includes terminal NUL.
struct Report {
    Status status; Domain domain; Limit limit;
    uint16_t httpStatus;
    IcsIngestion::Status ingestionStatus; IcsIngestion::Limit ingestionLimit;
    IcsStructure::Status structureStatus; IcsStructure::Limit structureLimit;
    IcsSourceStage::Status stageStatus; IcsSourceStage::Limit stageLimit;
    IcsNonrecurringSnapshot::Report producerReport;
    FiveDaySnapshot::Result snapshotResult;
    size_t publishedOccurrences, outsideWindowOccurrences;
};
namespace detail {
inline Report result(Status s, Domain d, Limit l = Limit::None) {
    Report r = {}; r.status=s; r.domain=d; r.limit=l;
    r.ingestionStatus=IcsIngestion::Status::NotInitialized;
    r.structureStatus=IcsStructure::Status::NotInitialized;
    r.stageStatus=IcsSourceStage::Status::NotInitialized;
    auto& p=r.producerReport;
    p.status=IcsNonrecurringSnapshot::Status::InvalidArgument;
    p.failureCategory=IcsNonrecurringSnapshot::FailureCategory::Admission;
    p.componentIndex=p.propertyIndex=IcsSourceStage::None;
    p.localReport.status=IcsSourceLocal::Status::InvalidArgument;
    p.localReport.componentIndex=p.localReport.propertyIndex=IcsSourceStage::None;
    p.typedStatus=IcsTypedValues::Status::InvalidArgument;
    p.snapshotResult=FiveDaySnapshot::Result::NotInitialized;
    r.snapshotResult=FiveDaySnapshot::Result::NotInitialized; return r;
}
inline bool alpha(uint8_t c) { return (c>='A' && c<='Z') || (c>='a' && c<='z'); }
inline bool digit(uint8_t c) { return c>='0' && c<='9'; }
inline bool hex(uint8_t c) { return digit(c) || (c>='a' && c<='f') || (c>='A' && c<='F'); }
struct Url { size_t hostBegin, hostSize, authorityEnd, resourceBegin, targetSize; uint16_t port; bool slash; };
inline Report url(View v, const Limits& l, Url& out) {
    if (!ee02_ics_detail::span(v.data,v.size) || v.size<8) return result(Status::InvalidUrl,Domain::Url);
    if (v.size>l.maxUrlBytes) return result(Status::ResourceLimit,Domain::Url,Limit::Url);
    if (!IcsStructure::identifier({v.data,8},"HTTPS://")) return result(Status::InvalidUrl,Domain::Url);
    size_t i=8;
    while(i<v.size && v.data[i]!='/' && v.data[i]!='?') ++i;
    const size_t authorityEnd=i;
    size_t hostEnd=8;
    while(hostEnd<authorityEnd && v.data[hostEnd]!=':') ++hostEnd;
    const size_t n=hostEnd-8;
    if (!n || n>253) return result(Status::InvalidUrl,Domain::Url);
    if (n>l.maxHostBytes) return result(Status::ResourceLimit,Domain::Url,Limit::Host);
    bool letter=false; size_t label=8;
    for(size_t j=8;j<=hostEnd;++j) {
        if(j==hostEnd || v.data[j]=='.') {
            if(j==label || j-label>63 || v.data[label]=='-' || v.data[j-1]=='-') return result(Status::InvalidUrl,Domain::Url);
            label=j+1;
        } else {
            const auto c=v.data[j]; letter=letter || alpha(c);
            if (!alpha(c) && !digit(c) && c!='-') return result(Status::InvalidUrl,Domain::Url);
        }
    }
    if (!letter) return result(Status::InvalidUrl,Domain::Url); // Numeric address forms unsupported.
    uint32_t port=443;
    if (hostEnd<authorityEnd) {
        port=0; size_t j=hostEnd+1;
        if(j==authorityEnd) return result(Status::InvalidUrl,Domain::Url);
        for(;j<authorityEnd;++j) {
            if(!digit(v.data[j]) || port>(65535U-(v.data[j]-'0'))/10) return result(Status::InvalidUrl,Domain::Url);
            port=port*10+v.data[j]-'0';
        }
        if(!port) return result(Status::InvalidUrl,Domain::Url);
    }
    const bool slash=i==v.size || v.data[i]=='?';
    if (v.size-i>SIZE_MAX-size_t(slash)) return result(Status::InvalidUrl,Domain::Url);
    const size_t target=v.size-i+size_t(slash);
    if(target>l.maxTargetBytes) return result(Status::ResourceLimit,Domain::Url,Limit::Target);
    for(size_t j=i;j<v.size;++j) {
        const auto c=v.data[j];
        if(c=='%') {
            if(v.size-j<3 || !hex(v.data[j+1]) || !hex(v.data[j+2])) return result(Status::InvalidUrl,Domain::Url);
            j+=2;
        } else if (!c || c>=127 || (!alpha(c) && !digit(c) && !std::strchr("-._~!$&'()*+,;=:@/?",c)))
            return result(Status::InvalidUrl,Domain::Url);
    }
    out={8,n,authorityEnd,i,target,uint16_t(port),slash}; return result(Status::Published,Domain::None);
}
inline bool append(uint8_t* dst,size_t capacity,size_t& used,const void* bytes,size_t n) {
    if(n>capacity-used) return false;
    if(n) std::memcpy(dst+used,bytes,n);
    used+=n; return true;
}
inline bool take(uint8_t* base,size_t capacity,size_t& used,size_t n,uint8_t*& out) {
    if(n>capacity-used) return false;
    out=base+used; used+=n; return true;
}
struct Clock {
    // UINT64_MAX is reserved for an invalid platform-clock observation.
    IcsIngestion::Clock read; void* context; uint64_t last; bool seen, bad;
    Clock(IcsIngestion::Clock fn,void* p) : read(fn),context(p),last(0),seen(false),bad(false) {}
    uint64_t sample() {
        const uint64_t now=read(context); if(now==UINT64_MAX || (seen && now<last)) bad=true;
        last=now; seen=true; return now;
    }
    static uint64_t callback(void* p) { return static_cast<Clock*>(p)->sample(); }
};
struct Close {
    const Transport& t; bool live;
    explicit Close(const Transport& io) : t(io),live(true) {}
    void now() { if(live) { live=false; t.close(t.context); } }
    ~Close() { now(); }
};
struct Call {
    View input; FiveDaySnapshot& snapshot; size_t source; const Limits& limits;
    const Transport& t; Prerequisites prerequisites; Clock clock; Report report;
    GfxResult run(uint8_t* bytes,size_t capacity) {
        Close socket{t};
        const auto rejectOverlap=[&](const void* p,size_t n) { return ee02_ics_detail::overlap(bytes,capacity,p,n); };
        if(rejectOverlap(input.data,input.size) || rejectOverlap(prerequisites.caPem.data,prerequisites.caPem.size) ||
            rejectOverlap(&snapshot,sizeof(snapshot)) || rejectOverlap(snapshot.window(),snapshot.allocationBytes()) ||
            rejectOverlap(&limits,sizeof(limits)) || rejectOverlap(&t,sizeof(t)) ||
            rejectOverlap(t.context,t.context?1:0) || rejectOverlap(clock.context,clock.context?1:0)) {
            report=result(Status::InvalidArgument,Domain::Admission); return GfxResult::success();
        }
        if(!prerequisites.wifiReady) { report=result(Status::WifiUnavailable,Domain::Prerequisite); return GfxResult::success(); }
        if(!prerequisites.trustedWallClock) { report=result(Status::UntrustedTime,Domain::Prerequisite); return GfxResult::success(); }
        const View ca=prerequisites.caPem;
        if(!ee02_ics_detail::span(ca.data,ca.size) || ca.size<2 || ca.data[ca.size-1] || std::memchr(ca.data,0,ca.size-1)) {
            report=result(Status::MissingTrust,Domain::Prerequisite); return GfxResult::success();
        }
        Url parsed={}; report=url(input,limits,parsed);
        if(report.status!=Status::Published) return GfxResult::success();
        size_t stageBytes=0,parserBytes=0;
        if(IcsSourceStage::requiredBytes(limits.stage,stageBytes)!=IcsSourceStage::Status::InProgress ||
            IcsStructure::scratchBytes(limits.structure,parserBytes)!=IcsStructure::Status::InProgress) {
            report=result(Status::InvalidArgument,Domain::Admission); return GfxResult::success();
        }
        const auto& h=limits.ingestion;
        size_t used=0; uint8_t *host,*request,*receive,*httpBytes,*parserStorage,*stageStorage,*text;
        size_t httpSize=h.metadataLine;
        if(h.location>SIZE_MAX-httpSize || h.logicalLine>SIZE_MAX-httpSize-h.location ||
            limits.producer.maxDecodedTextBytes>SIZE_MAX/2) {
            report=result(Status::ResourceLimit,Domain::Workspace,Limit::Workspace); return GfxResult::success();
        }
        httpSize+=h.location+h.logicalLine;
        if(!take(bytes,capacity,used,limits.maxHostBytes+1,host) || !take(bytes,capacity,used,limits.maxRequestBytes,request) ||
            !take(bytes,capacity,used,limits.receiveBytes,receive) || !take(bytes,capacity,used,httpSize,httpBytes) ||
            !take(bytes,capacity,used,parserBytes,parserStorage) || !take(bytes,capacity,used,stageBytes,stageStorage)) {
            report=result(Status::ResourceLimit,Domain::Workspace,Limit::Workspace); return GfxResult::success();
        }
        const size_t liveBytes=used;
        if(!take(bytes,capacity,used,2*limits.producer.maxDecodedTextBytes,text)) {
            report=result(Status::ResourceLimit,Domain::Workspace,Limit::Workspace); return GfxResult::success();
        }
        std::memcpy(host,input.data+parsed.hostBegin,parsed.hostSize); host[parsed.hostSize]=0;
        size_t requestBytes=0;
        const auto literal=[&](const char* s) { return append(request,limits.maxRequestBytes,requestBytes,s,std::strlen(s)); };
        if(!literal("GET ") || (parsed.slash && !literal("/")) ||
            !append(request,limits.maxRequestBytes,requestBytes,input.data+parsed.resourceBegin,input.size-parsed.resourceBegin) ||
            !literal(" HTTP/1.1\r\nHost: ") || !append(request,limits.maxRequestBytes,requestBytes,input.data+8,parsed.authorityEnd-8) ||
            !literal("\r\nAccept: text/calendar\r\nAccept-Encoding: identity\r\nConnection: close\r\n\r\n")) {
            report=result(Status::ResourceLimit,Domain::Write,Limit::Request); return GfxResult::success();
        }
        report=result(Status::Published,Domain::None); clock.sample();
        if(clock.bad) { report=result(Status::InvalidClock,Domain::Admission); return GfxResult::success(); }
        if(!t.resolve(t.context,reinterpret_cast<const char*>(host))) { report=result(Status::DnsFailure,Domain::Dns); return GfxResult::success(); }
        clock.sample(); if(clock.bad) { report=result(Status::InvalidClock,Domain::Admission); return GfxResult::success(); }
        if(!t.connect(t.context,reinterpret_cast<const char*>(host),parsed.port,reinterpret_cast<const char*>(ca.data),
                      prerequisites.trustedWallClock,limits.timeouts)) {
            report=result(Status::TlsFailure,Domain::Tls); return GfxResult::success();
        }
        uint64_t progress=clock.sample(); size_t sent=0;
        while(sent<requestBytes && !clock.bad) {
            const auto io=t.write(t.context,request+sent,requestBytes-sent); const uint64_t now=clock.sample();
            if(io.status==IoStatus::Data && io.bytes && io.bytes<=requestBytes-sent) { sent+=io.bytes; progress=now; }
            else if(io.status==IoStatus::WouldBlock && !io.bytes) {
                if(!clock.bad && now-progress>limits.timeouts.writeStalledMs) { report=result(Status::WriteTimeout,Domain::Write); return GfxResult::success(); }
                t.idle(t.context);
            } else { report=result(Status::WriteFailure,Domain::Write); return GfxResult::success(); }
        }
        if(clock.bad) { report=result(Status::InvalidClock,Domain::Admission); return GfxResult::success(); }
        IcsSourceStage stage; IcsStructure structure; IcsIngestion http;
        report.stageStatus=stage.initialize(stageStorage,stageBytes,limits.stage);
        if(report.stageStatus==IcsSourceStage::Status::InProgress)
            report.structureStatus=structure.initialize(parserStorage,parserBytes,limits.structure,IcsSourceStage::sink,&stage);
        if(report.structureStatus==IcsStructure::Status::InProgress)
            report.ingestionStatus=http.initialize(httpBytes,httpSize,h,IcsStructure::lineSink,&structure,Clock::callback,&clock);
        if(report.ingestionStatus!=IcsIngestion::Status::InProgress) {
            report.status=Status::InvalidArgument; report.domain=Domain::Admission; return GfxResult::success();
        }
        while(http.status()==IcsIngestion::Status::InProgress && !http.bodyBoundaryReached() && !clock.bad) {
            const auto io=t.read(t.context,receive,limits.receiveBytes);
            if(io.status==IoStatus::Data && io.bytes && io.bytes<=limits.receiveBytes) http.feed(receive,io.bytes);
            else if(io.status==IoStatus::WouldBlock && !io.bytes) {
                http.transport(IcsIngestion::Transport::WouldBlock); t.idle(t.context);
            } else if(io.status==IoStatus::Closed && !io.bytes) { http.transport(IcsIngestion::Transport::Closed); break; }
            else { http.transport(IcsIngestion::Transport::Error); break; }
        }
        if(http.status()==IcsIngestion::Status::InProgress) http.finish();
        report.ingestionStatus=http.status(); report.ingestionLimit=http.limit(); report.httpStatus=http.httpStatus();
        report.structureStatus=structure.status(); report.structureLimit=structure.diagnostic().limit;
        report.stageStatus=stage.status(); report.stageLimit=stage.diagnostic().limit;
        if(clock.bad || http.status()==IcsIngestion::Status::InvalidClock) { report.status=Status::InvalidClock; report.domain=Domain::Admission; }
        else if(stage.status()!=IcsSourceStage::Status::InProgress) { report.status=Status::StageFailure; report.domain=Domain::Stage; }
        else if(structure.status()!=IcsStructure::Status::InProgress) { report.status=Status::StructureFailure; report.domain=Domain::Structure; }
        else if(http.status()!=IcsIngestion::Status::Complete) {
            report.domain=Domain::Http; report.status=http.status()==IcsIngestion::Status::RedirectRequired?Status::RedirectRequired:
                http.limit()==IcsIngestion::Limit::StallTime?Status::ReadTimeout:
                http.status()==IcsIngestion::Status::TransportFailure?Status::ReadFailure:
                http.status()==IcsIngestion::Status::ResourceLimit?Status::ResourceLimit:Status::HttpFailure;
            if(http.status()==IcsIngestion::Status::ResourceLimit) report.limit=Limit::Ingestion;
        } else {
            report.structureStatus=structure.finish(); report.structureLimit=structure.diagnostic().limit;
            if(report.structureStatus!=IcsStructure::Status::StructurallyComplete) { report.status=Status::StructureFailure; report.domain=Domain::Structure; }
            else {
                report.stageStatus=stage.finish(http,structure); report.stageLimit=stage.diagnostic().limit;
                if(report.stageStatus!=IcsSourceStage::Status::StagedSuccessfully) { report.status=Status::StageFailure; report.domain=Domain::Stage; }
                else {
                    socket.now(); // No TLS/socket aliases remain during semantic publication.
                    report.producerReport=IcsNonrecurringSnapshot::publish(stage,snapshot,source,limits.producer,text,
                        2*limits.producer.maxDecodedTextBytes,{bytes,liveBytes});
                    const auto& p=report.producerReport; report.snapshotResult=p.snapshotResult;
                    if(p.status==IcsNonrecurringSnapshot::Status::SourceCommitted) {
                        report.publishedOccurrences=p.publishedOccurrences; report.outsideWindowOccurrences=p.outsideWindowOccurrences;
                    } else {
                        report.status=p.status==IcsNonrecurringSnapshot::Status::InvalidState?Status::InvalidState:
                            p.status==IcsNonrecurringSnapshot::Status::SnapshotFailure?Status::SnapshotFailure:Status::ProducerFailure;
                        if(p.status==IcsNonrecurringSnapshot::Status::ResourceLimit) report.limit=Limit::Producer;
                        report.domain=p.failureCategory==IcsNonrecurringSnapshot::FailureCategory::Snapshot?Domain::Snapshot:Domain::Producer;
                    }
                }
            }
        }
        if(report.stageLimit!=IcsSourceStage::Limit::None) report.limit=Limit::Stage;
        else if(report.structureLimit!=IcsStructure::Limit::None) report.limit=Limit::Structure;
        return GfxResult::success(); // Destruct all source users inside withScratch.
    }
    static GfxResult callback(uint8_t* p,size_t n,void* context) { return static_cast<Call*>(context)->run(p,n); }
};
}
inline Report acquire(View url,FiveDaySnapshot& snapshot,size_t sourceIndex,Ee02PsramWorkspace& workspace,
                      const Limits& limits,const Transport& transport,IcsIngestion::Clock clock,void* clockContext,
                      Prerequisites prerequisites) {
    using namespace detail;
    const auto* selected=snapshot.source(sourceIndex);
    if(!selected || !snapshot.initialized()) return result(Status::InvalidArgument,Domain::Admission);
    if(selected->outcome!=FiveDaySnapshot::Outcome::Pending || workspace.state()!=Ee02PsramWorkspace::State::Scratch)
        return result(Status::InvalidState,Domain::Admission);
    if(!clock || !transport.resolve || !transport.connect || !transport.write || !transport.read || !transport.idle || !transport.close ||
        !limits.maxUrlBytes || !limits.maxHostBytes || limits.maxHostBytes>253 || !limits.maxTargetBytes ||
        !limits.maxRequestBytes || limits.maxRequestBytes>INT_MAX || !limits.receiveBytes || limits.receiveBytes>INT_MAX ||
        !limits.timeouts.socketMs || limits.timeouts.socketMs>INT32_MAX || !limits.timeouts.handshakeSeconds ||
        limits.timeouts.handshakeSeconds>UINT32_MAX/1000 || !limits.timeouts.writeStalledMs ||
        limits.ingestion.processingMs || !limits.ingestion.stalledMs)
        return result(Status::InvalidArgument,Domain::Admission);
    Call call{url,snapshot,sourceIndex,limits,transport,prerequisites,{clock,clockContext},result(Status::InvalidState,Domain::Workspace)};
    if(!workspace.withScratch(0,workspace.capacity(),Call::callback,&call).ok()) return result(Status::InvalidState,Domain::Workspace);
    if(call.report.status!=Status::Published && call.report.status!=Status::InvalidArgument &&
        call.report.status!=Status::InvalidState && snapshot.source(sourceIndex)->outcome==FiveDaySnapshot::Outcome::Pending) {
        const auto begun=snapshot.beginSource(sourceIndex);
        if(begun==FiveDaySnapshot::Result::Ok) call.report.snapshotResult=snapshot.failSource(sourceIndex,FiveDaySnapshot::Reason::ProducerFailure);
        else { call.report.status=Status::InvalidState; call.report.domain=Domain::Snapshot; call.report.snapshotResult=begun; }
    }
    return call.report;
}
}
#endif
