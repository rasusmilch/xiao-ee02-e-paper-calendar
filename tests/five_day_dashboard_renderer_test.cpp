#include "../application/five_day_dashboard_renderer.h"
#include "../application/five_day_posix_time.h"
#include "../application/five_day_window.h"
#include "../application/ics_nonrecurring_snapshot.h"
#include "../application/psram_workspace.h"
#include <algorithm>
#include <cassert>
#include <cstddef>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

namespace { bool audit=false;size_t heapCalls=0,psramCalls=0,cases=0; }
__attribute__((noinline)) void* operator new(size_t n) {if(audit)++heapCalls;void* p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();return p;}
void* operator new[](size_t n){return ::operator new(n);}
__attribute__((noinline)) void operator delete(void* p) noexcept {std::free(p);}
void operator delete[](void* p) noexcept {std::free(p);}
namespace {
struct Allocation {uint8_t* raw;uint8_t* data;size_t bytes;bool live;};
std::vector<Allocation> allocations;
void guards(const Allocation& a){for(size_t i=0;i<16;++i)assert(a.raw[i]==0xa5 && a.data[a.bytes+i]==0xa5);}
}
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p){for(auto it=allocations.rbegin();it!=allocations.rend();++it)if(it->live && it->data==p){guards(*it);it->live=false;__real_free(it->raw);return;}__real_free(p);}
bool psramFound(){return true;}
void* ps_malloc(size_t n){++psramCalls;auto* p=static_cast<uint8_t*>(std::malloc(n+32));assert(p);std::memset(p,0xa5,n+32);allocations.push_back({p,p+16,n,true});return p+16;}
namespace {
namespace D=FiveDayDashboardRenderer;using Snap=FiveDaySnapshot;
const char* tz="CET-1CEST,M3.5.0,M10.5.0/3";FiveDayPosixTime::Context zone={tz};
WeatherData weather={true,"02d","Partly cloudy","2026-10-23 10:00",12.5f,68,0.2f,3.1f};
ForecastEntry shortValues[8]={};DailyForecastEntry dailyValues[8]={};
D::Inputs inputs(){return {&weather,shortValues,5,dailyValues,5,{true,50},true,1792746000};}
Snap::Text text(const char* p){return {p,std::strlen(p)};}
void resetWeather(){
 weather={true,"02d","Partly cloudy","2026-10-23 10:00",12.5f,68,0.2f,3.1f};
 for(size_t i=0;i<8;++i){shortValues[i]={true,"12:00","01d",14,7};dailyValues[i]={true,"Friday","10d","Rain",12,6};}
}
void init(Snap& s,size_t sources=2,int64_t now=1792746000){
 Snap::Window w={};assert(FiveDayWindow::build(now,true,tz,w)==FiveDayWindow::Status::Ok);
 const Snap::Text labels[]={{"Personal",8},{"Team",4},{"Other",5}};
 assert(s.initialize(w,128,32768,labels,sources)==Snap::Result::Ok);
}
void timed(Snap& s,int64_t a,int64_t b,const char* title,size_t source=0){Snap::Input e={};e.kind=Snap::Kind::Timed;e.range.timed={a,b};e.title=text(title);assert(s.addOccurrence(source,e)==Snap::Result::Ok);}
void day(Snap& s,int32_t a,int32_t b,const char* title){Snap::Input e={};e.kind=Snap::Kind::AllDay;e.range.civil={a,b};e.title=text(title);assert(s.addOccurrence(0,e)==Snap::Result::Ok);}
void failed(Snap& s,size_t source=1){assert(s.beginSource(source)==Snap::Result::Ok && s.failSource(source,Snap::Reason::ProducerFailure)==Snap::Result::Ok);}
D::Report draw(Seeed_GFX& gfx,Snap& s,D::Inputs in=inputs(),D::Geometry g=D::ee02PreviewGeometry(),FiveDayRenderer::TimeContext context={FiveDayPosixTime::convert,&zone}){
 const auto w= s.window()?*s.window():Snap::Window{};
 std::vector<Snap::Occurrence> records;std::vector<std::string> titles,labels;
 for(size_t i=0;i<s.occurrenceCount();++i){records.push_back(*s.occurrence(i));titles.emplace_back(s.title(i).data,s.title(i).size);}
 for(size_t i=0;i<s.sourceCount();++i)labels.emplace_back(s.sourceLabel(i).data,s.sourceLabel(i).size);
 const auto weatherBefore=weather;ForecastEntry shortBefore[8];DailyForecastEntry dailyBefore[8];
 std::memcpy(shortBefore,shortValues,sizeof(shortBefore));std::memcpy(dailyBefore,dailyValues,sizeof(dailyBefore));
 const auto battery=in.battery;const size_t before=heapCalls,allocated=psramCalls;
 audit=true;const auto r=D::render(gfx,s,context,in,g);audit=false;++cases;
 assert(heapCalls==before && psramCalls==allocated && !std::memcmp(&weatherBefore,&weather,sizeof(weather)));
 assert(!std::memcmp(shortBefore,shortValues,sizeof(shortBefore)) && !std::memcmp(dailyBefore,dailyValues,sizeof(dailyBefore)));
 assert(in.battery.valid==battery.valid && in.battery.percentage==battery.percentage && records.size()==s.occurrenceCount());
 if(s.window())assert(!std::memcmp(&w,s.window(),sizeof(w)));
 for(size_t i=0;i<records.size();++i){assert(!std::memcmp(&records[i],s.occurrence(i),sizeof(records[i])));assert(titles[i]==std::string(s.title(i).data,s.title(i).size));}
 for(size_t i=0;i<labels.size();++i)assert(labels[i]==std::string(s.sourceLabel(i).data,s.sourceLabel(i).size));
 if(r.status==D::Status::Drawn){
  const D::Region panels[]={g.calendar,g.weather,g.shortForecast,g.dailyForecast,g.today,g.battery};
  for(const auto& glyph:gfx.glyphs){bool inside=false;for(auto panel:panels)inside=inside || D::detail::inside({glyph.x,glyph.y,6*glyph.size,8*glyph.size},panel);assert(inside);}
  for(const auto& shape:gfx.shapes)assert(D::detail::inside({shape.x,shape.y,shape.width,shape.height},{shape.vx,shape.vy,shape.vw,shape.vh}));
 }
 for(const auto& a:allocations)if(a.live)guards(a);
 return r;
}
std::string line(const Seeed_GFX& gfx,int32_t y,D::Region region){
 std::vector<Seeed_GFX::Glyph> row;for(const auto& g:gfx.glyphs)if(g.y==y && g.x>=region.x && g.x<region.x+region.width)row.push_back(g);
 std::sort(row.begin(),row.end(),[](const Seeed_GFX::Glyph& a,const Seeed_GFX::Glyph& b){return a.x<b.x;});std::string value;for(const auto& g:row)value+=char(g.code);return value;
}
bool has(const Seeed_GFX& gfx,const std::string& value,D::Region r={0,0,1600,1200}){for(const auto& g:gfx.glyphs)if(g.y>=r.y && g.y<r.y+r.height && line(gfx,g.y,r).find(value)!=std::string::npos)return true;return false;}
int yOf(const Seeed_GFX& gfx,const std::string& value,D::Region r){for(const auto& g:gfx.glyphs)if(g.y>=r.y && g.y<r.y+r.height && line(gfx,g.y,r).find(value)!=std::string::npos)return g.y;return -1;}
void composition(){
 resetWeather();Snap s;init(s);assert(s.beginSource(0)==Snap::Result::Ok);
 timed(s,1792749600,1792753200,"Later");timed(s,1792746000,1792746000,"Point");day(s,20748,20751,"All day span");
 timed(s,1792702800,1792710000,"Continued timed");timed(s,1792746000,1792746000,"Stable tie");
 assert(s.completeSource(0)==Snap::Result::Ok);failed(s);
 Seeed_GFX gfx;const auto g=D::ee02PreviewGeometry();const auto r=draw(gfx,s);
 assert(r.status==D::Status::Drawn && r.todayDrawn==3 && r.todayOmitted==2 && r.shortDrawn==5 && r.dailyDrawn==5);
 assert(has(gfx,"Weather",g.weather) && has(gfx,"Fri 2026-10-23 11:00 +0200",g.weather));
 assert(has(gfx,"Temperature: 12.5 C",g.weather) && has(gfx,"Humidity: 68%",g.weather) && has(gfx,"Rain: 0.2 mm",g.weather) && has(gfx,"Wind: 3.1 m/s",g.weather));
 assert(has(gfx,"Fetch time (as supplied)",g.weather) && has(gfx,"Freshness not assessed",g.weather));
 assert(has(gfx,"Short forecast",g.shortForecast) && has(gfx,"12:00",g.shortForecast) && has(gfx,"14/7 C",g.shortForecast));
 assert(has(gfx,"Five-day forecast",g.dailyForecast) && has(gfx,"Friday",g.dailyForecast) && has(gfx,"Rain",g.dailyForecast));
 assert(has(gfx,"50%",g.battery) && has(gfx,"Today - detailed events",g.today) && has(gfx,"Personal",g.today));
 assert(has(gfx,"Incomplete schedule",g.today) && has(gfx,"Team",g.calendar) && has(gfx,"+2 more events",g.today));
 assert(yOf(gfx,"All day span",g.today)<yOf(gfx,"Continued timed",g.today) && yOf(gfx,"Continued timed",g.today)<yOf(gfx,"Point",g.today));
 assert(has(gfx,"end exclusive",g.today) && has(gfx,"2026-10-22",g.today) && has(gfx,"Point",g.today));
 bool vertical=false,left=false,right=false,todayDivider=false;
 for(const auto& q:gfx.shapes){vertical=vertical ||(q.x==1044 && q.y==10 && q.width==1 && q.height==1171);left=left ||(q.x==10 && q.y==1044 && q.width==1025 && q.height==1);right=right ||(q.x==1054 && q.y==570 && q.width==527 && q.height==1);todayDivider=todayDivider ||(q.x==1200 && q.y==970 && q.width==201 && q.height==1);}
 assert(vertical && left && right && todayDivider);
 const D::Region month={1054,600,526,360};for(const auto& glyph:gfx.glyphs)assert(!D::detail::inside({glyph.x,glyph.y,6*glyph.size,8*glyph.size},month));
}
void weatherAndForecasts(){
 Snap s;init(s,1);assert(s.beginSource(0)==Snap::Result::Ok && s.completeSource(0)==Snap::Result::Ok);const auto g=D::ee02PreviewGeometry();
 for(int bad=0;bad<7;++bad){resetWeather();auto in=inputs();if(bad==0)in.weather=nullptr;if(bad==1)weather.valid=false;if(bad==2)weather.temp=std::numeric_limits<float>::quiet_NaN();if(bad==3)weather.humidity=101;if(bad==4)weather.rain=-1;if(bad==5)weather.windSpeed=std::numeric_limits<float>::infinity();if(bad==6)weather.humidity=-1;
  Seeed_GFX gfx;const auto r=draw(gfx,s,in);assert(r.status==D::Status::Drawn && r.weatherUnavailable && has(gfx,"Weather unavailable",g.weather));assert(!has(gfx,"Temperature:",g.weather) && r.shortDrawn==5 && r.dailyDrawn==5);}
 const char* icons[]={"01d","01n","02d","02n","03d","04d","09d","10n","11d","13d","50d","50n","99d","xx"};
 for(const char* code:icons){resetWeather();std::memset(weather.icon,0,4);std::memcpy(weather.icon,code,std::strlen(code));Seeed_GFX gfx;const auto r=draw(gfx,s);assert(!r.weatherUnavailable);assert((r.iconFallbacks>0)==(std::strcmp(code,"99d")==0 || std::strcmp(code,"xx")==0));if(code[2]=='n')assert(has(gfx,"night",g.weather));}
 resetWeather();std::memset(weather.description,'A',64);weather.description[0]=char(0xff);weather.description[1]='Z';weather.description[2]='\n';
 std::memset(weather.fetchTime,'T',32);std::memset(dailyValues[0].description,'Q',32);std::memcpy(dailyValues[1].description,"Caf\xc3\xa9",6);
 std::memset(weather.icon,'1',4);shortValues[0].valid=false;shortValues[1].tempHigh=std::numeric_limits<float>::infinity();std::memcpy(shortValues[2].timeStr,"25:99",6);
 dailyValues[2].tempLow=20;dailyValues[3].dayName[0]=0;auto in=inputs();in.forecastCount=in.dailyCount=8;
 Seeed_GFX gfx;const auto r=draw(gfx,s,in);assert(r.status==D::Status::Drawn && r.shortUnavailable==3 && r.dailyUnavailable==2 && r.shortOmitted==3 && r.dailyOmitted==3);
 assert(r.textShortened && r.glyphsReplaced==2 && r.iconFallbacks==1 && has(gfx,"?Z ",g.weather) && has(gfx,"Caf?",g.dailyForecast) && has(gfx,"+3 more forecasts",g.shortForecast));
 resetWeather();in=inputs();in.forecast=nullptr;in.forecastCount=0;in.daily=nullptr;in.dailyCount=0;in.currentTimeValid=false;weather.fetchTime[0]=0;
 Seeed_GFX empty;draw(empty,s,in);assert(has(empty,"Forecast unavailable",g.shortForecast) && has(empty,"Forecast unavailable",g.dailyForecast) && has(empty,"Fetch time unavailable",g.weather) && has(empty,"Current time unavailable",g.weather));
}
void batteries(){
 resetWeather();Snap s;init(s,0);const auto g=D::ee02PreviewGeometry();
 for(int pct:{INT_MIN,-1,0,1,50,100,101,INT_MAX}){auto in=inputs();in.battery.percentage=pct;Seeed_GFX gfx;const auto r=draw(gfx,s,in);assert(r.status==D::Status::Drawn && r.batteryClamped==(pct<0 || pct>100));const int clamp=pct<0?0:pct>100?100:pct;assert(has(gfx,std::to_string(clamp)+"%",g.battery));
  bool fill=false;for(const auto& shape:gfx.shapes)if(shape.x==1535 && shape.y==1163){assert(shape.width==44*clamp/100 && shape.height==16);fill=true;}assert(fill==(44*clamp/100>0));}
 auto in=inputs();in.battery={false,100};Seeed_GFX gfx;const auto r=draw(gfx,s,in);assert(r.batteryUnavailable && !r.batteryClamped && has(gfx,"N/A",g.battery) && !has(gfx,"100%",g.battery));
}
void todayOrdering(){
 resetWeather();const auto g=D::ee02PreviewGeometry();
 {Snap s;init(s);assert(s.beginSource(0)==Snap::Result::Ok);
  timed(s,1792746000,1792746000,"Tie A");timed(s,1792746000,1792746000,"Tie B");assert(s.completeSource(0)==Snap::Result::Ok);
  assert(s.beginSource(1)==Snap::Result::Ok);timed(s,1792746000,1792746000,"Tie C",1);assert(s.completeSource(1)==Snap::Result::Ok);
  Seeed_GFX gfx;const auto r=draw(gfx,s);assert(r.todayDrawn==3 && !r.todayOmitted && has(gfx,"Team",g.today));
  assert(yOf(gfx,"Tie A",g.today)<yOf(gfx,"Tie B",g.today) && yOf(gfx,"Tie B",g.today)<yOf(gfx,"Tie C",g.today));}
 {Snap s;init(s,1);assert(s.beginSource(0)==Snap::Result::Ok);const auto w=*s.window();
  timed(s,w.utcBoundaries[0],w.utcBoundaries[0],"Boundary Caf\xc3\xa9");timed(s,w.utcBoundaries[1],w.utcBoundaries[1],"Tomorrow only");
  day(s,w.firstCivilDay+1,w.firstCivilDay+2,"Tomorrow date");
  const std::string longTitle(180,'L');timed(s,1792746000,1792749600,longTitle.c_str());assert(s.completeSource(0)==Snap::Result::Ok);
  Seeed_GFX gfx;const auto r=draw(gfx,s);assert(r.todayDrawn==2 && !r.todayOmitted && r.textShortened && r.glyphsReplaced==1);
  assert(has(gfx,"Boundary Caf?",g.today) && has(gfx,"00:00 +0200",g.today) && !has(gfx,"Tomorrow",g.today) && has(gfx,"...",g.today));}
}
void statesAndTime(){
 resetWeather();const auto g=D::ee02PreviewGeometry();
 for(int state=0;state<5;++state){Snap s;init(s,state==0?0:2);if(state==1){assert(s.beginSource(0)==Snap::Result::Ok && s.completeSource(0)==Snap::Result::Ok);assert(s.beginSource(1)==Snap::Result::Ok && s.completeSource(1)==Snap::Result::Ok);}if(state==2){failed(s,0);failed(s,1);}if(state==4){assert(s.beginSource(0)==Snap::Result::Ok && s.completeSource(0)==Snap::Result::Ok);failed(s,1);}Seeed_GFX gfx;draw(gfx,s);
  if(state==0)assert(has(gfx,"No calendars selected",g.today));
  if(state==1)assert(has(gfx,"No events today",g.today));
  if(state==2 || state==3)assert(has(gfx,"Calendar unavailable",g.today) && !has(gfx,"No events today",g.today));
  if(state==4)assert(has(gfx,"No available events today",g.today) && has(gfx,"Incomplete schedule",g.today) && !has(gfx,"No events today",g.today));}
 for(bool spring:{false,true}){const int64_t now=spring?1774744200:1792888200;Snap s;init(s,1,now);assert(s.beginSource(0)==Snap::Result::Ok);
  if(spring)timed(s,1774744200,1774747800,"Spring forward");else {timed(s,1792891800,1792891800,"Second fold");timed(s,1792888200,1792888200,"First fold");}
  assert(s.completeSource(0)==Snap::Result::Ok);auto in=inputs();in.currentUtc=now;Seeed_GFX gfx;const auto r=draw(gfx,s,in);assert(r.status==D::Status::Drawn);
  if(spring)assert(has(gfx,"01:30 +0100",g.today) && has(gfx,"03:30 +0200",g.today));
  else assert(has(gfx,"02:30 +0200",g.today) && has(gfx,"02:30 +0100",g.today) && yOf(gfx,"First fold",g.today)<yOf(gfx,"Second fold",g.today));}
}
void invalid(){
 resetWeather();Snap s;init(s,1);assert(s.beginSource(0)==Snap::Result::Ok && s.completeSource(0)==Snap::Result::Ok);
 for(int fault=0;fault<11;++fault){auto in=inputs();auto g=D::ee02PreviewGeometry();auto context=FiveDayRenderer::TimeContext{FiveDayPosixTime::convert,&zone};
  if(fault==0)in.forecast=nullptr;
  if(fault==1)in.dailyCount=SIZE_MAX;
  if(fault==2)g.weather.x=1000;
  if(fault==3)g.today.y=1160;
  if(fault==4)g.screen.width=INT_MAX;
  if(fault==5)g.dividerX=INT_MAX;
  if(fault==6)g.dailyForecast.height=80;
  if(fault==7)context.convert=nullptr;
  if(fault==8){assert(!setenv("TZ","UTC0",1));tzset();}if(fault==9)in.currentUtc=INT64_MAX;
  if(fault==10)g.calendar.width=100;
  Seeed_GFX gfx;const auto r=draw(gfx,s,in,g,context);assert(r.status!=D::Status::Drawn && gfx.glyphs.empty() && gfx.shapes.empty());
  if(fault==8){assert(!setenv("TZ",tz,1));tzset();}
 }
 Snap uninitialized;Seeed_GFX gfx;assert(draw(gfx,uninitialized).status==D::Status::InvalidSnapshot && gfx.glyphs.empty());
 // An arbitrary caller viewport/origin is restored after full composition.
 Seeed_GFX custom;custom.setViewport(123,234,400,300,true);custom.setOrigin(9,8);assert(draw(custom,s).status==D::Status::Drawn);
 assert(custom.getViewportX()==123 && custom.getViewportY()==234 && custom.getOriginX()==9 && custom.getOriginY()==8);
}
uint64_t clock(void*){return 0;}
void realIcs(){
 resetWeather();Snap s;init(s);Ee02PsramWorkspace workspace;assert(workspace.allocate(2097152).ok());
 const auto populate=[](uint8_t* p,size_t size,void* context)->GfxResult{
  auto& s=*static_cast<Snap*>(context);IcsSourceStage stage;IcsStructure structure;IcsIngestion ingestion;
  const IcsSourceStage::Limits sl={32,128,16384,32,4096};const IcsStructure::Limits pl={4096,32,32,64,8,64,128,512,32,1};
  const IcsIngestion::Limits il={32768,16384,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
  size_t sb=0,pb=0;assert(IcsSourceStage::requiredBytes(sl,sb)==IcsSourceStage::Status::InProgress && IcsStructure::scratchBytes(pl,pb)==IcsStructure::Status::InProgress && sb+pb+6144+8192<=size);
  assert(stage.initialize(p,sb,sl)==IcsSourceStage::Status::InProgress && structure.initialize(p+sb,pb,pl,IcsSourceStage::sink,&stage)==IcsStructure::Status::InProgress && ingestion.initialize(p+sb+pb,6144,il,IcsStructure::lineSink,&structure,clock,nullptr)==IcsIngestion::Status::InProgress);
  const char* body="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:Synthetic\r\nBEGIN:VEVENT\r\nUID:example\r\nDTSTAMP:20261017T120000Z\r\nDTSTART:20261023T090000Z\r\nDTEND:20261023T100000Z\r\nSUMMARY:Pipeline\r\n  title\\, complete\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
  char header[96];const int n=std::snprintf(header,sizeof(header),"HTTP/1.1 200 OK\r\nContent-Length: %u\r\n\r\n",unsigned(std::strlen(body)));assert(n>0 && size_t(n)<sizeof(header));
  assert(ingestion.feed(reinterpret_cast<const uint8_t*>(header),size_t(n))==IcsIngestion::Status::InProgress);
  for(size_t i=0;body[i];++i)assert(ingestion.feed(reinterpret_cast<const uint8_t*>(body+i),1)==IcsIngestion::Status::InProgress);
  assert(ingestion.finish()==IcsIngestion::Status::Complete && structure.finish()==IcsStructure::Status::StructurallyComplete && stage.finish(ingestion,structure)==IcsSourceStage::Status::StagedSuccessfully);
  const IcsNonrecurringSnapshot::Limits limits={{32,1,16,0,0,8,{8,32,4096},{8,8,4096,32,4096}},4096,120};
  assert(IcsNonrecurringSnapshot::publish(stage,s,0,limits,p+sb+pb+6144,8192,{p,sb+pb+6144}).status==IcsNonrecurringSnapshot::Status::SourceCommitted);
  ingestion.release();structure.release();stage.release();return GfxResult::success();
 };
 assert(workspace.withScratch(0,workspace.capacity(),populate,&s).ok());failed(s);
 assert(workspace.withScratch(0,workspace.capacity(),[](uint8_t* p,size_t n,void*){std::memset(p,0xdd,n);return GfxResult::success();}).ok());
 assert(s.occurrenceCount()==1 && s.occurrence(0)->range.timed.start==1792746000 && s.occurrence(0)->range.timed.end==1792749600);
 Seeed_GFX gfx;const auto r=draw(gfx,s);const auto g=D::ee02PreviewGeometry();assert(r.status==D::Status::Drawn && r.todayDrawn==1 && has(gfx,"Pipeline title, complete",g.today) && has(gfx,"11:00 +0200",g.today) && has(gfx,"Incomplete schedule",g.calendar));
}
}
int main(){
 static_assert(std::is_standard_layout<WeatherData>::value && std::is_trivially_copyable<WeatherData>::value,"shared POD");
 static_assert(sizeof(WeatherData)==120 && offsetof(WeatherData,temp)==104 && offsetof(WeatherData,windSpeed)==116,"original host weather layout");
 static_assert(sizeof(ForecastEntry)==20 && offsetof(ForecastEntry,tempHigh)==12,"original forecast layout");
 static_assert(sizeof(DailyForecastEntry)==56 && offsetof(DailyForecastEntry,tempHigh)==48,"original daily layout");
 allocations.reserve(256);assert(!setenv("TZ",tz,1));tzset();composition();weatherAndForecasts();batteries();todayOrdering();statesAndTime();invalid();realIcs();
 for(const auto& a:allocations)assert(!a.live);
 std::printf("Dashboard renderer: %zu compositions, immutable shared values, actual ICS pipeline, panel bounds and zero audited allocations passed\n",cases);
}
