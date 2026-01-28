#ifndef NET_H
#define NET_H

#include "../include/types.h"

#define ETH_TYPE_ARP 0x0806
#define ETH_TYPE_IP 0x0800

#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6
#define IP_PROTO_UDP 17

#define ARP_REQUEST 1
#define ARP_REPLY 2

extern u32 my_ip; // Exposed for TCP/UDP Checksums

struct eth_hdr {
  u8 dst[6];
  u8 src[6];
  u16 type;
} __attribute__((packed));

struct arp_pkt {
  u16 htype;
  u16 ptype;
  u8 hlen;
  u8 plen;
  u16 oper;
  u8 sha[6];
  u32 spa;
  u8 tha[6];
  u32 tpa;
} __attribute__((packed));

struct ip_hdr {
  u8 ver_ihl;
  u8 tos;
  u16 len;
  u16 id;
  u16 flags;
  u8 ttl;
  u8 proto;
  u16 checksum;
  u32 src;
  u32 dst;
} __attribute__((packed));

struct icmp_hdr {
  u8 type;
  u8 code;
  u16 checksum;
  u16 id;
  u16 seq;
} __attribute__((packed));

struct udp_hdr {
  u16 src;
  u16 dst;
  u16 len;
  u16 checksum;
} __attribute__((packed));

struct dns_hdr {
  u16 id;
  u16 flags;
  u16 qdcount;
  u16 ancount;
  u16 nscount;
  u16 arcount;
} __attribute__((packed));

extern "C" void net_poll();
extern "C" void net_init();

#endif
