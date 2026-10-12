#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <type_traits>
#include <vector>
#include "../application/five_day_snapshot.h"

using Snapshot = FiveDaySnapshot;
using Result = Snapshot::Result;
using Reason = Snapshot::Reason;
using Outcome = Snapshot::Outcome;
namespace {
bool psram = true, failAllocation = false, badAddress = false, misalign = false;
size_t attempts = 0, frees = 0, badFrees = 0, totalBytes = 0;
struct Allocation { uint8_t* raw; uint8_t* p; size_t prefix; size_t bytes; bool live; };
std::vector<Allocation> allocations;
void guards(const Allocation& a) {
    for (size_t i=0;i<a.prefix;++i) assert(a.raw[i]==0xa5);
    for (size_t i=0;i<16;++i) assert(a.p[a.bytes+i]==0xa5);
}
void expect(Result r,Result expected) {
    if (r!=expected) std::fprintf(stderr,"snapshot result %u, expected %u\n",
        static_cast<unsigned>(r),static_cast<unsigned>(expected));
    assert(r==expected);
}
Snapshot::Text text(const char* s) { return {s,std::strlen(s)}; }
Snapshot::Window window(int32_t day=0,int64_t first=0,int64_t specialDaySeconds=86400) {
    Snapshot::Window w = {};
    w.firstCivilDay=day; w.utcBoundaries[0]=first;
    for (size_t i=1;i<6;++i) w.utcBoundaries[i]=w.utcBoundaries[i-1]+(i==2 ? specialDaySeconds : 86400);
    return w;
}
Snapshot::Input timed(int64_t start,int64_t end,Snapshot::Text title=text("event")) {
    Snapshot::Input i = {}; i.kind=Snapshot::Kind::Timed;
    i.range.timed={start,end}; i.title=title; return i;
}
Snapshot::Input civil(int32_t start,int32_t end,Snapshot::Text title=text("all day")) {
    Snapshot::Input i = {}; i.kind=Snapshot::Kind::AllDay;
    i.range.civil={start,end}; i.title=title; return i;
}
void isText(Snapshot::Text actual,Snapshot::Text expected) {
    assert(actual.data && actual.size==expected.size);
    assert(!std::memcmp(actual.data,expected.data,actual.size));
    assert(actual.data[actual.size]==0);
}
}
extern "C" void __real_free(void*);
extern "C" void __wrap_free(void* p) {
    if (p==reinterpret_cast<void*>(UINTPTR_MAX-8)) { ++badFrees; return; }
    for (auto it=allocations.rbegin();it!=allocations.rend();++it) {
        if (it->p!=p) continue;
        assert(it->live); guards(*it); it->live=false; ++frees; __real_free(it->raw); return;
    }
    __real_free(p);
}
bool psramFound() { return psram; }
void* ps_malloc(size_t n) {
    ++attempts; totalBytes=n;
    if (failAllocation) return nullptr;
    if (badAddress) return reinterpret_cast<void*>(UINTPTR_MAX-8);
    const size_t prefix=misalign ? 17 : 16;
    auto* raw=static_cast<uint8_t*>(std::malloc(prefix+n+16)); assert(raw);
    std::memset(raw,0xa5,prefix+n+16);
    allocations.push_back({raw,raw+prefix,prefix,n,true}); return raw+prefix;
}
namespace {
void initialization() {
    Snapshot s; const auto w=window(); const Snapshot::Text labels[]={text("same"),text("same")};
    const size_t before=attempts;
    expect(s.beginSource(0),Result::NotInitialized); assert(!s.window() && !s.source(0));
    expect(s.initialize(w,4,64,nullptr,1),Result::InvalidSource);
    expect(s.initialize(w,SIZE_MAX,64,labels,2),Result::InvalidCapacity);
    expect(s.initialize(w,4,SIZE_MAX,labels,2),Result::InvalidCapacity);
    expect(s.initialize(w,4,64,labels,static_cast<size_t>(UINT16_MAX)+2),Result::InvalidCapacity);
    expect(s.initialize(w,4,64,reinterpret_cast<const Snapshot::Text*>(UINTPTR_MAX-4),1),Result::Overflow);
    auto invalid=w; invalid.firstCivilDay=INT32_MAX-4;
    expect(s.initialize(invalid,4,64,labels,2),Result::InvalidWindow);
    invalid=w; invalid.utcBoundaries[2]=invalid.utcBoundaries[1];
    expect(s.initialize(invalid,4,64,labels,2),Result::InvalidWindow);
    invalid=w; invalid.utcBoundaries[3]=INT64_MIN;
    expect(s.initialize(invalid,4,64,labels,2),Result::InvalidWindow);
    assert(attempts==before);
    psram=false; expect(s.initialize(w,4,64,labels,2),Result::AllocationFailed); assert(attempts==before);
    psram=true; failAllocation=true;
    expect(s.initialize(w,4,64,labels,2),Result::AllocationFailed); assert(!s.initialized());
    failAllocation=false; badAddress=true;
    expect(s.initialize(w,4,64,labels,2),Result::Overflow); assert(badFrees==1 && !s.initialized());
    badAddress=false; misalign=true;
    expect(s.initialize(w,4,64,labels,2),Result::Overflow); assert(!s.initialized()); misalign=false;
    // Width-bounded layout can exceed target address space without overflowing
    // size_t on a 64-bit host. Inject failure, never request a real huge buffer.
    failAllocation=true;
    const auto huge=s.initialize(w,UINT32_MAX,64,labels,2);
    assert(huge==Result::Overflow || huge==Result::AllocationFailed);
    failAllocation=false;
    expect(s.initialize(w,4,64,labels,2),Result::Ok);
    assert(s.allocationBytes()==56+4*24+2*16+64 && totalBytes==s.allocationBytes());
    assert(s.sourceCount()==2 && s.occurrenceCount()==0 && !s.occurrence(0));
    for(size_t i=0;i<2;++i) { assert(s.source(i)->outcome==Outcome::Pending); isText(s.sourceLabel(i),labels[i]); }
    assert(!s.source(2) && !s.sourceLabel(2).data && !s.title(0).data);
    expect(s.initialize(w,4,64,labels,2),Result::InvalidState);
    expect(s.beginSource(0),Result::Ok); expect(s.release(),Result::InvalidState);
    expect(s.completeSource(0),Result::Ok);
    assert(s.source(0)->outcome==Outcome::Complete && s.source(0)->occurrenceCount==0);
    expect(s.beginSource(0),Result::InvalidState); expect(s.completeSource(0),Result::InvalidState);
    expect(s.failSource(0,Reason::Cancelled),Result::InvalidState);
    expect(s.beginSource(2),Result::InvalidSource);
    expect(s.beginSource(1),Result::Ok);
    expect(s.failSource(1,Reason::None),Result::InvalidReason);
    expect(s.failSource(1,static_cast<Reason>(65535)),Result::InvalidReason);
    expect(s.failSource(1,Reason::ProducerFailure),Result::Ok);
    assert(s.source(1)->outcome==Outcome::Incomplete);
    expect(s.release(),Result::Ok); expect(s.release(),Result::Ok);
    assert(!s.initialized() && s.sourceCount()==0 && s.allocationBytes()==0);
    expect(s.initialize(w,0,0,nullptr,0),Result::Ok); // No UI/configuration policy implied.
}
void publicationAndRollback() {
    Snapshot s; const auto w=window(); const Snapshot::Text labels[]={text("A"),text("B"),text("A"),text("D")};
    expect(s.initialize(w,8,80,labels,4),Result::Ok);
    const size_t allocated=attempts;
    expect(s.beginSource(0),Result::Ok);
    expect(s.beginSource(1),Result::InvalidState);
    expect(s.addOccurrence(0,timed(1,2,text("first"))),Result::Ok);
    assert(s.occurrenceCount()==0 && !s.occurrence(0) && !s.title(0).data);
    assert(s.source(0)->outcome==Outcome::Pending && s.source(0)->occurrenceCount==0);
    expect(s.completeSource(0),Result::Ok);
    const auto saved=*s.occurrence(0); const auto savedSource=*s.source(0);
    const char* savedText=s.title(0).data;
    expect(s.beginSource(1),Result::Ok);
    expect(s.addOccurrence(1,timed(3,4,text("rollback"))),Result::Ok);
    expect(s.addOccurrence(1,civil(0,2,text("also rollback"))),Result::Ok);
    assert(s.occurrenceCount()==1 && !s.occurrence(1));
    expect(s.failSource(0,Reason::Cancelled),Result::InvalidState);
    expect(s.failSource(1,Reason::Cancelled),Result::Ok);
    assert(s.source(1)->outcome==Outcome::Incomplete && s.source(1)->occurrenceCount==0);
    assert(s.source(1)->reason==Reason::Cancelled && s.occurrenceCount()==1);
    assert(!std::memcmp(s.occurrence(0),&saved,sizeof saved));
    assert(!std::memcmp(s.source(0),&savedSource,sizeof savedSource));
    assert(s.title(0).data==savedText); isText(s.title(0),text("first"));
    expect(s.beginSource(2),Result::Ok);
    // Identical display values (within and across sources) remain distinct.
    expect(s.addOccurrence(2,timed(1,2,text("first"))),Result::Ok);
    expect(s.addOccurrence(2,timed(1,2,text("first"))),Result::Ok);
    expect(s.completeSource(2),Result::Ok);
    assert(s.occurrenceCount()==3 && s.occurrence(1)->sourceIndex==2 && s.occurrence(2)->sourceIndex==2);
    assert(s.source(2)->firstOccurrence==1 && s.source(2)->occurrenceCount==2);
    expect(s.beginSource(3),Result::Ok); expect(s.completeSource(3),Result::Ok);
    assert(s.source(3)->outcome==Outcome::Complete && s.source(3)->occurrenceCount==0);
    assert(attempts==allocated); // No allocation at add/commit/rollback/read.
}
void misaddressedAddAndIndexWidth() {
    Snapshot s; const Snapshot::Text labels[]={text("A"),text("B")};
    expect(s.initialize(window(),2,64,labels,2),Result::Ok);
    expect(s.beginSource(0),Result::Ok);
    expect(s.addOccurrence(0,timed(1,2)),Result::Ok);
    expect(s.addOccurrence(1,timed(1,2)),Result::InvalidState);
    assert(s.source(0)->outcome==Outcome::Incomplete && s.occurrenceCount()==0);
    expect(s.completeSource(0),Result::InvalidState);
    expect(s.beginSource(1),Result::Ok);
    expect(s.addOccurrence(SIZE_MAX,timed(1,2)),Result::InvalidSource);
    expect(s.completeSource(1),Result::InvalidState);
    expect(s.release(),Result::Ok);
    // Every uint16_t source index is usable; this is a representation bound,
    // not a selected calendar-count quota.
    const size_t count=static_cast<size_t>(UINT16_MAX)+1;
    const std::vector<Snapshot::Text> catalogue(count,text("x"));
    expect(s.initialize(window(),1,2*count+2,catalogue.data(),count),Result::Ok);
    expect(s.beginSource(UINT16_MAX),Result::Ok);
    expect(s.addOccurrence(UINT16_MAX,timed(1,2,text(""))),Result::Ok);
    expect(s.completeSource(UINT16_MAX),Result::Ok);
    assert(s.occurrence(0)->sourceIndex==UINT16_MAX);
}
void intervals() {
    const Snapshot::Text labels[]={text("intervals")};
    // Supplied Europe/Berlin dates around Mar 29 and Oct 25, 2026. No timezone
    // resolver is tested: these explicitly supplied days have 23/25 hours.
    for (const auto& w : {window(20540,1774652400,23*3600),window(20750,1792792800,25*3600)}) {
        Snapshot s; expect(s.initialize(w,20,256,labels,1),Result::Ok);
        expect(s.beginSource(0),Result::Ok);
        const int64_t start=w.utcBoundaries[0],end=w.utcBoundaries[5];
        expect(s.addOccurrence(0,timed(start-100,start)),Result::OutsideWindow);
        expect(s.addOccurrence(0,timed(end,end+100)),Result::OutsideWindow);
        expect(s.addOccurrence(0,timed(start-1,start-1)),Result::OutsideWindow);
        expect(s.addOccurrence(0,timed(end,end)),Result::OutsideWindow);
        expect(s.addOccurrence(0,timed(start,start,{nullptr,0})),Result::Ok);
        expect(s.addOccurrence(0,timed(end-1,end-1)),Result::Ok);
        expect(s.addOccurrence(0,timed(start-100,end+100)),Result::Ok);
        expect(s.addOccurrence(0,timed(w.utcBoundaries[1]-10,w.utcBoundaries[1]+10)),Result::Ok);
        expect(s.addOccurrence(0,civil(w.firstCivilDay-2,w.firstCivilDay)),Result::OutsideWindow);
        expect(s.addOccurrence(0,civil(w.firstCivilDay+5,w.firstCivilDay+6)),Result::OutsideWindow);
        expect(s.addOccurrence(0,civil(w.firstCivilDay,w.firstCivilDay+1)),Result::Ok);
        expect(s.addOccurrence(0,civil(w.firstCivilDay-2,w.firstCivilDay+6)),Result::Ok);
        expect(s.completeSource(0),Result::Ok);
        assert(s.occurrenceCount()==6 && s.title(0).size==0 && s.title(0).data);
        assert(s.occurrence(2)->range.timed.start==start-100 && s.occurrence(2)->range.timed.end==end+100);
        assert(s.occurrence(5)->range.civil.start==w.firstCivilDay-2 && s.occurrence(5)->range.civil.end==w.firstCivilDay+6);
        assert(s.window()->utcBoundaries[2]-s.window()->utcBoundaries[1]==(w.firstCivilDay==20540 ? 23 : 25)*3600);
    }
    Snapshot s; const auto w=window(); expect(s.initialize(w,1,32,labels,1),Result::Ok);
    expect(s.beginSource(0),Result::Ok);
    expect(s.addOccurrence(0,timed(INT64_MIN,INT64_MAX,text("extreme"))),Result::Ok); // Compare, never subtract duration.
    expect(s.completeSource(0),Result::Ok);
}
void invalidInput() {
    const Snapshot::Text labels[]={text("bad")}; const auto w=window();
    std::vector<Snapshot::Input> invalids={timed(2,1),civil(0,0),civil(2,1),civil(INT32_MAX,INT32_MIN)};
    auto unknown=timed(1,2); unknown.kind=static_cast<Snapshot::Kind>(99); invalids.push_back(unknown);
    for(const auto& input:invalids) {
        Snapshot s; expect(s.initialize(w,4,64,labels,1),Result::Ok);
        expect(s.beginSource(0),Result::Ok); expect(s.addOccurrence(0,timed(1,2)),Result::Ok);
        expect(s.addOccurrence(0,input),Result::InvalidOccurrence);
        assert(s.occurrenceCount()==0 && s.source(0)->reason==Reason::InvalidOccurrence);
        expect(s.completeSource(0),Result::InvalidState); // Ignoring add error cannot commit.
    }
    const char nul[]={'a',0,'b'};
    const std::string badUtf8[]={std::string("\x80",1),std::string("\xc0\xaf",2),std::string("\xe0\x80\x80",3),
        std::string("\xed\xa0\x80",3),std::string("\xf4\x90\x80\x80",4),std::string("\xf5\x80\x80\x80",4),
        std::string("\xc2",1),std::string("\xe2\x28\xa1",3),std::string("\xf0\x90\x80\x41",4)};
    std::vector<Snapshot::Text> badTexts={{nul,3},{nullptr,1},{"x",SIZE_MAX},
        {reinterpret_cast<const char*>(UINTPTR_MAX-1),4}};
    for(const auto& str:badUtf8) badTexts.push_back({str.data(),str.size()});
    for(const auto& t:badTexts) {
        Snapshot s; expect(s.initialize(w,4,64,labels,1),Result::Ok);
        expect(s.beginSource(0),Result::Ok); expect(s.addOccurrence(0,timed(1,2,t)),Result::InvalidText);
        assert(s.source(0)->outcome==Outcome::Incomplete && s.occurrenceCount()==0);
        expect(s.completeSource(0),Result::InvalidState);
        Snapshot badCatalogue; expect(badCatalogue.initialize(w,4,64,&t,1),Result::InvalidText);
        assert(!badCatalogue.initialized());
    }
    Snapshot s; const Snapshot::Text empty={nullptr,0};
    expect(s.initialize(w,4,64,&empty,1),Result::InvalidText);
    const Snapshot::Text longLabel=text("too long"); expect(s.initialize(w,4,2,&longLabel,1),Result::TextCapacity);
}
void exhaustion() {
    const auto w=window(); const Snapshot::Text labels[]={text("A"),text("B"),text("C")};
    for(bool recordsFull:{true,false}) {
        Snapshot s; expect(s.initialize(w,recordsFull ? 2 : 8,recordsFull ? 128 : 16,labels,3),Result::Ok);
        expect(s.beginSource(0),Result::Ok); expect(s.addOccurrence(0,timed(1,2,text("a"))),Result::Ok);
        expect(s.completeSource(0),Result::Ok);
        expect(s.beginSource(1),Result::Ok); expect(s.addOccurrence(1,timed(3,4,text("temp"))),Result::Ok);
        const auto rejected=timed(5,6,text(recordsFull ? "other" : "overflow"));
        expect(s.addOccurrence(1,rejected),recordsFull ? Result::RecordCapacity : Result::TextCapacity);
        assert(s.source(1)->outcome==Outcome::Incomplete && s.source(1)->occurrenceCount==0);
        assert(s.source(1)->reason==(recordsFull ? Reason::RecordCapacity : Reason::TextCapacity));
        assert(s.occurrenceCount()==1); isText(s.title(0),text("a"));
        expect(s.completeSource(1),Result::InvalidState); expect(s.beginSource(1),Result::InvalidState);
        expect(s.beginSource(2),Result::Ok);
        // Fits only if both staged record and text cursors were restored.
        expect(s.addOccurrence(2,timed(7,8,text("1234567"))),Result::Ok);
        expect(s.completeSource(2),Result::Ok);
        assert(s.occurrenceCount()==2 && s.occurrence(1)->sourceIndex==2);
    }
    Snapshot s; expect(s.initialize(w,0,6,labels,3),Result::Ok);
    expect(s.beginSource(0),Result::Ok); expect(s.completeSource(0),Result::Ok);
    expect(s.beginSource(1),Result::Ok);
    expect(s.failSource(1,Reason::ProducerFailure),Result::Ok);
    expect(s.beginSource(2),Result::Ok);
    expect(s.addOccurrence(2,timed(1,2,text(""))),Result::RecordCapacity);
    assert(s.source(0)->outcome==Outcome::Complete && s.source(1)->outcome==Outcome::Incomplete);
    assert(s.source(2)->outcome==Outcome::Incomplete); // No text/record space needed for outcomes.
}
void civilExtremesAndInvalidExclusion() {
    const Snapshot::Text labels[]={text("extremes")};
    for(int32_t first:{INT32_MIN,INT32_MAX-5}) {
        Snapshot s; const auto w=window(first);
        expect(s.initialize(w,1,32,labels,1),Result::Ok);
        expect(s.beginSource(0),Result::Ok);
        expect(s.addOccurrence(0,civil(first,first+5)),Result::Ok);
        expect(s.completeSource(0),Result::Ok);
        assert(s.occurrence(0)->range.civil.end==first+5);
    }
    Snapshot s; expect(s.initialize(window(),1,32,labels,1),Result::Ok);
    expect(s.beginSource(0),Result::Ok);
    const char invalid[]={'a',0,'b'};
    // Valid time exclusion must not hide malformed text from the producer.
    expect(s.addOccurrence(0,timed(-20,-10,{invalid,3})),Result::InvalidText);
    expect(s.completeSource(0),Result::InvalidState);
    assert(s.source(0)->outcome==Outcome::Incomplete);
}
void ownershipAndEvaluation() {
    Snapshot s; auto w=window(); char label[]="K\xc3\xbc"; char title[]="\xc2\xa2\xe2\x82\xac\xf0\x90\x8d\x88";
    Snapshot::Text labels[]={text(label)};
    expect(s.initialize(w,1024,262144,labels,1),Result::Ok);
    const size_t allocated=attempts;
    expect(s.beginSource(0),Result::Ok); expect(s.addOccurrence(0,timed(1,2,text(title))),Result::Ok);
    expect(s.completeSource(0),Result::Ok);
    std::memset(title,'?',sizeof(title)-1); std::memset(label,'?',sizeof(label)-1); w.firstCivilDay=123;
    isText(s.title(0),text("\xc2\xa2\xe2\x82\xac\xf0\x90\x8d\x88")); isText(s.sourceLabel(0),text("K\xc3\xbc"));
    assert(s.window()->firstCivilDay==0 && attempts==allocated);
    expect(s.release(),Result::Ok);
    Snapshot::Text catalogue[16]; for(auto& l:catalogue) l=text("same label");
    expect(s.initialize(window(),1024,262144,catalogue,16),Result::Ok);
    assert(s.allocationBytes()==287032);
    expect(s.release(),Result::Ok);
    for(int i=0;i<3;++i) {
        expect(s.initialize(window(),1,32,labels,1),Result::Ok);
        expect(s.beginSource(0),Result::Ok); expect(s.failSource(0,Reason::Cancelled),Result::Ok);
        expect(s.release(),Result::Ok);
    }
}
}
int main() {
    static_assert(!std::is_copy_constructible<Snapshot>::value,"one owner");
    static_assert(!std::is_move_constructible<Snapshot>::value,"stable views");
    initialization(); publicationAndRollback(); misaddressedAddAndIndexWidth();
    intervals(); invalidInput(); exhaustion(); civilExtremesAndInvalidExclusion(); ownershipAndEvaluation();
    for(const auto& a:allocations) assert(!a.live);
    std::printf("PASS: snapshot model, transactions, UTF-8, bounds and ownership; sizeof/alignof record=%zu/%zu source=%zu/%zu window=%zu/%zu\n",
        sizeof(Snapshot::Occurrence),alignof(Snapshot::Occurrence),sizeof(Snapshot::Source),alignof(Snapshot::Source),sizeof(Snapshot::Window),alignof(Snapshot::Window));
}
