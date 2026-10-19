#ifndef EE02_FIVE_DAY_CALENDAR_REFRESH_H
#define EE02_FIVE_DAY_CALENDAR_REFRESH_H
#include "five_day_window.h"
#include "ics_https_source.h"

// One synchronous refresh. Inputs, contexts and their readable spans remain
// outside workspace for the whole call. Transport starts closed and is exclusive.
// No concurrent TZ/snapshot/workspace use.
namespace FiveDayCalendarRefresh {
struct Source { bool enabled; FiveDaySnapshot::Text label; IcsHttpsSource::View url; };
struct Limits {
    size_t maxConfiguredSources, recordCapacity, textCapacity;
    IcsHttpsSource::Limits acquisition;
};
enum class Status { AllComplete, Partial, NoneComplete, NoSelection, AdmissionFailure, Interrupted };
enum class Error { None, InvalidArgument, InvalidState, Window, Snapshot, Workspace, Acquisition, Transaction, Overflow };
struct SourceResult {
    size_t configurationIndex, snapshotIndex;
    bool acquisitionInvoked; // Only then is acquisition authoritative.
    IcsHttpsSource::Report acquisition;
    FiveDaySnapshot::Outcome outcome;
    FiveDaySnapshot::Result reconciliation;
};
// Synchronous observation only: no reentry, TZ changes, snapshot mutation,
// workspace release or escaped references. The pointer-free value may be copied.
using Callback = void (*)(const SourceResult&, void*);
struct Report {
    Status status;
    Error error;
    FiveDayWindow::Status windowStatus;
    FiveDaySnapshot::Result initialization;
    size_t configuredSources, selectedSources, attemptedSources;
    size_t completeSources, incompleteSources, pendingSources;
    size_t publishedOccurrences, outsideWindowOccurrences;
    size_t fatalConfigurationIndex; // SIZE_MAX when not source-associated.
    bool acquisitionDiagnosticActive;
    IcsHttpsSource::Report acquisitionDiagnostic;
    FiveDaySnapshot::Result transactionResult;
    bool acquisitionComplete; // Normal end, all selected outcomes terminal; safe phase handoff.
};
namespace detail {
struct Initialize {
    const Source* sources; size_t count, selected;
    const char* timezone; FiveDaySnapshot& snapshot;
    const FiveDaySnapshot::Window& window; const Limits& limits;
    const IcsHttpsSource::Transport& transport;
    IcsHttpsSource::Prerequisites prerequisites;
    void* clockContext; void* callbackContext;
    Error error; FiveDaySnapshot::Result result;
    static GfxResult run(uint8_t* bytes,size_t capacity,void* context) {
        auto& c=*static_cast<Initialize*>(context);
        const auto outside=[&](const void* p,size_t n) {
            return ee02_ics_detail::span(p,n) && !ee02_ics_detail::overlap(bytes,capacity,p,n);
        };
        // Validate descriptors before dereferencing; disabled spans are never read.
        if(!outside(c.sources,c.count*sizeof(Source)) || !outside(&c.snapshot,sizeof(c.snapshot)) ||
            !outside(&c.limits,sizeof(c.limits)) || !outside(&c.transport,sizeof(c.transport)) ||
            !outside(c.timezone,std::strlen(c.timezone)+1) ||
            !outside(c.prerequisites.caPem.data,c.prerequisites.caPem.size) ||
            !outside(c.transport.context,c.transport.context?1:0) ||
            !outside(c.clockContext,c.clockContext?1:0) ||
            !outside(c.callbackContext,c.callbackContext?1:0)) return GfxResult::success();
        for(size_t i=0;i<c.count;++i) if(c.sources[i].enabled) {
            const auto& s=c.sources[i];
            if(!outside(s.label.data,s.label.size)) return GfxResult::success();
            // Invalid URL spans are source failures later. Valid spans must not
            // alias scratch, even though the snapshot never stores URL bytes.
            if(ee02_ics_detail::span(s.url.data,s.url.size) && !outside(s.url.data,s.url.size))
                return GfxResult::success();
        }
        const size_t padding=(alignof(FiveDaySnapshot::Text)-reinterpret_cast<uintptr_t>(bytes)%alignof(FiveDaySnapshot::Text))%alignof(FiveDaySnapshot::Text);
        if(padding>capacity || c.selected>(capacity-padding)/sizeof(FiveDaySnapshot::Text)) {
            c.error=Error::Workspace; return GfxResult::success();
        }
        auto* labels=reinterpret_cast<FiveDaySnapshot::Text*>(bytes+padding);
        size_t j=0;
        for(size_t i=0;i<c.count;++i) if(c.sources[i].enabled)
            new (labels+j++) FiveDaySnapshot::Text(c.sources[i].label);
        c.result=c.snapshot.initialize(c.window,c.limits.recordCapacity,c.limits.textCapacity,labels,c.selected);
        for(size_t i=0;i<c.selected;++i) labels[i].~Text();
        c.error=c.result==FiveDaySnapshot::Result::Ok?Error::None:Error::Snapshot;
        return GfxResult::success();
    }
};
inline void counts(Report& r,const FiveDaySnapshot& snapshot) {
    r.completeSources=r.incompleteSources=r.pendingSources=0;
    for(size_t i=0;i<snapshot.sourceCount();++i) {
        const auto outcome=snapshot.source(i)->outcome;
        if(outcome==FiveDaySnapshot::Outcome::Complete) ++r.completeSources;
        else if(outcome==FiveDaySnapshot::Outcome::Incomplete) ++r.incompleteSources;
        else ++r.pendingSources;
    }
    r.publishedOccurrences=snapshot.occurrenceCount();
}
}
inline Report refresh(int64_t nowUtc,bool clockTrusted,const char* configuredTimezone,
    Ee02PsramWorkspace& workspace,FiveDaySnapshot& snapshot,const Source* sources,size_t sourceCount,
    const Limits& limits,bool wifiReady,IcsHttpsSource::View caPem,const IcsHttpsSource::Transport& transport,
    IcsIngestion::Clock clock,void* clockContext,Callback callback=nullptr,void* callbackContext=nullptr) {
    Report r={}; r.status=Status::AdmissionFailure; r.error=Error::InvalidArgument;
    r.windowStatus=FiveDayWindow::Status::InvalidArgument;
    r.initialization=r.transactionResult=FiveDaySnapshot::Result::NotInitialized;
    r.fatalConfigurationIndex=SIZE_MAX; r.configuredSources=sourceCount;
    if(snapshot.initialized() || workspace.state()!=Ee02PsramWorkspace::State::Scratch) {
        r.error=Error::InvalidState; return r;
    }
    if(sourceCount>limits.maxConfiguredSources || sourceCount>size_t(PTRDIFF_MAX)/sizeof(Source) ||
        !ee02_ics_detail::span(sources,sourceCount*sizeof(Source)) ||
        (sourceCount && reinterpret_cast<uintptr_t>(sources)%alignof(Source))) return r;
    // Never inspect descriptors borrowed from workspace outside their lifetime.
    // Selection itself is performed under its exclusive scratch access below.
    struct Selection { const Source* sources; size_t count, selected; bool valid; } selection={sources,sourceCount,0,false};
    const auto select=[](uint8_t* p,size_t n,void* v)->GfxResult {
        auto& s=*static_cast<Selection*>(v);
        if(ee02_ics_detail::overlap(p,n,s.sources,s.count*sizeof(Source))) return GfxResult::success();
        for(size_t i=0;i<s.count;++i) if(s.sources[i].enabled) ++s.selected;
        s.valid=true; return GfxResult::success();
    };
    if(!workspace.withScratch(0,workspace.capacity(),select,&selection).ok() || !selection.valid) return r;
    r.selectedSources=selection.selected; // Bounded by admitted descriptor count.
    FiveDaySnapshot::Window window={};
    r.windowStatus=FiveDayWindow::build(nowUtc,clockTrusted,configuredTimezone,window);
    if(r.windowStatus!=FiveDayWindow::Status::Ok) { r.error=Error::Window; return r; }
    detail::Initialize init={sources,sourceCount,r.selectedSources,configuredTimezone,snapshot,window,limits,transport,
        {wifiReady,clockTrusted,caPem},clockContext,callbackContext,Error::InvalidArgument,FiveDaySnapshot::Result::NotInitialized};
    if(!workspace.withScratch(0,workspace.capacity(),detail::Initialize::run,&init).ok()) { r.error=Error::Workspace; return r; }
    r.initialization=init.result; r.error=init.error;
    if(r.error!=Error::None) return r;
    r.status=Status::Interrupted;
    size_t compact=0;
    for(size_t i=0;i<sourceCount;++i) if(sources[i].enabled) {
        if(workspace.state()!=Ee02PsramWorkspace::State::Scratch || snapshot.sourceCount()!=r.selectedSources || !snapshot.source(compact)) {
            r.error=Error::InvalidState; r.fatalConfigurationIndex=i; break;
        }
        SourceResult child={}; child.configurationIndex=i; child.snapshotIndex=compact;
        child.reconciliation=FiveDaySnapshot::Result::Ok;
        ++r.attemptedSources; // Includes selected configuration failures without network I/O.
        {
            child.acquisitionInvoked=true;
            // An unreadable arithmetic span is an invalid/missing URL, never
            // dereferenced. Still run component-owned shared admission checks.
            const auto url=ee02_ics_detail::span(sources[i].url.data,sources[i].url.size)?sources[i].url:IcsHttpsSource::View{nullptr,0};
            child.acquisition=IcsHttpsSource::acquire(url,snapshot,compact,workspace,limits.acquisition,transport,clock,clockContext,
                {wifiReady,clockTrusted,caPem});
            const auto& a=child.acquisition;
            // Component-owned admission checks remain authoritative. A shared
            // invalid limit/storage/state interrupts rather than blaming URLs.
            if(a.status==IcsHttpsSource::Status::InvalidArgument || a.status==IcsHttpsSource::Status::InvalidState ||
                a.domain==IcsHttpsSource::Domain::Workspace) {
                r.error=Error::Acquisition; r.fatalConfigurationIndex=i;
                r.acquisitionDiagnosticActive=true; r.acquisitionDiagnostic=a;
            }
        }
        if(r.error==Error::None && snapshot.source(compact)->outcome==FiveDaySnapshot::Outcome::Pending) {
            child.reconciliation=snapshot.beginSource(compact);
            if(child.reconciliation==FiveDaySnapshot::Result::Ok)
                child.reconciliation=snapshot.failSource(compact,FiveDaySnapshot::Reason::ProducerFailure);
            if(child.reconciliation!=FiveDaySnapshot::Result::Ok) {
                r.error=Error::Transaction; r.fatalConfigurationIndex=i; r.transactionResult=child.reconciliation;
            }
        }
        child.outcome=snapshot.source(compact)->outcome;
        if(child.outcome==FiveDaySnapshot::Outcome::Complete && child.acquisitionInvoked) {
            const size_t outside=child.acquisition.outsideWindowOccurrences;
            if(outside>SIZE_MAX-r.outsideWindowOccurrences) { r.error=Error::Overflow; r.fatalConfigurationIndex=i; }
            else r.outsideWindowOccurrences+=outside;
        }
        if(callback) callback(child,callbackContext);
        if(r.error!=Error::None) break;
        ++compact;
    }
    detail::counts(r,snapshot);
    if(r.error==Error::None && workspace.state()==Ee02PsramWorkspace::State::Scratch && !r.pendingSources) {
        r.acquisitionComplete=true;
        r.status=!r.selectedSources?Status::NoSelection:!r.incompleteSources?Status::AllComplete:
            r.completeSources?Status::Partial:Status::NoneComplete;
    } else if(r.error==Error::None) r.error=Error::InvalidState;
    return r;
}
}
#endif
