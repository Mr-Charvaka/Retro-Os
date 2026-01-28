// socket_api.cpp - BSD-style Socket API for Retro-OS
// Unified socket interface for TCP/UDP networking

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "net_advanced.h"
#include <stddef.h>
#include <stdint.h>

/* ===================== CONFIG ===================== */

#define MAX_SOCKETS 64
#define SOCKET_BUFFER_SIZE 8192
#define SOCKET_BACKLOG_MAX 16

/* ===================== SOCKET STATES ===================== */

enum socket_state {
  SOCK_STATE_FREE = 0,
  SOCK_STATE_CREATED,
  SOCK_STATE_BOUND,
  SOCK_STATE_LISTENING,
  SOCK_STATE_CONNECTING,
  SOCK_STATE_CONNECTED,
  SOCK_STATE_CLOSING,
  SOCK_STATE_CLOSED
};

/* ===================== SOCKET STRUCTURE ===================== */

struct socket_entry {
  int used;
  int type; // SOCK_STREAM or SOCK_DGRAM
  int protocol;
  socket_state state;

  // Local address
  uint32_t local_ip;
  uint16_t local_port;

  // Remote address (for connected sockets)
  uint32_t remote_ip;
  uint16_t remote_port;

  // TCP connection handle
  void *tcp_handle;

  // Receive buffer (for UDP and async TCP)
  uint8_t *rx_buffer;
  uint32_t rx_head;
  uint32_t rx_tail;
  uint32_t rx_capacity;

  // Pending connections (for listening sockets)
  struct {
    uint32_t remote_ip;
    uint16_t remote_port;
    void *tcp_handle;
  } backlog[SOCKET_BACKLOG_MAX];
  int backlog_count;

  // Options
  int reuse_addr;
  int keep_alive;
  uint32_t recv_timeout;
  uint32_t send_timeout;
};

/* ===================== GLOBALS ===================== */

static socket_entry sockets[MAX_SOCKETS];
static uint16_t next_ephemeral_port = 49152;
static uint32_t local_ip = 0x0F02000A; // 10.0.2.15

/* ===================== EXTERNAL TCP API ===================== */

struct tcp_tcb_t;
extern "C" tcp_tcb_t *tcp_connect(uint32_t local_ip, uint16_t local_port,
                                  uint32_t remote_ip, uint16_t remote_port);
extern "C" int tcp_send_data(tcp_tcb_t *tcb, void *data, uint16_t len);
extern "C" int tcp_read_data(tcp_tcb_t *tcb, void *buffer, uint16_t len);
extern "C" int tcp_is_connected(tcp_tcb_t *tcb);
extern "C" int tcp_has_data(tcp_tcb_t *tcb);
extern "C" void tcp_close(tcp_tcb_t *tcb);

/* ===================== EXTERNAL UDP API ===================== */

extern "C" void udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                         uint16_t dst_port, uint8_t *data, uint16_t length);
extern "C" void udp_bind(uint16_t port, void (*handler)(uint32_t, uint16_t,
                                                        uint8_t *, uint16_t));

extern "C" uint32_t timer_now_ms(void);
extern "C" void *kmalloc(uint32_t size);
extern "C" void kfree(void *ptr);
extern "C" void schedule(void);

/* ===================== HELPERS ===================== */

static int socket_alloc() {
  for (int i = 0; i < MAX_SOCKETS; i++) {
    if (!sockets[i].used) {
      memset(&sockets[i], 0, sizeof(socket_entry));
      sockets[i].used = 1;
      sockets[i].state = SOCK_STATE_CREATED;
      return i;
    }
  }
  return -1;
}

static socket_entry *socket_get(int fd) {
  if (fd < 0 || fd >= MAX_SOCKETS)
    return nullptr;
  if (!sockets[fd].used)
    return nullptr;
  return &sockets[fd];
}

static uint16_t alloc_ephemeral_port() {
  uint16_t port = next_ephemeral_port++;
  if (next_ephemeral_port > 65000) {
    next_ephemeral_port = 49152;
  }
  return port;
}

/* ===================== UDP RECEIVE CALLBACK ===================== */

// Per-socket UDP receive handler (socket index encoded in port)
static void udp_socket_rx(uint32_t src_ip, uint16_t src_port, uint8_t *data,
                          uint16_t length) {
  // Find matching socket by local port
  for (int i = 0; i < MAX_SOCKETS; i++) {
    socket_entry *s = &sockets[i];
    if (s->used && s->type == SOCK_DGRAM && s->state >= SOCK_STATE_BOUND) {
      // Store in receive buffer with source address prefix
      if (s->rx_buffer && s->rx_tail + length + 8 < s->rx_capacity) {
        // Store: [src_ip:4][src_port:2][length:2][data:length]
        uint8_t *ptr = s->rx_buffer + s->rx_tail;
        *(uint32_t *)ptr = src_ip;
        *(uint16_t *)(ptr + 4) = src_port;
        *(uint16_t *)(ptr + 6) = length;
        memcpy(ptr + 8, data, length);
        s->rx_tail += 8 + length;
      }
      return;
    }
  }
}

/* ===================== PUBLIC SOCKET API ===================== */

extern "C" int net_socket(int domain, int type, int protocol) {
  (void)domain;   // Only AF_INET supported
  (void)protocol; // Ignored, inferred from type

  if (type != SOCK_STREAM && type != SOCK_DGRAM) {
    serial_log("SOCKET: Unsupported socket type");
    return -1;
  }

  int fd = socket_alloc();
  if (fd < 0) {
    serial_log("SOCKET: No free sockets");
    return -1;
  }

  socket_entry *s = &sockets[fd];
  s->type = type;
  s->protocol = protocol;
  s->rx_capacity = SOCKET_BUFFER_SIZE;
  s->rx_buffer = (uint8_t *)kmalloc(SOCKET_BUFFER_SIZE);

  serial_log_hex("SOCKET: Created fd=", fd);
  return fd;
}

extern "C" int net_bind(int sockfd, const struct sockaddr *addr,
                        uint32_t addrlen) {
  (void)addrlen;

  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;

  s->local_ip = sin->sin_addr ? sin->sin_addr : local_ip;
  s->local_port = ntohs(sin->sin_port);
  s->state = SOCK_STATE_BOUND;

  // For UDP, register the receive handler
  if (s->type == SOCK_DGRAM) {
    udp_bind(s->local_port, udp_socket_rx);
  }

  serial_log_hex("SOCKET: Bound to port ", s->local_port);
  return 0;
}

extern "C" int net_listen(int sockfd, int backlog) {
  socket_entry *s = socket_get(sockfd);
  if (!s || s->type != SOCK_STREAM)
    return -1;
  if (s->state != SOCK_STATE_BOUND)
    return -1;

  (void)backlog; // TODO: implement proper backlog
  s->state = SOCK_STATE_LISTENING;

  serial_log("SOCKET: Listening");
  return 0;
}

extern "C" int net_accept(int sockfd, struct sockaddr *addr,
                          uint32_t *addrlen) {
  socket_entry *s = socket_get(sockfd);
  if (!s || s->state != SOCK_STATE_LISTENING)
    return -1;

  // TODO: Wait for incoming connection
  // For now, return error (not implemented)
  (void)addr;
  (void)addrlen;

  serial_log("SOCKET: accept() not fully implemented");
  return -1;
}

extern "C" int net_connect(int sockfd, const struct sockaddr *addr,
                           uint32_t addrlen) {
  (void)addrlen;

  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  const struct sockaddr_in *sin = (const struct sockaddr_in *)addr;

  s->remote_ip = sin->sin_addr;
  s->remote_port = ntohs(sin->sin_port);

  if (s->local_port == 0) {
    s->local_port = alloc_ephemeral_port();
  }
  s->local_ip = local_ip;

  if (s->type == SOCK_STREAM) {
    // TCP connect
    s->state = SOCK_STATE_CONNECTING;

    tcp_tcb_t *tcb =
        tcp_connect(s->local_ip, s->local_port, s->remote_ip, s->remote_port);
    if (!tcb) {
      serial_log("SOCKET: TCP connect failed");
      return -1;
    }

    s->tcp_handle = tcb;

    // Wait for connection with timeout
    uint32_t start = timer_now_ms();
    uint32_t timeout = s->recv_timeout ? s->recv_timeout : 10000;

    while (!tcp_is_connected(tcb)) {
      if (timer_now_ms() - start > timeout) {
        serial_log("SOCKET: Connect timeout");
        return -1;
      }
      schedule();
    }

    s->state = SOCK_STATE_CONNECTED;
    serial_log("SOCKET: Connected");
    return 0;
  } else {
    // UDP - just store remote address
    s->state = SOCK_STATE_CONNECTED;
    return 0;
  }
}

extern "C" int net_send(int sockfd, const void *buf, uint32_t len, int flags) {
  (void)flags;

  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  if (s->type == SOCK_STREAM) {
    if (s->state != SOCK_STATE_CONNECTED || !s->tcp_handle) {
      return -1;
    }
    return tcp_send_data((tcp_tcb_t *)s->tcp_handle, (void *)buf, len);
  } else {
    // UDP send
    if (s->remote_ip == 0)
      return -1;
    udp_send(s->local_ip, s->local_port, s->remote_ip, s->remote_port,
             (uint8_t *)buf, len);
    return len;
  }
}

extern "C" int net_recv(int sockfd, void *buf, uint32_t len, int flags) {
  (void)flags;

  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  if (s->type == SOCK_STREAM) {
    if (s->state != SOCK_STATE_CONNECTED || !s->tcp_handle) {
      return -1;
    }

    tcp_tcb_t *tcb = (tcp_tcb_t *)s->tcp_handle;

    // Wait for data with timeout
    uint32_t start = timer_now_ms();
    uint32_t timeout = s->recv_timeout ? s->recv_timeout : 10000;

    while (!tcp_has_data(tcb)) {
      if (!tcp_is_connected(tcb)) {
        return 0; // Connection closed
      }
      if (timer_now_ms() - start > timeout) {
        return -1; // Timeout
      }
      schedule();
    }

    return tcp_read_data(tcb, buf, len);
  } else {
    // UDP receive from buffer
    if (s->rx_head >= s->rx_tail) {
      return 0; // No data
    }

    // Parse stored packet
    uint8_t *ptr = s->rx_buffer + s->rx_head;
    uint16_t pkt_len = *(uint16_t *)(ptr + 6);

    if (pkt_len > len)
      pkt_len = len;
    memcpy(buf, ptr + 8, pkt_len);

    s->rx_head += 8 + *(uint16_t *)(ptr + 6);

    // Reset buffer if empty
    if (s->rx_head >= s->rx_tail) {
      s->rx_head = 0;
      s->rx_tail = 0;
    }

    return pkt_len;
  }
}

extern "C" int net_sendto(int sockfd, const void *buf, uint32_t len, int flags,
                          const struct sockaddr *dest_addr, uint32_t addrlen) {
  (void)flags;
  (void)addrlen;

  socket_entry *s = socket_get(sockfd);
  if (!s || s->type != SOCK_DGRAM)
    return -1;

  const struct sockaddr_in *sin = (const struct sockaddr_in *)dest_addr;

  if (s->local_port == 0) {
    s->local_port = alloc_ephemeral_port();
    s->local_ip = local_ip;
    udp_bind(s->local_port, udp_socket_rx);
  }

  udp_send(s->local_ip, s->local_port, sin->sin_addr, ntohs(sin->sin_port),
           (uint8_t *)buf, len);

  return len;
}

extern "C" int net_recvfrom(int sockfd, void *buf, uint32_t len, int flags,
                            struct sockaddr *src_addr, uint32_t *addrlen) {
  (void)flags;

  socket_entry *s = socket_get(sockfd);
  if (!s || s->type != SOCK_DGRAM)
    return -1;

  // Wait for data
  uint32_t start = timer_now_ms();
  uint32_t timeout = s->recv_timeout ? s->recv_timeout : 10000;

  while (s->rx_head >= s->rx_tail) {
    if (timer_now_ms() - start > timeout) {
      return -1;
    }
    schedule();
  }

  // Parse stored packet
  uint8_t *ptr = s->rx_buffer + s->rx_head;
  uint32_t pkt_src_ip = *(uint32_t *)ptr;
  uint16_t pkt_src_port = *(uint16_t *)(ptr + 4);
  uint16_t pkt_len = *(uint16_t *)(ptr + 6);

  if (pkt_len > len)
    pkt_len = len;
  memcpy(buf, ptr + 8, pkt_len);

  if (src_addr) {
    struct sockaddr_in *sin = (struct sockaddr_in *)src_addr;
    sin->sin_family = AF_INET;
    sin->sin_addr = pkt_src_ip;
    sin->sin_port = htons(pkt_src_port);
  }
  if (addrlen) {
    *addrlen = sizeof(struct sockaddr_in);
  }

  s->rx_head += 8 + *(uint16_t *)(ptr + 6);

  if (s->rx_head >= s->rx_tail) {
    s->rx_head = 0;
    s->rx_tail = 0;
  }

  return pkt_len;
}

extern "C" int net_close(int sockfd) {
  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  if (s->type == SOCK_STREAM && s->tcp_handle) {
    tcp_close((tcp_tcb_t *)s->tcp_handle);
  }

  if (s->rx_buffer) {
    kfree(s->rx_buffer);
  }

  memset(s, 0, sizeof(socket_entry));

  serial_log_hex("SOCKET: Closed fd=", sockfd);
  return 0;
}

extern "C" int net_setsockopt(int sockfd, int level, int optname,
                              const void *optval, uint32_t optlen) {
  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  (void)level;
  (void)optlen;

  switch (optname) {
  case SO_REUSEADDR:
    s->reuse_addr = *(int *)optval;
    break;
  case SO_KEEPALIVE:
    s->keep_alive = *(int *)optval;
    break;
  case SO_RCVTIMEO:
    s->recv_timeout = *(uint32_t *)optval;
    break;
  case SO_SNDTIMEO:
    s->send_timeout = *(uint32_t *)optval;
    break;
  default:
    return -1;
  }

  return 0;
}

extern "C" int net_getsockopt(int sockfd, int level, int optname, void *optval,
                              uint32_t *optlen) {
  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  (void)level;

  switch (optname) {
  case SO_REUSEADDR:
    *(int *)optval = s->reuse_addr;
    *optlen = sizeof(int);
    break;
  case SO_KEEPALIVE:
    *(int *)optval = s->keep_alive;
    *optlen = sizeof(int);
    break;
  case SO_RCVTIMEO:
    *(uint32_t *)optval = s->recv_timeout;
    *optlen = sizeof(uint32_t);
    break;
  case SO_SNDTIMEO:
    *(uint32_t *)optval = s->send_timeout;
    *optlen = sizeof(uint32_t);
    break;
  default:
    return -1;
  }

  return 0;
}

extern "C" int socket_poll(int sockfd, int timeout_ms) {
  socket_entry *s = socket_get(sockfd);
  if (!s)
    return -1;

  uint32_t start = timer_now_ms();

  while (1) {
    if (s->type == SOCK_STREAM && s->tcp_handle) {
      if (tcp_has_data((tcp_tcb_t *)s->tcp_handle)) {
        return 1;
      }
    } else if (s->type == SOCK_DGRAM) {
      if (s->rx_head < s->rx_tail) {
        return 1;
      }
    }

    if (timeout_ms == 0) {
      return 0;
    }

    if (timeout_ms > 0 && (int)(timer_now_ms() - start) >= timeout_ms) {
      return 0;
    }

    schedule();
  }
}

/* ===================== BYTE ORDER FUNCTIONS ===================== */

extern "C" uint16_t htons(uint16_t hostshort) {
  return (hostshort << 8) | (hostshort >> 8);
}

extern "C" uint32_t htonl(uint32_t hostlong) {
  return __builtin_bswap32(hostlong);
}

extern "C" uint16_t ntohs(uint16_t netshort) { return htons(netshort); }

extern "C" uint32_t ntohl(uint32_t netlong) { return htonl(netlong); }

/* ===================== INET FUNCTIONS ===================== */

static char inet_ntoa_buffer[16];

extern "C" int inet_aton(const char *cp, uint32_t *inp) {
  *inp = ip_from_string(cp);
  return (*inp != 0) ? 1 : 0;
}

extern "C" const char *inet_ntoa(uint32_t in) {
  ip_to_string(in, inet_ntoa_buffer);
  return inet_ntoa_buffer;
}

/* ===================== INITIALIZATION ===================== */

extern "C" void socket_api_init() {
  memset(sockets, 0, sizeof(sockets));
  next_ephemeral_port = 49152;
  serial_log("SOCKET: API initialized (max 64 sockets)");
}
