#ifndef EE02_FIVE_DAY_DASHBOARD_RENDERER_H
#define EE02_FIVE_DAY_DASHBOARD_RENDERER_H
#include "five_day_renderer.h"
#include "weather_values.h"
#include <cmath>
#include <climits>
#include <initializer_list>

// Already-acquired, immutable inputs only. No acquisition, clock lookup, panel
// lifecycle, allocations or retained aliases. The caller excludes concurrent use.
namespace FiveDayDashboardRenderer {
using Region=FiveDayRenderer::Region;
struct Geometry {
    Region screen,calendar,weather,shortForecast,dailyForecast,today,battery;
    int32_t dividerX,leftDividerY,rightDividerY;
};
// Provisional retained coordinates, not final month-grid/agenda policy. The
// production right divider is height/2-30 (570), despite older 600-area comments.
inline Geometry ee02PreviewGeometry() {
    return {{0,0,1600,1200},{10,10,1025,1025},{1054,10,526,410},
        {1054,430,526,130},{10,1054,1024,130},{1063,970,504,180},
        {1452,1160,134,24},1044,1044,570};
}
struct Battery { bool valid; int percentage; };
struct Inputs {
    const WeatherData* weather; // Null means unavailable.
    const ForecastEntry* forecast; size_t forecastCount;
    const DailyForecastEntry* daily; size_t dailyCount;
    Battery battery;
    bool currentTimeValid; int64_t currentUtc; // Explicit observation, never time().
};
enum class Status { Drawn, InvalidArgument, InvalidGeometry, InvalidSnapshot, InvalidTimeContext };
struct Report {
    Status status;
    FiveDayRenderer::Report calendar;
    size_t todayDrawn,todayOmitted,textShortened,glyphsReplaced,iconFallbacks;
    size_t shortDrawn,shortUnavailable,shortOmitted,dailyDrawn,dailyUnavailable,dailyOmitted;
    bool weatherUnavailable,batteryUnavailable,batteryClamped;
};
namespace detail {
namespace C=FiveDayRenderer::detail;
inline bool span(const void* p,size_t n,size_t width) {
    return n<=size_t(PTRDIFF_MAX)/width && (!n || p) &&
        (!p || n*width<=UINTPTR_MAX-reinterpret_cast<uintptr_t>(p));
}
inline bool inside(Region r,Region outer) {
    return r.width>0 && r.height>0 && r.x>=outer.x && r.y>=outer.y &&
        int64_t(r.x)+r.width<=int64_t(outer.x)+outer.width &&
        int64_t(r.y)+r.height<=int64_t(outer.y)+outer.height;
}
inline bool overlaps(Region a,Region b) {
    return int64_t(a.x)+a.width>b.x && int64_t(b.x)+b.width>a.x &&
        int64_t(a.y)+a.height>b.y && int64_t(b.y)+b.height>a.y;
}
inline bool geometry(const Geometry& g,const Seeed_GFX& gfx) {
    if(!inside(g.screen,{0,0,gfx.width(),gfx.height()}) || g.screen.width<40 || g.screen.height<40) return false;
    const Region panels[]={g.calendar,g.weather,g.shortForecast,g.dailyForecast,g.today,g.battery};
    for(size_t i=0;i<6;++i) {
        if(!inside(panels[i],g.screen)) return false;
        for(size_t j=0;j<i;++j) if(overlaps(panels[i],panels[j])) return false;
    }
    if(g.dividerX<=g.screen.x+10 || g.dividerX>=g.screen.x+g.screen.width-20 ||
       g.leftDividerY<=g.screen.y+10 || g.leftDividerY>=g.screen.y+g.screen.height-20 ||
       g.rightDividerY<=g.screen.y+10 || g.rightDividerY>=g.screen.y+g.screen.height-20) return false;
    if(int64_t(g.calendar.x)+g.calendar.width>g.dividerX || int64_t(g.calendar.y)+g.calendar.height>g.leftDividerY ||
       int64_t(g.dailyForecast.x)+g.dailyForecast.width>g.dividerX || g.dailyForecast.y<=g.leftDividerY) return false;
    for(Region r:{g.weather,g.shortForecast,g.today,g.battery}) if(r.x<=g.dividerX) return false;
    return int64_t(g.weather.y)+g.weather.height<=g.shortForecast.y &&
        int64_t(g.shortForecast.y)+g.shortForecast.height<=g.rightDividerY &&
        g.today.y>g.rightDividerY && g.battery.y>g.rightDividerY &&
        g.weather.width>=400 && g.weather.height>=370 && g.shortForecast.width>=100 && g.shortForecast.height>=120 &&
        g.dailyForecast.width>=180 && g.dailyForecast.height>=120 && g.today.width>=480 && g.today.height>=88 &&
        g.battery.width>=134 && g.battery.height>=24;
}
inline void label(Seeed_GFX& gfx,FiveDaySnapshot::Text t,Region r,int32_t dy,uint8_t scale,Report& report) {
    if(C::label(gfx,t,r.x+4,r.y+dy,size_t((r.width-8)/(6*scale)),1,scale,TFT_BLACK,report.glyphsReplaced)) ++report.textShortened;
}
inline void label(Seeed_GFX& gfx,const char* s,Region r,int32_t dy,uint8_t scale,Report& report) {
    label(gfx,C::text(s),r,dy,scale,report);
}
// Weather arrays can be full or malformed. Consume a complete valid UTF-8 scalar
// as one unsupported glyph, or one invalid byte as '?'; never skip following ASCII.
template<size_t N> inline void field(Seeed_GFX& gfx,const char (&value)[N],Region r,int32_t dy,uint8_t scale,Report& report) {
    char ascii[N]; size_t bytes=0,used=0;
    while(bytes<N && value[bytes]) {
        const uint8_t c=uint8_t(value[bytes]); size_t step=1; char glyph=char(c);
        if(c>=128) {
            size_t length=c>=0xc2 && c<=0xdf?2:c>=0xe0 && c<=0xef?3:c>=0xf0 && c<=0xf4?4:1;
            bool valid=length>1 && length<=N-bytes;
            for(size_t i=1;i<length && valid;++i) valid=(uint8_t(value[bytes+i])&0xc0)==0x80;
            if(valid && length>=3) {
                const uint8_t b=uint8_t(value[bytes+1]);
                valid=!(c==0xe0 && b<0xa0) && !(c==0xed && b>=0xa0) && !(c==0xf0 && b<0x90) && !(c==0xf4 && b>=0x90);
            }
            if(valid) step=length;
            glyph='?'; ++report.glyphsReplaced;
        } else if(c<32 || c==127) glyph=' ';
        ascii[used++]=glyph; bytes+=step;
    }
    label(gfx,{ascii,used},r,dy,scale,report);
}
inline const char* condition(const char (&icon)[4],bool& night,int& code) {
    night=icon[2]=='n'; code=-1;
    if(icon[3] || (icon[2]!='d' && icon[2]!='n') || icon[0]<'0' || icon[0]>'9' || icon[1]<'0' || icon[1]>'9') return "Unknown";
    code=(icon[0]-'0')*10+icon[1]-'0';
    switch(code) {
    case 1:return "Clear";case 2:return "Partly cloudy";case 3:return "Cloudy";case 4:return "Overcast";
    case 9:return "Showers";case 10:return "Rain";case 11:return "Thunderstorm";case 13:return "Snow";case 50:return night?"Fog":"Haze";
    default:code=-1;return "Unknown";
    }
}
inline void icon(Seeed_GFX& gfx,const char (&value)[4],Region r,int32_t dy,Report& report) {
    bool night=false;int code=-1;const char* name=condition(value,night,code);
    const int32_t x=r.x+20,y=r.y+dy+16;
    if(code<0) { ++report.iconFallbacks; gfx.drawRect(x-14,y-14,29,29,TFT_BLACK); gfx.drawChar(x-3,y-4,'?',TFT_BLACK,TFT_WHITE,1); }
    else {
        if(code==1 || code==2) {
            gfx.drawCircle(x,y-4,9,TFT_BLACK);
            if(night) gfx.fillCircle(x+4,y-7,7,TFT_WHITE);
            else { gfx.drawLine(x-14,y-4,x+14,y-4,TFT_BLACK);gfx.drawLine(x,y-18,x,y+10,TFT_BLACK); }
        }
        if(code!=1) { gfx.drawCircle(x-6,y,6,TFT_BLACK);gfx.drawCircle(x+4,y-3,9,TFT_BLACK);gfx.drawLine(x-12,y+6,x+13,y+6,TFT_BLACK); }
        if(code==9 || code==10) for(int i=-8;i<10;i+=8) gfx.drawLine(x+i,y+10,x+i-2,y+17,TFT_BLACK);
        if(code==11) { gfx.drawLine(x+2,y+8,x-4,y+13,TFT_BLACK);gfx.drawLine(x-4,y+13,x+3,y+13,TFT_BLACK);gfx.drawLine(x+3,y+13,x-2,y+18,TFT_BLACK); }
        if(code==13) { gfx.drawLine(x-6,y+13,x+6,y+13,TFT_BLACK);gfx.drawLine(x,y+8,x,y+18,TFT_BLACK); }
        if(code==50 || code==4) { gfx.drawLine(x-14,y+11,x+14,y+11,TFT_BLACK);gfx.drawLine(x-14,y+16,x+14,y+16,TFT_BLACK); }
    }
    char description[40];std::snprintf(description,sizeof(description),"%s%s",name,code>=0 && night?" night":"");
    label(gfx,description,r,dy+40,1,report);
}
inline bool temperatures(float high,float low) { return std::isfinite(high) && std::isfinite(low) && high>=low; }
inline bool forecastTime(const char (&s)[6]) {
    return s[0]>='0' && s[0]<='2' && s[1]>='0' && s[1]<='9' && (s[0]!='2' || s[1]<='3') &&
        s[2]==':' && s[3]>='0' && s[3]<='5' && s[4]>='0' && s[4]<='9' && !s[5];
}
inline bool range(const FiveDaySnapshot::Occurrence& e,FiveDayRenderer::TimeContext zone,char (&out)[160]) {
    char a[32],b[32],start[32],end[32];
    if(e.kind==FiveDaySnapshot::Kind::AllDay) {
        C::date(e.range.civil.start,a,sizeof(a));C::date(e.range.civil.end,b,sizeof(b));
        std::snprintf(out,sizeof(out),"All day %s to %s (end exclusive)",a,b);
    } else {
        FiveDayRenderer::LocalTime x={},y={};
        if(!C::local(zone,e.range.timed.start,x) || !C::local(zone,e.range.timed.end,y)) return false;
        C::date(x.civilDay,a,sizeof(a));C::date(y.civilDay,b,sizeof(b));C::time(x,start,sizeof(start));C::time(y,end,sizeof(end));
        if(e.range.timed.start==e.range.timed.end) std::snprintf(out,sizeof(out),"%s %s Point",a,start);
        else std::snprintf(out,sizeof(out),"%s %s to %s %s",a,start,b,end);
    }
    return true;
}
}
inline Report render(Seeed_GFX& gfx,const FiveDaySnapshot& snapshot,FiveDayRenderer::TimeContext zone,
    const Inputs& inputs,const Geometry& g) {
    namespace C=FiveDayRenderer::detail;using namespace detail;
    Report r={};r.status=Status::InvalidArgument;
    if((inputs.weather && (!span(inputs.weather,1,sizeof(WeatherData)) || reinterpret_cast<uintptr_t>(inputs.weather)%alignof(WeatherData))) ||
       !span(inputs.forecast,inputs.forecastCount,sizeof(ForecastEntry)) || !span(inputs.daily,inputs.dailyCount,sizeof(DailyForecastEntry)) ||
       (inputs.forecastCount && reinterpret_cast<uintptr_t>(inputs.forecast)%alignof(ForecastEntry)) ||
       (inputs.dailyCount && reinterpret_cast<uintptr_t>(inputs.daily)%alignof(DailyForecastEntry))) return r;
    r.status=Status::InvalidGeometry;if(!geometry(g,gfx)) return r;
    r.status=Status::InvalidSnapshot;if(!snapshot.initialized() || !snapshot.window()) return r;
    FiveDayRenderer::LocalTime current={};r.status=Status::InvalidTimeContext;
    if(inputs.currentTimeValid && !C::local(zone,inputs.currentUtc,current)) return r;
    // Validate detailed temporal labels before any drawing. Use the exact same
    // overlap/conversion/order helpers as the five-day agenda, no event catalogue.
    for(size_t i=0;i<snapshot.occurrenceCount();++i) if(C::overlaps(*snapshot.occurrence(i),*snapshot.window(),0)) {
        char value[160];if(!range(*snapshot.occurrence(i),zone,value)) return r;
        if(std::strlen(value)>size_t((g.today.width-8)/6)) {r.status=Status::InvalidGeometry;return r;}
    }
    r.calendar=FiveDayRenderer::render(gfx,snapshot,g.calendar,zone);
    if(r.calendar.status!=FiveDayRenderer::Status::Drawn) {
        r.status=r.calendar.status==FiveDayRenderer::Status::InvalidSnapshot?Status::InvalidSnapshot:
            r.calendar.status==FiveDayRenderer::Status::InvalidRegion?Status::InvalidGeometry:Status::InvalidTimeContext;return r;
    }
    C::ViewportScope screen(gfx,g.screen);gfx.setFreeFont(nullptr);
    // Clear only outside the already drawn calendar, including the provisional
    // unassigned month area. Admission failures above emit no graphics.
    const auto clear=[&](Region a){if(a.width>0 && a.height>0) gfx.fillRect(a.x,a.y,a.width,a.height,TFT_WHITE);};
    clear({g.screen.x,g.screen.y,g.screen.width,g.calendar.y-g.screen.y});
    clear({g.screen.x,g.calendar.y+g.calendar.height,g.screen.width,g.screen.y+g.screen.height-g.calendar.y-g.calendar.height});
    clear({g.screen.x,g.calendar.y,g.calendar.x-g.screen.x,g.calendar.height});
    clear({g.calendar.x+g.calendar.width,g.calendar.y,g.screen.x+g.screen.width-g.calendar.x-g.calendar.width,g.calendar.height});
    gfx.drawLine(g.dividerX,g.screen.y+10,g.dividerX,g.screen.y+g.screen.height-20,TFT_BLACK);
    gfx.drawLine(g.screen.x+10,g.leftDividerY,g.dividerX-10,g.leftDividerY,TFT_BLACK);
    gfx.drawLine(g.dividerX+10,g.rightDividerY,g.screen.x+g.screen.width-20,g.rightDividerY,TFT_BLACK);
    { C::ViewportScope panel(gfx,g.weather);label(gfx,"Weather",g.weather,4,2,r);
      char value[128];
      if(inputs.currentTimeValid) {char date[32],time[32];C::date(current.civilDay,date,sizeof(date));C::time(current,time,sizeof(time));
          const char* weekdays[]={"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
          std::snprintf(value,sizeof(value),"%s %s %s",weekdays[(int64_t(current.civilDay)%7+11)%7],date,time);label(gfx,value,g.weather,28,1,r);}
      else label(gfx,"Current time unavailable",g.weather,28,1,r);
      const auto* w=inputs.weather;
      r.weatherUnavailable=!w || !w->valid || !std::isfinite(w->temp) || w->humidity<0 || w->humidity>100 ||
          !std::isfinite(w->rain) || w->rain<0 || !std::isfinite(w->windSpeed) || w->windSpeed<0;
      if(r.weatherUnavailable) label(gfx,"Weather unavailable",g.weather,70,2,r);
      else {
          icon(gfx,w->icon,g.weather,70,r);field(gfx,w->description,g.weather,132,2,r);
          std::snprintf(value,sizeof(value),"Temperature: %.1f C",double(w->temp));label(gfx,value,g.weather,164,2,r);
          std::snprintf(value,sizeof(value),"Humidity: %d%%",w->humidity);label(gfx,value,g.weather,196,2,r);
          std::snprintf(value,sizeof(value),"Rain: %.1f mm (reported)",double(w->rain));label(gfx,value,g.weather,228,1,r);
          std::snprintf(value,sizeof(value),"Wind: %.1f m/s",double(w->windSpeed));label(gfx,value,g.weather,252,1,r);
      }
      if(w && w->valid && w->fetchTime[0]) {label(gfx,"Fetch time (as supplied):",g.weather,294,1,r);field(gfx,w->fetchTime,g.weather,310,1,r);}
      else label(gfx,"Fetch time unavailable",g.weather,310,1,r);
      label(gfx,"Freshness not assessed",g.weather,342,1,r);
    }
    // Supplied order/labels are used as acquired. No forecast grouping, date
    // conversion, interpolation or persistence/freshness policy is introduced.
    for(bool daily:{false,true}) {
        const Region area=daily?g.dailyForecast:g.shortForecast;
        C::ViewportScope panel(gfx,area);const size_t count=daily?inputs.dailyCount:inputs.forecastCount;
        const size_t columns=size_t(area.width/(daily?180:100));const size_t visible=count<columns?count:columns;
        label(gfx,daily?"Five-day forecast (high/low C)":"Short forecast (high/low C)",area,0,1,r);
        if(!count) label(gfx,"Forecast unavailable",area,20,1,r);
        for(size_t i=0;i<visible;++i) {
            Region cell={area.x+int32_t(i)*int32_t(area.width/int32_t(columns)),area.y,area.width/int32_t(columns),area.height};
            if(i) gfx.drawLine(cell.x,cell.y+16,cell.x,cell.y+cell.height-12,TFT_BLACK);
            const bool valid=daily?inputs.daily[i].valid && inputs.daily[i].dayName[0] && temperatures(inputs.daily[i].tempHigh,inputs.daily[i].tempLow):
                inputs.forecast[i].valid && forecastTime(inputs.forecast[i].timeStr) && temperatures(inputs.forecast[i].tempHigh,inputs.forecast[i].tempLow);
            if(!valid) {label(gfx,"N/A",cell,24,1,r);if(daily)++r.dailyUnavailable;else ++r.shortUnavailable;continue;}
            if(daily) {field(gfx,inputs.daily[i].dayName,cell,16,1,r);icon(gfx,inputs.daily[i].icon,cell,32,r);}
            else {field(gfx,inputs.forecast[i].timeStr,cell,16,1,r);icon(gfx,inputs.forecast[i].icon,cell,32,r);}
            char value[128];std::snprintf(value,sizeof(value),"%.0f/%.0f C",double(daily?inputs.daily[i].tempHigh:inputs.forecast[i].tempHigh),double(daily?inputs.daily[i].tempLow:inputs.forecast[i].tempLow));
            label(gfx,value,cell,88,1,r);
            if(daily) {field(gfx,inputs.daily[i].description,cell,100,1,r);++r.dailyDrawn;}else ++r.shortDrawn;
        }
        if(daily)r.dailyOmitted=count-visible;else r.shortOmitted=count-visible;
        if(count>visible) {char value[64];std::snprintf(value,sizeof(value),"+%lu more forecasts",static_cast<unsigned long>(count-visible));label(gfx,value,area,area.height-8,1,r);}
    }
    { C::ViewportScope panel(gfx,g.today);
      const int32_t divider=g.today.x+(g.today.width-200)/2-15;
      gfx.drawLine(divider,g.today.y,divider+200,g.today.y,TFT_BLACK);
      label(gfx,"Today - detailed events",g.today,4,2,r);
      const bool complete=r.calendar.completeSources>0;
      label(gfx,!snapshot.sourceCount()?"No calendars selected":!complete?"Calendar unavailable":r.calendar.incompleteSources || r.calendar.pendingSources?"Incomplete schedule - available events":"All sources complete",g.today,22,1,r);
      size_t count=0;for(size_t i=0;i<snapshot.occurrenceCount();++i)if(C::overlaps(*snapshot.occurrence(i),*snapshot.window(),0))++count;
      const size_t rows=size_t((g.today.height-52)/36),visible=count<rows?count:rows;
      size_t previous=SIZE_MAX;
      for(size_t row=0;row<visible;++row) {
          size_t next=SIZE_MAX;
          for(size_t i=0;i<snapshot.occurrenceCount();++i) if(C::overlaps(*snapshot.occurrence(i),*snapshot.window(),0) &&
              (previous==SIZE_MAX || C::before(snapshot,previous,i)) && (next==SIZE_MAX || C::before(snapshot,i,next))) next=i;
          const int32_t y=36+int32_t(row)*36;char value[160];range(*snapshot.occurrence(next),zone,value);
          label(gfx,snapshot.title(next),g.today,y,2,r);label(gfx,value,g.today,y+16,1,r);
          label(gfx,snapshot.sourceLabel(snapshot.occurrence(next)->sourceIndex),g.today,y+24,1,r);
          previous=next;++r.todayDrawn;
      }
      r.todayOmitted=count-visible;
      if(r.todayOmitted) {char value[48];std::snprintf(value,sizeof(value),"+%lu more events",static_cast<unsigned long>(r.todayOmitted));label(gfx,value,g.today,g.today.height-8,1,r);}
      else if(!count && complete) label(gfx,r.calendar.completeSources==snapshot.sourceCount()?"No events today":"No available events today",g.today,44,1,r);
    }
    { C::ViewportScope panel(gfx,g.battery);r.batteryUnavailable=!inputs.battery.valid;
      const int percentage=inputs.battery.percentage<0?0:inputs.battery.percentage>100?100:inputs.battery.percentage;
      r.batteryClamped=inputs.battery.valid && percentage!=inputs.battery.percentage;
      const int32_t x=g.battery.x+g.battery.width-54,y=g.battery.y;
      gfx.drawRect(x,y,50,22,TFT_BLACK);gfx.fillRect(x+50,y+5,4,12,TFT_BLACK);
      const int level=44*percentage/100;
      if(inputs.battery.valid && level) gfx.fillRect(x+3,y+3,level,16,TFT_BLACK);
      char value[16];if(inputs.battery.valid)std::snprintf(value,sizeof(value),"%d%%",percentage);else std::snprintf(value,sizeof(value),"N/A");
      Region caption={g.battery.x,g.battery.y,g.battery.width-62,g.battery.height};label(gfx,value,caption,4,2,r);
    }
    r.status=Status::Drawn;return r;
}
}
#endif
