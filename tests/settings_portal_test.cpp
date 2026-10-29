// Actual bounded parser/page/session + actual V1 service. Only socket/clock/NVS
// operations are scripted. No physical AP, browser, cryptography or flash proof.
#include "../application/settings_portal.h"
#include "device_settings_fixture.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
using namespace SettingsPortal;
namespace {
const std::string token="4142434445464748494a4b4c4d4e4f50";
size_t cases=0;
std::string encode(const char* p,size_t n) {
 const char hex[]="0123456789ABCDEF";std::string out;
 for(size_t i=0;i<n;++i){const auto c=uint8_t(p[i]);if(WeatherHttps::unreserved(c))out+=char(c);else {out+='%';out+=hex[c>>4];out+=hex[c&15];}}
 return out;
}
std::string pair(const std::string& name,const std::string& value){return name+"="+encode(value.data(),value.size());}
std::string form(const DeviceSettings::Values& v) {
 std::string body="csrf="+token;
 const auto add=[&](const std::string& n,const std::string& value){body+='&'+pair(n,value);};
 add("ssid",v.ssid.data);add("timezone",v.timezone.data);add("ntp",v.ntp.data);add("refresh",std::to_string(v.refreshSeconds));
 add("count",std::to_string(v.sourceCount));add("location",v.location.data);
 add("password_action",v.password.size?"replace":"keep");add("password",v.password.data);
 add("key_action",v.apiKey.size?"replace":"keep");add("key",v.apiKey.data);
 if(v.weatherEnabled)add("weather_enabled","1");
 for(size_t i=0;i<v.sourceCount;++i){const auto& c=v.sources[i];const std::string prefix="calendar"+std::to_string(i);
  add(prefix+"n",c.label.data);add(prefix+"u",c.url.data);if(c.enabled)add(prefix+"e","1");}
 return body;
}
std::string request(const char* route,const std::string& body) {
 return std::string("POST ")+route+" HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
}
std::string get="GET /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n";
struct Guard {uint8_t before[16];Work work;uint8_t after[16];Guard(){memset(before,0xd1,16);memset(after,0xd2,16);}void check(){for(size_t i=0;i<16;++i)assert(before[i]==0xd1 && after[i]==0xd2);}} guard;
SettingsHttp::Status parse(const std::string& wire,size_t chunk=1,bool eof=true) {
 guard.work.request.reset();for(size_t i=0;i<wire.size();i+=chunk) {
  guard.work.request.feed(reinterpret_cast<const uint8_t*>(wire.data()+i),std::min(chunk,wire.size()-i),"192.168.4.1",token.c_str());
  if(guard.work.request.status!=SettingsHttp::Status::Reading && guard.work.request.status!=SettingsHttp::Status::Ready)break;
 }
 if(eof) guard.work.request.eof();
 guard.check();++cases;return guard.work.request.status;
}
struct Exchange {std::string request,response;size_t at,chunk;uint64_t readDelay;Exchange(std::string q,std::string r="",size_t a=0,size_t c=128,uint64_t d=0):request(q),response(r),at(a),chunk(c),readDelay(d){}};
struct Endpoint {
 std::vector<Exchange> exchanges;size_t index=0,started=0,stopped=0,closed=0;
 bool startOk=true,closeOk=true,stopOk=true,live=false,startedAp=false,stationOwned=false,writeFail=false,writeStall=false;size_t writeBlocks=0;
 uint64_t now=0,idleStep=1000;bool regress=false,invalidClock=false;uint64_t advanceAfterClose=0;
 bool start(Mode m){++started;live=true;startedAp=m==Mode::FirstBootAp;stationOwned=m==Mode::AwakeLan;return startOk;}
 bool accept(){return index<exchanges.size();}
 int receive(uint8_t* p,size_t n){auto& e=exchanges[index];now+=e.readDelay;
  if(e.at==e.request.size())return 0;
  n=std::min(n,std::min(e.chunk,e.request.size()-e.at));memcpy(p,e.request.data()+e.at,n);e.at+=n;return int(n);}
 int send(const uint8_t* p,size_t n){if(writeFail)return -1;if(writeStall)return 0;if(writeBlocks){--writeBlocks;return 0;}n=std::min<size_t>(n,71);exchanges[index].response.append(reinterpret_cast<const char*>(p),n);++now;return int(n);}
 void idle(){now+=idleStep;}
 uint8_t nonceStart='A';
 void random16(uint8_t (&out)[16]){for(size_t i=0;i<16;++i)out[i]=uint8_t(nonceStart+i);}
 bool closeClient(){++closed;++index;now+=advanceAfterClose;return closeOk;}
 bool stop(Mode){++stopped;live=false;return stopOk;}
 static uint64_t clock(void* p){auto& e=*static_cast<Endpoint*>(p);if(e.invalidClock)return UINT64_MAX;if(e.regress && e.now>100)return 0;return e.now;}
};
Report runCase(DeviceSettings::Service& s,Endpoint& e,Mode mode=Mode::FirstBootAp){auto r=run(s,guard.work,mode,"192.168.4.1",e,Endpoint::clock,&e);guard.check();++cases;assert(!NvsHost::state().opened);return r;}
void reset(){NvsHost::state()={};guard.work.request.reset();}
bool contains(const std::string& s,const char* p){return s.find(p)!=std::string::npos;}
void noWrite(){assert(!NvsHost::state().sets && !NvsHost::state().commits && !NvsHost::state().erases);}
}
int main(){
 using SettingsHttp::Status;
 auto v=SettingsFixture::complete();const std::string good=request("/settings",form(v));
 assert(parse(get)==Status::Ready);assert(parse(good)==Status::Ready);
 for(size_t split=1;split<=good.size();split+=7)assert(parse(good,split)==Status::Ready);
 std::vector<std::string> bad={"", "GET /settings HTTP/1.0\r\n\r\n","PUT /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",
  "GET /favicon.ico HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",get+"x",get+get,
  "GET /settings?csrf=bad HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",
  "GET /session/finish HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",
  "GET / HTTP/1.1\r\nHost: attacker.invalid\r\n\r\n",
  "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://attacker.invalid\r\n\r\n",
  "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: null\r\n\r\n",
  "GET / HTTP/1.1\r\nHost: 192.168.4.1\r\nHost: 192.168.4.1\r\n\r\n",
  "POST /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n",
  "POST /settings HTTP/1.1\r\nHost: 192.168.4.1\r\nContent-Length: 0\r\nContent-Type: application/x-www-form-urlencoded\r\n\r\n",
  "GET /"+std::string(130,'x')+" HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n"};
 for(const std::string& name:std::vector<std::string>{"Content-Length: -1","Content-Length: 18446744073709551616","Content-Length: 24577","Content-Length: 10\r\nContent-Length: 10","Transfer-Encoding: chunked","Content-Type: application/json","Expect: 100-continue","X: "+std::string(1025,'x')})
  bad.push_back("POST /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n"+name+"\r\n\r\n");
 std::string headers="GET /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n";for(size_t i=0;i<25;++i)headers+="X: value\r\n";bad.push_back(headers+"\r\n");
 std::string headerBytes="GET /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n";for(size_t i=0;i<5;++i)headerBytes+="X: "+std::string(900,'x')+"\r\n";bad.push_back(headerBytes+"\r\n");
 for(const std::string& body:std::vector<std::string>{"csrf="+token,"csrf=wrong",form(v)+"&ssid=duplicate",form(v)+"&unknown=value",form(v)+"&x=%GG",form(v)+"&ssid=%00",form(v)+"&ssid=%",form(v)+"&ssid=%1",form(v)+"&ssid="+std::string(513,'x')}) bad.push_back(request("/settings",body));
  std::string wrongToken=good;wrongToken.replace(wrongToken.find(token),32,std::string(32,'f'));bad.push_back(wrongToken);
 std::string wrongOrigin=good;wrongOrigin.replace(wrongOrigin.find("Origin: http://192.168.4.1"),25,"Origin: http://evil.invalid");bad.push_back(wrongOrigin);
 std::string wrongHost=good;wrongHost.replace(wrongHost.find("Host: 192.168.4.1"),17,"Host: evil.invalid");bad.push_back(wrongHost);
 std::string encodedBody=good;encodedBody.insert(encodedBody.find("Host:"),"Content-Encoding: gzip\r\n");bad.push_back(encodedBody);
 bad.push_back(good.substr(0,good.size()-1));bad.push_back(good+"trailing");
 for(const auto& wire:bad){assert(parse(wire)!=Status::Ready);reset();DeviceSettings::Service s;Endpoint e;e.exchanges.push_back({wire,"",0,7,0});runCase(s,e);noWrite();}
 // Exact admitted metadata capacities remain usable.
 std::string exactHeaders=get.substr(0,get.size()-2);
 for(size_t i=0;i<4;++i)exactHeaders+="X: "+std::string(900,'x')+"\r\n";
 const size_t remaining=SettingsHttp::HeaderBytes-exactHeaders.size()-2;
 exactHeaders+="X: "+std::string(remaining-5,'x')+"\r\n\r\n";
 assert(exactHeaders.size()==SettingsHttp::HeaderBytes && parse(exactHeaders)==Status::Ready);
 assert(parse("GET /settings HTTP/1.1\r\nHost: 192.168.4.1:80\r\nX: "+std::string(1021,'x')+"\r\n\r\n")==Status::Ready);
 std::string counts=get.substr(0,get.size()-2);for(size_t i=1;i<SettingsHttp::HeaderCount;++i)counts+="X: value\r\n";
 assert(parse(counts+"\r\n")==Status::Ready);
 assert(parse("GET /"+std::string(33,'x')+" HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n")==Status::TooLarge);
 guard.work.request.reset();assert(guard.work.request.feed(nullptr,0,"192.168.4.1",token.c_str()));
 assert(!guard.work.request.feed(reinterpret_cast<const uint8_t*>(UINTPTR_MAX-1),4,"192.168.4.1",token.c_str()));guard.check();
 // A maximum V1 record fits the form intake and preserves exact escaped URLs.
 auto max=v;max.sourceCount=8;max.weatherEnabled=false;
 for(size_t i=0;i<8;++i){max.sources[i]=v.sources[0];SettingsFixture::set(max.sources[i].label,std::string(64,'L').c_str());
  const std::string url="https://example.invalid/?token="+std::string(512-31,'&');SettingsFixture::set(max.sources[i].url,url.c_str());}
 const auto maxForm=form(max);assert(maxForm.size()<SettingsHttp::BodyBytes);assert(parse(request("/settings",maxForm),1)==Status::Ready);
 // Configured responses expose exactly presentation values, never saved secrets.
 reset();DeviceSettings::Service s;auto html=v;SettingsFixture::set(html.sources[0].label,"<img src=x onerror='bad'> & \"name\"");
 SettingsFixture::set(html.sources[0].url,"https://example.invalid/?token=a&next=%22secret%22");assert(s.save(html).status==DeviceSettings::Status::Ok);
 Endpoint page;page.exchanges.push_back({get,"",0,1,0});auto report=runCase(s,page,Mode::AwakeLan);assert(report.status==SettingsPortal::Status::IdleExpired && report.shutdownVerified && page.stationOwned);
 const auto& response=page.exchanges[0].response;
 assert(!contains(response,html.password.data) && !contains(response,html.apiKey.data));
 for(const char* required:{"Cache-Control: no-store","Referrer-Policy: no-referrer","nosniff","Content-Security-Policy:","form-action 'self'","saved value exists","value=\"\"","&lt;img","&#39;bad&#39;","&amp;next=%22secret%22"})assert(contains(response,required));
 assert(!contains(response,"<img src=x") && !contains(response,"<script") && !contains(response,"Access-Control-Allow-Origin"));
 // Rejected Host/Origin/token requests never disclose the active presentation/token.
 Endpoint hostile;hostile.exchanges={{"GET /settings HTTP/1.1\r\nHost: attacker.invalid\r\n\r\n","",0,128,0},
  {request("/settings","csrf=wrong"),"",0,128,0}};
 runCase(s,hostile,Mode::AwakeLan);
 for(const auto& x:hostile.exchanges)assert(!contains(x.response,"name=\"csrf\"") && !contains(x.response,"token=a"));
 // Maximum form succeeds through actual save and bounded escaped HTML response.
 reset();DeviceSettings::Service maximum;Endpoint big;big.exchanges={{request("/settings",maxForm),"",0,1,0},{get,"",0,128,0}};
 report=runCase(maximum,big);assert(report.saves==1 && big.exchanges[1].response.size()<SettingsHttp::ResponseBytes);
 assert(contains(big.exchanges[1].response,"&amp;&amp;&amp;"));
 // Byte-oriented fields retain valid V1 UTF-8/TAB data instead of imposing a second text policy.
 auto utf=v;SettingsFixture::set(utf.ssid,"Caf\xc3\xa9");SettingsFixture::set(utf.sources[0].label,"A\tB");
 assert(parse(request("/settings",form(utf)))==Status::Ready);
 reset();DeviceSettings::Service unicode;Endpoint text;text.exchanges={{request("/settings",form(utf)),"",0,1,0}};
 report=runCase(unicode,text);assert(report.saves==1 && contains(text.exchanges[0].response,"A&#9;B"));
 // Complete save, explicit Finish, and independent persisted reload.
 reset();DeviceSettings::Service first;Endpoint provision;provision.exchanges={{get,"",0,1,0},{good,"",0,3,0},{request("/session/finish","csrf="+token),"",0,1,0}};
 report=runCase(first,provision);assert(report.status==SettingsPortal::Status::Finished && report.shutdownVerified && report.saves==1 && provision.closed==3);
 assert(NvsHost::state().sets==1 && NvsHost::state().commits==1);DeviceSettings::Service reboot;assert(reboot.load().status==DeviceSettings::Status::Ok && !strcmp(reboot.internal()->ssid.data,v.ssid.data));
 for(const auto& x:provision.exchanges)assert(!contains(x.response,v.password.data) && !contains(x.response,v.apiKey.data));
 assert(!contains(provision.exchanges[0].response,"synthetic-network") && !contains(provision.exchanges[0].response,"one.example.invalid") && contains(provision.exchanges[0].response,"no saved value"));
 // Premature Finish is refused without locking a legitimately Missing form.
 reset();DeviceSettings::Service premature;Endpoint early;early.exchanges={{request("/session/finish","csrf="+token),"",0,128,0},{good,"",0,128,0},{request("/session/finish","csrf="+token),"",0,128,0}};
 report=runCase(premature,early);assert(report.status==SettingsPortal::Status::Finished && report.saves==1 && contains(early.exchanges[0].response,"Finish refused") && contains(early.exchanges[0].response,"Save settings"));
 // Equal submission is write-free; explicit edits preserve full validation.
 auto disk=NvsHost::state();NvsHost::state().sets=NvsHost::state().commits=0;Endpoint unchanged;unchanged.exchanges={{good,"",0,128,0}};
 report=runCase(first,unchanged,Mode::AwakeLan);assert(report.unchanged==1);noWrite();
 for(int which=0;which<5;++which){NvsHost::state()=disk;NvsHost::state().sets=NvsHost::state().commits=0;auto edit=v;
  if(which==0)edit.timezone={};
  if(which==1)SettingsFixture::set(edit.sources[0].url,"http://example.invalid/a");
  if(which==2)SettingsFixture::set(edit.password,"short");
  if(which==3)edit.refreshSeconds=0;
  if(which==4)edit.sources[0].label.data[0]=char(0xff);
  Endpoint rejected;rejected.exchanges={{request("/settings",form(edit)),"",0,64,0}};runCase(first,rejected,Mode::AwakeLan);noWrite();assert(contains(rejected.exchanges[0].response,"Check field:"));}
 // Explicit Keep/Clear/Replace semantics use the real accepted service.
 NvsHost::state()=disk;NvsHost::state().sets=NvsHost::state().commits=0;
 std::string keep=form(v);const auto replaceAll=[](std::string& text,const std::string& from,const std::string& to){const auto at=text.find(from);assert(at!=std::string::npos);text.replace(at,from.size(),to);};
 replaceAll(keep,"password_action=replace","password_action=keep");replaceAll(keep,"password=synthetic-password","password=");
 replaceAll(keep,"key_action=replace","key_action=keep");replaceAll(keep,"key=synthetic-key","key=");
 Endpoint kept;kept.exchanges={{request("/settings",keep),"",0,128,0}};report=runCase(first,kept,Mode::AwakeLan);assert(report.unchanged==1);noWrite();
 std::string ambiguous=keep;replaceAll(ambiguous,"password_action=keep","password_action=clear");replaceAll(ambiguous,"password=&","password=not-empty&");
 Endpoint ambiguousEdit;ambiguousEdit.exchanges={{request("/settings",ambiguous),"",0,128,0}};runCase(first,ambiguousEdit,Mode::AwakeLan);noWrite();
 std::string cleared=keep;replaceAll(cleared,"password_action=keep","password_action=clear");
 Endpoint clearPassword;clearPassword.exchanges={{request("/settings",cleared),"",0,128,0}};report=runCase(first,clearPassword,Mode::AwakeLan);
 assert(report.saves==1 && first.internal()->password.size==0 && first.internal()->apiKey.size);
 std::string masked=keep;replaceAll(masked,"password_action=keep","password_action=replace");replaceAll(masked,"password=&","password=********&");
 const auto writes=NvsHost::state().sets;Endpoint mask;mask.exchanges={{request("/settings",masked),"",0,128,0}};runCase(first,mask,Mode::AwakeLan);assert(NvsHost::state().sets==writes);
 // Failed saves report failure, and reload uncertain persistent state.
 for(int which=0;which<4;++which){reset();DeviceSettings::Service failed;Endpoint e;e.exchanges={{good,"",0,128,0}};
  if(which==0)NvsHost::state().writeError=77;
  if(which==1)NvsHost::state().commitError=77;
  if(which==2){NvsHost::state().commitError=77;NvsHost::state().immediate=true;}
  if(which==3)NvsHost::state().corruptVerify=true;
  report=runCase(failed,e);assert(report.saves==0 && contains(e.exchanges[0].response,"503 Service Unavailable") && contains(e.exchanges[0].response,"Save failed"));
  assert(report.reloaded.status==(which==2?DeviceSettings::Status::Ok:which==3?DeviceSettings::Status::Corrupt:DeviceSettings::Status::Missing));}
 // Missing-only admission. Never expose a valid/corrupt/future/unreadable record via first-boot AP.
 for(int which=0;which<4;++which){reset();DeviceSettings::Service blocked;assert(blocked.save(v).status==DeviceSettings::Status::Ok);
  if(which==1)NvsHost::state().durable[12]^=1;
  if(which==2)NvsHost::state().readError=77;
  if(which==3){auto& b=NvsHost::state().durable;b[4]=2;DeviceSettings::codec::put(b.data()+12,DeviceSettings::checksum(b.data(),b.size()),4);}
  Endpoint e;report=runCase(blocked,e);assert(report.status==SettingsPortal::Status::AdmissionFailure && !e.started);}
 // Clock, client deadline, idle and absolute limits: progress does not restart deadlines.
 reset();DeviceSettings::Service timers;Endpoint idle;report=runCase(timers,idle);assert(report.status==SettingsPortal::Status::IdleExpired && idle.now>=IdleMs && idle.now<IdleMs+1001);noWrite();
 reset();Endpoint polling;for(int i=0;i<20;++i)polling.exchanges.push_back({get,"",0,128,0});polling.advanceAfterClose=40000;report=runCase(timers,polling);assert(report.status==SettingsPortal::Status::IdleExpired && polling.index<20);noWrite();
 reset();Endpoint probes;for(int i=0;i<20;++i)probes.exchanges.push_back({"GET /generate_204 HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n","",0,128,0});probes.advanceAfterClose=40000;report=runCase(timers,probes);assert(report.status==SettingsPortal::Status::IdleExpired && probes.index<20);noWrite();
 reset();Endpoint replay;replay.nonceStart='B';replay.exchanges={{good,"",0,128,0}};runCase(timers,replay);noWrite();
 reset();Endpoint extend;for(int i=0;i<5;++i)extend.exchanges.push_back({request("/session/extend","csrf="+token),"",0,128,0});extend.advanceAfterClose=400000;report=runCase(timers,extend);assert(report.status==SettingsPortal::Status::AbsoluteExpired && extend.index==5);noWrite();
 reset();Endpoint slow;slow.exchanges={{good,"",0,1,1000}};report=runCase(timers,slow);assert(slow.exchanges[0].response.empty() && report.status==SettingsPortal::Status::IdleExpired);noWrite();
 reset();Endpoint badClock;badClock.invalidClock=true;report=runCase(timers,badClock);assert(report.status==SettingsPortal::Status::ClockFailure && report.shutdownVerified);noWrite();
 reset();Endpoint regression;regression.exchanges={{get,"",0,1,2}};regression.regress=true;report=runCase(timers,regression);assert(report.status==SettingsPortal::Status::ClockFailure);noWrite();
 reset();Endpoint failStop;failStop.stopOk=false;report=runCase(timers,failStop);assert(report.status==SettingsPortal::Status::ShutdownFailure && !report.shutdownVerified);
 reset();Endpoint failClient;failClient.exchanges={{get,"",0,128,0}};failClient.closeOk=false;report=runCase(timers,failClient);assert(report.status==SettingsPortal::Status::ShutdownFailure);
 reset();Endpoint failStart;failStart.startOk=false;report=runCase(timers,failStart);assert(report.status==SettingsPortal::Status::StartFailure && report.shutdownVerified);
 reset();Endpoint lostFinish;lostFinish.writeFail=true;lostFinish.exchanges={{good,"",0,128,0},{request("/session/finish","csrf="+token),"",0,128,0}};
 report=runCase(timers,lostFinish);assert(report.status==SettingsPortal::Status::IdleExpired && report.saves==1); // no restart after an unsent completion response
 reset();Endpoint stalledResponse;stalledResponse.writeStall=true;stalledResponse.exchanges={{get,"",0,128,0}};report=runCase(timers,stalledResponse);assert(stalledResponse.closed==1 && report.status==SettingsPortal::Status::IdleExpired);noWrite();
 reset();Endpoint blockedResponse;blockedResponse.writeBlocks=2;blockedResponse.exchanges={{get,"",0,128,0}};report=runCase(timers,blockedResponse);assert(contains(blockedResponse.exchanges[0].response,"200 OK"));noWrite();
 // Explicit activity extends idle; GET alone never does.
 reset();Endpoint activity;activity.exchanges={{get,"",0,128,0},{request("/session/extend","csrf="+token),"",0,128,0}};activity.advanceAfterClose=500000;
 report=runCase(timers,activity);assert(report.status==SettingsPortal::Status::IdleExpired && activity.now>1100000);noWrite();
 std::printf("Settings HTTP/session: %zu cases; bounded Work=%zu bytes; synthetic NVS/I/O, no physical AP/browser evidence\n",cases,sizeof(Work));
}
