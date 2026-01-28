// dhcp.cpp - DHCP Client for Retro-OS
// RFC 2131 compliant DHCP client for dynamic IP configuration

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "net.h"
#include <stddef.h>
#include <stdint.h>

/* ===================== CONFIG ===================== */

#define DHCP_SERVER_PORT 67
#define DHCP_CLIENT_PORT 68
#define DHCP_MAGIC_COOKIE 0x63825363

/* ===================== DHCP MESSAGE TYPES ===================== */

#define DHCP_DISCOVER 1
#define DHCP_OFFER 2
#define DHCP_REQUEST 3
#define DHCP_DECLINE 4
#define DHCP_ACK 5
#define DHCP_NAK 6
#define DHCP_RELEASE 7
#define DHCP_INFORM 8

/* ===================== DHCP OPTIONS ===================== */

#define DHCP_OPT_PAD 0
#define DHCP_OPT_SUBNET 1
#define DHCP_OPT_ROUTER 3
#define DHCP_OPT_DNS 6
#define DHCP_OPT_HOSTNAME 12
#define DHCP_OPT_DOMAIN 15
#define DHCP_OPT_BROADCAST 28
#define DHCP_OPT_REQUESTED_IP 50
#define DHCP_OPT_LEASE_TIME 51
#define DHCP_OPT_MSG_TYPE 53
#define DHCP_OPT_SERVER_ID 54
#define DHCP_OPT_PARAM_REQ 55
#define DHCP_OPT_END 255

/* ===================== STRUCTURES ===================== */

struct dhcp_packet {
  uint8_t op;    // 1=Request, 2=Reply
  uint8_t htype; // Hardware type (1=Ethernet)
  uint8_t hlen;  // Hardware address length (6)
  uint8_t hops;
  uint32_t xid; // Transaction ID
  uint16_t secs;
  uint16_t flags;
  uint32_t ciaddr;      // Client IP
  uint32_t yiaddr;      // Your (client) IP
  uint32_t siaddr;      // Server IP
  uint32_t giaddr;      // Gateway IP
  uint8_t chaddr[16];   // Client hardware address
  uint8_t sname[64];    // Server hostname
  uint8_t file[128];    // Boot filename
  uint32_t magic;       // Magic cookie
  uint8_t options[308]; // Options
} __attribute__((packed));

/* ===================== NETWORK CONFIG STATE ===================== */

struct network_config {
  uint32_t ip_addr;
  uint32_t subnet_mask;
  uint32_t gateway;
  uint32_t dns_server;
  uint32_t dhcp_server;
  uint32_t lease_time;
  uint32_t lease_start;
  int configured;
};

static network_config net_config;
static uint32_t dhcp_xid = 0x12345678;
static volatile int dhcp_state = 0; // 0=init, 1=discover, 2=request, 3=bound

/* ===================== BYTE ORDER ===================== */

static inline uint16_t dhcp_htons(uint16_t x) { return (x << 8) | (x >> 8); }
static inline uint32_t dhcp_htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t dhcp_ntohl(uint32_t x) { return dhcp_htonl(x); }

/* ===================== HELPERS ===================== */

extern "C" uint32_t timer_now_ms(void);
extern "C" void udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                         uint16_t dst_port, uint8_t *data, uint16_t length);
extern "C" void udp_bind(uint16_t port, void (*handler)(uint32_t, uint16_t,
                                                        uint8_t *, uint16_t));
extern "C" void schedule(void);
extern "C" void net_poll(void);

// MAC address (should match e1000 driver)
static uint8_t local_mac[6] = {0x52, 0x54, 0x00, 0x12, 0x34, 0x56};

/* ===================== OPTION PARSING ===================== */

static uint8_t *dhcp_find_option(uint8_t *options, int len, uint8_t opt_code) {
  int i = 0;
  while (i < len) {
    if (options[i] == DHCP_OPT_PAD) {
      i++;
      continue;
    }
    if (options[i] == DHCP_OPT_END) {
      break;
    }
    if (options[i] == opt_code) {
      return &options[i];
    }
    i += 2 + options[i + 1]; // Skip option
  }
  return nullptr;
}

static int dhcp_add_option(uint8_t *buf, int pos, uint8_t code, uint8_t len,
                           void *data) {
  buf[pos++] = code;
  buf[pos++] = len;
  memcpy(&buf[pos], data, len);
  return pos + len;
}

/* ===================== SEND DHCP DISCOVER ===================== */

static void dhcp_send_discover() {
  dhcp_packet pkt;
  memset(&pkt, 0, sizeof(pkt));

  pkt.op = 1;    // BOOTREQUEST
  pkt.htype = 1; // Ethernet
  pkt.hlen = 6;
  pkt.hops = 0;
  pkt.xid = dhcp_htonl(dhcp_xid);
  pkt.secs = 0;
  pkt.flags = dhcp_htons(0x8000); // Broadcast flag
  pkt.ciaddr = 0;
  pkt.yiaddr = 0;
  pkt.siaddr = 0;
  pkt.giaddr = 0;
  memcpy(pkt.chaddr, local_mac, 6);
  pkt.magic = dhcp_htonl(DHCP_MAGIC_COOKIE);

  // Build options
  int opt_pos = 0;
  uint8_t msg_type = DHCP_DISCOVER;
  opt_pos =
      dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_MSG_TYPE, 1, &msg_type);

  // Request subnet, router, DNS
  uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
  opt_pos =
      dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_PARAM_REQ, 3, params);

  pkt.options[opt_pos++] = DHCP_OPT_END;

  serial_log("DHCP: Sending DISCOVER...");
  dhcp_state = 1;

  // Broadcast to 255.255.255.255
  udp_send(0, DHCP_CLIENT_PORT, 0xFFFFFFFF, DHCP_SERVER_PORT, (uint8_t *)&pkt,
           sizeof(pkt));
}

/* ===================== SEND DHCP REQUEST ===================== */

static void dhcp_send_request(uint32_t offered_ip, uint32_t server_ip) {
  dhcp_packet pkt;
  memset(&pkt, 0, sizeof(pkt));

  pkt.op = 1;
  pkt.htype = 1;
  pkt.hlen = 6;
  pkt.xid = dhcp_htonl(dhcp_xid);
  pkt.flags = dhcp_htons(0x8000);
  memcpy(pkt.chaddr, local_mac, 6);
  pkt.magic = dhcp_htonl(DHCP_MAGIC_COOKIE);

  int opt_pos = 0;
  uint8_t msg_type = DHCP_REQUEST;
  opt_pos =
      dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_MSG_TYPE, 1, &msg_type);
  opt_pos = dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_REQUESTED_IP, 4,
                            &offered_ip);
  opt_pos =
      dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_SERVER_ID, 4, &server_ip);

  uint8_t params[] = {DHCP_OPT_SUBNET, DHCP_OPT_ROUTER, DHCP_OPT_DNS};
  opt_pos =
      dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_PARAM_REQ, 3, params);

  pkt.options[opt_pos++] = DHCP_OPT_END;

  serial_log("DHCP: Sending REQUEST...");
  dhcp_state = 2;

  udp_send(0, DHCP_CLIENT_PORT, 0xFFFFFFFF, DHCP_SERVER_PORT, (uint8_t *)&pkt,
           sizeof(pkt));
}

/* ===================== DHCP RESPONSE HANDLER ===================== */

static void dhcp_rx_handler(uint32_t src_ip, uint16_t src_port, uint8_t *data,
                            uint16_t length) {
  (void)src_ip;
  (void)src_port;

  if (length < sizeof(dhcp_packet) - 308) {
    serial_log("DHCP: Packet too short");
    return;
  }

  dhcp_packet *pkt = (dhcp_packet *)data;

  // Verify transaction ID
  if (dhcp_ntohl(pkt->xid) != dhcp_xid) {
    serial_log("DHCP: XID mismatch");
    return;
  }

  // Verify magic cookie
  if (dhcp_ntohl(pkt->magic) != DHCP_MAGIC_COOKIE) {
    serial_log("DHCP: Invalid magic cookie");
    return;
  }

  // Find message type
  uint8_t *msg_opt = dhcp_find_option(pkt->options, 308, DHCP_OPT_MSG_TYPE);
  if (!msg_opt) {
    serial_log("DHCP: No message type");
    return;
  }

  uint8_t msg_type = msg_opt[2];

  switch (msg_type) {
  case DHCP_OFFER: {
    if (dhcp_state != 1)
      return;

    serial_log("DHCP: Received OFFER");
    serial_log_hex("DHCP: Offered IP: ", pkt->yiaddr);

    uint32_t offered_ip = pkt->yiaddr;
    uint32_t server_ip = pkt->siaddr;

    // Get server ID from options if siaddr is 0
    uint8_t *srv_opt = dhcp_find_option(pkt->options, 308, DHCP_OPT_SERVER_ID);
    if (srv_opt) {
      server_ip = *(uint32_t *)(srv_opt + 2);
    }

    net_config.dhcp_server = server_ip;
    dhcp_send_request(offered_ip, server_ip);
    break;
  }

  case DHCP_ACK: {
    if (dhcp_state != 2)
      return;

    serial_log("DHCP: Received ACK - Configuration complete!");

    net_config.ip_addr = pkt->yiaddr;
    net_config.lease_start = timer_now_ms();

    // Parse options
    uint8_t *opt;

    if ((opt = dhcp_find_option(pkt->options, 308, DHCP_OPT_SUBNET))) {
      net_config.subnet_mask = *(uint32_t *)(opt + 2);
    }

    if ((opt = dhcp_find_option(pkt->options, 308, DHCP_OPT_ROUTER))) {
      net_config.gateway = *(uint32_t *)(opt + 2);
    }

    if ((opt = dhcp_find_option(pkt->options, 308, DHCP_OPT_DNS))) {
      net_config.dns_server = *(uint32_t *)(opt + 2);
    }

    if ((opt = dhcp_find_option(pkt->options, 308, DHCP_OPT_LEASE_TIME))) {
      net_config.lease_time = dhcp_ntohl(*(uint32_t *)(opt + 2));
    }

    net_config.configured = 1;
    dhcp_state = 3;

    // Log configuration
    serial_log("DHCP: Network Configured:");
    serial_log_hex("  IP:      ", net_config.ip_addr);
    serial_log_hex("  Subnet:  ", net_config.subnet_mask);
    serial_log_hex("  Gateway: ", net_config.gateway);
    serial_log_hex("  DNS:     ", net_config.dns_server);
    serial_log_hex("  Lease:   ", net_config.lease_time);
    break;
  }

  case DHCP_NAK: {
    serial_log("DHCP: Received NAK - restarting discovery");
    dhcp_state = 0;
    dhcp_xid++;
    dhcp_send_discover();
    break;
  }
  }
}

/* ===================== PUBLIC API ===================== */

extern "C" int dhcp_configure() {
  dhcp_state = 0;
  dhcp_xid = timer_now_ms() ^ 0xDEADBEEF;
  memset(&net_config, 0, sizeof(net_config));

  dhcp_send_discover();

  // Wait for configuration (with timeout)
  uint32_t start = timer_now_ms();
  while (timer_now_ms() - start < 10000) { // 10 second timeout
    if (net_config.configured) {
      return 0; // Success
    }
    net_poll();
    schedule();
  }

  serial_log("DHCP: Configuration timeout");
  return -1;
}

extern "C" int dhcp_is_configured() { return net_config.configured; }

extern "C" uint32_t dhcp_get_ip() { return net_config.ip_addr; }

extern "C" uint32_t dhcp_get_gateway() { return net_config.gateway; }

extern "C" uint32_t dhcp_get_dns() { return net_config.dns_server; }

extern "C" uint32_t dhcp_get_subnet() { return net_config.subnet_mask; }

extern "C" void dhcp_release() {
  if (!net_config.configured)
    return;

  dhcp_packet pkt;
  memset(&pkt, 0, sizeof(pkt));

  pkt.op = 1;
  pkt.htype = 1;
  pkt.hlen = 6;
  pkt.xid = dhcp_htonl(dhcp_xid);
  pkt.ciaddr = net_config.ip_addr;
  memcpy(pkt.chaddr, local_mac, 6);
  pkt.magic = dhcp_htonl(DHCP_MAGIC_COOKIE);

  int opt_pos = 0;
  uint8_t msg_type = DHCP_RELEASE;
  opt_pos =
      dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_MSG_TYPE, 1, &msg_type);
  opt_pos = dhcp_add_option(pkt.options, opt_pos, DHCP_OPT_SERVER_ID, 4,
                            &net_config.dhcp_server);
  pkt.options[opt_pos++] = DHCP_OPT_END;

  udp_send(net_config.ip_addr, DHCP_CLIENT_PORT, net_config.dhcp_server,
           DHCP_SERVER_PORT, (uint8_t *)&pkt, sizeof(pkt));

  memset(&net_config, 0, sizeof(net_config));
  dhcp_state = 0;

  serial_log("DHCP: Released IP address");
}

// Check if lease needs renewal
extern "C" void dhcp_check_lease() {
  if (!net_config.configured)
    return;

  uint32_t elapsed = (timer_now_ms() - net_config.lease_start) / 1000;

  // Renew at T1 (50% of lease time)
  if (elapsed > net_config.lease_time / 2 && dhcp_state == 3) {
    serial_log("DHCP: Initiating lease renewal");
    dhcp_xid++;
    dhcp_send_request(net_config.ip_addr, net_config.dhcp_server);
  }
}

extern "C" void dhcp_init() {
  memset(&net_config, 0, sizeof(net_config));
  udp_bind(DHCP_CLIENT_PORT, dhcp_rx_handler);
  serial_log("DHCP: Client initialized");
}
