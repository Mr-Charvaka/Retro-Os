#include "net.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "e1000.h"

static u8 my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
static u32 my_ip = 0x0F02000A; // 10.0.2.15 in memory (0A 00 02 0F)

static inline u16 htons(u16 x) { return (x << 8) | (x >> 8); }

static inline u32 htonl(u32 x) {
  return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) |
         ((x & 0xFF000000) >> 24);
}

static u16 checksum(void *data, int len) {
  u32 sum = 0;
  u16 *ptr = (u16 *)data;
  while (len > 1) {
    sum += *ptr++;
    len -= 2;
  }
  if (len)
    sum += *(u8 *)ptr;
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~sum;
}

void send_arp_reply(arp_pkt *req) {
  u8 buf[64];
  memset(buf, 0, 64);
  eth_hdr *eth = (eth_hdr *)buf;
  arp_pkt *arp = (arp_pkt *)(buf + sizeof(eth_hdr));

  for (int i = 0; i < 6; i++) {
    eth->dst[i] = req->sha[i];
    eth->src[i] = my_mac[i];
    arp->tha[i] = req->sha[i];
    arp->sha[i] = my_mac[i];
  }

  eth->type = htons(ETH_TYPE_ARP);

  arp->htype = htons(1);
  arp->ptype = htons(0x0800);
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = htons(ARP_REPLY);
  arp->spa = my_ip;
  arp->tpa = req->spa;

  serial_log("NET: Sending ARP Reply");
  e1000_send(buf, sizeof(eth_hdr) + sizeof(arp_pkt));
}

void handle_arp(u8 *packet) {
  arp_pkt *arp = (arp_pkt *)(packet + sizeof(eth_hdr));
  if (htons(arp->oper) == ARP_REQUEST && arp->tpa == my_ip) {
    serial_log("NET: Received ARP Request for our IP");
    send_arp_reply(arp);
  }
}

void send_icmp_reply(ip_hdr *ip, icmp_hdr *icmp, u8 *data, int data_len) {
  u8 buf[512];
  memset(buf, 0, 512);

  eth_hdr *eth_req = (eth_hdr *)((u8 *)ip - sizeof(eth_hdr));
  eth_hdr *eth = (eth_hdr *)buf;
  ip_hdr *ip2 = (ip_hdr *)(buf + sizeof(eth_hdr));
  icmp_hdr *icmp2 = (icmp_hdr *)((u8 *)ip2 + sizeof(ip_hdr));

  for (int i = 0; i < 6; i++) {
    eth->dst[i] = eth_req->src[i];
    eth->src[i] = my_mac[i];
  }

  eth->type = htons(ETH_TYPE_IP);

  *ip2 = *ip;
  ip2->src = my_ip;
  ip2->dst = ip->src;
  ip2->checksum = 0;
  ip2->checksum = htons(checksum(ip2, sizeof(ip_hdr)));

  icmp2->type = 0; // Echo Reply
  icmp2->code = 0;
  icmp2->id = icmp->id;
  icmp2->seq = icmp->seq;
  icmp2->checksum = 0;

  // Copy data back if any
  if (data_len > 0) {
    memcpy((u8 *)icmp2 + sizeof(icmp_hdr), data, data_len);
  }

  icmp2->checksum = htons(checksum(icmp2, sizeof(icmp_hdr) + data_len));

  serial_log("NET: Sending ICMP Echo Reply");
  e1000_send(buf,
             sizeof(eth_hdr) + sizeof(ip_hdr) + sizeof(icmp_hdr) + data_len);
}

// Generic IP send function for UDP/TCP
extern "C" void ip_send(uint32_t dst_ip, uint8_t protocol, uint8_t *data,
                        uint16_t length) {
  static uint16_t ip_id = 1;
  u8 buf[1500];
  memset(buf, 0, sizeof(buf));

  eth_hdr *eth = (eth_hdr *)buf;
  ip_hdr *ip = (ip_hdr *)(buf + sizeof(eth_hdr));

  // Use broadcast MAC for now (ARP resolution todo)
  for (int i = 0; i < 6; i++) {
    eth->dst[i] = 0xFF;
    eth->src[i] = my_mac[i];
  }
  eth->type = htons(ETH_TYPE_IP);

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = htons(sizeof(ip_hdr) + length);
  ip->id = htons(ip_id++);
  ip->flags = 0;
  ip->ttl = 64;
  ip->proto = protocol;
  ip->src = 0x0F02000A; // 10.0.2.15
  ip->dst = dst_ip;
  ip->checksum = 0;
  ip->checksum = checksum(ip, sizeof(ip_hdr));

  memcpy(buf + sizeof(eth_hdr) + sizeof(ip_hdr), data, length);

  serial_log_hex("NET: ip_send proto=", protocol);
  e1000_send(buf, sizeof(eth_hdr) + sizeof(ip_hdr) + length);
}

void handle_icmp(u8 *pkt) {
  ip_hdr *ip = (ip_hdr *)pkt;
  icmp_hdr *icmp = (icmp_hdr *)((u8 *)ip + sizeof(ip_hdr));
  int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
  int icmp_total_len = htons(ip->len) - ip_hdr_len;
  if (icmp_total_len < (int)sizeof(icmp_hdr))
    return;
  int icmp_data_len = icmp_total_len - sizeof(icmp_hdr);

  if (icmp->type == 8) { // Echo Request
    serial_log("NET: Received ICMP Echo Request (Ping)");
    send_icmp_reply(ip, icmp, (u8 *)icmp + sizeof(icmp_hdr), icmp_data_len);
  } else if (icmp->type == 0) { // Echo Reply
    serial_log("NET: Received ICMP Echo Reply! (Ping Successful)");
  }
}

// Forward declaration for UDP layer
extern "C" void udp_receive(uint32_t src_ip, uint32_t dst_ip, uint8_t *packet,
                            uint16_t length);

void handle_udp(u8 *pkt) {
  ip_hdr *ip = (ip_hdr *)pkt;
  int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
  int udp_len = htons(ip->len) - ip_hdr_len;

  serial_log("NET: Received UDP Packet");
  udp_receive(ip->src, ip->dst, (u8 *)ip + ip_hdr_len, udp_len);
}

// Forward declaration for TCP layer
extern "C" void tcp_handle_packet(uint32_t src_ip, uint32_t dst_ip,
                                  uint8_t *packet, uint16_t length);

void handle_tcp(u8 *pkt, int total_len) {
  ip_hdr *ip = (ip_hdr *)pkt;
  int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
  int tcp_len = htons(ip->len) - ip_hdr_len;

  serial_log("NET: Received TCP Packet");
  tcp_handle_packet(ip->src, ip->dst, (u8 *)ip + ip_hdr_len, tcp_len);
}

void handle_ethernet(u8 *packet, int len) {
  eth_hdr *eth = (eth_hdr *)packet;
  u16 type = htons(eth->type);

  if (type == ETH_TYPE_ARP) {
    handle_arp(packet);
  } else if (type == ETH_TYPE_IP) {
    ip_hdr *ip = (ip_hdr *)(packet + sizeof(eth_hdr));
    if (ip->proto == IP_PROTO_ICMP)
      handle_icmp((u8 *)ip);
    else if (ip->proto == IP_PROTO_UDP)
      handle_udp((u8 *)ip);
    else if (ip->proto == 6) // TCP
      handle_tcp((u8 *)ip, len - sizeof(eth_hdr));
    else {
      serial_log_hex("NET: Received unknown IP proto: ", ip->proto);
    }
  } else {
    // serial_log_hex("NET: Received unknown Eth type: ", type);
  }
}

// Forward declarations for new net_stack
extern "C" void net_stack_init(void);
extern "C" void net_stack_rx(uint8_t *packet, uint16_t len);
extern "C" void tcp_timer_poll(void);

extern "C" void net_poll() {
  static int call_count = 0;
  static int timer_count = 0;

  if (++call_count >= 10000) {
    call_count = 0;
    serial_log("NET: poll active");
  }

  // Check TCP retransmission timers periodically
  if (++timer_count >= 100) {
    timer_count = 0;
    tcp_timer_poll();
  }

  u8 buf[2048];
  int len = e1000_receive(buf);
  if (len > 0) {
    serial_log_hex("NET: Incoming Packet, len: ", len);
    // Route to new net_stack for proper ARP/TCP handling
    net_stack_rx(buf, len);
    // Also call old handler for compatibility
    handle_ethernet(buf, len);
  }
}

extern "C" void net_init() {
  serial_log("NET: Network Stack Initialized (10.0.2.15)");
  // Initialize new net_stack with ARP
  net_stack_init();
}

extern "C" void net_ping(u32 target_ip) {
  u8 buf[128];
  memset(buf, 0, 128);

  eth_hdr *eth = (eth_hdr *)buf;
  ip_hdr *ip = (ip_hdr *)(buf + sizeof(eth_hdr));
  icmp_hdr *icmp = (icmp_hdr *)((u8 *)ip + sizeof(ip_hdr));

  // Use broadcast MAC - QEMU SLIRP will accept and respond
  eth->dst[0] = 0xFF;
  eth->dst[1] = 0xFF;
  eth->dst[2] = 0xFF;
  eth->dst[3] = 0xFF;
  eth->dst[4] = 0xFF;
  eth->dst[5] = 0xFF;
  for (int i = 0; i < 6; i++) {
    eth->src[i] = my_mac[i];
  }
  eth->type = htons(ETH_TYPE_IP);

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = htons(sizeof(ip_hdr) + sizeof(icmp_hdr));
  ip->id = htons(1);
  ip->flags = 0;
  ip->ttl = 64;
  ip->proto = IP_PROTO_ICMP;
  // IP addresses in NETWORK byte order (big endian):
  // 10.0.2.15 = 0x0A, 0x00, 0x02, 0x0F = 0x0A00020F
  // 10.0.2.2  = 0x0A, 0x00, 0x02, 0x02 = 0x0A000202
  ip->src = 0x0F02000A; // 10.0.2.15 in little-endian storage
  ip->dst = 0x0202000A; // 10.0.2.2 in little-endian storage
  ip->checksum = 0;
  ip->checksum = checksum(ip, sizeof(ip_hdr));

  icmp->type = 8; // Echo Request
  icmp->code = 0;
  icmp->id = htons(0x1234);
  icmp->seq = htons(1);
  icmp->checksum = 0;
  icmp->checksum = checksum(icmp, sizeof(icmp_hdr));

  serial_log("NET: Sending ICMP Echo Request to Gateway (10.0.2.2)...");

  // Debug: dump first 20 bytes of packet
  serial_log_hex("PKT[0-3]: ", *(u32 *)&buf[0]);
  serial_log_hex("PKT[4-7]: ", *(u32 *)&buf[4]);
  serial_log_hex("PKT[8-11]: ", *(u32 *)&buf[8]);
  serial_log_hex("PKT[12-15]: ", *(u32 *)&buf[12]);
  serial_log_hex("PKT[16-19]: ", *(u32 *)&buf[16]);

  e1000_send(buf, sizeof(eth_hdr) + sizeof(ip_hdr) + sizeof(icmp_hdr));
}
