// tcp.cpp - Kernel TCP implementation
// Full TCP stack with handshake, data transfer, and connection management

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include <stddef.h>
#include <stdint.h>


/* ================= CONFIG ================= */

#define MAX_TCP_CONNECTIONS 16
#define TCP_DEFAULT_WINDOW 4096
#define IPPROTO_TCP 6

/* ================= TCP FLAGS ================= */

#define TCP_FIN 0x01
#define TCP_SYN 0x02
#define TCP_RST 0x04
#define TCP_PSH 0x08
#define TCP_ACK 0x10

/* ================= TCP STATES ================= */

typedef enum {
  TCP_CLOSED,
  TCP_LISTEN,
  TCP_SYN_SENT,
  TCP_SYN_RECEIVED,
  TCP_ESTABLISHED,
  TCP_FIN_WAIT_1,
  TCP_FIN_WAIT_2,
  TCP_CLOSE_WAIT,
  TCP_LAST_ACK,
  TCP_TIME_WAIT
} tcp_state_t;

/* ================= TCP HEADER ================= */

typedef struct __attribute__((packed)) {
  uint16_t src_port;
  uint16_t dst_port;
  uint32_t seq;
  uint32_t ack;
  uint8_t offset_reserved;
  uint8_t flags;
  uint16_t window;
  uint16_t checksum;
  uint16_t urgent_ptr;
} tcp_header_t;

/* ================= TCP CONTROL BLOCK ================= */

typedef struct {
  int used;

  uint32_t local_ip;
  uint32_t remote_ip;
  uint16_t local_port;
  uint16_t remote_port;

  uint32_t snd_una; // Oldest unacknowledged sequence number
  uint32_t snd_nxt; // Next sequence number to send
  uint32_t rcv_nxt; // Next expected receive sequence number

  uint16_t snd_wnd; // Send window
  uint16_t rcv_wnd; // Receive window

  tcp_state_t state;

  // Receive buffer for incoming data
  uint8_t *rx_buffer;
  uint16_t rx_len;
  uint16_t rx_capacity;
} tcp_tcb_t;

/* ================= GLOBALS ================= */

static tcp_tcb_t tcp_table[MAX_TCP_CONNECTIONS];

/* ================= BYTE ORDER HELPERS ================= */

static inline uint16_t tcp_htons(uint16_t x) { return (x << 8) | (x >> 8); }

static inline uint16_t tcp_ntohs(uint16_t x) { return tcp_htons(x); }

static inline uint32_t tcp_htonl(uint32_t x) {
  return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x & 0xFF0000) >> 8) |
         ((x & 0xFF000000) >> 24);
}

static inline uint32_t tcp_ntohl(uint32_t x) { return tcp_htonl(x); }

/* ================= TCP HELPERS ================= */

static tcp_tcb_t *tcp_alloc_tcb() {
  for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
    if (!tcp_table[i].used) {
      memset(&tcp_table[i], 0, sizeof(tcp_tcb_t));
      tcp_table[i].used = 1;
      tcp_table[i].rx_capacity = 4096;
      tcp_table[i].rx_buffer = (uint8_t *)kmalloc(4096);
      tcp_table[i].rx_len = 0;
      return &tcp_table[i];
    }
  }
  return nullptr;
}

static void tcp_free_tcb(tcp_tcb_t *tcb) {
  if (tcb && tcb->used) {
    if (tcb->rx_buffer) {
      kfree(tcb->rx_buffer);
    }
    memset(tcb, 0, sizeof(tcp_tcb_t));
  }
}

static tcp_tcb_t *tcp_find(uint32_t src_ip, uint32_t dst_ip, uint16_t src_port,
                           uint16_t dst_port) {
  for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
    tcp_tcb_t *t = &tcp_table[i];
    if (!t->used)
      continue;
    if (t->local_ip == dst_ip && t->remote_ip == src_ip &&
        t->local_port == dst_port && t->remote_port == src_port)
      return t;
  }
  return nullptr;
}

/* ================= TCP CHECKSUM ================= */

static uint16_t tcp_checksum(uint32_t src_ip, uint32_t dst_ip,
                             tcp_header_t *tcp, uint16_t tcp_len) {
  struct {
    uint32_t src;
    uint32_t dst;
    uint8_t zero;
    uint8_t proto;
    uint16_t len;
  } __attribute__((packed)) pseudo;

  pseudo.src = src_ip;
  pseudo.dst = dst_ip;
  pseudo.zero = 0;
  pseudo.proto = IPPROTO_TCP;
  pseudo.len = tcp_htons(tcp_len);

  uint32_t sum = 0;
  uint16_t *p = (uint16_t *)&pseudo;
  for (size_t i = 0; i < sizeof(pseudo) / 2; i++)
    sum += p[i];

  p = (uint16_t *)tcp;
  for (size_t i = 0; i < tcp_len / 2; i++)
    sum += p[i];

  // Handle odd byte
  if (tcp_len & 1)
    sum += ((uint8_t *)tcp)[tcp_len - 1];

  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);

  return ~sum;
}

/* ================= TCP SEND SEGMENT ================= */

// Forward declaration
extern "C" void ip_send(uint32_t dst_ip, uint8_t protocol, uint8_t *data,
                        uint16_t length);

static void tcp_send_segment(tcp_tcb_t *tcb, uint8_t flags, void *data,
                             uint16_t len) {
  size_t total_len = sizeof(tcp_header_t) + len;
  uint8_t *buffer = (uint8_t *)kmalloc(total_len);
  tcp_header_t *tcp = (tcp_header_t *)buffer;

  memset(tcp, 0, sizeof(tcp_header_t));

  tcp->src_port = tcb->local_port;
  tcp->dst_port = tcb->remote_port;
  tcp->seq = tcp_htonl(tcb->snd_nxt);
  tcp->ack = tcp_htonl(tcb->rcv_nxt);
  tcp->offset_reserved = (sizeof(tcp_header_t) / 4) << 4;
  tcp->flags = flags;
  tcp->window = tcp_htons(tcb->rcv_wnd);
  tcp->checksum = 0;
  tcp->urgent_ptr = 0;

  if (len > 0)
    memcpy(buffer + sizeof(tcp_header_t), data, len);

  tcp->checksum = tcp_checksum(tcb->local_ip, tcb->remote_ip, tcp, total_len);

  serial_log_hex("TCP: Sending segment, flags=", flags);
  serial_log_hex("TCP: seq=", tcb->snd_nxt);
  serial_log_hex("TCP: ack=", tcb->rcv_nxt);

  ip_send(tcb->remote_ip, IPPROTO_TCP, buffer, total_len);

  // Advance sequence number
  if (flags & TCP_SYN || flags & TCP_FIN)
    tcb->snd_nxt++;
  else
    tcb->snd_nxt += len;

  kfree(buffer);
}

/* ================= TCP CONNECT (Client) ================= */

extern "C" tcp_tcb_t *tcp_connect(uint32_t local_ip, uint16_t local_port,
                                  uint32_t remote_ip, uint16_t remote_port) {
  tcp_tcb_t *tcb = tcp_alloc_tcb();
  if (!tcb) {
    serial_log("TCP: Failed to allocate TCB");
    return nullptr;
  }

  tcb->local_ip = local_ip;
  tcb->remote_ip = remote_ip;
  tcb->local_port = tcp_htons(local_port);
  tcb->remote_port = tcp_htons(remote_port);

  tcb->snd_nxt = 0x1000; // Initial sequence number
  tcb->snd_una = tcb->snd_nxt;
  tcb->rcv_nxt = 0;

  tcb->snd_wnd = TCP_DEFAULT_WINDOW;
  tcb->rcv_wnd = TCP_DEFAULT_WINDOW;

  tcb->state = TCP_SYN_SENT;

  serial_log("TCP: Initiating connection (sending SYN)...");
  tcp_send_segment(tcb, TCP_SYN, nullptr, 0);

  return tcb;
}

/* ================= TCP RECEIVE PACKET ================= */

extern "C" void tcp_handle_packet(uint32_t src_ip, uint32_t dst_ip,
                                  uint8_t *packet, uint16_t len) {
  if (len < sizeof(tcp_header_t)) {
    serial_log("TCP: Packet too short");
    return;
  }

  tcp_header_t *tcp = (tcp_header_t *)packet;

  serial_log_hex("TCP: Received packet from port ", tcp_ntohs(tcp->src_port));
  serial_log_hex("TCP: flags=", tcp->flags);

  tcp_tcb_t *tcb = tcp_find(src_ip, dst_ip, tcp->src_port, tcp->dst_port);

  if (!tcb) {
    serial_log("TCP: No matching connection found");
    return;
  }

  uint16_t hdr_len = (tcp->offset_reserved >> 4) * 4;
  uint16_t data_len = len - hdr_len;
  uint8_t *data = packet + hdr_len;

  uint32_t seg_seq = tcp_ntohl(tcp->seq);
  uint32_t seg_ack = tcp_ntohl(tcp->ack);

  serial_log_hex("TCP: Current state=", tcb->state);

  switch (tcb->state) {
  case TCP_SYN_SENT:
    // Waiting for SYN+ACK
    if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
      tcb->rcv_nxt = seg_seq + 1;
      tcb->snd_una = seg_ack;
      tcb->state = TCP_ESTABLISHED;
      serial_log("TCP: Connection ESTABLISHED! Sending ACK...");
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
    }
    break;

  case TCP_ESTABLISHED:
    // Handle incoming data
    if (data_len > 0 && seg_seq == tcb->rcv_nxt) {
      serial_log_hex("TCP: Received data, len=", data_len);

      // Store data in receive buffer
      if (tcb->rx_len + data_len <= tcb->rx_capacity) {
        memcpy(tcb->rx_buffer + tcb->rx_len, data, data_len);
        tcb->rx_len += data_len;
      }

      tcb->rcv_nxt += data_len;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
    }

    // Handle FIN from peer
    if (tcp->flags & TCP_FIN) {
      tcb->rcv_nxt++;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
      tcb->state = TCP_CLOSE_WAIT;
      serial_log("TCP: Received FIN, moving to CLOSE_WAIT");
    }
    break;

  case TCP_FIN_WAIT_1:
    if (tcp->flags & TCP_ACK) {
      tcb->state = TCP_FIN_WAIT_2;
      serial_log("TCP: Moving to FIN_WAIT_2");
    }
    if (tcp->flags & TCP_FIN) {
      tcb->rcv_nxt++;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
      tcb->state = TCP_TIME_WAIT;
      serial_log("TCP: Moving to TIME_WAIT");
    }
    break;

  case TCP_FIN_WAIT_2:
    if (tcp->flags & TCP_FIN) {
      tcb->rcv_nxt++;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
      tcb->state = TCP_TIME_WAIT;
      serial_log("TCP: Moving to TIME_WAIT");
    }
    break;

  case TCP_LAST_ACK:
    if (tcp->flags & TCP_ACK) {
      tcb->state = TCP_CLOSED;
      tcp_free_tcb(tcb);
      serial_log("TCP: Connection closed");
    }
    break;

  default:
    break;
  }
}

/* ================= TCP SEND DATA ================= */

extern "C" int tcp_send_data(tcp_tcb_t *tcb, void *data, uint16_t len) {
  if (!tcb || tcb->state != TCP_ESTABLISHED) {
    serial_log("TCP: Cannot send - not established");
    return -1;
  }

  serial_log_hex("TCP: Sending data, len=", len);
  tcp_send_segment(tcb, TCP_ACK | TCP_PSH, data, len);
  return len;
}

/* ================= TCP READ DATA ================= */

extern "C" int tcp_read_data(tcp_tcb_t *tcb, void *buffer, uint16_t len) {
  if (!tcb)
    return -1;

  uint16_t to_read = len < tcb->rx_len ? len : tcb->rx_len;
  if (to_read > 0) {
    memcpy(buffer, tcb->rx_buffer, to_read);
    // Shift remaining data (manual memmove since overlapping)
    uint16_t remaining = tcb->rx_len - to_read;
    for (uint16_t i = 0; i < remaining; i++) {
      tcb->rx_buffer[i] = tcb->rx_buffer[to_read + i];
    }
    tcb->rx_len -= to_read;
  }
  return to_read;
}

/* ================= TCP CLOSE ================= */

extern "C" void tcp_close(tcp_tcb_t *tcb) {
  if (!tcb)
    return;

  if (tcb->state == TCP_ESTABLISHED) {
    tcb->state = TCP_FIN_WAIT_1;
    serial_log("TCP: Closing connection (sending FIN)...");
    tcp_send_segment(tcb, TCP_FIN | TCP_ACK, nullptr, 0);
  } else if (tcb->state == TCP_CLOSE_WAIT) {
    tcb->state = TCP_LAST_ACK;
    tcp_send_segment(tcb, TCP_FIN | TCP_ACK, nullptr, 0);
  }
}

/* ================= TCP STATE CHECK ================= */

extern "C" int tcp_is_connected(tcp_tcb_t *tcb) {
  return tcb && tcb->state == TCP_ESTABLISHED;
}

extern "C" int tcp_has_data(tcp_tcb_t *tcb) { return tcb && tcb->rx_len > 0; }

/* ================= TCP INIT ================= */

extern "C" void tcp_init() {
  for (int i = 0; i < MAX_TCP_CONNECTIONS; i++) {
    memset(&tcp_table[i], 0, sizeof(tcp_tcb_t));
  }
  serial_log("TCP: Stack initialized");
}
