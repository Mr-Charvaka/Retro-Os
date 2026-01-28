// net_advanced.h - Advanced Network Stack Headers for Retro-OS
// Unified API for DNS, DHCP, HTTP, and Socket operations

#ifndef NET_ADVANCED_H
#define NET_ADVANCED_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ====================== IP ADDRESS HELPERS ====================== */

// Convert dotted-decimal string to network byte order IP
// Example: "10.0.2.15" -> 0x0F02000A
static inline uint32_t ip_from_string(const char *str) {
  uint32_t a = 0, b = 0, c = 0, d = 0;
  int i = 0;

  // Parse first octet
  while (str[i] >= '0' && str[i] <= '9') {
    a = a * 10 + (str[i] - '0');
    i++;
  }
  if (str[i] != '.')
    return 0;
  i++;

  // Parse second octet
  while (str[i] >= '0' && str[i] <= '9') {
    b = b * 10 + (str[i] - '0');
    i++;
  }
  if (str[i] != '.')
    return 0;
  i++;

  // Parse third octet
  while (str[i] >= '0' && str[i] <= '9') {
    c = c * 10 + (str[i] - '0');
    i++;
  }
  if (str[i] != '.')
    return 0;
  i++;

  // Parse fourth octet
  while (str[i] >= '0' && str[i] <= '9') {
    d = d * 10 + (str[i] - '0');
    i++;
  }

  // Return in network byte order (little-endian storage)
  return (d << 24) | (c << 16) | (b << 8) | a;
}

// Convert network byte order IP to dotted-decimal string
static inline void ip_to_string(uint32_t ip, char *buf) {
  uint8_t a = ip & 0xFF;
  uint8_t b = (ip >> 8) & 0xFF;
  uint8_t c = (ip >> 16) & 0xFF;
  uint8_t d = (ip >> 24) & 0xFF;

  // Simple itoa for each octet
  char *p = buf;

  // First octet
  if (a >= 100) {
    *p++ = '0' + a / 100;
    a %= 100;
  }
  if (a >= 10 || p > buf) {
    *p++ = '0' + a / 10;
    a %= 10;
  }
  *p++ = '0' + a;
  *p++ = '.';

  // Second octet
  char *start = p;
  if (b >= 100) {
    *p++ = '0' + b / 100;
    b %= 100;
  }
  if (b >= 10 || p > start) {
    *p++ = '0' + b / 10;
    b %= 10;
  }
  *p++ = '0' + b;
  *p++ = '.';

  // Third octet
  start = p;
  if (c >= 100) {
    *p++ = '0' + c / 100;
    c %= 100;
  }
  if (c >= 10 || p > start) {
    *p++ = '0' + c / 10;
    c %= 10;
  }
  *p++ = '0' + c;
  *p++ = '.';

  // Fourth octet
  start = p;
  if (d >= 100) {
    *p++ = '0' + d / 100;
    d %= 100;
  }
  if (d >= 10 || p > start) {
    *p++ = '0' + d / 10;
    d %= 10;
  }
  *p++ = '0' + d;
  *p = '\0';
}

/* ====================== DNS API ====================== */

// Initialize DNS resolver
void dns_init(void);

// Resolve hostname to IP address (blocking)
// Returns IP in network byte order, or 0 on failure
uint32_t dns_resolve(const char *hostname);

// Start async DNS resolution
void dns_resolve_async(const char *hostname);

// Check async resolution result
// Returns: 1=success (ip written), 0=pending, -1=failed
int dns_get_result(uint32_t *ip_out);

// Clear DNS cache
void dns_cache_clear(void);

/* ====================== DHCP API ====================== */

// Initialize DHCP client
void dhcp_init(void);

// Attempt to configure network via DHCP (blocking, ~10s timeout)
// Returns: 0=success, -1=failure
int dhcp_configure(void);

// Check if DHCP configuration is active
int dhcp_is_configured(void);

// Get current network configuration
uint32_t dhcp_get_ip(void);
uint32_t dhcp_get_gateway(void);
uint32_t dhcp_get_dns(void);
uint32_t dhcp_get_subnet(void);

// Release DHCP lease
void dhcp_release(void);

// Check and renew lease if needed (call periodically)
void dhcp_check_lease(void);

/* ====================== HTTP API ====================== */

#define HTTP_MAX_HEADERS 32

struct http_header {
  char name[64];
  char value[256];
};

struct http_response {
  int status_code;
  char status_text[64];
  struct http_header headers[HTTP_MAX_HEADERS];
  int header_count;
  uint8_t *body;
  uint32_t body_length;
  uint32_t content_length;
  int chunked;
};

// Initialize HTTP client
void http_init(void);

// Perform HTTP GET request
// Returns: bytes received, or -1 on error
int http_get(const char *url, uint8_t *buffer, int max_len,
             struct http_response *resp);

// Perform HTTP POST request
int http_post(const char *url, const char *content_type, uint8_t *post_data,
              int post_len, uint8_t *buffer, int max_len,
              struct http_response *resp);

// Get specific header value from response
const char *http_get_header(struct http_response *resp, const char *name);

/* ====================== SOCKET API ====================== */

// Socket types
#define SOCK_STREAM 1 // TCP
#define SOCK_DGRAM 2  // UDP
#define SOCK_RAW 3    // Raw IP

// Address families
#define AF_INET 2 // IPv4

// Socket address structure
struct sockaddr_in {
  uint16_t sin_family;
  uint16_t sin_port; // Network byte order
  uint32_t sin_addr; // Network byte order
  uint8_t sin_zero[8];
};

struct sockaddr {
  uint16_t sa_family;
  char sa_data[14];
};

// Socket options
#define SOL_SOCKET 1
#define SO_REUSEADDR 2
#define SO_KEEPALIVE 9
#define SO_RCVTIMEO 20
#define SO_SNDTIMEO 21

// Create a socket
// Returns: socket descriptor, or -1 on error
int net_socket(int domain, int type, int protocol);

// Bind socket to local address
int net_bind(int sockfd, const struct sockaddr *addr, uint32_t addrlen);

// Listen for incoming connections (TCP)
int net_listen(int sockfd, int backlog);

// Accept incoming connection (TCP)
int net_accept(int sockfd, struct sockaddr *addr, uint32_t *addrlen);

// Connect to remote address
int net_connect(int sockfd, const struct sockaddr *addr, uint32_t addrlen);

// Send data on connected socket
int net_send(int sockfd, const void *buf, uint32_t len, int flags);

// Receive data from connected socket
int net_recv(int sockfd, void *buf, uint32_t len, int flags);

// Send data to specific address (UDP)
int net_sendto(int sockfd, const void *buf, uint32_t len, int flags,
               const struct sockaddr *dest_addr, uint32_t addrlen);

// Receive data with source address (UDP)
int net_recvfrom(int sockfd, void *buf, uint32_t len, int flags,
                 struct sockaddr *src_addr, uint32_t *addrlen);

// Close socket
int net_close(int sockfd);

// Set socket options
int net_setsockopt(int sockfd, int level, int optname, const void *optval,
                   uint32_t optlen);

// Get socket options
int net_getsockopt(int sockfd, int level, int optname, void *optval,
                   uint32_t *optlen);

// Check if socket has data available
int socket_poll(int sockfd, int timeout_ms);

/* ====================== UTILITY FUNCTIONS ====================== */

// Byte order conversion (host to network)
uint16_t htons(uint16_t hostshort);
uint32_t htonl(uint32_t hostlong);

// Byte order conversion (network to host)
uint16_t ntohs(uint16_t netshort);
uint32_t ntohl(uint32_t netlong);

// Convert IP string to network address
int inet_aton(const char *cp, uint32_t *inp);

// Convert network address to IP string
const char *inet_ntoa(uint32_t in);

/* ====================== INITIALIZATION ====================== */

// Initialize entire advanced network stack
void net_advanced_init(void);

// Get current network status
struct net_status {
  int link_up;
  int dhcp_configured;
  uint32_t ip_addr;
  uint32_t gateway;
  uint32_t dns_server;
  uint32_t packets_sent;
  uint32_t packets_received;
  uint32_t bytes_sent;
  uint32_t bytes_received;
};

void net_get_status(struct net_status *status);
void net_print_status(void);

// Test functions
int net_test_dns(const char *hostname);
int net_test_http(const char *url);
int net_self_test(void);

#ifdef __cplusplus
}
#endif

#endif // NET_ADVANCED_H
