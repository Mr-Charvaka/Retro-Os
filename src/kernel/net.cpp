// net.cpp - Core Network Layer with Gateway Routing
// All traffic is routed through the gateway MAC (10.0.2.2)
// QEMU SLIRP requires proper MAC addressing, not broadcast

#include "net.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "e1000.h"

// Our network identity
// Our network identity
static u8 my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
u32 my_ip = 0x0F02000A; // 10.0.2.15

// Gateway info - CRITICAL for routing
static u8 gateway_mac[6] = {0xFF, 0xFF, 0xFF,
                            0xFF, 0xFF, 0xFF}; // Initially broadcast
u32 gateway_ip = 0x0202000A;                   // 10.0.2.2
volatile int gateway_mac_known = 0;

// Byte order helpers
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

// ============== ARP Layer ==============

void send_arp_request(u32 target_ip) {
  u8 buf[64];
  memset(buf, 0, 64);

  eth_hdr *eth = (eth_hdr *)buf;
  arp_pkt *arp = (arp_pkt *)(buf + sizeof(eth_hdr));

  // Broadcast ARP request
  for (int i = 0; i < 6; i++) {
    eth->dst[i] = 0xFF;
    eth->src[i] = my_mac[i];
    arp->sha[i] = my_mac[i];
    arp->tha[i] = 0x00;
  }

  eth->type = htons(ETH_TYPE_ARP);

  arp->htype = htons(1);      // Ethernet
  arp->ptype = htons(0x0800); // IPv4
  arp->hlen = 6;
  arp->plen = 4;
  arp->oper = htons(ARP_REQUEST);
  arp->spa = my_ip;
  arp->tpa = target_ip;

  serial_log("NET: Sending ARP Request for gateway");
  e1000_send(buf, sizeof(eth_hdr) + sizeof(arp_pkt));
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
  u16 oper = htons(arp->oper);

  if (oper == ARP_REQUEST && arp->tpa == my_ip) {
    serial_log("NET: Received ARP Request for our IP");
    send_arp_reply(arp);
  } else if (oper == ARP_REPLY) {
    serial_log("ARP: Received reply!");

    // Check if this is from the gateway
    if (arp->spa == gateway_ip) {
      // Store gateway MAC - THIS IS CRITICAL
      for (int i = 0; i < 6; i++) {
        gateway_mac[i] = arp->sha[i];
      }
      gateway_mac_known = 1;
      serial_log("ARP: Gateway MAC learned!");
      serial_log_hex("  MAC[0-1]: ", (gateway_mac[0] << 8) | gateway_mac[1]);
      serial_log_hex("  MAC[2-3]: ", (gateway_mac[2] << 8) | gateway_mac[3]);
      serial_log_hex("  MAC[4-5]: ", (gateway_mac[4] << 8) | gateway_mac[5]);
    }
  }
}

// ============== ICMP Layer ==============

void send_icmp_reply(ip_hdr *ip, icmp_hdr *icmp, u8 *data, int data_len) {
  u8 buf[512];
  memset(buf, 0, 512);

  eth_hdr *eth_req = (eth_hdr *)((u8 *)ip - sizeof(eth_hdr));
  eth_hdr *eth = (eth_hdr *)buf;
  ip_hdr *ip2 = (ip_hdr *)(buf + sizeof(eth_hdr));
  icmp_hdr *icmp2 = (icmp_hdr *)((u8 *)ip2 + sizeof(ip_hdr));

  // Reply to sender's MAC
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

  if (data_len > 0) {
    memcpy((u8 *)icmp2 + sizeof(icmp_hdr), data, data_len);
  }

  icmp2->checksum = htons(checksum(icmp2, sizeof(icmp_hdr) + data_len));

  serial_log("NET: Sending ICMP Echo Reply");
  e1000_send(buf,
             sizeof(eth_hdr) + sizeof(ip_hdr) + sizeof(icmp_hdr) + data_len);
}

// ============== IP Layer (with Gateway Routing) ==============

extern "C" void ip_send(uint32_t dst_ip, uint8_t protocol, uint8_t *data,
                        uint16_t length) {
  static uint16_t ip_id = 1;
  u8 buf[1500];
  memset(buf, 0, sizeof(buf));

  eth_hdr *eth = (eth_hdr *)buf;
  ip_hdr *ip = (ip_hdr *)(buf + sizeof(eth_hdr));

  // CRITICAL: Use gateway MAC for ALL outgoing packets
  // QEMU SLIRP requires proper MAC addressing
  if (gateway_mac_known) {
    for (int i = 0; i < 6; i++) {
      eth->dst[i] = gateway_mac[i];
      eth->src[i] = my_mac[i];
    }
    serial_log("NET: Using gateway MAC for routing");
  } else {
    // Fallback: use broadcast (may not work for all packets in SLIRP)
    for (int i = 0; i < 6; i++) {
      eth->dst[i] = 0xFF;
      eth->src[i] = my_mac[i];
    }
    serial_log("NET: WARNING - Gateway MAC unknown, using broadcast");
  }

  eth->type = htons(ETH_TYPE_IP);

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = htons(sizeof(ip_hdr) + length);
  ip->id = htons(ip_id++);
  ip->flags = 0;
  ip->ttl = 64;
  ip->proto = protocol;
  ip->src = my_ip;  // 10.0.2.15
  ip->dst = dst_ip; // Destination (e.g., 10.0.2.3 for DNS)
  ip->checksum = 0;
  ip->checksum = checksum(ip, sizeof(ip_hdr));

  memcpy(buf + sizeof(eth_hdr) + sizeof(ip_hdr), data, length);

  serial_log_hex("NET: ip_send proto=", protocol);
  serial_log_hex("NET: ip_send dst=", dst_ip);
  e1000_send(buf, sizeof(eth_hdr) + sizeof(ip_hdr) + length);
}

// ============== Protocol Handlers ==============

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
                                  uint8_t *seg, uint16_t len);

void handle_tcp(u8 *pkt) {
  ip_hdr *ip = (ip_hdr *)pkt;
  int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
  int tcp_len = htons(ip->len) - ip_hdr_len;

  serial_log("NET: Received TCP Packet");
  tcp_handle_packet(ip->src, ip->dst, (u8 *)ip + ip_hdr_len, tcp_len);
}

// ============== Main RX Handler ==============

extern "C" void net_rx_handler(u8 *packet, u16 len) {
  if (len < sizeof(eth_hdr))
    return;

  eth_hdr *eth = (eth_hdr *)packet;
  u16 type = htons(eth->type);

  serial_log_hex("NET: Incoming Packet, len: ", len);

  if (type == ETH_TYPE_ARP) {
    handle_arp(packet);
  } else if (type == ETH_TYPE_IP) {
    ip_hdr *ip = (ip_hdr *)(packet + sizeof(eth_hdr));
    if (ip->proto == IP_PROTO_ICMP) {
      handle_icmp((u8 *)ip);
    } else if (ip->proto == IP_PROTO_UDP) {
      handle_udp((u8 *)ip);
    } else if (ip->proto == IP_PROTO_TCP) {
      handle_tcp((u8 *)ip);
    }
  }
}

// ============== Polling ==============

extern "C" void net_poll(void) {
  // Periodic logging DISABLED to reduce log spam

  u8 buf[2048];
  int len = e1000_receive(buf);
  if (len > 0) {
    net_rx_handler(buf, len);
  }
}

// ============== Initialization ==============

extern "C" void net_init(void) {
  serial_log("NET: Network Stack Initialized (10.0.2.15)");

  // Send ARP request for gateway to learn its MAC
  // This is CRITICAL - without gateway MAC, routing won't work
  send_arp_request(gateway_ip);
}

// ============== Legacy Ping Function ==============

extern "C" void net_ping(void) {
  u8 buf[128];
  memset(buf, 0, 128);

  eth_hdr *eth = (eth_hdr *)buf;
  ip_hdr *ip = (ip_hdr *)(buf + sizeof(eth_hdr));
  icmp_hdr *icmp = (icmp_hdr *)((u8 *)ip + sizeof(ip_hdr));

  // Use gateway MAC if known
  if (gateway_mac_known) {
    for (int i = 0; i < 6; i++) {
      eth->dst[i] = gateway_mac[i];
      eth->src[i] = my_mac[i];
    }
  } else {
    for (int i = 0; i < 6; i++) {
      eth->dst[i] = 0xFF;
      eth->src[i] = my_mac[i];
    }
  }
  eth->type = htons(ETH_TYPE_IP);

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = htons(sizeof(ip_hdr) + sizeof(icmp_hdr));
  ip->id = htons(1);
  ip->flags = 0;
  ip->ttl = 64;
  ip->proto = IP_PROTO_ICMP;
  ip->src = my_ip;
  ip->dst = gateway_ip;
  ip->checksum = 0;
  ip->checksum = checksum(ip, sizeof(ip_hdr));

  icmp->type = 8; // Echo Request
  icmp->code = 0;
  icmp->id = htons(0x1234);
  icmp->seq = htons(1);
  icmp->checksum = 0;
  icmp->checksum = checksum(icmp, sizeof(icmp_hdr));

  serial_log("NET: Sending ICMP Echo Request to Gateway (10.0.2.2)...");
  e1000_send(buf, sizeof(eth_hdr) + sizeof(ip_hdr) + sizeof(icmp_hdr));
}
