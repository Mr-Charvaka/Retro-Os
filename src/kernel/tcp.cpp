// tcp.cpp - Kernel TCP implementation
// Full TCP stack with handshake, data transfer, and connection management

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "net.h" // Access to my_ip
#include <stddef.h>
#include <stdint.h>

/* ================= CONFIG ================= */

#define MAX_TCP_CONNECTIONS 16
#define TCP_RX_BUFFER_SIZE 32768 // 32KB to be safe for modern TLS
#define INITIAL_SEQ 0x1000

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
  uint32_t rx_len;
  uint32_t rx_capacity;
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
      tcp_table[i].rx_capacity = TCP_RX_BUFFER_SIZE;
      tcp_table[i].rx_buffer = (uint8_t *)kmalloc(TCP_RX_BUFFER_SIZE);
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
  uint32_t sum = 0;
  sum += (src_ip >> 16) & 0xFFFF;
  sum += (src_ip) & 0xFFFF;
  sum += (dst_ip >> 16) & 0xFFFF;
  sum += (dst_ip) & 0xFFFF;
  sum += tcp_htons(IP_PROTO_TCP);
  sum += tcp_htons(tcp_len);

  uint8_t *p = (uint8_t *)tcp;
  for (size_t i = 0; i < tcp_len / 2; i++) {
    uint16_t word = p[2 * i] | (p[2 * i + 1] << 8);
    sum += word;
  }
  if (tcp_len & 1) {
    sum += ((uint8_t *)tcp)[tcp_len - 1];
  }
  while (sum >> 16)
    sum = (sum & 0xFFFF) + (sum >> 16);
  return ~sum;
}

/* ================= TCP SEND SEGMENT ================= */

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

  // Backpressure: Update advertised window based on free space
  uint32_t free_space = tcb->rx_capacity - tcb->rx_len;
  tcp->window =
      tcp_htons((uint16_t)(free_space > 0xFFFF ? 0xFFFF : free_space));

  tcp->checksum = 0;
  tcp->urgent_ptr = 0;

  if (len > 0)
    memcpy(buffer + sizeof(tcp_header_t), data, len);

  tcp->checksum = tcp_checksum(tcb->local_ip, tcb->remote_ip, tcp, total_len);
  ip_send(tcb->remote_ip, IP_PROTO_TCP, buffer, total_len);

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
  if (!tcb)
    return nullptr;

  tcb->local_ip = (local_ip == 0) ? my_ip : local_ip;
  tcb->remote_ip = remote_ip;
  tcb->local_port = tcp_htons(local_port);
  tcb->remote_port = tcp_htons(remote_port);

  tcb->snd_nxt = INITIAL_SEQ;
  tcb->snd_una = tcb->snd_nxt;
  tcb->rcv_nxt = 0;
  tcb->state = TCP_SYN_SENT;

  tcp_send_segment(tcb, TCP_SYN, nullptr, 0);
  return tcb;
}

/* ================= TCP RECEIVE PACKET ================= */

extern "C" void tcp_handle_packet(uint32_t src_ip, uint32_t dst_ip,
                                  uint8_t *packet, uint16_t len) {
  if (len < sizeof(tcp_header_t))
    return;
  tcp_header_t *tcp = (tcp_header_t *)packet;
  tcp_tcb_t *tcb = tcp_find(src_ip, dst_ip, tcp->src_port, tcp->dst_port);
  if (!tcb)
    return;

  uint16_t hdr_len = (tcp->offset_reserved >> 4) * 4;
  uint16_t data_len = len - hdr_len;
  uint8_t *data = packet + hdr_len;
  uint32_t seg_seq = tcp_ntohl(tcp->seq);
  uint32_t seg_ack = tcp_ntohl(tcp->ack);

  switch (tcb->state) {
  case TCP_SYN_SENT:
    if ((tcp->flags & (TCP_SYN | TCP_ACK)) == (TCP_SYN | TCP_ACK)) {
      tcb->rcv_nxt = seg_seq + 1;
      tcb->snd_una = seg_ack;
      tcb->state = TCP_ESTABLISHED;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
    }
    break;

  case TCP_ESTABLISHED:
    if (data_len > 0) {
      if (seg_seq == tcb->rcv_nxt) {
        if (tcb->rx_len + data_len <= tcb->rx_capacity) {
          memcpy(tcb->rx_buffer + tcb->rx_len, data, data_len);
          tcb->rx_len += data_len;
          tcb->rcv_nxt += data_len;
          tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
        } else {
          serial_log("TCP: RX Buffer Full! Dropping segment.");
        }
      }
    }
    if (tcp->flags & TCP_FIN) {
      tcb->rcv_nxt++;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
      tcb->state = TCP_CLOSE_WAIT;
    }
    break;

  case TCP_FIN_WAIT_1:
    if (tcp->flags & TCP_ACK)
      tcb->state = TCP_FIN_WAIT_2;
    if (tcp->flags & TCP_FIN) {
      tcb->rcv_nxt++;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
      tcb->state = TCP_TIME_WAIT;
    }
    break;

  case TCP_FIN_WAIT_2:
    if (tcp->flags & TCP_FIN) {
      tcb->rcv_nxt++;
      tcp_send_segment(tcb, TCP_ACK, nullptr, 0);
      tcb->state = TCP_TIME_WAIT;
    }
    break;

  case TCP_LAST_ACK:
    if (tcp->flags & TCP_ACK) {
      tcb->state = TCP_CLOSED;
      tcp_free_tcb(tcb);
    }
    break;

  default:
    break;
  }
}

/* ================= TCP PUBLIC API ================= */

extern "C" int tcp_send_data(tcp_tcb_t *tcb, void *data, uint16_t len) {
  if (!tcb || tcb->state != TCP_ESTABLISHED)
    return -1;
  tcp_send_segment(tcb, TCP_ACK | TCP_PSH, data, len);
  return len;
}

extern "C" int tcp_read_data(tcp_tcb_t *tcb, void *buffer, uint16_t len) {
  if (!tcb)
    return -1;
  uint16_t to_read = (uint16_t)(len < tcb->rx_len ? len : tcb->rx_len);
  if (to_read > 0) {
    memcpy(buffer, tcb->rx_buffer, to_read);
    uint32_t remaining = tcb->rx_len - to_read;
    if (remaining > 0) {
      memmove(tcb->rx_buffer, tcb->rx_buffer + to_read, remaining);
    }
    tcb->rx_len -= to_read;
  }
  return (int)to_read;
}

extern "C" void tcp_close(tcp_tcb_t *tcb) {
  if (!tcb)
    return;
  if (tcb->state == TCP_ESTABLISHED) {
    tcb->state = TCP_FIN_WAIT_1;
    tcp_send_segment(tcb, TCP_FIN | TCP_ACK, nullptr, 0);
  } else if (tcb->state == TCP_CLOSE_WAIT) {
    tcb->state = TCP_LAST_ACK;
    tcp_send_segment(tcb, TCP_FIN | TCP_ACK, nullptr, 0);
  }
}

extern "C" int tcp_is_connected(tcp_tcb_t *tcb) {
  return tcb && (tcb->state == TCP_ESTABLISHED || tcb->state == TCP_CLOSE_WAIT);
}

extern "C" int tcp_has_data(tcp_tcb_t *tcb) { return tcb && tcb->rx_len > 0; }

extern "C" void tcp_init() {
  for (int i = 0; i < MAX_TCP_CONNECTIONS; i++)
    tcp_table[i].used = 0;
  serial_log("TCP: Stack initialized with 32KB RX support");
}
