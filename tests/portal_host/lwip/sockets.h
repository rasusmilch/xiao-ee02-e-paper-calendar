#ifndef EE02_PORTAL_SOCKETS_H
#define EE02_PORTAL_SOCKETS_H
#include <NetworkServer.h>
#define MSG_DONTWAIT 0x40
inline int send(int fd,const void* bytes,size_t n,int flags){assert(fd==55 && flags==MSG_DONTWAIT);auto& s=PortalHost::state();s.exchanges[s.index].response.append(static_cast<const char*>(bytes),n);return int(n);}
#endif
