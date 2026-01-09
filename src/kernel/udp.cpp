// udp.cpp - Kernel UDP implementation
// Works with e1000 + IPv4 stack

#include "../drivers/serial.h"
#include "../include/string.h"
#include "net.h"
#include <stddef.h>
#include <stdint.h>


/* ===================== CONFIG ===================== */

#define UDP_MAX_PORTS 256   // Reduced from 65536 for kernel memory
#define UDP_MAX_PACKET 1472 // MTU - IP header - UDP header

/* ===================== HELPERS ===================== */

static inline uint16_t udp_htons(uint16_t x) { return (x << 8) | (x >> 8); }

static inline uint16_t udp_ntohs(uint16_t x) { return udp_htons(x); }

/* ===================== PORT HANDLER ===================== */

typedef void (*udp_handler_t)(uint32_t src_ip, uint16_t src_port, uint8_t *data,
                              uint16_t length);

static udp_handler_t udp_port_table[UDP_MAX_PORTS];

/* ===================== CHECKSUM ===================== */

struct udp_pseudo_header {
  uint32_t src_ip;
  uint32_t dst_ip;
  uint8_t zero;
  uint8_t protocol;
  uint16_t udp_length;
} __attribute__((packed));

static uint16_t udp_checksum(uint32_t src_ip, uint32_t dst_ip, udp_hdr *udp,
                             uint8_t *payload, uint16_t payload_len) {
  uint32_t sum = 0;

  udp_pseudo_header pseudo;
  pseudo.src_ip = src_ip;
  pseudo.dst_ip = dst_ip;
  pseudo.zero = 0;
  pseudo.protocol = 17; // UDP
  pseudo.udp_length = udp->len;

  uint16_t *ptr = (uint16_t *)&pseudo;
  for (size_t i = 0; i < sizeof(pseudo) / 2; i++)
    sum += udp_ntohs(ptr[i]);

  ptr = (uint16_t *)udp;
  for (size_t i = 0; i < sizeof(udp_hdr) / 2; i++)
    sum += udp_ntohs(ptr[i]);

  ptr = (uint16_t *)payload;
  for (size_t i = 0; i < payload_len / 2; i++)
    sum += udp_ntohs(ptr[i]);

  if (payload_len & 1)
    sum += payload[payload_len - 1] << 8;

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);

  return udp_htons(~sum);
}

/* ===================== PUBLIC API ===================== */

extern "C" void udp_bind(uint16_t port, udp_handler_t handler) {
  if (port < UDP_MAX_PORTS) {
    udp_port_table[port] = handler;
    serial_log_hex("UDP: Bound port ", port);
  }
}

extern "C" void udp_unbind(uint16_t port) {
  if (port < UDP_MAX_PORTS) {
    udp_port_table[port] = nullptr;
  }
}

/* ===================== RX ENTRY ===================== */

extern "C" void udp_receive(uint32_t src_ip, uint32_t dst_ip, uint8_t *packet,
                            uint16_t length) {
  if (length < sizeof(udp_hdr)) {
    serial_log("UDP: Packet too short");
    return;
  }

  udp_hdr *udp = (udp_hdr *)packet;

  uint16_t dst_port = udp_ntohs(udp->dst);
  uint16_t src_port = udp_ntohs(udp->src);
  uint16_t udp_len = udp_ntohs(udp->len);

  serial_log_hex("UDP: Received packet, dst_port=", dst_port);
  serial_log_hex("UDP: src_port=", src_port);
  serial_log_hex("UDP: length=", udp_len);

  if (udp_len < sizeof(udp_hdr))
    return;

  uint8_t *payload = packet + sizeof(udp_hdr);
  uint16_t payload_len = udp_len - sizeof(udp_hdr);

  if (dst_port >= UDP_MAX_PORTS || !udp_port_table[dst_port]) {
    serial_log("UDP: No handler for this port");
    return;
  }

  udp_port_table[dst_port](src_ip, src_port, payload, payload_len);
}

/* ===================== TX API ===================== */

// Forward declaration - must be implemented in net.cpp
extern "C" void ip_send(uint32_t dst_ip, uint8_t protocol, uint8_t *data,
                        uint16_t length);

extern "C" void udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                         uint16_t dst_port, uint8_t *data, uint16_t length) {
  if (length > UDP_MAX_PACKET) {
    serial_log("UDP: Packet too large");
    return;
  }

  uint16_t total_len = sizeof(udp_hdr) + length;

  uint8_t buffer[1500]; // Safe MTU
  udp_hdr *udp = (udp_hdr *)buffer;

  udp->src = udp_htons(src_port);
  udp->dst = udp_htons(dst_port);
  udp->len = udp_htons(total_len);
  udp->checksum = 0;

  memcpy(buffer + sizeof(udp_hdr), data, length);

  udp->checksum =
      udp_checksum(src_ip, dst_ip, udp, buffer + sizeof(udp_hdr), length);

  serial_log_hex("UDP: Sending packet to port ", dst_port);
  ip_send(dst_ip, 17, buffer, total_len);
}

/* ===================== INIT ===================== */

extern "C" void udp_init() {
  for (int i = 0; i < UDP_MAX_PORTS; i++) {
    udp_port_table[i] = nullptr;
  }
  serial_log("UDP: Stack initialized");
}
