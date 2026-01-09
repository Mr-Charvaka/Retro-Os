// net_stack.cpp - Integrated RFC-Compliant TCP/IP stack
// Based on User Architecture Requirements Phase 1-6

#include "../drivers/serial.h"
#include "../include/string.h"
#include <stdint.h>

/* =================== BASIC TYPES =================== */
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

/* =================== CONSTANTS =================== */
#define ETH_TYPE_IP 0x0800
#define ETH_TYPE_ARP 0x0806
#define IP_PROTO_ICMP 1
#define IP_PROTO_TCP 6

#define TCP_FLAG_FIN 0x01
#define TCP_FLAG_SYN 0x02
#define TCP_FLAG_RST 0x04
#define TCP_FLAG_PSH 0x08
#define TCP_FLAG_ACK 0x10

#define ARP_TABLE_SIZE 16
#define TCP_MAX_CONNS 8
#define TCP_RETRANS_TIMEOUT 1000 /* ms */

/* =================== UTIL =================== */
static inline u16 net_htons(u16 x) { return (x << 8) | (x >> 8); }
static inline u16 net_ntohs(u16 x) { return net_htons(x); }
static inline u32 net_htonl(u32 x) { return __builtin_bswap32(x); }
static inline u32 net_ntohl(u32 x) { return net_htonl(x); }

/* =================== HARDWARE CONFIG =================== */
static u8 local_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static u32 local_ip = 0x0F02000A; // 10.0.2.15 (Network Byte Order: 0A 00 02 0F)
static u32 gateway_ip =
    0x0202000A; // 10.0.2.2  (Network Byte Order: 02 02 00 0A)

extern "C" void e1000_send(u8 *buf, u32 len);
extern "C" u32 timer_now_ms(void);
extern "C" void *kmalloc(u32 size);
extern "C" void kfree(void *ptr);

/* =================== STRUCTURES =================== */
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

struct tcp_header {
  u16 src_port;
  u16 dst_port;
  u32 seq;
  u32 ack;
  u8 offset_res;
  u8 flags;
  u16 window;
  u16 checksum;
  u16 urgent;
} __attribute__((packed));

struct tcp_pseudo {
  u32 src_ip;
  u32 dst_ip;
  u8 zero;
  u8 proto;
  u16 tcp_len;
} __attribute__((packed));

enum tcp_state { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTABLISHED };

struct tcp_socket {
  u32 src_ip;
  u32 dst_ip;
  u16 src_port;
  u16 dst_port;

  u32 snd_una;
  u32 snd_nxt;
  u32 rcv_nxt;

  enum tcp_state state;
  u64 last_tx_time;
  u8 used;
};

static struct tcp_socket tcp_sockets[TCP_MAX_CONNS];

/* =================== ARP CACHE =================== */
struct arp_entry {
  u32 ip;
  u8 mac[6];
  int valid;
};
static struct arp_entry arp_table[ARP_TABLE_SIZE];

void arp_cache_insert(u32 ip, u8 *mac) {
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_table[i].valid && arp_table[i].ip == ip) {
      memcpy(arp_table[i].mac, mac, 6);
      return;
    }
  }
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (!arp_table[i].valid) {
      arp_table[i].ip = ip;
      memcpy(arp_table[i].mac, mac, 6);
      arp_table[i].valid = 1;
      break;
    }
  }
}

u8 *arp_cache_lookup(u32 ip) {
  for (int i = 0; i < ARP_TABLE_SIZE; i++) {
    if (arp_table[i].valid && arp_table[i].ip == ip)
      return arp_table[i].mac;
  }
  return nullptr;
}

void arp_send_request(u32 target_ip) {
  u8 frame[64];
  memset(frame, 0, 64);
  struct eth_hdr *eth = (struct eth_hdr *)frame;
  struct arp_pkt *arp = (struct arp_pkt *)(frame + sizeof(*eth));

  memset(eth->dst, 0xFF, 6);
  memcpy(eth->src, local_mac, 6);
  eth->type = net_htons(ETH_TYPE_ARP);

  arp->htype = net_htons(1);
  arp->ptype = net_htons(ETH_TYPE_IP);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = net_htons(1);
  memcpy(arp->sha, local_mac, 6);
  arp->spa = local_ip;
  memset(arp->tha, 0, 6);
  arp->tpa = target_ip;

  e1000_send(frame, sizeof(*eth) + sizeof(*arp));
}

/* =================== PHASE 2 — FIX CHECKSUMS =================== */

uint16_t net_checksum(const void *data, uint32_t len) {
  uint32_t sum = 0;
  const uint16_t *ptr = (const uint16_t *)data;

  while (len > 1) {
    sum += *ptr++;
    if (sum & 0x80000000)
      sum = (sum & 0xFFFF) + (sum >> 16);
    len -= 2;
  }

  if (len)
    sum += *(const uint8_t *)ptr;

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);

  return ~sum;
}

uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip, struct tcp_header *tcp,
                      uint16_t tcp_len) {
  struct tcp_pseudo psh;

  psh.src_ip = src_ip;
  psh.dst_ip = dst_ip;
  psh.zero = 0;
  psh.proto = 6;
  psh.tcp_len = net_htons(tcp_len);

  uint32_t size = sizeof(psh) + tcp_len;
  uint8_t *buf = (uint8_t *)kmalloc(size);
  if (!buf)
    return 0;

  memcpy(buf, &psh, sizeof(psh));
  memcpy(buf + sizeof(psh), tcp, tcp_len);

  uint16_t cs = net_checksum(buf, size);
  kfree(buf);
  return cs;
}

/* =================== PHASE 4 — FIX ROUTING =================== */

void ethernet_send(u32 dst_ip, u16 eth_type, void *payload, int len) {
  u8 frame[1514];
  struct eth_hdr *eth = (struct eth_hdr *)frame;

  // Correct Routing: Internet packets must go to gateway MAC
  u32 route_ip =
      ((dst_ip & 0xFFFFFF00) == (local_ip & 0xFFFFFF00)) ? dst_ip : gateway_ip;

  u8 *dst_mac = arp_cache_lookup(route_ip);
  if (!dst_mac) {
    serial_log("NET: ARP cache miss, requesting...");
    arp_send_request(route_ip);
    return;
  }

  memcpy(eth->dst, dst_mac, 6);
  memcpy(eth->src, local_mac, 6);
  eth->type = net_htons(eth_type);

  memcpy(frame + sizeof(*eth), payload, len);
  e1000_send(frame, sizeof(*eth) + len);
}

/* =================== PHASE 3 — BUILD PERFECT SYN =================== */

void ip_send(u32 dst_ip, u8 proto, void *payload, int len) {
  u8 packet[1500];
  struct ip_hdr *ip = (struct ip_hdr *)packet;
  static u16 ip_id = 0;

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = net_htons(sizeof(*ip) + len);
  ip->id = net_htons(ip_id++);
  ip->flags = 0;
  ip->ttl = 64;
  ip->proto = proto;
  ip->src = local_ip;
  ip->dst = dst_ip;
  ip->checksum = 0;
  ip->checksum = net_checksum(ip, sizeof(*ip));

  memcpy(packet + sizeof(*ip), payload, len);
  ethernet_send(dst_ip, ETH_TYPE_IP, packet, sizeof(*ip) + len);
}

void tcp_send_segment(struct tcp_socket *s, uint8_t flags) {
  struct tcp_header tcp;
  memset(&tcp, 0, sizeof(tcp));

  tcp.src_port = net_htons(s->src_port);
  tcp.dst_port = net_htons(s->dst_port);
  tcp.seq = net_htonl(s->snd_nxt);
  tcp.ack = net_htonl(s->rcv_nxt);
  tcp.offset_res = (5 << 4);
  tcp.flags = flags;
  tcp.window = net_htons(65535);
  tcp.urgent = 0;
  tcp.checksum = 0;

  tcp.checksum = tcp_checksum(s->src_ip, s->dst_ip, &tcp, sizeof(tcp));

  // PHASE 5 — VERIFY BEFORE TRANSMIT
  if (flags & TCP_FLAG_SYN) {
    serial_log("TCP SYN DUMP:");
    u8 *b = (u8 *)&tcp;
    for (int i = 0; i < (int)sizeof(tcp); i++) {
      serial_log_hex(" ", b[i]);
    }
  }

  ip_send(s->dst_ip, IP_PROTO_TCP, &tcp, sizeof(tcp));

  s->last_tx_time = timer_now_ms();

  // SYN consumes 1 seq
  if (flags & TCP_FLAG_SYN)
    s->snd_nxt++;
}

/* =================== PHASE 6 — RX PATH =================== */

struct tcp_socket *tcp_lookup(u32 src_ip, u16 src_port, u16 dst_port) {
  for (int i = 0; i < TCP_MAX_CONNS; i++) {
    if (tcp_sockets[i].used && tcp_sockets[i].dst_ip == src_ip &&
        tcp_sockets[i].dst_port == src_port &&
        tcp_sockets[i].src_port == dst_port) {
      return &tcp_sockets[i];
    }
  }
  return nullptr;
}

void tcp_rx(uint32_t src_ip, uint32_t dst_ip, uint8_t *payload, uint16_t len) {
  if (len < sizeof(struct tcp_header))
    return;

  struct tcp_header *tcp = (struct tcp_header *)payload;

  u16 src_port = net_ntohs(tcp->src_port);
  u16 dst_port = net_ntohs(tcp->dst_port);
  u8 flags = tcp->flags;
  u32 seq = net_ntohl(tcp->seq);
  u32 ack = net_ntohl(tcp->ack);

  // Verbose logging as requested
  serial_log_hex("TCP RX Flags: ", flags);
  serial_log_hex("  Port: ", src_port);
  serial_log_hex("  Seq:  ", seq);
  serial_log_hex("  Ack:  ", ack);

  struct tcp_socket *s = tcp_lookup(src_ip, src_port, dst_port);
  if (!s) {
    serial_log_hex("TCP: No matching socket for port ", src_port);
    return;
  }

  /* === CHECK FOR SYN-ACK (0x12) === */
  if ((flags & 0x12) == 0x12) {
    serial_log("TCP: >>> SYN-ACK RECEIVED <<<");

    /* Validate ACK (must be our SYN seq + 1) */
    if (ack != s->snd_nxt) {
      serial_log_hex("TCP: BAD ACK VALUE! Expected ", s->snd_nxt);
      return;
    }

    serial_log("TCP: Handshake step 2 OK");

    /* Store remote sequence and update local state */
    s->rcv_nxt = seq + 1;
    s->snd_una = ack;
    s->state = TCP_ESTABLISHED;

    /* Send final ACK */
    tcp_send_segment(s, TCP_FLAG_ACK);
    serial_log("TCP: >>> CONNECTION ESTABLISHED <<<");
  }
}

/* =================== TIMER & POLLING =================== */

extern "C" void tcp_timer_poll(void) {
  for (int i = 0; i < TCP_MAX_CONNS; i++) {
    struct tcp_socket *s = &tcp_sockets[i];
    if (!s->used)
      continue;

    if (s->state == TCP_SYN_SENT) {
      if (timer_now_ms() - s->last_tx_time > TCP_RETRANS_TIMEOUT) {
        serial_log("TCP: RETRANSMIT SYN");
        s->snd_nxt--; // same seq
        tcp_send_segment(s, TCP_FLAG_SYN);
      }
    }
  }
}

extern "C" void net_stack_rx(u8 *buf, u16 len) {
  if (len < sizeof(struct eth_hdr))
    return;
  struct eth_hdr *eth = (struct eth_hdr *)buf;
  u16 eth_type = net_ntohs(eth->type);

  if (eth_type == ETH_TYPE_ARP) {
    struct arp_pkt *arp = (struct arp_pkt *)(eth + 1);
    if (net_ntohs(arp->oper) == 2) {
      serial_log("ARP: Received reply!");
      arp_cache_insert(arp->spa, arp->sha);
    } else if (net_ntohs(arp->oper) == 1 && arp->tpa == local_ip) {
      // ARP Reply for us
      u8 reply[64];
      memset(reply, 0, 64);
      struct eth_hdr *reth = (struct eth_hdr *)reply;
      struct arp_pkt *rarp = (struct arp_pkt *)(reply + sizeof(*reth));
      memcpy(reth->dst, arp->sha, 6);
      memcpy(reth->src, local_mac, 6);
      reth->type = net_htons(ETH_TYPE_ARP);
      rarp->htype = net_htons(1);
      rarp->ptype = net_htons(ETH_TYPE_IP);
      rarp->hlen = 6;
      rarp->plen = 4;
      rarp->oper = net_htons(2);
      memcpy(rarp->sha, local_mac, 6);
      rarp->spa = local_ip;
      memcpy(rarp->tha, arp->sha, 6);
      rarp->tpa = arp->spa;
      e1000_send(reply, sizeof(*reth) + sizeof(*rarp));
    }
  } else if (eth_type == ETH_TYPE_IP) {
    struct ip_hdr *ip = (struct ip_hdr *)(eth + 1);
    if (ip->dst != local_ip) {
      serial_log_hex("NET: IP packet for other host: ", ip->dst);
      return;
    }

    u16 ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    u8 *payload = (u8 *)ip + ip_hdr_len;
    u16 payload_len = net_ntohs(ip->len) - ip_hdr_len;

    if (ip->proto == IP_PROTO_TCP) {
      tcp_rx(ip->src, ip->dst, payload, payload_len);
    } else {
      serial_log_hex("NET: Received non-TCP IP proto: ", ip->proto);
    }
  } else if (eth_type != ETH_TYPE_ARP) {
    // serial_log_hex("NET: Ignored Eth type: ", eth_type);
  }
}

/* =================== INIT & TEST =================== */

extern "C" void net_stack_init(void) {
  memset(arp_table, 0, sizeof(arp_table));
  memset(tcp_sockets, 0, sizeof(tcp_sockets));
  serial_log("NET_STACK: V6 RFC-Compliant Init");
  arp_send_request(gateway_ip);
}

extern "C" int net_stack_tcp_test(void) {
  for (int i = 0; i < TCP_MAX_CONNS; i++) {
    if (!tcp_sockets[i].used) {
      struct tcp_socket *s = &tcp_sockets[i];
      s->used = 1;
      s->src_ip = local_ip;
      s->dst_ip = 0x22D8B85D; // example.com
      s->src_port = 49152 + (timer_now_ms() % 10000);
      s->dst_port = 80;
      s->snd_nxt = 0x12345678;
      s->snd_una = s->snd_nxt;
      s->rcv_nxt = 0;
      s->state = TCP_SYN_SENT;

      serial_log("TCP: Connecting to example.com:80...");
      tcp_send_segment(s, TCP_FLAG_SYN);
      return 0;
    }
  }
  return -1;
}
