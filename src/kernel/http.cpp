// http.cpp - HTTP Client for Retro-OS
// Powered by http-parser (Mbed architecture)

#include "../drivers/serial.h"
#include "../include/string.h"
#include "heap.h"
#include "http_parser.h"
#include "memory.h"
#include <stddef.h>
#include <stdint.h>


/* ===================== CONFIG ===================== */

#define HTTP_DEFAULT_PORT 80
#define HTTP_MAX_HEADERS 32
#define HTTP_TIMEOUT_MS 10000

/* ===================== STRUCTURES ===================== */

struct http_header {
  char name[64];
  char value[256];
};

struct http_response {
  int status_code;
  char status_text[64];
  http_header headers[HTTP_MAX_HEADERS];
  int header_count;
  uint8_t *body;
  uint32_t body_length;
  uint32_t content_length;
  int chunked;
};

struct http_request {
  char method[16];
  char path[256];
  char host[128];
  http_header headers[HTTP_MAX_HEADERS];
  int header_count;
  uint8_t *body;
  uint32_t body_length;
};

struct parser_context {
  http_response *resp;
  char last_header[64];
};

/* ===================== EXTERNAL DECLARATIONS ===================== */

extern "C" uint32_t dns_resolve(const char *hostname);
extern "C" uint32_t timer_now_ms(void);

// TCP API
struct tcp_tcb_t;
extern "C" tcp_tcb_t *tcp_connect(uint32_t local_ip, uint16_t local_port,
                                  uint32_t remote_ip, uint16_t remote_port);
extern "C" int tcp_send_data(tcp_tcb_t *tcb, void *data, uint16_t len);
extern "C" int tcp_read_data(tcp_tcb_t *tcb, void *buffer, uint16_t len);
extern "C" int tcp_is_connected(tcp_tcb_t *tcb);
extern "C" int tcp_has_data(tcp_tcb_t *tcb);
extern "C" void tcp_close(tcp_tcb_t *tcb);
extern "C" void schedule(void);
extern "C" void net_poll(void);

static uint32_t local_ip = 0x0F02000A; // 10.0.2.15

/* ===================== STRING HELPERS ===================== */

static int http_atoi(const char *s) {
  int result = 0;
  while (*s >= '0' && *s <= '9') {
    result = result * 10 + (*s - '0');
    s++;
  }
  return result;
}

static int http_strncasecmp(const char *s1, const char *s2, size_t n) {
  while (n-- && *s1 && *s2) {
    char c1 = *s1++;
    char c2 = *s2++;
    if (c1 >= 'A' && c1 <= 'Z')
      c1 += 32;
    if (c2 >= 'A' && c2 <= 'Z')
      c2 += 32;
    if (c1 != c2)
      return c1 - c2;
  }
  return 0;
}

/* ===================== PARSER CALLBACKS ===================== */

static int on_header_field(http_parser *p, const char *at, size_t length) {
  auto ctx = (parser_context *)p->data;
  size_t copy_len = length < 63 ? length : 63;
  memcpy(ctx->last_header, at, copy_len);
  ctx->last_header[copy_len] = 0;
  return 0;
}

static int on_header_value(http_parser *p, const char *at, size_t length) {
  auto ctx = (parser_context *)p->data;
  if (ctx->resp->header_count < HTTP_MAX_HEADERS) {
    http_header *h = &ctx->resp->headers[ctx->resp->header_count];
    strcpy(h->name, ctx->last_header);
    size_t copy_len = length < 255 ? length : 255;
    memcpy(h->value, at, copy_len);
    h->value[copy_len] = 0;

    if (http_strncasecmp(h->name, "Content-Length", 14) == 0)
      ctx->resp->content_length = http_atoi(h->value);
    if (http_strncasecmp(h->name, "Transfer-Encoding", 17) == 0) {
      if (strstr(h->value, "chunked"))
        ctx->resp->chunked = 1;
    }
    ctx->resp->header_count++;
  }
  return 0;
}

static int on_body(http_parser *p, const char *at, size_t length) {
  auto ctx = (parser_context *)p->data;
  // We parse in-place, but move body to a contiguous start point if needed
  if (!ctx->resp->body) {
    ctx->resp->body = (uint8_t *)at;
    ctx->resp->body_length = length;
  } else {
    // http-parser handles chunking: we just need to append/ensure contiguous if
    // multi-callback For simplicity in this kernel, we assume the initial
    // buffer is continuous and parser just segments it If it's chunked,
    // http-parser calls this FOR EACH CHUNK without sizes. Contiguous!
    if ((uint8_t *)at != ctx->resp->body + ctx->resp->body_length) {
      memmove(ctx->resp->body + ctx->resp->body_length, at, length);
    }
    ctx->resp->body_length += length;
  }
  return 0;
}

static int on_status(http_parser *p, const char *at, size_t length) {
  auto ctx = (parser_context *)p->data;
  ctx->resp->status_code = p->status_code;
  size_t copy_len = length < 63 ? length : 63;
  memcpy(ctx->resp->status_text, at, copy_len);
  ctx->resp->status_text[copy_len] = 0;
  return 0;
}

static int http_parse_response(uint8_t *data, int len, http_response *resp) {
  memset(resp, 0, sizeof(http_response));
  http_parser parser;
  http_parser_init(&parser, HTTP_RESPONSE);
  parser_context ctx;
  ctx.resp = resp;
  parser.data = &ctx;

  http_parser_settings settings;
  memset(&settings, 0, sizeof(settings));
  settings.on_header_field = on_header_field;
  settings.on_header_value = on_header_value;
  settings.on_body = on_body;
  settings.on_status = on_status;

  http_parser_execute(&parser, &settings, (const char *)data, len);
  return 0;
}

/* ===================== REQUEST BUILDING ===================== */

static int http_append(char *buffer, int pos, int max_len, const char *str) {
  while (*str && pos < max_len - 1)
    buffer[pos++] = *str++;
  return pos;
}

static int http_append_uint(char *buffer, int pos, int max_len, uint32_t val) {
  char tmp[12];
  int i = 0;
  if (val == 0)
    tmp[i++] = '0';
  else
    while (val > 0) {
      tmp[i++] = '0' + (val % 10);
      val /= 10;
    }
  while (i > 0 && pos < max_len - 1)
    buffer[pos++] = tmp[--i];
  return pos;
}

static int http_build_request(http_request *req, char *buffer, int max_len) {
  int pos = 0;
  pos = http_append(buffer, pos, max_len, req->method);
  pos = http_append(buffer, pos, max_len, " ");
  pos = http_append(buffer, pos, max_len, req->path);
  pos = http_append(buffer, pos, max_len, " HTTP/1.1\r\nHost: ");
  pos = http_append(buffer, pos, max_len, req->host);
  pos = http_append(buffer, pos, max_len,
                    "\r\nUser-Agent: RetroOS/1.0\r\nAccept-Encoding: "
                    "identity\r\nConnection: close\r\n\r\n");
  buffer[pos] = '\0';
  return pos;
}

/* ===================== PUBLIC API ===================== */

extern "C" void *tls_init(void *tcp_conn, const char *hostname);
extern "C" int tls_write(void *context, const void *buf, size_t len);
extern "C" int tls_read(void *context, void *buf, size_t len);
extern "C" void tls_close(void *context);

extern "C" int http_get(const char *url, uint8_t *buffer, int max_len,
                        http_response *resp) {
  bool is_https = (strncmp(url, "https://", 8) == 0);
  const char *host_start = is_https ? url + 8 : url + 7;
  const char *path_start = strchr(host_start, '/');

  char hostname[128], path[256];
  uint16_t port = is_https ? 443 : 80;

  if (path_start) {
    int host_len = path_start - host_start;
    if (host_len > 127)
      host_len = 127;
    memcpy(hostname, host_start, host_len);
    hostname[host_len] = 0;
    strncpy(path, path_start, 255);
  } else {
    strncpy(hostname, host_start, 127);
    strcpy(path, "/");
  }

  uint32_t remote_ip = dns_resolve(hostname);
  if (remote_ip == 0)
    return -1;

  uint16_t local_port = 49152 + (timer_now_ms() % 10000);
  tcp_tcb_t *conn = tcp_connect(local_ip, local_port, remote_ip, port);
  if (!conn)
    return -1;

  uint32_t start = timer_now_ms();
  while (!tcp_is_connected(conn)) {
    if (timer_now_ms() - start > HTTP_TIMEOUT_MS) {
      tcp_close(conn);
      return -1;
    }
    net_poll();
    schedule();
  }

  void *tls_context = is_https ? tls_init(conn, hostname) : nullptr;
  if (is_https && !tls_context) {
    tcp_close(conn);
    return -1;
  }

  char req_buffer[1024];
  http_request req_struct;
  strcpy(req_struct.method, "GET");
  strcpy(req_struct.path, path);
  strcpy(req_struct.host, hostname);
  int req_len = http_build_request(&req_struct, req_buffer, sizeof(req_buffer));

  if (is_https)
    tls_write(tls_context, req_buffer, req_len);
  else
    tcp_send_data(conn, req_buffer, req_len);

  int total_received = 0;
  start = timer_now_ms();
  while (total_received < max_len) {
    int received =
        is_https
            ? tls_read(tls_context, buffer + total_received,
                       max_len - total_received)
            : (tcp_has_data(conn) ? tcp_read_data(conn, buffer + total_received,
                                                  max_len - total_received)
                                  : 0);
    if (received > 0) {
      total_received += received;
      start = timer_now_ms();
    } else if (received < 0 && received != -2)
      break;
    if (!tcp_is_connected(conn) && !tcp_has_data(conn))
      break;
    if (timer_now_ms() - start > HTTP_TIMEOUT_MS)
      break;
    net_poll();
    schedule();
  }

  if (is_https)
    tls_close(tls_context);
  tcp_close(conn);

  if (total_received > 0 && resp) {
    http_parse_response(buffer, total_received, resp);
    if (resp->body) {
      // Contiguous copy body to front of buffer
      memmove(buffer, resp->body, resp->body_length);
      buffer[resp->body_length] = 0;
      return resp->body_length;
    }
  }
  return total_received;
}

extern "C" const char *http_get_header(http_response *resp, const char *name) {
  for (int i = 0; i < resp->header_count; i++) {
    if (http_strncasecmp(resp->headers[i].name, name, strlen(name)) == 0)
      return resp->headers[i].value;
  }
  return nullptr;
}

extern "C" void http_init() { serial_log("HTTP: Mbed-Parser Ready"); }
