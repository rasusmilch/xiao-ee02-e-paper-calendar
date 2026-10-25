#ifndef EE02_DEVICE_SETTINGS_H
#define EE02_DEVICE_SETTINGS_H
#include "ics_https_source.h"
#include "weather_request.h"

// V1 implementation bounds, bytes excluding NUL; not measured device headroom.
// Owned values only. No events, observations, CA material or routine state.
namespace DeviceSettings {
static const size_t SsidBytes=32, PasswordBytes=64, TimezoneBytes=127, NtpBytes=253,
    LocationBytes=127, ApiKeyBytes=128, LabelBytes=64, UrlBytes=512,
    MaxSources=8, MaxEncoded=5632;
static_assert(38+SsidBytes+PasswordBytes+TimezoneBytes+NtpBytes+LocationBytes+ApiKeyBytes+
              MaxSources*(5+LabelBytes+UrlBytes)<=MaxEncoded,"V1 worst-case record fits");
template<size_t N> struct Text {
    static_assert(N<=UINT16_MAX,"counted settings field fits u16");
    uint16_t size; char data[N+1];
};
struct Calendar { bool enabled; Text<LabelBytes> label; Text<UrlBytes> url; };
struct Values {
    Text<SsidBytes> ssid; Text<PasswordBytes> password;
    Text<TimezoneBytes> timezone; Text<NtpBytes> ntp;
    bool weatherEnabled; Text<LocationBytes> location; Text<ApiKeyBytes> apiKey;
    uint64_t refreshSeconds; uint8_t sourceCount; Calendar sources[MaxSources];
};
enum class Status { Ok, Unchanged, Missing, InvalidArgument, InvalidSettings,
    Corrupt, UnsupportedVersion, NotInitialized, OpenFailure, ReadFailure,
    WriteFailure, CommitFailure, InsufficientStorage, VerifyFailure, ClearFailure };
enum class Field { None, Ssid, Password, Timezone, Ntp, Weather, Refresh, Sources, Label, Url, SecretEdit };
struct Result { Status status; Field field; int32_t storageError; };
inline Result result(Status s,Field f=Field::None,int32_t e=0) { return {s,f,e}; }
// Caller supplies actually readable spans; arithmetic checks cannot prove memory ownership.
template<size_t N> inline bool assign(Text<N>& out,const char* p,size_t n) {
    if(n>N || (!p && n) || (p && n>UINTPTR_MAX-reinterpret_cast<uintptr_t>(p))) return false;
    if(n && memchr(p,0,n)) return false;
    Text<N> temp={}; temp.size=uint16_t(n); if(n) memcpy(temp.data,p,n); out=temp; return true;
}
template<size_t N> inline bool valid(const Text<N>& v,bool required=false) {
    return v.size<=N && (!required || v.size) && !v.data[v.size] && !memchr(v.data,0,v.size);
}
template<size_t N> inline bool text(const Text<N>& v,bool required=false) {
    return valid(v,required) && IcsTypedValues::parsed(IcsTypedValues::detail::utf8(
        {reinterpret_cast<const uint8_t*>(v.data),v.size}));
}
inline bool interval(uint64_t seconds,uint64_t& micros) {
    if(!seconds || seconds>uint64_t(INT64_MAX)/1000000) return false;
    micros=seconds*1000000; return true;
}
inline Result validate(const Values& v) {
    if(!text(v.ssid,true)) return result(Status::InvalidSettings,Field::Ssid);
    if(!valid(v.password)) return result(Status::InvalidSettings,Field::Password);
    const size_t p=v.password.size;
    if(p && p<8) return result(Status::InvalidSettings,Field::Password);
    for(size_t i=0;i<p;++i) {
        const uint8_t c=uint8_t(v.password.data[i]);
        if(c<32 || c>126 || (p==64 && !IcsHttpsSource::detail::hex(c)))
            return result(Status::InvalidSettings,Field::Password);
    }
    if(!text(v.timezone,true)) return result(Status::InvalidSettings,Field::Timezone);
    if(!valid(v.ntp,true)) return result(Status::InvalidSettings,Field::Ntp);
    // Conservative DNS labels; no second calendar URL parser or TZ grammar.
    size_t begin=0;
    for(size_t i=0;i<=v.ntp.size;++i) {
        if(i==v.ntp.size || v.ntp.data[i]=='.') {
            if(i==begin || i-begin>63 || v.ntp.data[begin]=='-' || v.ntp.data[i-1]=='-')
                return result(Status::InvalidSettings,Field::Ntp);
            begin=i+1;
        } else if(!IcsHttpsSource::detail::alpha(uint8_t(v.ntp.data[i])) &&
                  !IcsHttpsSource::detail::digit(uint8_t(v.ntp.data[i])) && v.ntp.data[i]!='-')
            return result(Status::InvalidSettings,Field::Ntp);
    }
    uint64_t us=0; if(!interval(v.refreshSeconds,us)) return result(Status::InvalidSettings,Field::Refresh);
    if(!text(v.location) || !valid(v.apiKey)) return result(Status::InvalidSettings,Field::Weather);
    if(v.weatherEnabled) {
        char url[256]; const WeatherHttps::Request request={v.location.data,v.location.size,v.apiKey.data,v.apiKey.size};
        if(!WeatherHttps::makeUrl(request,"forecast","&cnt=40",url)) return result(Status::InvalidSettings,Field::Weather);
    }
    if(v.sourceCount>MaxSources) return result(Status::InvalidSettings,Field::Sources);
    for(size_t i=0;i<v.sourceCount;++i) {
        const Calendar& c=v.sources[i];
        if(!text(c.label,true)) return result(Status::InvalidSettings,Field::Label);
        if(!valid(c.url)) return result(Status::InvalidSettings,Field::Url);
        // Empty disabled slots remain editable. Nonempty URLs use the exact acquisition admission parser.
        if(c.enabled || c.url.size) {
            IcsHttpsSource::Limits limits={}; limits.maxUrlBytes=UrlBytes; limits.maxHostBytes=NtpBytes; limits.maxTargetBytes=UrlBytes;
            IcsHttpsSource::detail::Url parsed={};
            if(IcsHttpsSource::detail::url({reinterpret_cast<const uint8_t*>(c.url.data),c.url.size},limits,parsed).status!=IcsHttpsSource::Status::Published)
                return result(Status::InvalidSettings,Field::Url);
        }
    }
    return result(Status::Ok);
}
// Separate presentation model: no secret field, raw record or internal Values alias.
// URLs deliberately remain complete, including bearer-equivalent queries.
struct Presentation {
    Text<SsidBytes> ssid; bool hasPassword; Text<TimezoneBytes> timezone; Text<NtpBytes> ntp;
    bool weatherEnabled; Text<LocationBytes> location; bool hasApiKey;
    uint64_t refreshSeconds; uint8_t sourceCount; Calendar sources[MaxSources];
};
inline Presentation present(const Values& v) {
    Presentation p={}; p.ssid=v.ssid; p.hasPassword=v.password.size!=0;
    p.timezone=v.timezone; p.ntp=v.ntp; p.weatherEnabled=v.weatherEnabled;
    p.location=v.location; p.hasApiKey=v.apiKey.size!=0; p.refreshSeconds=v.refreshSeconds;
    p.sourceCount=v.sourceCount<=MaxSources?v.sourceCount:0;
    for(size_t i=0;i<p.sourceCount;++i) p.sources[i]=v.sources[i];
    return p;
}
enum class SecretAction { Keep, Replace, Clear };
struct SecretEdit { SecretAction action; const char* data; size_t size; };
template<size_t N> inline bool edit(Text<N>& out,const Text<N>& old,const SecretEdit& e) {
    if(e.action==SecretAction::Keep || e.action==SecretAction::Clear) {
        if(e.data || e.size) return false; // A mask/empty form field is never an implicit replacement.
        out=e.action==SecretAction::Keep?old:Text<N>{}; return true;
    }
    if(e.action!=SecretAction::Replace || !e.data || !e.size || e.size>N ||
       e.size>UINTPTR_MAX-reinterpret_cast<uintptr_t>(e.data) ||
       (e.size==8 && !memcmp(e.data,"********",8))) return false;
    return assign(out,e.data,e.size);
}
// Draft contains only public values plus explicit transient secret edits.
inline Result candidate(const Presentation& p,const Values* previous,const SecretEdit& password,
                        const SecretEdit& key,Values& out) {
    if(p.sourceCount>MaxSources) return result(Status::InvalidSettings,Field::Sources);
    Values v={}; v.ssid=p.ssid; v.timezone=p.timezone; v.ntp=p.ntp;
    v.weatherEnabled=p.weatherEnabled; v.location=p.location; v.refreshSeconds=p.refreshSeconds;
    v.sourceCount=p.sourceCount; for(size_t i=0;i<p.sourceCount;++i) v.sources[i]=p.sources[i];
    const Text<PasswordBytes> noPassword={}; const Text<ApiKeyBytes> noKey={};
    if(!edit(v.password,previous?previous->password:noPassword,password) ||
       !edit(v.apiKey,previous?previous->apiKey:noKey,key))
        return result(Status::InvalidSettings,Field::SecretEdit);
    const Result r=validate(v); if(r.status==Status::Ok) out=v; return r;
}
} // namespace DeviceSettings
#endif
