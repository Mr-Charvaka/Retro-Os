// net_init.cpp - Advanced Network Stack Initialization for Retro-OS
// Initializes and coordinates all network components

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "net_advanced.h"
#include <stddef.h>
#include <stdint.h>

/* ===================== EXTERNAL INIT FUNCTIONS ===================== */

extern "C" void dns_init(void);
extern "C" void dhcp_init(void);
extern "C" void http_init(void);
extern "C" void socket_api_init(void);
extern "C" void tcp_init(void);
extern "C" void udp_init(void);

// From existing net.cpp
extern "C" void net_init(void);
extern "C" void net_ping(uint32_t target_ip);

// DHCP functions
extern "C" int dhcp_configure(void);
extern "C" int dhcp_is_configured(void);
extern "C" uint32_t dhcp_get_ip(void);
extern "C" uint32_t dhcp_get_gateway(void);
extern "C" uint32_t dhcp_get_dns(void);

/* ===================== STATUS TRACKING ===================== */

static struct net_status global_net_status;

/* ===================== NETWORK ADVANCED INIT ===================== */

extern "C" void net_advanced_init(void) {
  serial_log("========================================");
  serial_log("NET_ADV: Initializing Advanced Network Stack...");
  serial_log("========================================");

  // Clear status
  memset(&global_net_status, 0, sizeof(global_net_status));

  // Initialize base networking first (already done in Kernel.cpp, but safe to
  // call) net_init();  // Commented - already called in kernel

  // Initialize transport layers
  serial_log("NET_ADV: Initializing TCP...");
  tcp_init();

  serial_log("NET_ADV: Initializing UDP...");
  udp_init();

  // Initialize socket API
  serial_log("NET_ADV: Initializing Socket API...");
  socket_api_init();

  // Initialize DHCP client
  serial_log("NET_ADV: Initializing DHCP Client...");
  dhcp_init();

  // Initialize DNS resolver
  serial_log("NET_ADV: Initializing DNS Resolver...");
  dns_init();

  // Initialize HTTP client
  serial_log("NET_ADV: Initializing HTTP Client...");
  http_init();

  serial_log("NET_ADV: All components initialized!");
  serial_log("========================================");

  // Attempt DHCP configuration (optional - uses static IP as fallback)
  serial_log("NET_ADV: Attempting DHCP configuration...");
  if (dhcp_configure() == 0) {
    serial_log("NET_ADV: DHCP configuration successful!");
    global_net_status.dhcp_configured = 1;
    global_net_status.ip_addr = dhcp_get_ip();
    global_net_status.gateway = dhcp_get_gateway();
    global_net_status.dns_server = dhcp_get_dns();
  } else {
    serial_log("NET_ADV: DHCP failed, using static configuration");
    serial_log("NET_ADV: IP: 10.0.2.15 (QEMU default)");
    global_net_status.dhcp_configured = 0;
    global_net_status.ip_addr = 0x0F02000A;    // 10.0.2.15
    global_net_status.gateway = 0x0202000A;    // 10.0.2.2
    global_net_status.dns_server = 0x0302000A; // 10.0.2.3 (QEMU SLIRP DNS)
  }

  global_net_status.link_up = 1;

  serial_log("========================================");
  serial_log("NET_ADV: Network Stack Ready!");
  serial_log("========================================");
}

/* ===================== STATUS FUNCTIONS ===================== */

extern "C" void net_get_status(struct net_status *status) {
  if (status) {
    memcpy(status, &global_net_status, sizeof(struct net_status));
  }
}

extern "C" void net_update_stats(uint32_t pkt_sent, uint32_t pkt_recv,
                                 uint32_t bytes_sent, uint32_t bytes_recv) {
  global_net_status.packets_sent += pkt_sent;
  global_net_status.packets_received += pkt_recv;
  global_net_status.bytes_sent += bytes_sent;
  global_net_status.bytes_received += bytes_recv;
}

/* ===================== CONVENIENCE FUNCTIONS ===================== */

// Print current network status to serial
extern "C" void net_print_status(void) {
  serial_log("--- Network Status ---");
  serial_log_hex("  Link:     ", global_net_status.link_up);
  serial_log_hex("  DHCP:     ", global_net_status.dhcp_configured);
  serial_log_hex("  IP:       ", global_net_status.ip_addr);
  serial_log_hex("  Gateway:  ", global_net_status.gateway);
  serial_log_hex("  DNS:      ", global_net_status.dns_server);
  serial_log_hex("  TX Pkts:  ", global_net_status.packets_sent);
  serial_log_hex("  RX Pkts:  ", global_net_status.packets_received);
  serial_log("----------------------");
}

// Check if network is ready for use
extern "C" int net_is_ready(void) {
  return global_net_status.link_up && (global_net_status.ip_addr != 0);
}

// Get local IP address
extern "C" uint32_t net_get_local_ip(void) { return global_net_status.ip_addr; }

// Get gateway IP address
extern "C" uint32_t net_get_gateway_ip(void) {
  return global_net_status.gateway;
}

// Get DNS server IP address
extern "C" uint32_t net_get_dns_ip(void) {
  return global_net_status.dns_server;
}

/* ===================== NETWORK TEST FUNCTIONS ===================== */

// Test DNS resolution
extern "C" int net_test_dns(const char *hostname) {
  serial_log("NET_TEST: Testing DNS resolution...");
  serial_log(hostname);

  uint32_t ip = dns_resolve(hostname);

  if (ip) {
    serial_log("NET_TEST: DNS resolution successful!");
    serial_log_hex("NET_TEST: Resolved IP: ", ip);
    return 0;
  } else {
    serial_log("NET_TEST: DNS resolution FAILED");
    return -1;
  }
}

// Test HTTP GET
extern "C" int net_test_http(const char *url) {
  serial_log("NET_TEST: Testing HTTP GET...");
  serial_log(url);

  uint8_t *buffer = (uint8_t *)kmalloc(4096);
  if (!buffer) {
    serial_log("NET_TEST: Buffer allocation failed");
    return -1;
  }

  int received = http_get(url, buffer, 4096 - 1, nullptr);

  if (received > 0) {
    buffer[received < 4095 ? received : 4095] = 0;
    serial_log("NET_TEST: HTTP GET successful!");
    serial_log_hex("NET_TEST: Received bytes: ", received);
    // Print first 200 chars of response
    serial_log("NET_TEST: Response preview:");
    char preview[201];
    int copy_len = received < 200 ? received : 200;
    memcpy(preview, buffer, copy_len);
    preview[copy_len] = 0;
    serial_log(preview);
    kfree(buffer);
    return 0;
  } else {
    serial_log("NET_TEST: HTTP GET FAILED");
    kfree(buffer);
    return -1;
  }
}

// Full network self-test
extern "C" int net_self_test(void) {
  serial_log("========================================");
  serial_log("NET_TEST: Running Network Self-Test...");
  serial_log("========================================");

  int failures = 0;

  // Test 1: Check link status
  serial_log("NET_TEST: [1/4] Checking link status...");
  if (net_is_ready()) {
    serial_log("NET_TEST: PASS - Network is ready");
  } else {
    serial_log("NET_TEST: FAIL - Network not ready");
    failures++;
  }

  // Test 2: DNS resolution - use example.com
  serial_log("NET_TEST: [2/4] Testing DNS resolution (example.com)...");
  if (net_test_dns("example.com") == 0) {
    serial_log("NET_TEST: PASS - DNS working");
  } else {
    serial_log("NET_TEST: FAIL - DNS failed");
    failures++;
  }

  // Test 3: ICMP ping to gateway
  serial_log("NET_TEST: [3/4] Testing ICMP ping to gateway...");
  net_ping(global_net_status.gateway);
  serial_log("NET_TEST: Ping sent (check for reply in net_poll)");

  // Test 4: HTTP test (optional - may timeout without real network)
  // Test 4: HTTP test
  serial_log("NET_TEST: [4/4] Testing HTTP GET (example.com)...");
  if (net_test_http("http://example.com") == 0) {
    serial_log("NET_TEST: PASS - HTTP working");
  } else {
    serial_log("NET_TEST: FAIL - HTTP failed");
    failures++;
  }

  serial_log("========================================");
  if (failures == 0) {
    serial_log("NET_TEST: All tests PASSED!");
  } else {
    serial_log_hex("NET_TEST: Tests FAILED: ", failures);
  }
  serial_log("========================================");

  return failures;
}
