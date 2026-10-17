#include "../application/ics_recur_value.h"
#include "../application/ics_source_local.h"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <type_traits>
#include <vector>

namespace { bool audit = false; size_t allocations = 0, scenarios = 0; }
__attribute__((noinline)) void* operator new(size_t n) {
    if (audit) ++allocations;
    void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p;
}
void* operator new[](size_t n) { return ::operator new(n); }
__attribute__((noinline)) void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }

namespace {
namespace R = IcsRecurValue;
using S = R::Status;
using F = R::Frequency;
using View = IcsStructure::View;
using Stage = IcsSourceStage;
struct Audit {
    size_t before;
    Audit() : before(allocations) { assert(!audit); audit = true; }
    ~Audit() { audit = false; assert(before == allocations); }
};
struct Guard {
    std::vector<uint8_t> bytes;
    size_t capacity;
    explicit Guard(size_t n) : bytes(n + 32, 0xa5), capacity(n) {}
    uint8_t* data() { return bytes.data() + 16; }
    void check() const {
        for (size_t i = 0; i < 16; ++i) assert(bytes[i] == 0xa5);
        for (size_t i = 16 + capacity; i < bytes.size(); ++i) assert(bytes[i] == 0xa5);
    }
};
R::Limits limits() { return {4096, 64, 256}; }
R::Report checked(View v, S status, size_t offset, R::Limits budget = limits()) {
    R::Report r;
    { Audit a; r = R::validate(v, budget); }
    ++scenarios;
    if (r.status != status || r.offset != offset)
        std::fprintf(stderr, "scenario %zu expected %u/%zu got %u/%zu\n", scenarios,
            unsigned(status), offset, unsigned(r.status), r.offset);
    assert(r.status == status && r.offset == offset);
    if (status != S::CoreGrammarValid)
        assert(r.frequency == F::Unknown && !r.presence && !r.leapSecondPending);
    return r;
}
R::Report check(const std::string& rule, S status = S::CoreGrammarValid, size_t offset = 0,
                R::Limits budget = limits()) {
    Guard input(rule.size());
    std::copy(rule.begin(), rule.end(), input.data());
    const auto before = input.bytes;
    R::Report r = checked({input.data(), rule.size()}, status, offset, budget);
    assert(before == input.bytes); input.check();
    std::fill(input.data(), input.data() + input.capacity, 0xdd);
    return r; // Copied fields remain usable without input storage.
}
void malformed(const std::string& rule, size_t offset) { check(rule, S::MalformedRule, offset); }

void framing() {
    const char* const tokens[] = {"SECONDLY", "MINUTELY", "HOURLY", "DAILY", "WEEKLY", "MONTHLY", "YEARLY"};
    for (size_t i = 0; i < 7; ++i) {
        const auto r = check(std::string("FREQ=") + tokens[i]);
        assert(r.frequency == static_cast<F>(i) && r.presence == R::FREQ && !r.leapSecondPending);
    }
    auto r = check("iNtErVaL=2;fReQ=wEeKlY;bYdAy=mO,fR;wKsT=sU");
    assert(r.frequency == F::Weekly && r.presence == (R::INTERVAL | R::FREQ | R::BYDAY | R::WKST));
    check("COUNT=2;FREQ=DAILY");
    malformed("", 0);
    malformed("COUNT=1", 7);
    malformed("FREQ=DAILY;freq=DAILY", 11);
    malformed("FREQ=DAILY;COUNT=1;count=2", 19);
    const char* const names[] = {"UNTIL", "COUNT", "INTERVAL", "BYSECOND", "BYMINUTE", "BYHOUR",
        "BYDAY", "BYMONTHDAY", "BYYEARDAY", "BYWEEKNO", "BYMONTH", "BYSETPOS", "WKST"};
    for (const char* name : names) {
        const std::string first = std::string("FREQ=YEARLY;") + name + "=1;";
        malformed(first + name + "=1", first.size()); // Framing wins before typed checks.
    }
    malformed(";FREQ=DAILY", 0);
    malformed("FREQ=DAILY;", 11);
    malformed("FREQ=DAILY;;COUNT=2", 11);
    malformed("FREQ", 4);
    malformed("FREQ;COUNT=1", 4);
    malformed("=DAILY", 0);
    malformed("FREQ=", 5);
    malformed("FREQ=;COUNT=1", 5);
    malformed("FREQ==DAILY", 5);
    malformed("FREQ=DAILY,COUNT=1", 16);
    malformed("FREQ=DAILY:COUNT=1", 16);
    malformed("FREQ=DAILY ", 5);
    malformed(" FREQ=DAILY", 0);
    malformed("FRE_Q=DAILY", 3);
    malformed("FREQ =DAILY", 4);
    malformed("FREQ= DAilY", 5);
    malformed("FREQ=FORTNIGHTLY", 5);
    malformed("FREQ=DAILY\n", 5);
    malformed(std::string("FREQ=DAILY\0", 11), 5);
    malformed("FREQ=DAILY/WEEKLY", 5);
    malformed("FREQ=DAILY;count=", 17);
}
void integersAndUntil() {
    check("FREQ=DAILY;COUNT=1;INTERVAL=0002");
    check("FREQ=DAILY;COUNT=18446744073709551615;INTERVAL=18446744073709551615");
    for (const char* name : {"COUNT", "INTERVAL"}) {
        const std::string p = std::string("FREQ=DAILY;") + name + "=";
        for (const char* invalid : {"0", "000", "+1", "-1", " 1", "1 ", "0x1", "1,2", "1.0"}) {
            const std::string s = invalid;
            const size_t bad = s == "1 " || s == "1,2" || s == "1.0" ? 1 : s == "0x1" ? 1 : 0;
            malformed(p + s, p.size() + bad);
        }
        check(p + "18446744073709551616", S::ResourceLimit, p.size() + 19);
    }
    const std::string p = "FREQ=DAILY;UNTIL=";
    for (const char* value : {"00000101", "99991231", "20000229", "20261017T120000", "20261017T120000Z", "20261017t120000z"}) {
        const auto r = check(p + value); assert((r.presence & R::UNTIL) && !r.leapSecondPending);
    }
    assert(check(p + "20261231T235960Z").leapSecondPending);
    assert(check(p + "20261231T235960").leapSecondPending);
    malformed(p + "19000229", p.size() + 6);
    malformed(p + "20261301", p.size() + 4);
    malformed(p + "20261017T240000Z", p.size() + 9);
    malformed(p + "20261017T120000+0100", p.size());
    malformed(p + "20261017T1200Z", p.size());
    malformed(p + "20261017T120061Z", p.size() + 9);
    malformed(p + "20261017T120000Z ", p.size());
    const std::string first = "UNTIL=20261017;";
    malformed(first + "COUNT=1;FREQ=DAILY", first.size());
    const std::string second = "COUNT=1;FREQ=DAILY;";
    malformed(second + "UNTIL=20261017", second.size());
    // No DTSTART supplied: no comparison or universal UTC restriction is possible.
    check("UNTIL=00000101T000000;FREQ=DAILY");
}
void lists() {
    struct Numeric { const char* name; unsigned min, max, width; bool sign; };
    const Numeric fields[] = {{"BYSECOND",0,60,2,false},{"BYMINUTE",0,59,2,false},{"BYHOUR",0,23,2,false},
        {"BYMONTHDAY",1,31,2,true},{"BYYEARDAY",1,366,3,true},{"BYWEEKNO",1,53,2,true},
        {"BYMONTH",1,12,2,false},{"BYSETPOS",1,366,3,true}};
    for (const auto& field : fields) {
        const std::string prefix = std::string("FREQ=YEARLY;BYDAY=MO;") + field.name + "=";
        const std::string low = std::to_string(field.min), high = std::to_string(field.max);
        const auto r = check(prefix + low + "," + high + "," + low);
        assert(r.leapSecondPending == (std::string(field.name) == "BYSECOND"));
        if (field.sign) {
            check(prefix + "+" + low + ",-" + high);
            malformed(prefix + "0", prefix.size());
            malformed(prefix + "-0", prefix.size());
            malformed(prefix + "+", prefix.size());
            malformed(prefix + "-", prefix.size());
        } else {
            malformed(prefix + "+" + low, prefix.size());
            malformed(prefix + "-" + high, prefix.size());
            if (field.min) malformed(prefix + "0", prefix.size());
        }
        malformed(prefix + std::to_string(field.max + 1), prefix.size());
        malformed(prefix + std::string(field.width + 1, '0') + "1", prefix.size() + field.width);
        malformed(prefix + ",1", prefix.size());
        malformed(prefix + "1,", prefix.size() + 2);
        malformed(prefix + "1,,2", prefix.size() + 2);
        malformed(prefix + "1x", prefix.size() + 1);
        malformed(prefix + "1 2", prefix.size() + 1);
        check(prefix + std::string(field.width - 1, '0') + "1");
    }
    const std::string all = "FREQ=YEARLY;UNTIL=20271017T120000Z;INTERVAL=2;BYSECOND=0,60;BYMINUTE=0,59;"
        "BYHOUR=0,23;BYDAY=SU,MO,TU,WE,TH,FR,SA;BYMONTHDAY=-31,+31;BYYEARDAY=-366,+366;"
        "BYWEEKNO=-53,+53;BYMONTH=1,12;BYSETPOS=-366,+366;WKST=MO";
    const auto r = check(all);
    assert(r.presence == ((1U << 14) - 1 - R::COUNT) && r.leapSecondPending);
    assert(check("FREQ=DAILY;COUNT=5").presence == (R::FREQ | R::COUNT));
    malformed("FREQ=YEARLY;BYMONTH=5L", 21);
}
void weekdaysAndRelationships() {
    for (const char* day : {"SU", "MO", "TU", "WE", "TH", "FR", "SA", "mo"}) {
        check(std::string("FREQ=YEARLY;BYDAY=") + day);
        check(std::string("FREQ=WEEKLY;WKST=") + day);
        for (const char* ordinal : {"1", "+1", "-1", "53", "+53", "-53", "01"})
            check(std::string("FREQ=MONTHLY;BYDAY=") + ordinal + day);
    }
    const std::string p = "FREQ=YEARLY;BYDAY=";
    for (const char* bad : {"8", "MO1", "+MO", "0MO", "-0MO", "54MO", "XX", "MO,", ",MO", "001MO"}) {
        const std::string s = bad;
        size_t offset = p.size();
        if (s == "MO1") offset += 1;
        if (s == "MO,") offset += 3;
        if (s == "001MO") offset += 2;
        malformed(p + s, offset);
    }
    for (const char* bad : {"MO,TU", "1MO", "+MO", "XX", "MO ", "MO1"}) malformed(std::string("FREQ=DAILY;WKST=") + bad, 16);
    malformed("FREQ=WEEKLY;BYMONTHDAY=1", 12);
    for (const char* freq : {"DAILY", "WEEKLY", "MONTHLY"}) {
        const std::string head = std::string("FREQ=") + freq + ";";
        malformed(head + "BYYEARDAY=1", head.size());
    }
    for (const char* freq : {"SECONDLY", "MINUTELY", "HOURLY", "DAILY", "WEEKLY", "MONTHLY"}) {
        const std::string head = std::string("FREQ=") + freq + ";";
        malformed(head + "BYWEEKNO=1", head.size());
        if (std::string(freq) != "MONTHLY") malformed(head + "BYDAY=1MO", head.size() + 6);
    }
    malformed("FREQ=YEARLY;BYDAY=MO,1FR;BYWEEKNO=1", 21);
    malformed("BYWEEKNO=1;BYDAY=-1SU;FREQ=YEARLY", 17);
    malformed("FREQ=DAILY;BYSETPOS=1", 11);
    check("FREQ=DAILY;BYSETPOS=1;BYSECOND=0");
    check("BYSETPOS=-1;BYDAY=MO,TU,WE,TH,FR;FREQ=MONTHLY");
    check("BYMONTHDAY=31;FREQ=MONTHLY");
    check("BYWEEKNO=53;BYDAY=MO;FREQ=YEARLY");
    check("FREQ=HOURLY;BYYEARDAY=366");
    check("FREQ=SECONDLY;BYSECOND=0,0,00");
}
void extensionsAndBounds() {
    for (const char* s : {"RSCALE=HEBREW;FREQ=YEARLY;BYMONTH=5L", "RSCALE=GREGORIAN;FREQ=DAILY",
        "SKIP=FORWARD;FREQ=YEARLY", "X-TEST=token;FREQ=DAILY", "UNKNOWN=token;FREQ=DAILY"})
        check(s, S::UnsupportedFeature, 0);
    const std::string core = "FREQ=YEARLY;BYMONTH=5L;";
    check(core + "RSCALE=HEBREW;SKIP=FORWARD", S::UnsupportedFeature, core.size());
    check("FREQ=YEARLY;BYMONTH=13;RSCALE=ETHIOPIC", S::UnsupportedFeature, 23);
    check("FREQ=YEARLY;BYYEARDAY=999;RSCALE=CHINESE", S::UnsupportedFeature, 26);
    check("FREQ=NEW-FREQUENCY;X-RULE=YES", S::UnsupportedFeature, 19);
    check("FREQ=DAILY;SKIP=OMIT", S::UnsupportedFeature, 11);
    check("FREQ=DAILY;RSCALE=GREGORIAN;SKIP=BACKWARD", S::UnsupportedFeature, 11);
    check("FREQ=DAILY;X-TEST=1;X-TEST=2", S::UnsupportedFeature, 11); // No extension cardinality policy.
    malformed("RSCALE=HEBREW;BYMONTH=5L", 24);
    malformed("RSCALE=HEBREW;FREQ=DAILY;FREQ=YEARLY", 25);
    malformed("FREQ=DAILY;RSCALE", 17);
    malformed("FREQ=DAILY;RSCALE=", 18);
    malformed("FREQ=DAILY;RSCALE=X;BAD NAME=1", 23);
    malformed("FREQ=DAILY;RSCALE=X;COUNT==1", 26);
    malformed("FREQ=DAILY;RSCALE=X;BYMONTH=1,,5L", 30);
    malformed("FREQ=DAILY;RSCALE=X;", 20);
    // Framing pass must finish even when an earlier core representation is bad.
    malformed("FREQ=BAD;COUNT=1;INTERVAL", 25);
    const std::string rule = "FREQ=YEARLY;BYSECOND=0,1;BYDAY=MO,TU";
    check(rule, S::CoreGrammarValid, 0, {rule.size(),3,4});
    check(rule, S::ResourceLimit, rule.size()-1, {rule.size()-1,3,4});
    check(rule, S::ResourceLimit, rule.find("BYDAY"), {rule.size(),2,4});
    check(rule, S::ResourceLimit, rule.find("TU"), {rule.size(),3,3});
    check("FREQ=DAILY", S::CoreGrammarValid, 0, {10,1,0});
    check("FREQ=DAILY", S::ResourceLimit, 0, {10,0,0});
    check("FREQ=DAILY", S::ResourceLimit, 0, {0,1,0});
    check("FREQ=DAILY;BYSECOND=0", S::ResourceLimit, 20, {100,2,0});
    check("FREQ=YEARLY;BYMONTH=5L;RSCALE=HEBREW", S::ResourceLimit, 20, {100,3,0});
    checked({nullptr,0}, S::MalformedRule, 0);
    checked({nullptr,1}, S::InvalidArgument, 0);
    checked({reinterpret_cast<const uint8_t*>(UINTPTR_MAX-1),4}, S::InvalidArgument, 0);
    const uint8_t byte = 'F';
    checked({&byte,SIZE_MAX}, S::InvalidArgument, 0);
    check("FREQ=DAILY", S::CoreGrammarValid, 0, {SIZE_MAX,SIZE_MAX,SIZE_MAX});
}

void passOrderAndPermutation() {
    const char* const parts[] = {"FREQ=YEARLY", "UNTIL=20261231T235960Z", "INTERVAL=2", "BYSECOND=60",
        "BYMINUTE=59", "BYHOUR=23", "BYDAY=MO", "BYMONTHDAY=-31", "BYYEARDAY=-366",
        "BYWEEKNO=53", "BYMONTH=12", "BYSETPOS=-1", "WKST=SU"};
    for (size_t start=0;start<13;++start) {
        std::string rule;
        for (size_t n=0;n<13;++n) { if (n) rule+=';'; rule+=parts[(start+n)%13]; }
        const auto r=check(rule);
        assert(r.frequency==F::Yearly && r.presence==((1U<<14)-1-R::COUNT) && r.leapSecondPending);
    }
    // Ordering of passes is observable, including cleanup after parsed leap data.
    const std::string lead="FREQ=YEARLY;BYSECOND=60;";
    malformed(lead+"COUNT=0",lead.size()+6);
    malformed(lead+"BYMONTH=13",lead.size()+8);
    malformed("FREQ=BAD;COUNT=1;COUNT=2",17);
    malformed("RSCALE=X;COUNT=1;COUNT=2",17);
    malformed("RSCALE=X;BYMONTH=5L;BROKEN",26);
    check("RSCALE=X;FREQ=BAD;BYMONTH=5L",S::UnsupportedFeature,0);
    const std::string prefix="FREQ=DAILY;BYSECOND=";
    std::string repeated="0";
    for (size_t n=1;n<256;++n) repeated+=",0";
    check(prefix+repeated,S::CoreGrammarValid,0,{4096,2,256});
    check(prefix+repeated+",0",S::ResourceLimit,prefix.size()+512,{4096,2,256});
    check("FREQ=DAILY;X-A=1;X-B=2",S::ResourceLimit,17,{4096,2,0});
    const uint8_t byte='F';
    checked({&byte,static_cast<size_t>(PTRDIFF_MAX)+1},S::InvalidArgument,0);
}

uint64_t clock(void*) { return 0; }
IcsStructure::Limits syntax() { return {4096,32,32,64,8,64,4096,8192,128,8}; }
Stage::Limits storage() { return {64,256,16384,64,4096}; }
size_t stageBytes() { size_t n=0; assert(Stage::requiredBytes(storage(),n)==Stage::Status::InProgress); return n; }
size_t syntaxBytes() { size_t n=0; assert(IcsStructure::scratchBytes(syntax(),n)==IcsStructure::Status::InProgress); return n; }
void composition(const std::string& rule, bool chunked, S expected) {
    Guard data(stageBytes()), parser(syntaxBytes()), httpData(6144);
    Stage stage; IcsStructure structure; IcsIngestion http;
    assert(stage.initialize(data.data(),data.capacity,storage())==Stage::Status::InProgress);
    assert(structure.initialize(parser.data(),parser.capacity,syntax(),Stage::sink,&stage)==IcsStructure::Status::InProgress);
    const IcsIngestion::Limits il={131072,65536,4096,32,1024,256,1024,16,1024,4096,4096,10000,1000};
    assert(http.initialize(httpData.data(),httpData.capacity,il,IcsStructure::lineSink,&structure,clock,nullptr)==IcsIngestion::Status::InProgress);
    const std::string body="BEGIN:VCALENDAR\r\nVERSION:2.0\r\nPRODID:test\r\nBEGIN:VEVENT\r\nUID:exact-identity\r\n"
        "DTSTAMP:20261017T120000Z\r\nDTSTART:20261017T130000Z\r\nRRULE:"+rule+"\r\nEND:VEVENT\r\nEND:VCALENDAR\r\n";
    std::string response;
    if (!chunked) response="HTTP/1.1 200 OK\r\nContent-Length: "+std::to_string(body.size())+"\r\n\r\n"+body;
    else {
        response="HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n";
        for (char c : body) { response+="1\r\n"; response+=c; response+="\r\n"; }
        response+="0\r\n\r\n";
    }
    for (const char& c : response)
        assert(http.feed(reinterpret_cast<const uint8_t*>(&c),1)==IcsIngestion::Status::InProgress);
    assert(http.finish()==IcsIngestion::Status::Complete);
    assert(structure.finish()==IcsStructure::Status::StructurallyComplete);
    assert(stage.finish(http,structure)==Stage::Status::StagedSuccessfully);
    http.release(); structure.release();
    std::fill(response.begin(),response.end(),'!');
    std::fill(parser.data(),parser.data()+parser.capacity,0xdd);
    const auto before=data.bytes; const auto counts=stage.counts();
    const IcsSourceLocal::Limits sl={64,4,16,16,16,64,{64,128,4096},{64,64,4096,64,4096}};
    const auto coverage=IcsSourceLocal::inspect(stage,sl);
    assert(coverage.status==IcsSourceLocal::Status::LocalCoveragePassed);
    assert(coverage.eventDeferred&IcsEventLocal::RecurrenceValidationPending);
    const auto event=IcsEventLocal::validate(stage,0,1,sl.event);
    assert(event.status==IcsEventLocal::Status::LocalChecksPassed);
    size_t index=Stage::None;
    for (uint32_t p=stage.component(1)->firstProperty;p!=Stage::None;p=stage.property(p)->next)
        if (stage.property(p)->code==Stage::Code::Rrule) index=p;
    assert(index!=Stage::None && stage.property(index)->owner==1);
    View view={}; assert(stage.value(index,view)==Stage::Read::Ok);
    assert(view.size==rule.size() && !std::memcmp(view.data,rule.data(),rule.size()));
    const size_t offset=expected==S::CoreGrammarValid ? 0 : rule.find("RSCALE");
    const auto r=checked(view,expected,offset);
    checked(view,expected,offset);
    const auto afterCoverage=IcsSourceLocal::inspect(stage,sl);
    const auto afterEvent=IcsEventLocal::validate(stage,0,1,sl.event);
    assert(coverage.status==afterCoverage.status && coverage.failureKind==afterCoverage.failureKind &&
        coverage.componentIndex==afterCoverage.componentIndex && coverage.propertyIndex==afterCoverage.propertyIndex &&
        coverage.propertyCode==afterCoverage.propertyCode && coverage.eventStatus==afterCoverage.eventStatus &&
        coverage.timezoneStatus==afterCoverage.timezoneStatus && coverage.eventDeferred==afterCoverage.eventDeferred &&
        coverage.timezoneDeferred==afterCoverage.timezoneDeferred && coverage.sourceDeferred==afterCoverage.sourceDeferred &&
        coverage.calendars==afterCoverage.calendars && coverage.events==afterCoverage.events &&
        coverage.timezones==afterCoverage.timezones && coverage.observances==afterCoverage.observances &&
        coverage.projectedNonEventComponents==afterCoverage.projectedNonEventComponents);
    assert(event.status==afterEvent.status && event.deferred==afterEvent.deferred &&
        event.propertyIndex==afterEvent.propertyIndex && event.propertyCode==afterEvent.propertyCode);
    const auto after=stage.counts();
    assert(before==data.bytes && counts.components==after.components && counts.calendars==after.calendars &&
        counts.observedProperties==after.observedProperties && counts.ignoredOpaqueProperties==after.ignoredOpaqueProperties &&
        counts.storedProperties==after.storedProperties && counts.poolBytes==after.poolBytes &&
        counts.reviewItems==after.reviewItems && counts.reviewBytes==after.reviewBytes);
    stage.release(); assert(!stage.component(0));
    assert(stage.value(index,view)!=Stage::Read::Ok);
    std::fill(data.data(),data.data()+data.capacity,0xdd);
    assert(r.status==expected); // Pointer-free result survives release and poisoning.
    data.check(); parser.check(); httpData.check();
}
}

int main() {
    static_assert(std::is_trivially_copyable<R::Report>::value,"Report is copied scalar evidence");
    static_assert(std::is_standard_layout<R::Report>::value,"Report has no lifetime owner");
    framing(); integersAndUntil(); lists(); weekdaysAndRelationships(); extensionsAndBounds(); passOrderAndPermutation();
    for (bool chunked : {false,true}) {
        composition("FREQ=DAILY;COUNT=4;BYHOUR=13;BYMINUTE=0",chunked,S::CoreGrammarValid);
        composition("FREQ=YEARLY;BYMONTH=5L;RSCALE=HEBREW;SKIP=FORWARD",chunked,S::UnsupportedFeature);
    }
    std::printf("RECUR grammar: %zu deterministic cases; zero audited allocations; report %zu/%zu\n",
        scenarios,sizeof(R::Report),alignof(R::Report));
}
