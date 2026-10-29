#ifndef EE02_SETTINGS_HTTP_H
#define EE02_SETTINGS_HTTP_H
#include "device_settings_nvs.h"
#include <cstdio>
#include <new>

// Only the settings session's five routes. No general server, body String, or
// retained request catalogue. All spans passed to feed must actually be readable.
namespace SettingsHttp {
static const size_t RequestLineBytes=128, UriBytes=32, HeaderLineBytes=1024,
    HeaderBytes=4096, HeaderCount=24, BodyBytes=24576, FieldCount=12+3*DeviceSettings::MaxSources,
    FieldNameBytes=32, FieldValueBytes=DeviceSettings::UrlBytes, ResponseBytes=65536;
static_assert(BodyBytes>=3*DeviceSettings::MaxEncoded+2048,"percent-encoded V1 plus field names fits intake");
static_assert(DeviceSettings::MaxSources==8 && FieldCount<=64,"V1 form field mask/count");
enum class Status { Reading, Ready, BadRequest, TooLarge, Forbidden, NotFound, MethodNotAllowed };
enum class Route { Page, Save, Extend, Finish };
inline bool equal(const char* a,const char* b) {
    while(*a && *b) {
        unsigned char x=static_cast<unsigned char>(*a++), y=static_cast<unsigned char>(*b++);
        if(x>='A' && x<='Z') x=static_cast<unsigned char>(x+32);
        if(y>='A' && y<='Z') y=static_cast<unsigned char>(y+32);
        if(x!=y) return false;
    }
    return !*a && !*b;
}
inline bool decimal(const char* s,size_t n,uint64_t maximum,uint64_t& out) {
    if(!n) return false;
    uint64_t v=0;
    for(size_t i=0;i<n;++i) { const unsigned char c=static_cast<unsigned char>(s[i]);
        if(c<'0' || c>'9' || uint64_t(c-'0')>maximum || v>(maximum-uint64_t(c-'0'))/10) return false;
        v=v*10+uint64_t(c-'0');
    }
    out=v; return true;
}
struct Form {
    DeviceSettings::Presentation view={};
    DeviceSettings::Text<DeviceSettings::PasswordBytes> password={};
    DeviceSettings::Text<DeviceSettings::ApiKeyBytes> key={};
    DeviceSettings::SecretAction passwordAction=DeviceSettings::SecretAction::Keep,
        keyAction=DeviceSettings::SecretAction::Keep;
    char token[33]={}; uint64_t seen=0; size_t fields=0;
    char name[FieldNameBytes+1]={}, value[FieldValueBytes+1]={};
    size_t nameSize=0,valueSize=0; bool inValue=false; unsigned escape=0,high=0;
    static bool secret(const char* p,DeviceSettings::SecretAction& out) {
        if(!strcmp(p,"keep")) out=DeviceSettings::SecretAction::Keep;
        else if(!strcmp(p,"replace")) out=DeviceSettings::SecretAction::Replace;
        else if(!strcmp(p,"clear")) out=DeviceSettings::SecretAction::Clear;
        else return false;
        return true;
    }
    bool field() {
        if(!inValue || !nameSize || escape || ++fields>FieldCount) return false;
        name[nameSize]=0; value[valueSize]=0;
        const char* const names[]={"csrf","ssid","timezone","ntp","refresh","count","weather_enabled",
            "location","password_action","password","key_action","key"};
        size_t id=FieldCount;
        for(size_t i=0;i<12;++i) if(!strcmp(name,names[i])) id=i;
        if(nameSize==10 && !memcmp(name,"calendar",8) && name[8]>='0' && name[8]<='7') {
            const char part=name[9];
            if(part=='e' || part=='n' || part=='u') id=12+size_t(name[8]-'0')*3+(part=='e'?0:part=='n'?1:2);
        }
        if(id>=FieldCount || (seen&(uint64_t(1)<<id))) return false;
        seen|=uint64_t(1)<<id; bool ok=false; uint64_t number=0;
        switch(id) {
        case 0: ok=valueSize==32; if(ok) memcpy(token,value,33); break;
        case 1: ok=DeviceSettings::assign(view.ssid,value,valueSize); break;
        case 2: ok=DeviceSettings::assign(view.timezone,value,valueSize); break;
        case 3: ok=DeviceSettings::assign(view.ntp,value,valueSize); break;
        case 4: ok=decimal(value,valueSize,UINT64_MAX,number); view.refreshSeconds=number; break;
        case 5: ok=decimal(value,valueSize,DeviceSettings::MaxSources,number); view.sourceCount=uint8_t(number); break;
        case 6: ok=!strcmp(value,"1"); view.weatherEnabled=ok; break;
        case 7: ok=DeviceSettings::assign(view.location,value,valueSize); break;
        case 8: ok=secret(value,passwordAction); break;
        case 9: ok=DeviceSettings::assign(password,value,valueSize); break;
        case 10: ok=secret(value,keyAction); break;
        case 11: ok=DeviceSettings::assign(key,value,valueSize); break;
        default: {
            auto& c=view.sources[(id-12)/3];
            if((id-12)%3==0) { ok=!strcmp(value,"1"); c.enabled=ok; }
            else if((id-12)%3==1) ok=DeviceSettings::assign(c.label,value,valueSize);
            else ok=DeviceSettings::assign(c.url,value,valueSize);
        }}
        nameSize=valueSize=0; inValue=false; return ok;
    }
    bool byte(uint8_t c) {
        const bool encoded=escape!=0;
        if(!escape && c=='&') return field();
        if(!escape && c=='=' && !inValue) { inValue=true; return true; }
        if(!escape && c=='%') { escape=1; return true; }
        if(escape) {
            if(!IcsHttpsSource::detail::hex(c)) return false;
            const unsigned digit=c<='9'?unsigned(c-'0'):unsigned((c|32)-'a'+10);
            if(escape==1) { high=digit; escape=2; return true; }
            c=uint8_t(high*16+digit); escape=0;
        } else if(c=='+') c=' ';
        if(!c || (!encoded && (c<32 || c==127))) return false;
        if(inValue) { if(valueSize==FieldValueBytes) return false; value[valueSize++]=char(c); }
        else { if(nameSize==FieldNameBytes || c>=127) return false; name[nameSize++]=char(c); }
        return true;
    }
    bool finish(Route route,const char* expectedToken) {
        if(!field()) return false;
        unsigned different=0; for(size_t i=0;i<32;++i) different|=unsigned(uint8_t(token[i])^uint8_t(expectedToken[i]));
        if(different || !(seen&1)) return false;
        if(route!=Route::Save) return seen==1;
        const uint64_t required=0xfffU & ~(uint64_t(1)<<6); // checkbox absent means false
        if((seen&required)!=required) return false;
        for(size_t i=0;i<view.sourceCount;++i) {
            const uint64_t pair=(uint64_t(6)<<(12+i*3)); // label and URL fields, even if disabled
            if((seen&pair)!=pair) return false;
        }
        // Count explicitly removes rows above the configured range, including any
        // previously populated fields still submitted by a non-JS browser.
        for(size_t i=view.sourceCount;i<DeviceSettings::MaxSources;++i) view.sources[i]={};
        return true;
    }
    DeviceSettings::SecretEdit passwordEdit() const { return {passwordAction,password.size?password.data:nullptr,password.size}; }
    DeviceSettings::SecretEdit keyEdit() const { return {keyAction,key.size?key.data:nullptr,key.size}; }
};
class Parser {
public:
    Form form;
    Status status=Status::Reading; Route route=Route::Page;
    void reset() { this->~Parser(); new(this) Parser; }
    void resetForm() { form.~Form(); new(&form) Form; }
    bool feed(const uint8_t* bytes,size_t count,const char* authority,const char* token) {
        if((!bytes && count) || (bytes && count>UINTPTR_MAX-reinterpret_cast<uintptr_t>(bytes))) { status=Status::BadRequest; return false; }
        for(size_t i=0;i<count;++i) {
            if(status!=Status::Reading) { if(status==Status::Ready) status=Status::BadRequest; return false; }
            const uint8_t c=bytes[i];
            if(body_) {
                if(!form.byte(c)) { status=Status::BadRequest; return false; }
                if(++received_==length_) status=form.finish(route,token)?Status::Ready:Status::BadRequest;
            } else {
                if(++headers_>HeaderBytes) { status=Status::TooLarge; return false; }
                if(cr_) {
                    if(c!='\n') { status=Status::BadRequest; return false; }
                    line_[used_]=0; cr_=false; parseLine(authority); used_=0;
                } else if(c=='\r') cr_=true;
                else if(c<32 || c>=127 || used_==(first_?RequestLineBytes:HeaderLineBytes)) { status=Status::BadRequest; return false; }
                else line_[used_++]=char(c);
            }
        }
        return status==Status::Reading || status==Status::Ready;
    }
    void eof() { if(status==Status::Reading) status=Status::BadRequest; }
private:
    char line_[HeaderLineBytes+1]={}; size_t used_=0,headers_=0,fields_=0,length_=0,received_=0;
    bool first_=true,cr_=false,body_=false,post_=false,host_=false,origin_=false,type_=false,cl_=false;
    static bool hostValue(const char* s,const char* authority) {
        if(!strcmp(s,authority)) return true;
        const size_t n=strlen(authority); return strlen(s)==n+3 && !memcmp(s,authority,n) && !strcmp(s+n,":80");
    }
    void parseLine(const char* authority) {
        if(first_) {
            first_=false; char* a=strchr(line_,' '); if(!a) { status=Status::BadRequest; return; }
            *a++=0; char* b=strchr(a,' '); if(!b || strcmp(b," HTTP/1.1")) { status=Status::BadRequest; return; } *b=0;
            if(strlen(a)>UriBytes) { status=Status::TooLarge; return; }
            post_=!strcmp(line_,"POST");
            if(!post_ && strcmp(line_,"GET")) { status=Status::MethodNotAllowed; return; }
            if(!strcmp(a,"/") || !strcmp(a,"/settings")) route=post_?Route::Save:Route::Page;
            else if(!strcmp(a,"/session/extend")) route=Route::Extend;
            else if(!strcmp(a,"/session/finish")) route=Route::Finish;
            else { status=Status::NotFound; return; }
            if((!strcmp(a,"/") && post_) || (!post_ && route!=Route::Page)) status=Status::MethodNotAllowed;
            return;
        }
        if(!used_) {
            if(!host_ || (post_ && (!cl_ || !type_ || !length_)) || (!post_ && length_)) { status=Status::BadRequest; return; }
            if(post_) body_=true; else status=Status::Ready;
            return;
        }
        if(++fields_>HeaderCount) { status=Status::TooLarge; return; }
        char* colon=strchr(line_,':'); if(!colon || colon==line_) { status=Status::BadRequest; return; }
        for(char* p=line_;p<colon;++p) if(!IcsHttpsSource::detail::alpha(uint8_t(*p)) && !IcsHttpsSource::detail::digit(uint8_t(*p)) && *p!='-') { status=Status::BadRequest; return; }
        *colon++=0; while(*colon==' ') ++colon;
        size_t n=strlen(colon); while(n && colon[n-1]==' ') colon[--n]=0;
        if(equal(line_,"Host")) { if(host_ || !hostValue(colon,authority)) status=Status::Forbidden; host_=true; }
        else if(equal(line_,"Origin")) {
            if(origin_ || strncmp(colon,"http://",7) || !hostValue(colon+7,authority)) status=Status::Forbidden;
            origin_=true;
        } else if(equal(line_,"Content-Length")) {
            uint64_t amount=0;
            if(cl_ || !decimal(colon,n,UINT64_MAX,amount)) status=Status::BadRequest;
            else if(amount>BodyBytes) status=Status::TooLarge;
            else length_=size_t(amount);
            cl_=true;
        } else if(equal(line_,"Content-Type")) {
            if(type_ || (!equal(colon,"application/x-www-form-urlencoded") && !equal(colon,"application/x-www-form-urlencoded; charset=UTF-8"))) status=Status::BadRequest;
            type_=true;
        } else if(equal(line_,"Transfer-Encoding") || equal(line_,"Content-Encoding") || equal(line_,"Expect") || equal(line_,"Upgrade")) status=Status::BadRequest;
        else if(equal(line_,"Sec-Fetch-Site") && equal(colon,"cross-site")) status=Status::Forbidden;
    }
};
inline const char* fieldName(DeviceSettings::Field field) {
    switch(field) {
    case DeviceSettings::Field::Ssid:return "Wi-Fi SSID";
    case DeviceSettings::Field::Password:return "Wi-Fi password";
    case DeviceSettings::Field::Timezone:return "POSIX timezone";
    case DeviceSettings::Field::Ntp:return "NTP hostname";
    case DeviceSettings::Field::Weather:return "Weather location/key";
    case DeviceSettings::Field::Refresh:return "Refresh interval";
    case DeviceSettings::Field::Sources:return "Calendar count";
    case DeviceSettings::Field::Label:return "Calendar display name";
    case DeviceSettings::Field::Url:return "Calendar HTTPS URL";
    case DeviceSettings::Field::SecretEdit:return "Secret edit action/value";
    default:return "Settings/storage";
    }
}
// A sink supports text(), escaped() and number(). It must enforce its own write
// deadline and total byte budget. No dynamic value is inserted as raw markup.
template<class Sink> inline void hidden(Sink& out,const char* token) {
    out.text("<input type=\"hidden\" name=\"csrf\" value=\""); out.escaped(token,32); out.text("\">");
}
template<class Sink> inline void input(Sink& out,const char* label,const char* name,const char* data,size_t n,size_t max) {
    out.text("<p><label>"); out.text(label); out.text(" <input name=\""); out.text(name);
    out.text("\" maxlength=\"");out.number(max);out.text("\" value=\"");out.escaped(data,n);out.text("\"></label></p>");
}
template<class Sink> inline void secretInput(Sink& out,const char* label,const char* action,const char* name,bool saved,size_t max) {
    out.text("<p>");out.text(label);out.text(saved?" (saved value exists)":" (no saved value)");
    out.text(" <select name=\"");out.text(action);out.text("\"><option value=\"keep\">Keep existing value</option><option value=\"replace\">Replace</option><option value=\"clear\">Clear</option></select>");
    out.text(" New value <input type=\"password\" autocomplete=\"new-password\" name=\"");out.text(name);
    out.text("\" maxlength=\"");out.number(max);out.text("\" value=\"\"></p>");
}
template<class Sink> inline void page(Sink& out,const DeviceSettings::Presentation& p,const char* token,
    uint64_t idleSeconds,uint64_t absoluteSeconds,bool accepted,bool storageUsable,const char* message,DeviceSettings::Field field) {
    out.text("<!doctype html><html lang=\"en\"><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width\"><title>EE02 settings</title><h1>EE02 settings</h1>");
    out.text("<p>This setup service is open and has no login. Anyone with access can read full calendar URLs, including access tokens, and attempt changes. HTTP is unencrypted: newly entered secrets can be observed on the network. Use a trusted local environment.</p>");
    out.text("<p>Idle time remaining: ");out.number(idleSeconds);out.text(" seconds. Absolute time remaining: ");out.number(absoluteSeconds);out.text(" seconds. Reloading this page does not extend time. Extend opens a confirmation tab so this form can retain unsaved edits.</p>");
    out.text("<p>");out.text(message);out.text("</p>");
    if(field!=DeviceSettings::Field::None) {out.text("<p>Check field: ");out.text(fieldName(field));out.text(".</p>");}
    if(storageUsable) {
        out.text("<p>Saved passwords and API keys are never returned. Choose Keep, Replace or Clear explicitly; leave new-value fields empty for Keep/Clear.</p>");
        out.text("<form method=\"post\" action=\"/settings\">");hidden(out,token);
        input(out,"Wi-Fi SSID","ssid",p.ssid.data,p.ssid.size,DeviceSettings::SsidBytes);
        secretInput(out,"Wi-Fi password","password_action","password",p.hasPassword,DeviceSettings::PasswordBytes);
        input(out,"POSIX timezone","timezone",p.timezone.data,p.timezone.size,DeviceSettings::TimezoneBytes);
        input(out,"NTP hostname","ntp",p.ntp.data,p.ntp.size,DeviceSettings::NtpBytes);
        out.text("<p><label>Refresh seconds <input type=\"number\" min=\"1\" name=\"refresh\" value=\"");
        if(p.refreshSeconds) out.number(p.refreshSeconds);
        out.text("\"></label></p>");
        out.text("<p><label>Configured calendar count (0-8) <input type=\"number\" min=\"0\" max=\"8\" name=\"count\" value=\"");out.number(p.sourceCount);out.text("\"></label></p><p>Only the first count rows are saved. Disabled rows may have blank URLs. Names are required for configured rows.</p>");
        for(size_t i=0;i<DeviceSettings::MaxSources;++i) {
            const auto& c=p.sources[i]; char enabled[11]="calendar0e",name[11]="calendar0n",url[11]="calendar0u";
            enabled[8]=name[8]=url[8]=char('0'+i);
            out.text("<fieldset><legend>Calendar ");out.number(i+1);out.text("</legend><label>Enabled <input type=\"checkbox\" name=\"");out.text(enabled);out.text("\" value=\"1\"");if(c.enabled)out.text(" checked");out.text("></label>");
            input(out,"Display name",name,c.label.data,c.label.size,DeviceSettings::LabelBytes);
            input(out,"Full HTTPS ICS URL",url,c.url.data,c.url.size,DeviceSettings::UrlBytes);out.text("</fieldset>");
        }
        out.text("<p><label>Weather enabled <input type=\"checkbox\" name=\"weather_enabled\" value=\"1\"");if(p.weatherEnabled)out.text(" checked");out.text("></label></p>");
        input(out,"Weather location","location",p.location.data,p.location.size,DeviceSettings::LocationBytes);
        secretInput(out,"Weather API key","key_action","key",p.hasApiKey,DeviceSettings::ApiKeyBytes);
        out.text("<p><button type=\"submit\">Save settings</button></p></form>");
    }
    out.text("<form method=\"post\" action=\"/session/extend\" target=\"_blank\" rel=\"noopener\">");hidden(out,token);out.text("<button>Extend setup session</button></form>");
    if(accepted) {out.text("<form method=\"post\" action=\"/session/finish\">");hidden(out,token);out.text("<button>Finish setup</button></form>");}
    out.text("<p>Save does not test Wi-Fi credentials or HTTPS trust. Finish closes setup; after verified shutdown, first-boot setup restarts into the normal application. If you do not finish, timeout still applies.</p></html>");
}
} // namespace SettingsHttp
#endif
