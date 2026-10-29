// Executes the canonical root's actual setup(), not a parallel refresh implementation.
// TLS, SNTP and drawing hardware are stand-ins. Weather decoded values are scripted,
// NOT parsed JSON. Calendar HTTP/ICS parsing, staging/publication and rendering are real.
#include "../application/five_day_calendar_refresh.h"
#include "../application/five_day_dashboard_renderer.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <NetworkClientSecure.h>
#include "device_settings_fixture.h"
#include <nvs.h>
#include <algorithm>
#include <cassert>
#include <vector>
#include <string>

namespace ObservationHost {Action actions[16];size_t actionCount=0;int adc=0;SerialSink serial;}
TestNetwork Network;
int64_t testEspMicroseconds=0;
struct InitializeHost {InitializeHost(){LiveSocket::state();ObservationHost::https();ObservationHost::boot();}} initializedHost;
// Suppress private-file lookup entirely. This public, synthetic fixture implements
// the same config contract; it never creates/opens an ignored device_config.h.
#define EE02_DEVICE_CONFIG_H
namespace DeviceConfig {
bool configured=true;
char caPem[]="synthetic-calendar-CA",weatherCaPem[]="synthetic-weather-CA";
size_t maxConfiguredSources=8,workspaceBytes=2097152,recordCapacity=128,textCapacity=32768;
uint64_t wifiWaitMs=100,ntpWaitMs=100,unconfiguredWakeSeconds=60;
IcsHttpsSource::Limits calendarLimits={2048,253,2048,4096,512,{10000,15,10000},
 {1048576,983040,16384,64,2048,512,2048,32,2048,8192,8192,0,15000},
 {8192,64,64,128,16,256,8192,16384,512,4},{512,4096,524288,512,65536},
 {{512,4,256,128,256,512,{512,512,8192},{512,512,8192,256,8192}},8192,65536}};
}
#define ARDUINO_ARCH_ESP32 1
#define CONFIG_IDF_TARGET_ESP32S3 1
#define ARDUINO_XIAO_ESP32S3_PLUS 1
#define ARDUHAL_LOG_LEVEL 0
#include "../xiao-ee02-e-paper-calendar.ino"

namespace {
struct Allocation{uint8_t* raw;uint8_t* data;size_t size;bool live;};
std::vector<Allocation> allocations;
size_t refreshes=0,cases=0;bool shutdownFails=false,rendererFails=false,failAllocation=false;
std::vector<Seeed_GFX::Glyph> glyphs;
size_t recordedEvents=0,recordedComplete=0,recordedIncomplete=0;
void guards(const Allocation& a){for(size_t i=0;i<16;++i)assert(a.raw[i]==0xdd && a.data[a.size+i]==0xdd);}
void sleptSafely(){
 assert(workspace.state()==Ee02PsramWorkspace::State::Unallocated && !snapshot.initialized());
 assert(ObservationHost::boot().wifiMode==WIFI_OFF && !LiveSocket::state().live && !LiveSocket::state().ca);
 assert(!esp_sntp_enabled() && !ObservationHost::boot().callback);
 for(const auto& a:allocations)assert(!a.live);
}
DeviceSettings::Values draft=SettingsFixture::complete();
bool seed=true; int damaged=0;
void invoke(bool allowWrites=false){
 auto& disk=NvsHost::state();
 if(seed){uint8_t encoded[DeviceSettings::MaxEncoded];size_t n=0;
  const auto r=DeviceSettings::encode(draft,encoded,sizeof(encoded),n);
  if(r.status==DeviceSettings::Status::Ok)disk.durable.assign(encoded,encoded+n);
  else { // Deliberately persist a semantically invalid but checksummed record.
   auto base=SettingsFixture::complete();assert(DeviceSettings::encode(base,encoded,sizeof(encoded),n).status==DeviceSettings::Status::Ok);
   DeviceSettings::codec::put(encoded+16,0,8);DeviceSettings::codec::put(encoded+12,DeviceSettings::checksum(encoded,n),4);
   disk.durable.assign(encoded,encoded+n);
  }
  disk.exists=disk.key=true;
 }
 if(damaged==1)disk.durable[12]^=1;
 if(damaged==2){disk.durable[4]=2;DeviceSettings::codec::put(disk.durable.data()+12,DeviceSettings::checksum(disk.durable.data(),disk.durable.size()),4);}
 try{setup();}catch(const SleepTrace::Handoff&) {}catch(const RestartTrace::Handoff&) {}
 assert((allowWrites || (!disk.sets && !disk.commits)) && !disk.erases && !disk.opened); // Every wake is read-only, including failures.
}
void weatherPhase(){assert(workspace.state()==Ee02PsramWorkspace::State::Scratch && !LiveSocket::state().live);}
void calendarPhase(){assert(workspace.state()==Ee02PsramWorkspace::State::ScratchAccess);assert(ObservationHost::https().stops==ObservationHost::https().begins);}
void tick(unsigned long ms){
 auto& b=ObservationHost::boot();if(b.apActive && ms==5 && PortalHost::state().index>=PortalHost::state().exchanges.size())ms=1000;testEspMicroseconds+=int64_t(ms)*1000;b.elapsedMs+=unsigned(ms);
 if(ms==5)ObservationHost::action(2,0,5); // Exactly the measurement settling delay.
 if(b.notify && b.sntpEnabled && b.callback){b.notify=false;timeval stamp={};b.callback(&stamp);}
}
void observed(const Seeed_GFX& gfx){
 assert(!workspace.withScratch(0,1,poison).ok() && !workspace.release().ok()); // Forbidden during borrow.
 assert(workspace.state()==Ee02PsramWorkspace::State::Rendering && ObservationHost::boot().wifiMode==WIFI_OFF);
 assert(!LiveSocket::state().live && !LiveSocket::state().ca && !ObservationHost::boot().callback);
 glyphs=gfx.glyphs;recordedEvents=snapshot.occurrenceCount();recordedComplete=recordedIncomplete=0;
 for(size_t i=0;i<snapshot.sourceCount();++i){
  recordedComplete+=snapshot.source(i)->outcome==FiveDaySnapshot::Outcome::Complete;
  recordedIncomplete+=snapshot.source(i)->outcome==FiveDaySnapshot::Outcome::Incomplete;
 }
 assert(snapshot.window()->firstCivilDay==20751);
 for(size_t i=0;i<recordedEvents;++i)assert(snapshot.title(i).size && snapshot.title(i).data[0]);
 if(recordedEvents){
  assert(snapshot.occurrence(0)->range.timed.start==1792891800);
  assert(snapshot.occurrence(1)->range.timed.start==1792888200);
  assert(snapshot.occurrence(2)->range.civil.start==20750 && snapshot.occurrence(2)->range.civil.end==20753);
  const auto label=snapshot.sourceLabel(0);assert(std::string(label.data,label.size)==std::string(settings.internal()->sources[0].label.data));
 }
}
bool has(const std::string& wanted){for(const auto& g:glyphs){
 std::vector<Seeed_GFX::Glyph> row;for(const auto& c:glyphs)if(c.y==g.y)row.push_back(c);
 std::sort(row.begin(),row.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});
 std::string text;for(const auto& c:row)text+=char(c.code);if(text.find(wanted)!=std::string::npos)return true;
 }return false;}
std::string event(const char* uid,const char* title,const char* times){return std::string("BEGIN:VEVENT\r\nUID:")+uid+
 "\r\nDTSTAMP:20261017T120000Z\r\n"+times+"SUMMARY:"+title+"\r\nEND:VEVENT\r\n";}
std::string wire(const std::string& events,bool chunked){
 const std::string body="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:Synthetic\r\nMETHOD:PUBLISH\r\n"+events+"END:VCALENDAR\r\n";
 if(!chunked)return "HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
 std::string response="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
 for(size_t i=0;i<body.size();){const size_t n=std::min<size_t>(11,body.size()-i);char h[16];snprintf(h,sizeof(h),"%zx",n);
  response+=std::string(h)+"\r\n"+body.substr(i,n)+"\r\n";i+=n;}
 return response+"0\r\n\r\n";
}
void reset(){
 assert(workspace.state()==Ee02PsramWorkspace::State::Unallocated && !snapshot.initialized());
 ObservationHost::boot()={};ObservationHost::https()={};LiveSocket::state()={};SleepTrace::state()={};SleepTrace::state().check=sleptSafely;testEspMicroseconds=0;
 ObservationHost::delayHook()=tick;ObservationHost::actionCount=0;ObservationHost::adc=2040;
 ObservationHost::https().statuses={200,200,200};ObservationHost::https().payload=LiveDecoded::payload();
 ObservationHost::https().checkPhase=weatherPhase;LiveSocket::state().checkPhase=calendarPhase;
 DeviceConfig::configured=true;DeviceConfig::unconfiguredWakeSeconds=60;draft=SettingsFixture::complete();seed=true;damaged=0;NvsHost::state()={};PortalHost::state()={};RestartTrace::count()=0;
 DeviceConfig::maxConfiguredSources=8;
 DeviceConfig::caPem[0]='s';DeviceConfig::weatherCaPem[0]='s';
 std::memset(&weatherData,0,sizeof(weatherData));std::memset(forecastData,0,sizeof(forecastData));std::memset(dailyForecastData,0,sizeof(dailyForecastData));
 const std::string events=event("second","Second fold","DTSTART:20261025T013000Z\r\n")+
  event("first","First fold","DTSTART:20261025T003000Z\r\n")+
  event("day","All day span","DTSTART;VALUE=DATE:20261024\r\nDTEND;VALUE=DATE:20261027\r\n")+
  event("cross","Cross midnight","DTSTART:20261024T210000Z\r\nDTEND:20261025T010000Z\r\n");
 LiveSocket::state().feeds={{wire(events,true),"",true,true},
  {wire(event("last","Final source","DTSTART:20261026T090000Z\r\n"),false),"",true,true}};
 freshSync.store(false);renderingAttempted=false;rendering={};glyphs.clear();refreshes=0;shutdownFails=false;rendererFails=false;failAllocation=false;
 recordedEvents=recordedComplete=recordedIncomplete=0;Seeed_GFX::observer()=observed;
}
void finished(bool drawn=true,bool sleeping=true){
 ++cases;if(ObservationHost::boot().wifiBegins!=1 || ObservationHost::boot().starts!=1)std::fprintf(stderr,"case=%zu settings=%u cycle=%u wifi=%u ntp=%u\n",cases,unsigned(settingsResult.status),unsigned(cycleStatus),ObservationHost::boot().wifiBegins,ObservationHost::boot().starts);assert(ObservationHost::boot().wifiBegins==1 && ObservationHost::boot().starts==1);
 assert(ObservationHost::https().stops==ObservationHost::https().begins && !LiveSocket::state().live);
 if(renderingAttempted!=drawn || refreshes!=size_t(drawn))std::fprintf(stderr,"case=%zu cycle=%u sleep=%u drew=%u refresh=%zu expected=%u\n",cases,unsigned(cycleStatus),unsigned(sleepStatus),unsigned(renderingAttempted),refreshes,unsigned(drawn));
 assert(renderingAttempted==drawn && refreshes==size_t(drawn));
 assert(!ObservationHost::boot().apStarts && !PortalHost::state().starts);
 assert(!snapshot.initialized());
 if(drawn){assert(rendering.status==FiveDayDashboardRenderer::Status::Drawn && !has("DEVELOPMENT"));
  assert(ObservationHost::actionCount==5 && ObservationHost::actions[3].operation==3 && ObservationHost::actions[4].value==LOW);}
 assert(workspace.state()==Ee02PsramWorkspace::State::Unallocated);
 for(const auto& a:allocations)assert(!a.live);
 assert(SleepTrace::state().handoffs==size_t(sleeping) && SleepTrace::state().timerCalls==size_t(sleeping));
 if(sleeping){assert(SleepTrace::state().micros==42000000 && sleepStatus==SleepStatus::Entering);}
}
struct Bus:IBus{
 bool begin()override{return true;}void end()override{}void beginWrite()override{}void endWrite()override{}
 void writeCommand(uint8_t)override{}void writeData(uint8_t)override{}void writeData(const uint8_t*,size_t)override{}
 void beginRead()override{}void endRead()override{}uint8_t readData()override{return 0;}
 void setFrequency(uint32_t)override{}uint32_t frequency()const override{return 10000000;}
};
struct Board:IBoard{
 const char* name()const override{return "host";}bool begin()override{return true;}
 int8_t pinCS()const override{return -1;}int8_t pinDC()const override{return -1;}int8_t pinRST()const override{return -1;}
 int8_t pinBacklight()const override{return -1;}int8_t pinMOSI()const override{return -1;}int8_t pinMISO()const override{return -1;}
 int8_t pinSCLK()const override{return -1;}IBus* createBus()override{return nullptr;}
};
struct Driver:IDriver{
 IBus* transport=nullptr;
 uint16_t width()const override{return 1200;}uint16_t height()const override{return 1600;}
 uint8_t colorDepth()const override{return 4;}const char* name()const override{return "T133A01";}
 bool supportsColorfulEPaper()const override{return true;}bool init(IBus& b)override{transport=&b;return true;}
 void setRotation(uint8_t r)override{_rotation=r;}uint8_t rotation()const override{return _rotation;}
 void invertDisplay(bool)override{}void displayOn()override{}void displayOff()override{}
 void setAddrWindow(uint16_t,uint16_t,uint16_t,uint16_t)override{}void writePixel(uint16_t)override{}
 void writePixels(const uint16_t*,size_t)override{}void writeFill(uint16_t,size_t)override{}
 void sleep()override{if(shutdownFails)setOperationError(DriverOperationError::BusyTimeout);}
 void wake()override{}void update()override{++refreshes;assert(refreshes<=1);}
 void pushNewColors(const uint8_t* p,size_t n)override{assert(n==Ee02PsramWorkspace::FrameBufferBytes);
  bool borrowed=false;for(const auto& a:allocations)if(a.live && a.size==DeviceConfig::workspaceBytes)borrowed=p==a.data;
  assert(borrowed);
 }
 IBus& bus()override{return *transport;}
};
}
extern "C" time_t __wrap_time(time_t* out){const time_t value=1792915200;if(out)*out=value;return value;} // Oct25 08:00 UTC.
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* data){if(!data)return;for(auto& a:allocations)if(a.live && a.data==data){guards(a);a.live=false;__real_free(a.raw);return;}__real_free(data);}
bool psramFound(){return true;}
void* ps_malloc(size_t n){if(failAllocation)return nullptr;auto* raw=static_cast<uint8_t*>(std::malloc(n+32));assert(raw);memset(raw,0xdd,n+32);
 allocations.push_back({raw,raw+16,n,true});return raw+16;}
GfxResult ProductCatalog::create(Seeed_Product::Product product,DisplayInstance& instance){
 assert(product==Seeed_Product::Seeed_ePaper_13INCH3_C && workspace.state()==Ee02PsramWorkspace::State::Rendering);
 assert(ObservationHost::boot().wifiMode==WIFI_OFF && !LiveSocket::state().live && !LiveSocket::state().ca);
 bool poisoned=false;for(const auto& a:allocations)if(a.live && a.size==DeviceConfig::workspaceBytes){
  assert(std::all_of(a.data,a.data+a.size,[](uint8_t b){return b==0xa5;}));poisoned=true;}
 if(rendererFails){assert(!setenv("TZ","UTC0",1));tzset();}
 assert(poisoned);auto* bus=new Bus;auto* board=new Board;auto* driver=new Driver;
 return instance.adopt(board,bus,driver,new Panel_EPaper(*driver,*bus,board));
}
int main(){
 allocations.reserve(64);
 reset();invoke();finished();assert(recordedComplete==2 && recordedEvents==5 && !rendering.weatherUnavailable);
 assert(has("First fold") && has("Second fold") && has("+0200") && has("+0100") && has("All day span") && has("Personal"));
 assert(rendering.shortDrawn==1 && rendering.dailyDrawn==1 && has("Temperature: 17.0 C"));
 assert(rendering.todayDrawn+rendering.todayOmitted==4 && !rendering.batteryUnavailable && battery.valid && battery.percentage==0);
 assert(ObservationHost::https().begins==3 && LiveSocket::state().resolves==2);
 assert(ObservationHost::https().requestHistory[0]=="https://api.openweathermap.org/data/2.5/weather?q=Example%20City,XX&appid=synthetic-key&units=metric");
 assert(ObservationHost::https().caHistory[0]==DeviceConfig::weatherCaPem && LiveSocket::state().caHistory[0]==DeviceConfig::caPem);
 for(int which=0;which<3;++which){reset();ObservationHost::https().statuses[size_t(which)]=503;invoke();finished();
  assert(recordedComplete==2 && recordedEvents==5);
  assert(weatherData.valid==(which!=0) && forecastData[0].valid==(which!=1) && dailyForecastData[0].valid==(which!=2));
  assert(rendering.weatherUnavailable==(which==0) && rendering.shortDrawn==size_t(which!=1) && rendering.dailyDrawn==size_t(which!=2));}
 reset();LiveSocket::state().feeds[1].tls=false;invoke();finished();assert(recordedComplete==1 && recordedIncomplete==1 && has("Incomplete schedule") && weatherData.valid);
 reset();LiveSocket::state().feeds[0].dns=false;LiveSocket::state().feeds[1].tls=false;invoke();finished();assert(!recordedComplete && recordedIncomplete==2 && has("unavailable") && weatherData.valid);
 reset();draft.sources[0].enabled=draft.sources[2].enabled=false;DeviceConfig::caPem[0]='\0';invoke();finished();assert(!LiveSocket::state().resolves && has("No calendars selected") && weatherData.valid);
 reset();draft.weatherEnabled=false;draft.apiKey={};draft.location={};DeviceConfig::weatherCaPem[0]='\0';invoke();finished();assert(!ObservationHost::https().begins && recordedComplete==2 && rendering.weatherUnavailable);
 // User-field errors are now rejected at NVS load, before any network.
 for(int invalid=0;invalid<5;++invalid){reset();if(invalid==0)draft.apiKey.data[0]='&';
  if(invalid==1)draft.location.data[0]='\n';
  if(invalid==2)draft.apiKey.data[0]='\0';
  if(invalid==3)draft.apiKey.data[draft.apiKey.size]='k';
  if(invalid==4)draft.location.data[draft.location.size]='A';
  invoke();++cases;assert(cycleStatus==CycleStatus::SettingsFailure && !ObservationHost::boot().wifiBegins && !renderingAttempted && !SleepTrace::state().handoffs);}
 reset();DeviceConfig::weatherCaPem[0]='\0';invoke();finished();assert(!ObservationHost::https().begins && recordedComplete==2 && rendering.weatherUnavailable);
 reset();ObservationHost::adc=0;invoke();finished();assert(rendering.batteryUnavailable && !battery.valid);
 reset();DeviceConfig::configured=false;invoke();++cases;assert(!ObservationHost::boot().wifiBegins && !renderingAttempted);
 reset();ObservationHost::boot().notify=false;invoke();++cases;assert(!renderingAttempted && !ObservationHost::https().begins && !LiveSocket::state().resolves && !ObservationHost::actionCount && SleepTrace::state().handoffs==1 && cycleStatus==CycleStatus::TimeFailure);
 reset();ObservationHost::boot().wifiReady=false;invoke();++cases;assert(!ObservationHost::boot().starts && !renderingAttempted && !ObservationHost::https().begins && !LiveSocket::state().resolves && SleepTrace::state().handoffs==1 && cycleStatus==CycleStatus::WifiFailure);
 reset();ObservationHost::boot().shutdownOk=false;invoke();finished(false,false);assert(cycleStatus==CycleStatus::NetworkShutdownFailure && sleepStatus==SleepStatus::UnsafeShutdown);
 reset();DeviceConfig::maxConfiguredSources=0;invoke();finished(false);assert(!LiveSocket::state().resolves && cycleStatus==CycleStatus::CalendarInterruption);
 reset();DeviceConfig::caPem[0]='\0';invoke();finished();assert(recordedIncomplete==2 && !recordedComplete && !LiveSocket::state().resolves);
 reset();draft.timezone.data[0]='\0';invoke();++cases;assert(cycleStatus==CycleStatus::SettingsFailure && !ObservationHost::boot().wifiBegins && !SleepTrace::state().handoffs);
 reset();failAllocation=true;invoke();finished(false);assert(cycleStatus==CycleStatus::WorkspaceFailure && !ObservationHost::https().begins && !LiveSocket::state().resolves);
 reset();rendererFails=true;invoke();++cases;assert(cycleStatus==CycleStatus::RendererFailure && !refreshes && SleepTrace::state().handoffs==1 && workspace.state()==Ee02PsramWorkspace::State::Unallocated);
 for(uint64_t invalid:{uint64_t(0),UINT64_MAX/1000000+1,uint64_t(INT64_MAX)/1000000+1}){
  reset();draft.refreshSeconds=invalid;invoke();++cases;
  assert(cycleStatus==CycleStatus::SettingsFailure && !ObservationHost::boot().wifiBegins && !SleepTrace::state().timerCalls);
 }
 reset();SleepTrace::state().timerResult=ESP_ERR_INVALID_ARG;invoke();++cases;
 assert(cycleStatus==CycleStatus::Refreshed && sleepStatus==SleepStatus::TimerWakeFailure && SleepTrace::state().timerCalls==1 && !SleepTrace::state().handoffs);
 reset();SleepTrace::state().maxMicros=1000000;invoke();++cases;assert(sleepStatus==SleepStatus::TimerWakeFailure && !SleepTrace::state().handoffs);
 // Prior plausible/fresh RAM evidence is not inherited by a subsequent wake.
 reset();freshSync.store(true);ObservationHost::boot().notify=false;invoke();++cases;
 assert(cycleStatus==CycleStatus::TimeFailure && !ObservationHost::https().begins && SleepTrace::state().handoffs==1);
 // Missing/corrupt/future/storage failures do not acquire, guess dates or sleep.
 reset();seed=false;invoke();++cases;assert(cycleStatus==CycleStatus::Unconfigured && !ObservationHost::boot().wifiBegins && ObservationHost::boot().apStarts==1 && setupReport.shutdownVerified && SleepTrace::state().handoffs==1 && SleepTrace::state().micros==60000000);
 for(int bad=1;bad<=2;++bad){reset();damaged=bad;invoke();++cases;assert(cycleStatus==CycleStatus::SettingsFailure && !ObservationHost::boot().wifiBegins);}
 reset();NvsHost::state().openError=77;invoke();++cases;assert(cycleStatus==CycleStatus::SettingsFailure && !ObservationHost::boot().wifiBegins);
 reset();NvsHost::state().readError=77;invoke();++cases;assert(cycleStatus==CycleStatus::SettingsFailure && !ObservationHost::boot().wifiBegins);
 // Reboot/reload the same persisted bytes; no defaults or user-field fallback.
 reset();invoke();finished();auto saved=NvsHost::state().durable;
 reset();seed=false;NvsHost::state().exists=NvsHost::state().key=true;NvsHost::state().durable=saved;invoke();finished();
 // Changed persistent credentials/timezone/source views are used on later setup.
 reset();SettingsFixture::set(draft.ssid,"updated-network");SettingsFixture::set(draft.password,"updated-password");
 SettingsFixture::set(draft.ntp,"new-ntp.example.invalid");SettingsFixture::set(draft.timezone,"UTC0");
 SettingsFixture::set(draft.sources[0].label,"Changed name");SettingsFixture::set(draft.sources[0].url,"https://new.example.invalid/feed?token=preserved");
 invoke();finished();
 assert(ObservationHost::boot().ssid=="updated-network" && ObservationHost::boot().password=="updated-password");
 assert(ObservationHost::boot().ntp=="new-ntp.example.invalid");
 assert(!strcmp(settings.internal()->timezone.data,"UTC0") && !strcmp(calendarSources[0].label.data,"Changed name"));
 assert(LiveSocket::state().feeds[0].request.find("GET /feed?token=preserved HTTP/1.1")!=std::string::npos);
 assert(LiveSocket::state().feeds[0].request.find("Host: new.example.invalid")!=std::string::npos && has("Changed name"));
 reset();SettingsFixture::set(draft.ntp,"new-ntp.example.invalid");ObservationHost::boot().notify=false;invoke();++cases;
 assert(ObservationHost::boot().ntp=="new-ntp.example.invalid");
 // First-boot setup owns the entire invocation, with no trusted clock/display.
 reset();seed=false;DeviceConfig::configured=false;invoke();++cases;
 assert(!ObservationHost::boot().apStarts && !PortalHost::state().starts && !NvsHost::state().opens && !SleepTrace::state().timerCalls);
 reset();seed=false;DeviceConfig::unconfiguredWakeSeconds=0;invoke();++cases;
 assert(cycleStatus==CycleStatus::InvalidInterval && !ObservationHost::boot().apStarts && !SleepTrace::state().timerCalls);
 for(uint64_t invalid:{UINT64_MAX,uint64_t(INT64_MAX)/1000000+1}){reset();seed=false;DeviceConfig::unconfiguredWakeSeconds=invalid;invoke();++cases;
  assert(cycleStatus==CycleStatus::InvalidInterval && !ObservationHost::boot().apStarts);}
 reset();seed=false;SleepTrace::state().timerResult=ESP_ERR_INVALID_ARG;invoke();++cases;
 assert(setupReport.shutdownVerified && sleepStatus==SleepStatus::TimerWakeFailure && !SleepTrace::state().handoffs);
 for(int failure=0;failure<4;++failure){reset();seed=false;
  if(failure==0)PortalHost::state().failStop=true;
  if(failure==1)ObservationHost::boot().apStopOk=false;
  if(failure==2)ObservationHost::boot().shutdownOk=false;
  if(failure==3){PortalHost::state().exchanges.emplace_back("GET / HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n");PortalHost::state().failClientStop=true;}
  invoke();++cases;assert(!setupReport.shutdownVerified && !SleepTrace::state().timerCalls && !SleepTrace::state().handoffs && !renderingAttempted);}
 reset();seed=false;PortalHost::state().failStart=true;invoke();++cases;
 assert(setupReport.status==SettingsPortal::Status::StartFailure && setupReport.shutdownVerified && SleepTrace::state().handoffs==1);
 reset();seed=false;testEspMicroseconds=-1;invoke();++cases;
 assert(setupReport.status==SettingsPortal::Status::ClockFailure && setupReport.shutdownVerified && SleepTrace::state().handoffs==1 && !ObservationHost::boot().starts);
 reset();seed=false;assert(workspace.allocate(DeviceConfig::workspaceBytes).ok());invoke();++cases;
 assert(cycleStatus==CycleStatus::SetupFailure && !ObservationHost::boot().apStarts && !SleepTrace::state().timerCalls);assert(workspace.release().ok());
 // Actual root: GET obtains token, Save commits, Finish sends response and closes
 // service/AP before restart. No temporary draft bypasses the next boot's load.
 reset();seed=false;PortalHost::state().advance=[](PortalHost::State& p){
  if(p.exchanges.empty()){p.exchanges.emplace_back("GET /settings HTTP/1.1\r\nHost: 192.168.4.1\r\n\r\n");return;}
  if(p.index!=p.exchanges.size() || p.index>2)return;
  const std::string marker="name=\"csrf\" value=\"";const auto pos=p.exchanges[0].response.find(marker);assert(pos!=std::string::npos);
  const std::string csrf=p.exchanges[0].response.substr(pos+marker.size(),32);
  const std::string body=p.index==1?"csrf="+csrf+"&ssid=synthetic-network&timezone=CET-1CEST%2CM3.5.0%2CM10.5.0%2F3&ntp=ntp.example.invalid&refresh=42&count=2&location=&password_action=replace&password=synthetic-password&key_action=keep&key=&calendar0e=1&calendar0n=Personal&calendar0u=https%3A%2F%2Fone.example.invalid%2Fone.ics&calendar1e=1&calendar1n=Shared&calendar1u=https%3A%2F%2Ftwo.example.invalid%2Ftwo.ics":"csrf="+csrf;
  const std::string route=p.index==1?"/settings":"/session/finish";
  p.exchanges.emplace_back("POST "+route+" HTTP/1.1\r\nHost: 192.168.4.1\r\nOrigin: http://192.168.4.1\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body);
 };
 invoke(true);++cases;
 assert(setupReport.status==SettingsPortal::Status::Finished && setupReport.shutdownVerified && RestartTrace::count()==1);
 assert(ObservationHost::boot().apStarts==1 && !ObservationHost::boot().apActive && ObservationHost::boot().wifiMode==WIFI_OFF);
 assert(!ObservationHost::boot().wifiBegins && !ObservationHost::boot().starts && !ObservationHost::https().begins && !LiveSocket::state().resolves && !ObservationHost::actionCount && !renderingAttempted && !SleepTrace::state().timerCalls);
 assert(NvsHost::state().sets==1 && NvsHost::state().commits==1 && !PortalHost::state().listening);
 for(const auto& exchange:PortalHost::state().exchanges){assert(exchange.closed && exchange.response.find("synthetic-password")==std::string::npos);}
 assert(PortalHost::state().exchanges[2].response.find("Finish accepted")!=std::string::npos);
 auto provisioned=NvsHost::state().durable;
 static auto completeSetupScript=PortalHost::state().advance;
 reset();seed=false;NvsHost::state().exists=NvsHost::state().key=true;NvsHost::state().durable=provisioned;
 invoke();finished();assert(recordedComplete==2 && recordedEvents==5 && !ObservationHost::boot().apStarts && !ObservationHost::https().begins);
 // Saving without Finish expires normally and sleeps with the build-only retry,
 // preserving the saved record for an independent configured wake.
 reset();seed=false;PortalHost::state().advance=[](PortalHost::State& p){if(p.index<2)completeSetupScript(p);};
 invoke(true);++cases;assert(setupReport.status==SettingsPortal::Status::IdleExpired && setupReport.saves==1 && setupReport.shutdownVerified && !RestartTrace::count());
 assert(SleepTrace::state().handoffs==1 && SleepTrace::state().micros==60000000);
 auto savedWithoutFinish=NvsHost::state().durable;
 reset();seed=false;NvsHost::state().exists=NvsHost::state().key=true;NvsHost::state().durable=savedWithoutFinish;
 invoke();finished();assert(recordedComplete==2 && !ObservationHost::boot().apStarts);
 // Last: uncertain shutdown deliberately retains the borrowed allocation. No
 // release/free/reuse is attempted; the globally reachable owner keeps it alive.
 reset();shutdownFails=true;invoke();++cases;assert(refreshes<=1 && workspace.state()==Ee02PsramWorkspace::State::Retained);
 assert(!workspace.release().ok() && !snapshot.initialized() && !SleepTrace::state().timerCalls && !SleepTrace::state().handoffs && cycleStatus==CycleStatus::PanelShutdownFailure);
 std::printf("Canonical application: %zu root boot/sleep cycles, real calendar pipeline/borrow, scripted decoded-weather branch coverage; target/TLS/JSON parser unverified\n",cases);
}
