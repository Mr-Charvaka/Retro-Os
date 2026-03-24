// net.cpp - Core Network Layer with Gateway Routing
// All traffic is routed through the gateway MAC (10.0.2.2)
// QEMU SLIRP requires proper MAC addressing, not broadcast

#include "net.h"
#include "../drivers/serial.h"
#include "../include/string.h"
#include "e1000.h"

// Our network identity
// Our network identity
extern "C" u8 my_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};
extern "C" uint32_t net_get_local_ip(void);

/* ================= IP REASSEMBLY ================= */
#define MAX_REASSEMBLY_SLOTS 8
#define MAX_REASSEMBLY_SIZE 65535

struct ReassemblySlot {
    u32 src_ip;
    u16 id;
    u8* buffer;
    u16 current_size;
    u16 total_expected;
    u32 timeout;
    bool used;
};

static ReassemblySlot reassembly_table[MAX_REASSEMBLY_SLOTS];
extern "C" uint32_t timer_now_ms(void);
extern "C" void* kmalloc(uint32_t);
extern "C" void kfree(void*);
extern "C" void dns_poll_callbacks(void); // Fix #4: drive async DNS from net tick

// Gateway info - CRITICAL for routing
static u8 gateway_mac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF}; // Initially broadcast
u32 gateway_ip = 0x0202000A;                   // 10.0.2.2
volatile int gateway_mac_known = 0;            // Start unknown to trigger ARP

// Byte order helpers
// Helpers now in net.h

extern "C" uint32_t net_checksum_acc(void *data, int len, uint32_t sum) {
  uint16_t *ptr = (uint16_t *)data;
  int left = len;
  while (left > 1) {
    sum += *ptr++;
    left -= 2;
  }
  if (left) {
    sum += *(uint8_t *)ptr;
  }
  return sum;
}

extern "C" uint16_t net_checksum_finalize(uint32_t sum) {
  while (sum >> 16) {
    sum = (sum & 0xFFFF) + (sum >> 16);
  }
  return ~sum;
}

static u16 checksum(void *data, int len) {
  return net_checksum_finalize(net_checksum_acc(data, len, 0));
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
  arp->spa = net_get_local_ip();
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
  arp->spa = net_get_local_ip();
  arp->tpa = req->spa;

  serial_log("NET: Sending ARP Reply");
  e1000_send(buf, sizeof(eth_hdr) + sizeof(arp_pkt));
}

void handle_arp(u8 *packet) {
  arp_pkt *arp = (arp_pkt *)(packet + sizeof(eth_hdr));
  u16 oper = htons(arp->oper);

  if (oper == ARP_REQUEST && arp->tpa == net_get_local_ip()) {
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
  ip2->src = net_get_local_ip();
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

  if (dst_ip == 0xFFFFFFFF) {
    // Broadcast MAC for DHCP
    for (int i = 0; i < 6; i++) eth->dst[i] = 0xFF;
  } else if (gateway_mac_known) {
    for (int i = 0; i < 6; i++) {
        eth->dst[i] = gateway_mac[i];
    }
  } else {
    // Default fallback to SLIRP router MAC if ARP hasn't succeeded yet
    // QEMU SLIRP Gateway (10.0.2.2) MAC is standard: 52:54:00:12:35:02
    eth->dst[0] = 0x52; eth->dst[1] = 0x54; eth->dst[2] = 0x00;
    eth->dst[3] = 0x12; eth->dst[4] = 0x35; eth->dst[5] = 0x02;
    serial_log("NET: Fallback to QEMU Gateway MAC (Fixed)");
  }
  for (int i = 0; i < 6; i++) eth->src[i] = my_mac[i];

  eth->type = htons(ETH_TYPE_IP);

  ip->ver_ihl = 0x45;
  ip->tos = 0;
  ip->len = htons(sizeof(ip_hdr) + length);
  ip->id = htons(ip_id++);
  ip->flags = 0;
  ip->ttl = 64;
  ip->proto = protocol;
  ip->src = net_get_local_ip();
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
    // Verify checksum
    u16 received_chk = icmp->checksum;
    icmp->checksum = 0;
    u16 calculated_chk = htons(checksum(icmp, icmp_total_len));
    if (received_chk != calculated_chk) {
        serial_log("NET: ICMP Checksum Failed!");
        return;
    }
    icmp->checksum = received_chk;

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

void process_full_ip_packet(u8 *pkt) {
    ip_hdr *ip = (ip_hdr *)pkt;
    if (ip->proto == IP_PROTO_ICMP) {
      handle_icmp((u8 *)ip);
    } else if (ip->proto == IP_PROTO_UDP) {
      handle_udp((u8 *)ip);
    } else if (ip->proto == IP_PROTO_TCP) {
      handle_tcp((u8 *)ip);
    }
}

void handle_ip_fragment(u8 *packet) {
    ip_hdr *ip = (ip_hdr *)(packet + sizeof(eth_hdr));
    u16 frag_info = htons(ip->flags);
    u16 offset = (frag_info & 0x1FFF) * 8;
    bool more_frags = (frag_info & 0x2000) != 0;
    int ip_hdr_len = (ip->ver_ihl & 0x0F) * 4;
    int payload_len = htons(ip->len) - ip_hdr_len;

    if (offset == 0 && !more_frags) {
        // Not a fragment, process normally
        process_full_ip_packet((u8*)ip);
        return;
    }

    // fragmented packet logic
    ReassemblySlot *slot = nullptr;
    for (int i=0; i<MAX_REASSEMBLY_SLOTS; i++) {
        if (reassembly_table[i].used && reassembly_table[i].src_ip == ip->src && reassembly_table[i].id == ip->id) {
            slot = &reassembly_table[i];
            break;
        }
    }

    if (!slot) {
        // Find free slot
        for (int i=0; i<MAX_REASSEMBLY_SLOTS; i++) {
            if (!reassembly_table[i].used) {
                slot = &reassembly_table[i];
                slot->used = true;
                slot->src_ip = ip->src;
                slot->id = ip->id;
                slot->buffer = (u8*)kmalloc(MAX_REASSEMBLY_SIZE);
                slot->current_size = 0;
                slot->total_expected = 0;
                slot->timeout = timer_now_ms() + 5000;
                break;
            }
        }
    }

    if (!slot) return; // Drop if no slots

    // Copy payload to slot buffer at correct offset
    if (offset + payload_len <= MAX_REASSEMBLY_SIZE) {
        memcpy(slot->buffer + offset, (u8*)ip + ip_hdr_len, payload_len);
        if (offset + payload_len > slot->current_size) {
            slot->current_size = offset + payload_len;
        }
    }

    if (!more_frags) {
        slot->total_expected = offset + payload_len;
    }

    // Check if fully reassembled (simplified check)
    if (slot->total_expected > 0 && slot->current_size >= slot->total_expected) {
        // We have the full packet. Create a temporary full IP buffer to pass to handlers
        u8* full_pkt = (u8*)kmalloc(slot->total_expected + sizeof(ip_hdr));
        ip_hdr* new_ip = (ip_hdr*)full_pkt;
        memcpy(new_ip, ip, sizeof(ip_hdr));
        new_ip->len = htons(slot->total_expected + sizeof(ip_hdr));
        new_ip->flags = 0; // Clear flags
        memcpy(full_pkt + sizeof(ip_hdr), slot->buffer, slot->total_expected);
        
        process_full_ip_packet(full_pkt);
        
        kfree(full_pkt);
        kfree(slot->buffer);
        slot->used = false;
    }
}

// ============== Main RX Handler ==============

extern "C" void net_rx_handler(u8 *packet, u16 len) {
  if (len < sizeof(eth_hdr))
    return;

  eth_hdr *eth = (eth_hdr *)packet;
  u16 type = htons(eth->type);

  // Auto-learn gateway MAC from any incoming IP packet
  if (type == ETH_TYPE_IP) {
    ip_hdr *ip = (ip_hdr *)(packet + sizeof(eth_hdr));
    if (!gateway_mac_known || ip->src == gateway_ip) {
        for (int i = 0; i < 6; i++) {
            gateway_mac[i] = eth->src[i];
        }
        gateway_mac_known = 1;
         serial_log("NET: Learned/Updated gateway MAC from incoming IP");
    }
  }

  // serial_log_hex("NET: Incoming Packet, len: ", len);

  if (type == ETH_TYPE_ARP) {
    handle_arp(packet);
  } else if (type == ETH_TYPE_IP) {
    handle_ip_fragment(packet);
  }
}

// ============== Polling ==============

static volatile int in_net_poll = 0;
static u8 net_rx_buffer[2048]; // Move out of stack to prevent overflow

extern "C" void net_poll(void) {
  if (__sync_lock_test_and_set(&in_net_poll, 1)) return;

  // ── Async DNS: drive pending callbacks (Fix #4) ──────────────────────────
  dns_poll_callbacks();

  // Check for timed out reassembly slots
  static u32 last_cleanup = 0;
  u32 now = timer_now_ms();
  if (now - last_cleanup > 1000) {
      for (int i=0; i<MAX_REASSEMBLY_SLOTS; i++) {
          if (reassembly_table[i].used && now > reassembly_table[i].timeout) {
              kfree(reassembly_table[i].buffer);
              reassembly_table[i].used = false;
              serial_log("NET: IP Reassembly Timeout");
          }
      }
      last_cleanup = now;
  }

  int processed = 0;
  while (processed < 32) { // Process up to 32 packets in a burst
      int len = e1000_receive(net_rx_buffer);
      if (len <= 0) break;
      net_rx_handler(net_rx_buffer, len);
      processed++;
  }

  __sync_lock_release(&in_net_poll);
}

// ============== Initialization ==============

extern "C" void net_init(void) {
  serial_log("NET: Network Stack Initialized");

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
  ip->src = net_get_local_ip();
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
