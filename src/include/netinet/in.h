#ifndef _NETINET_IN_H
#define _NETINET_IN_H

#include <stdint.h>
#include <sys/socket.h>

#define IPPROTO_IP 0
#define IPPROTO_TCP 6
#define IPPROTO_UDP 17

#define INADDR_ANY 0
#define INADDR_BROADCAST 0xffffffff
#define INADDR_NONE 0xffffffff

typedef uint32_t in_addr_t;
typedef uint16_t in_port_t;

struct in_addr {
  in_addr_t s_addr;
};

struct sockaddr_in {
  uint16_t sin_family;
  in_port_t sin_port;
  struct in_addr sin_addr;
  char sin_zero[8];
};

#ifdef __cplusplus
extern "C" {
#endif

// Byte order conversion
static inline uint32_t htonl(uint32_t hostlong) {
  return __builtin_bswap32(hostlong);
}

static inline uint16_t htons(uint16_t hostshort) {
  return (hostshort << 8) | (hostshort >> 8);
}

static inline uint32_t ntohl(uint32_t netlong) {
  return __builtin_bswap32(netlong);
}

static inline uint16_t ntohs(uint16_t netshort) {
  return (netshort << 8) | (netshort >> 8);
}

// Address conversion (should be moved to libc implementation eventually)
char *inet_ntoa(struct in_addr in);
int inet_aton(const char *cp, struct in_addr *inp);

#ifdef __cplusplus
}
#endif

#endif
