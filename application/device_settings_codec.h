#ifndef EE02_DEVICE_SETTINGS_CODEC_H
#define EE02_DEVICE_SETTINGS_CODEC_H
#include "device_settings.h"
namespace DeviceSettings {
// LE: magic EE02, u16 version=1, u16 headerBytes=16, u32 totalBytes, u32 CRC32.
// CRC32 (IEEE) covers every byte except checksum bytes 12..15. Not authentication.
// Payload: u64 refresh, u8 weatherEnabled, u8 count; six u16-length strings
// ssid/password/timezone/ntp/location/key, then count*(u8 enabled,label,url).
static const uint16_t SchemaVersion=1;
inline uint32_t checksum(const uint8_t* p,size_t n) {
    uint32_t crc=0xffffffff;
    for(size_t i=0;i<n;++i) { if(i>=12 && i<16) continue;
        crc^=p[i]; for(unsigned j=0;j<8;++j) crc=(crc>>1)^(0xedb88320U & (0U-(crc&1U)));
    }
    return ~crc;
}
namespace codec {
inline void put(uint8_t* p,uint64_t v,size_t n) { for(size_t i=0;i<n;++i) p[i]=uint8_t(v>>(i*8)); }
inline uint64_t get(const uint8_t* p,size_t n) { uint64_t v=0; for(size_t i=0;i<n;++i) v|=uint64_t(p[i])<<(i*8); return v; }
template<size_t N> inline void write(uint8_t* p,size_t& pos,const Text<N>& t) {
    put(p+pos,t.size,2); pos+=2; memcpy(p+pos,t.data,t.size); pos+=t.size;
}
template<size_t N> inline bool read(const uint8_t* p,size_t n,size_t& pos,Text<N>& t) {
    if(n-pos<2) return false;
    const size_t len=size_t(get(p+pos,2)); pos+=2;
    if(len>N || len>n-pos || !assign(t,reinterpret_cast<const char*>(p+pos),len)) return false;
    pos+=len; return true;
}
}
inline Result encode(const Values& v,uint8_t* bytes,size_t capacity,size_t& size) {
    size=0; const Result r=validate(v); if(r.status!=Status::Ok) return r;
    size_t n=26+12+v.ssid.size+v.password.size+v.timezone.size+v.ntp.size+v.location.size+v.apiKey.size;
    for(size_t i=0;i<v.sourceCount;++i) n+=5+v.sources[i].label.size+v.sources[i].url.size;
    if(!bytes || ee02_ics_detail::overlap(bytes,capacity,&v,sizeof(v)) || n>MaxEncoded || capacity<n || capacity>UINTPTR_MAX-reinterpret_cast<uintptr_t>(bytes))
        return result(Status::InvalidArgument);
    memcpy(bytes,"EE02",4); codec::put(bytes+4,SchemaVersion,2); codec::put(bytes+6,16,2);
    codec::put(bytes+8,n,4); codec::put(bytes+12,0,4); codec::put(bytes+16,v.refreshSeconds,8);
    bytes[24]=uint8_t(v.weatherEnabled); bytes[25]=v.sourceCount; size_t pos=26;
    codec::write(bytes,pos,v.ssid); codec::write(bytes,pos,v.password); codec::write(bytes,pos,v.timezone);
    codec::write(bytes,pos,v.ntp); codec::write(bytes,pos,v.location); codec::write(bytes,pos,v.apiKey);
    for(size_t i=0;i<v.sourceCount;++i) { bytes[pos++]=uint8_t(v.sources[i].enabled);
        codec::write(bytes,pos,v.sources[i].label); codec::write(bytes,pos,v.sources[i].url); }
    codec::put(bytes+12,checksum(bytes,n),4); size=n; return result(Status::Ok);
}
// Caller supplies disjoint owned work/output storage; output changes only on success.
inline Result decode(const uint8_t* bytes,size_t n,Values& output,Values& work) {
    if(!bytes || n>UINTPTR_MAX-reinterpret_cast<uintptr_t>(bytes) || ee02_ics_detail::overlap(&output,sizeof(output),&work,sizeof(work)) ||
       ee02_ics_detail::overlap(bytes,n,&work,sizeof(work)) || ee02_ics_detail::overlap(bytes,n,&output,sizeof(output)))
        return result(Status::InvalidArgument);
    if(n<16 || n>MaxEncoded || memcmp(bytes,"EE02",4) || codec::get(bytes+6,2)!=16 ||
       codec::get(bytes+8,4)!=n || codec::get(bytes+12,4)!=checksum(bytes,n)) return result(Status::Corrupt);
    if(codec::get(bytes+4,2)!=SchemaVersion) return result(Status::UnsupportedVersion);
    if(n<38 || bytes[24]>1 || bytes[25]>MaxSources) return result(Status::Corrupt);
    work={}; work.refreshSeconds=codec::get(bytes+16,8); work.weatherEnabled=bytes[24]!=0; work.sourceCount=bytes[25];
    size_t pos=26;
    if(!codec::read(bytes,n,pos,work.ssid) || !codec::read(bytes,n,pos,work.password) ||
       !codec::read(bytes,n,pos,work.timezone) || !codec::read(bytes,n,pos,work.ntp) ||
       !codec::read(bytes,n,pos,work.location) || !codec::read(bytes,n,pos,work.apiKey)) return result(Status::Corrupt);
    for(size_t i=0;i<work.sourceCount;++i) {
        if(pos==n || bytes[pos]>1) return result(Status::Corrupt);
        work.sources[i].enabled=bytes[pos++]!=0;
        if(!codec::read(bytes,n,pos,work.sources[i].label) || !codec::read(bytes,n,pos,work.sources[i].url)) return result(Status::Corrupt);
    }
    if(pos!=n || validate(work).status!=Status::Ok) return result(Status::Corrupt);
    output=work; return result(Status::Ok);
}
} // namespace DeviceSettings
#endif
