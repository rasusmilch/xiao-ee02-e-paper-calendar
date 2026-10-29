#ifndef EE02_SETTINGS_PORTAL_H
#define EE02_SETTINGS_PORTAL_H
#include "settings_http.h"

namespace SettingsPortal {
static const uint64_t IdleMs=600000, AbsoluteMs=1800000, RequestMs=30000;
enum class Mode { FirstBootAp, AwakeLan };
enum class Status { AdmissionFailure, StartFailure, IdleExpired, AbsoluteExpired, ClockFailure, Finished, ShutdownFailure };
struct Report {
    Status status; bool shutdownVerified; size_t requests,saves,unchanged;
    DeviceSettings::Result initial,save,reloaded;
};
struct Work { SettingsHttp::Parser request; }; // caller-owned, exclusive, outside acquisition scratch/task stack
using Clock=uint64_t (*)(void*);
struct Session {
    Clock clock; void* context; uint64_t start=0,activity=0,last=0;
    Status status=Status::AdmissionFailure; bool valid=false;
    Session(Clock c,void* p):clock(c),context(p) {}
    bool begin() {
        if(!clock) return false;
        start=activity=last=clock(context); valid=last!=UINT64_MAX;
        if(!valid) status=Status::ClockFailure;
        return valid;
    }
    bool check() {
        if(!valid) return false;
        const uint64_t now=clock(context);
        if(now==UINT64_MAX || now<last) { valid=false;status=Status::ClockFailure;return false; }
        last=now;
        if(now-start>=AbsoluteMs) {status=Status::AbsoluteExpired;valid=false;}
        else if(now-activity>=IdleMs) {status=Status::IdleExpired;valid=false;}
        return valid;
    }
    bool requestLive(uint64_t began) { return check() && last>=began && last-began<RequestMs; }
};
// Endpoint is one synchronous byte-I/O seam, not a network service registry.
// start(mode), accept(), receive()/send(): positive bytes, 0 would-block,
// negative closed/error; idle() yields; closeClient()/stop(mode) verify cleanup;
// random16() supplies fresh cryptographic platform randomness after radio startup.
template<class Endpoint> class Writer {
    Endpoint& endpoint_; Session& session_; uint64_t began_; char buffer_[256]; size_t used_=0,total_=0;
public:
    bool ok=true;
    Writer(Endpoint& endpoint,Session& session,uint64_t began):endpoint_(endpoint),session_(session),began_(began) {}
    bool flush() {
        size_t sent=0;
        while(ok && sent<used_) {
            if(!session_.requestLive(began_)) {ok=false;break;}
            const int n=endpoint_.send(reinterpret_cast<const uint8_t*>(buffer_+sent),used_-sent);
            if(n<0 || size_t(n)>used_-sent) {ok=false;break;}
            if(n) sent+=size_t(n); else endpoint_.idle();
        }
        used_=0; return ok;
    }
    void bytes(const char* p,size_t n) {
        if(!ok || n>SettingsHttp::ResponseBytes-total_) {ok=false;return;}
        total_+=n;
        for(size_t i=0;i<n && ok;++i) {buffer_[used_++]=p[i];if(used_==sizeof(buffer_))flush();}
    }
    void text(const char* s) { bytes(s,strlen(s)); }
    void number(uint64_t n) { char buf[21];const int count=snprintf(buf,sizeof(buf),"%llu",static_cast<unsigned long long>(n));if(count>0)bytes(buf,size_t(count)); }
    void escaped(const char* p,size_t n) {
        for(size_t i=0;i<n;++i) switch(p[i]) {
        case '&':text("&amp;");break;case '<':text("&lt;");break;case '>':text("&gt;");break;
        case '"':text("&quot;");break;case '\'':text("&#39;");break;
        case '\t':text("&#9;");break;case '\n':text("&#10;");break;case '\r':text("&#13;");break;default:bytes(p+i,1);break;
        }
    }
};
template<class Sink> inline void headers(Sink& out,unsigned code) {
    const char* reason=code==200?"OK":code==400?"Bad Request":code==403?"Forbidden":code==404?"Not Found":
        code==405?"Method Not Allowed":code==413?"Content Too Large":code==422?"Unprocessable Content":"Service Unavailable";
    out.text("HTTP/1.1 ");out.number(code);out.text(" ");out.text(reason);
    out.text("\r\nContent-Type: text/html; charset=utf-8\r\nCache-Control: no-store\r\nReferrer-Policy: no-referrer\r\nX-Content-Type-Options: nosniff\r\nContent-Security-Policy: default-src 'none'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'\r\nConnection: close\r\n\r\n");
}
// No automatic station teardown/restart/sleep here. The root owns AP handoff;
// an explicit LAN caller owns its existing STA connection and subsequent action.
template<class Endpoint> Report run(DeviceSettings::Service& settings,Work& work,Mode mode,
    const char* authority,Endpoint& endpoint,Clock clock,void* context=nullptr) {
    Report report={};report.status=Status::AdmissionFailure;
    if(!clock || !authority || !*authority || strlen(authority)>15) return report;
    report.initial=settings.load();
    if((mode==Mode::FirstBootAp && report.initial.status!=DeviceSettings::Status::Missing) ||
       (mode==Mode::AwakeLan && report.initial.status!=DeviceSettings::Status::Ok)) return report;
    if(!endpoint.start(mode)) {report.status=Status::StartFailure;report.shutdownVerified=endpoint.stop(mode);if(!report.shutdownVerified)report.status=Status::ShutdownFailure;return report;}
    Session session(clock,context);uint8_t entropy[16]={};char token[33]={};
    endpoint.random16(entropy);
    const char hex[]="0123456789abcdef";
    for(size_t i=0;i<16;++i){token[2*i]=hex[entropy[i]>>4];token[2*i+1]=hex[entropy[i]&15];}
    bool clean=true,finished=false,usable=true;
    if(!session.begin()) report.status=Status::ClockFailure;
    while(session.check()) {
        if(!endpoint.accept()) {endpoint.idle();continue;}
        ++report.requests;const uint64_t began=session.last;
        auto& parser=work.request;parser.reset();uint8_t received[128];
        while(parser.status==SettingsHttp::Status::Reading && session.requestLive(began)) {
            const int count=endpoint.receive(received,sizeof(received));
            if(count<0) {parser.eof();break;}
            if(size_t(count)>sizeof(received)) {parser.status=SettingsHttp::Status::BadRequest;break;}
            if(count) parser.feed(received,size_t(count),authority,token);else endpoint.idle();
        }
        if(parser.status==SettingsHttp::Status::Ready) {
            // Observe surplus already available, including a pipelined request.
            // Bytes never received after a framed request is closed cannot be detected.
            const int extra=endpoint.receive(received,1);
            if(extra>0) parser.status=SettingsHttp::Status::BadRequest;
        }
        bool wantFinish=false;
        if(session.requestLive(began)) {
            unsigned code=200;const char* message="Edit all settings, then Save. No live credential test is performed.";
            DeviceSettings::Field field=DeviceSettings::Field::None;
            if(parser.status!=SettingsHttp::Status::Ready) {
                code=parser.status==SettingsHttp::Status::Forbidden?403:
                    parser.status==SettingsHttp::Status::TooLarge?413:
                    parser.status==SettingsHttp::Status::NotFound?404:
                    parser.status==SettingsHttp::Status::MethodNotAllowed?405:400;
                message="Request rejected. Nothing was saved.";
            } else if(parser.route==SettingsHttp::Route::Save) {
                session.activity=session.last; // intentional well-formed, token-checked submission only
                if(usable) {
                    report.save=settings.saveEdit(parser.form.view,parser.form.passwordEdit(),parser.form.keyEdit());
                    field=report.save.field;
                    if(report.save.status==DeviceSettings::Status::Ok) {++report.saves;message="Settings saved and verified. Secret entry fields are empty. You may Finish setup.";}
                    else if(report.save.status==DeviceSettings::Status::Unchanged) {++report.unchanged;message="Settings unchanged; no flash write. You may Finish setup.";}
                    else if(report.save.status==DeviceSettings::Status::InvalidSettings || report.save.status==DeviceSettings::Status::InvalidArgument) {code=422;message="Settings rejected; previous settings preserved.";}
                    else {
                        // A failed write/commit may have persisted a whole new record.
                        // Never claim success; reload determines current accepted state.
                        report.reloaded=settings.load();
                        usable=report.reloaded.status==DeviceSettings::Status::Ok || report.reloaded.status==DeviceSettings::Status::Missing;
                        code=503;message=report.reloaded.status==DeviceSettings::Status::Ok?
                            "Save failed; flash state was uncertain. Reload found a valid stored record; the form shows that record. This is not a confirmed successful save.":
                            report.reloaded.status==DeviceSettings::Status::Missing?
                            "Save failed; reload found no saved configuration.":"Save failed and stored state is unavailable. Close this session; no further edits are accepted.";
                    }
                } else {code=503;message="Storage unavailable; no changes accepted.";}
            } else if(parser.route==SettingsHttp::Route::Extend) {session.activity=session.last;message="Idle session extended, subject to the absolute limit.";}
            else if(parser.route==SettingsHttp::Route::Finish) {
                report.reloaded=settings.load();
                if(report.reloaded.status==DeviceSettings::Status::Ok) {wantFinish=true;message="Settings verified. Finish accepted; restart follows only if shutdown is verified.";}
                else {code=503;usable=report.reloaded.status==DeviceSettings::Status::Missing;message="Finish refused: no currently verified saved configuration. Save complete settings first.";}
            }
            Writer<Endpoint> out(endpoint,session,began);headers(out,code);
            // Never echo submitted fields, including rejected password/key text.
            parser.resetForm();const bool accepted=settings.presentation(parser.form.view);
            if(parser.status!=SettingsHttp::Status::Ready) {out.text("<!doctype html><title>EE02 request rejected</title><p>");out.text(message);out.text("</p>");}
            else if(wantFinish) {out.text("<!doctype html><title>EE02 setup complete</title><p>");out.text(message);out.text("</p>");}
            else SettingsHttp::page(out,parser.form.view,token,
                session.last-session.activity<IdleMs?(IdleMs-(session.last-session.activity))/1000:0,
                session.last-session.start<AbsoluteMs?(AbsoluteMs-(session.last-session.start))/1000:0,
                accepted,usable,message,field);
            const bool sent=out.flush();
            if(wantFinish && sent) finished=true;
        }
        // Revoke every request/secret alias before the next connection or exit.
        work.request.reset();
        if(!endpoint.closeClient()) {clean=false;break;}
        if(finished) break;
        endpoint.idle();
    }
    report.status=finished?Status::Finished:session.status;
    report.shutdownVerified=endpoint.stop(mode) && clean;
    if(!report.shutdownVerified) report.status=Status::ShutdownFailure;
    memset(token,0,sizeof(token));memset(entropy,0,sizeof(entropy));
    return report;
}
} // namespace SettingsPortal
#endif
