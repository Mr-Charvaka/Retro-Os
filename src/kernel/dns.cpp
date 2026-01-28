// dns.cpp - DNS Resolver for Retro-OS
// RFC 1035 compliant DNS query/response handling

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "net.h"
#include <stddef.h>
#include <stdint.h>

/* ===================== CONFIG ===================== */

#define DNS_PORT 53
#define DNS_SERVER 0x0302000A // 10.0.2.3 (QEMU SLIRP DNS)
#define DNS_CACHE_SIZE 32
#define DNS_TIMEOUT_MS 5000
#define DNS_MAX_RETRIES 3
#define DNS_MAX_NAME 256

/* ===================== DNS FLAGS ===================== */

#define DNS_QR_QUERY 0x0000
#define DNS_QR_RESPONSE 0x8000
#define DNS_OPCODE_STD 0x0000
#define DNS_RD 0x0100 // Recursion Desired
#define DNS_RA 0x0080 // Recursion Available
#define DNS_RCODE_OK 0x0000
#define DNS_RCODE_NXDOMAIN 0x0003

/* ===================== DNS TYPES ===================== */

#define DNS_TYPE_A 1     // IPv4 Address
#define DNS_TYPE_AAAA 28 // IPv6 Address
#define DNS_TYPE_CNAME 5 // Canonical Name
#define DNS_TYPE_MX 15   // Mail Exchange
#define DNS_TYPE_TXT 16  // Text Record
#define DNS_TYPE_NS 2    // Name Server

#define DNS_CLASS_IN 1 // Internet

/* ===================== STRUCTURES ===================== */

struct dns_header {
  uint16_t id;
  uint16_t flags;
  uint16_t qdcount; // Question count
  uint16_t ancount; // Answer count
  uint16_t nscount; // Authority count
  uint16_t arcount; // Additional count
} __attribute__((packed));

struct dns_question {
  uint16_t qtype;
  uint16_t qclass;
} __attribute__((packed));

struct dns_rr_data {
  uint16_t type;
  uint16_t rclass;
  uint32_t ttl;
  uint16_t rdlength;
} __attribute__((packed));

struct dns_cache_entry {
  char name[DNS_MAX_NAME];
  uint32_t ip;
  uint32_t ttl;
  uint32_t timestamp;
  int valid;
};

/* ===================== GLOBALS ===================== */

static dns_cache_entry dns_cache[DNS_CACHE_SIZE];
static uint16_t dns_tx_id = 0x1234;
static volatile int dns_response_received = 0;
static volatile uint32_t dns_resolved_ip = 0;
static volatile uint16_t dns_pending_id = 0;

/* ===================== BYTE ORDER ===================== */

static inline uint16_t dns_htons(uint16_t x) { return (x << 8) | (x >> 8); }
static inline uint16_t dns_ntohs(uint16_t x) { return dns_htons(x); }
static inline uint32_t dns_htonl(uint32_t x) { return __builtin_bswap32(x); }
static inline uint32_t dns_ntohl(uint32_t x) { return dns_htonl(x); }

/* ===================== HELPERS ===================== */

extern "C" uint32_t timer_now_ms(void);
extern "C" void udp_send(uint32_t src_ip, uint16_t src_port, uint32_t dst_ip,
                         uint16_t dst_port, uint8_t *data, uint16_t length);
extern "C" void udp_bind(uint16_t port, void (*handler)(uint32_t, uint16_t,
                                                        uint8_t *, uint16_t));
extern "C" void schedule(void);
extern "C" void net_poll(void); // Poll network for incoming packets

// Local IP (should match net.cpp)
static uint32_t local_ip = 0x0F02000A; // 10.0.2.15

/* ===================== DNS NAME ENCODING ===================== */

// Encode hostname to DNS wire format (labels)
// www.example.com -> 3www7example3com0
static int dns_encode_name(const char *name, uint8_t *buf) {
  int pos = 0;
  const char *p = name;

  while (*p) {
    const char *dot = p;
    while (*dot && *dot != '.')
      dot++;

    int label_len = dot - p;
    if (label_len > 63 || label_len == 0)
      return -1;

    buf[pos++] = (uint8_t)label_len;
    for (int i = 0; i < label_len; i++) {
      buf[pos++] = p[i];
    }

    p = dot;
    if (*p == '.')
      p++;
  }

  buf[pos++] = 0; // Null terminator
  return pos;
}

// Decode DNS wire format name to string
static int dns_decode_name(uint8_t *response, int response_len, uint8_t *ptr,
                           char *out, int max_len) {
  int pos = 0;
  int jumped = 0;
  int jump_offset = 0;
  uint8_t *start = ptr;

  while (*ptr && pos < max_len - 1) {
    if ((*ptr & 0xC0) == 0xC0) {
      // Compression pointer
      if (!jumped) {
        jump_offset = (ptr - start) + 2;
      }
      int offset = ((*ptr & 0x3F) << 8) | *(ptr + 1);
      if (offset >= response_len)
        return -1;
      ptr = response + offset;
      jumped = 1;
      continue;
    }

    int label_len = *ptr++;
    if (label_len > 63)
      return -1;

    if (pos > 0)
      out[pos++] = '.';

    for (int i = 0; i < label_len && pos < max_len - 1; i++) {
      out[pos++] = *ptr++;
    }
  }

  out[pos] = 0;

  if (jumped) {
    return jump_offset;
  }
  return (ptr - start) + 1;
}

/* ===================== DNS CACHE ===================== */

static void dns_cache_insert(const char *name, uint32_t ip, uint32_t ttl) {
  uint32_t now = timer_now_ms();

  // Check if already cached
  for (int i = 0; i < DNS_CACHE_SIZE; i++) {
    if (dns_cache[i].valid && strcmp(dns_cache[i].name, name) == 0) {
      dns_cache[i].ip = ip;
      dns_cache[i].ttl = ttl;
      dns_cache[i].timestamp = now;
      return;
    }
  }

  // Find empty or oldest slot
  int oldest = 0;
  uint32_t oldest_time = 0xFFFFFFFF;

  for (int i = 0; i < DNS_CACHE_SIZE; i++) {
    if (!dns_cache[i].valid) {
      oldest = i;
      break;
    }
    if (dns_cache[i].timestamp < oldest_time) {
      oldest_time = dns_cache[i].timestamp;
      oldest = i;
    }
  }

  strncpy(dns_cache[oldest].name, name, DNS_MAX_NAME - 1);
  dns_cache[oldest].name[DNS_MAX_NAME - 1] = 0;
  dns_cache[oldest].ip = ip;
  dns_cache[oldest].ttl = ttl;
  dns_cache[oldest].timestamp = now;
  dns_cache[oldest].valid = 1;

  serial_log("DNS: Cached ");
  serial_log(name);
}

static uint32_t dns_cache_lookup(const char *name) {
  uint32_t now = timer_now_ms();

  for (int i = 0; i < DNS_CACHE_SIZE; i++) {
    if (dns_cache[i].valid && strcmp(dns_cache[i].name, name) == 0) {
      // Check if TTL expired (TTL is in seconds, timestamp in ms)
      uint32_t age_ms = now - dns_cache[i].timestamp;
      if (age_ms < dns_cache[i].ttl * 1000) {
        serial_log("DNS: Cache hit for ");
        serial_log(name);
        return dns_cache[i].ip;
      } else {
        dns_cache[i].valid = 0; // Expired
      }
    }
  }
  return 0;
}

/* ===================== DNS RESPONSE HANDLER ===================== */

static void dns_rx_handler(uint32_t src_ip, uint16_t src_port, uint8_t *data,
                           uint16_t length) {
  (void)src_ip;
  (void)src_port;

  if (length < sizeof(dns_header)) {
    serial_log("DNS: Response too short");
    return;
  }

  dns_header *hdr = (dns_header *)data;
  uint16_t id = dns_ntohs(hdr->id);
  uint16_t flags = dns_ntohs(hdr->flags);
  uint16_t qdcount = dns_ntohs(hdr->qdcount);
  uint16_t ancount = dns_ntohs(hdr->ancount);

  serial_log_hex("DNS: Response ID=", id);
  serial_log_hex("DNS: Flags=", flags);
  serial_log_hex("DNS: Answers=", ancount);

  // Verify this is our response
  if (id != dns_pending_id) {
    serial_log("DNS: ID mismatch, ignoring");
    return;
  }

  // Check for errors
  if ((flags & 0x000F) != DNS_RCODE_OK) {
    serial_log_hex("DNS: Error RCODE=", flags & 0x000F);
    dns_response_received = -1;
    return;
  }

  if (ancount == 0) {
    serial_log("DNS: No answers");
    dns_response_received = -1;
    return;
  }

  // Skip header
  uint8_t *ptr = data + sizeof(dns_header);

  // Skip question section
  for (int i = 0; i < qdcount; i++) {
    while (*ptr) {
      if ((*ptr & 0xC0) == 0xC0) {
        ptr += 2;
        break;
      }
      ptr += *ptr + 1;
    }
    if (*ptr == 0)
      ptr++;
    ptr += 4; // QTYPE + QCLASS
  }

  // Parse answer section
  for (int i = 0; i < ancount; i++) {
    char answer_name[DNS_MAX_NAME];
    int name_len =
        dns_decode_name(data, length, ptr, answer_name, DNS_MAX_NAME);
    if (name_len < 0)
      break;
    ptr += name_len;

    if (ptr + sizeof(dns_rr_data) > data + length)
      break;

    dns_rr_data *rr = (dns_rr_data *)ptr;
    uint16_t rtype = dns_ntohs(rr->type);
    uint16_t rdlen = dns_ntohs(rr->rdlength);
    uint32_t ttl = dns_ntohl(rr->ttl);

    ptr += sizeof(dns_rr_data);

    if (rtype == DNS_TYPE_A && rdlen == 4) {
      // IPv4 address
      uint32_t ip = *(uint32_t *)ptr;
      dns_resolved_ip = ip;
      dns_response_received = 1;

      serial_log_hex("DNS: Resolved to ", ip);

      // Cache the result
      // Need original query name - stored globally
      return;
    }

    ptr += rdlen;
  }

  if (!dns_response_received) {
    serial_log("DNS: No A record found");
    dns_response_received = -1;
  }
}

/* ===================== DNS QUERY ===================== */

static void dns_send_query(const char *hostname, uint16_t qtype) {
  uint8_t packet[512];
  memset(packet, 0, sizeof(packet));

  dns_header *hdr = (dns_header *)packet;
  hdr->id = dns_htons(dns_tx_id);
  hdr->flags = dns_htons(DNS_QR_QUERY | DNS_OPCODE_STD | DNS_RD);
  hdr->qdcount = dns_htons(1);
  hdr->ancount = 0;
  hdr->nscount = 0;
  hdr->arcount = 0;

  uint8_t *qname = packet + sizeof(dns_header);
  int name_len = dns_encode_name(hostname, qname);
  if (name_len < 0) {
    serial_log("DNS: Invalid hostname");
    return;
  }

  dns_question *q = (dns_question *)(qname + name_len);
  q->qtype = dns_htons(qtype);
  q->qclass = dns_htons(DNS_CLASS_IN);

  int total_len = sizeof(dns_header) + name_len + sizeof(dns_question);

  dns_pending_id = dns_tx_id++;
  dns_response_received = 0;
  dns_resolved_ip = 0;

  serial_log("DNS: Sending query for ");
  serial_log(hostname);

  udp_send(local_ip, 53053, DNS_SERVER, DNS_PORT, packet, total_len);
}

/* ===================== PUBLIC API ===================== */

// Resolve hostname to IP address (blocking)
extern "C" uint32_t dns_resolve(const char *hostname) {
  // Check cache first
  uint32_t cached = dns_cache_lookup(hostname);
  if (cached) {
    return cached;
  }

  // Send query
  dns_send_query(hostname, DNS_TYPE_A);

  // Wait for response with timeout
  uint32_t start = timer_now_ms();
  int retries = 0;

  while (retries < DNS_MAX_RETRIES) {
    if (dns_response_received == 1) {
      // Cache the result
      dns_cache_insert(hostname, dns_resolved_ip, 300); // 5 min TTL
      return dns_resolved_ip;
    }

    if (dns_response_received == -1) {
      serial_log("DNS: Resolution failed");
      return 0;
    }

    if (timer_now_ms() - start > DNS_TIMEOUT_MS) {
      retries++;
      if (retries < DNS_MAX_RETRIES) {
        serial_log("DNS: Timeout, retrying...");
        dns_send_query(hostname, DNS_TYPE_A);
        start = timer_now_ms();
      }
    }

    // Poll network for incoming packets (critical - gui_main doesn't run during
    // syscall)
    net_poll();

    // Yield to scheduler
    schedule();

    // Yield to scheduler
    schedule();
  }

  serial_log("DNS: Resolution timed out");
  return 0;
}

// Non-blocking resolve - returns immediately, use dns_get_result after
extern "C" void dns_resolve_async(const char *hostname) {
  uint32_t cached = dns_cache_lookup(hostname);
  if (cached) {
    dns_resolved_ip = cached;
    dns_response_received = 1;
    return;
  }

  dns_send_query(hostname, DNS_TYPE_A);
}

extern "C" int dns_get_result(uint32_t *ip_out) {
  if (dns_response_received == 1) {
    *ip_out = dns_resolved_ip;
    return 1;
  }
  if (dns_response_received == -1) {
    return -1; // Failed
  }
  return 0; // Pending
}

// Clear DNS cache
extern "C" void dns_cache_clear() {
  for (int i = 0; i < DNS_CACHE_SIZE; i++) {
    dns_cache[i].valid = 0;
  }
  serial_log("DNS: Cache cleared");
}

// Initialize DNS subsystem
extern "C" void dns_init() {
  memset(dns_cache, 0, sizeof(dns_cache));
  udp_bind(53053, dns_rx_handler); // Bind response port
  serial_log("DNS: Resolver initialized");
  serial_log_hex("DNS: Server IP = ", DNS_SERVER);
  serial_log_hex("DNS: Client port = ", 53053);
}
