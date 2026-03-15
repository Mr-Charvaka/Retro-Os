#include "../drivers/serial.h"
#include "../include/mbedtls/ctr_drbg.h"
#include "../include/mbedtls/entropy.h"
#include "../include/mbedtls/error.h"
#include "../include/mbedtls/ssl.h"
#include "../include/string.h"

// TCP API from http.cpp
struct tcp_tcb_t;
extern "C" int tcp_send_data(tcp_tcb_t *tcb, void *data, uint16_t len);
extern "C" int tcp_read_data(tcp_tcb_t *tcb, void *buffer, uint16_t len);
extern "C" int tcp_has_data(tcp_tcb_t *tcb);
extern "C" void net_poll(void);
extern "C" void schedule(void);
extern "C" uint32_t timer_now_ms(void);
extern "C" int mbedtls_ssl_set_hostname( mbedtls_ssl_context *ssl, const char *hostname );
extern "C" int mbedtls_ssl_conf_alpn_protocols( mbedtls_ssl_config *conf, const char **protos );

struct TLSContext {
  mbedtls_ssl_context ssl;
  mbedtls_ssl_config conf;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context ctr_drbg;
};

// Custom entropy source using system clock jitter
static int kernel_entropy_source(void *data, unsigned char *output, size_t len, size_t *olen) {
    (void)data;
    static uint32_t state = 0xDEADBEEF;
    for (size_t i = 0; i < len; i++) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        state += timer_now_ms();
        output[i] = (unsigned char)(state & 0xFF);
    }
    *olen = len;
    return 0;
}

static int tls_send_cb(void *ctx, const unsigned char *buf, size_t len) {
  tcp_tcb_t *conn = (tcp_tcb_t *)ctx;
  int ret = tcp_send_data(conn, (void *)buf, (uint16_t)len);
  return ret <= 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret;
}

static int tls_recv_cb(void *ctx, unsigned char *buf, size_t len) {
  tcp_tcb_t *conn = (tcp_tcb_t *)ctx;
  if (!tcp_has_data(conn))
    return MBEDTLS_ERR_SSL_WANT_READ;
  int ret = tcp_read_data(conn, buf, (uint16_t)len);
  return ret <= 0 ? MBEDTLS_ERR_SSL_INTERNAL_ERROR : ret;
}

extern "C" void *tls_init(void *tcp_conn, const char *hostname) {
  TLSContext *ctx = new TLSContext();
  mbedtls_ssl_init(&ctx->ssl);
  mbedtls_ssl_config_init(&ctx->conf);
  mbedtls_ctr_drbg_init(&ctx->ctr_drbg);
  mbedtls_entropy_init(&ctx->entropy);

  const char *pers = "retroos_tls";
  mbedtls_ctr_drbg_seed(&ctx->ctr_drbg, mbedtls_entropy_func, &ctx->entropy,
                        (const unsigned char *)pers, strlen(pers));

  // Add our custom entropy source to the mbedtls pool
  mbedtls_entropy_add_source(&ctx->entropy, kernel_entropy_source, NULL, 1, 1);

  mbedtls_ssl_config_defaults(&ctx->conf, MBEDTLS_SSL_IS_CLIENT,
                              MBEDTLS_SSL_TRANSPORT_STREAM,
                              MBEDTLS_SSL_PRESET_DEFAULT);

  // In a hobby OS, we skip certificate verification for now to actually reach
  // sites
  mbedtls_ssl_conf_authmode(&ctx->conf, MBEDTLS_SSL_VERIFY_NONE);
  mbedtls_ssl_conf_rng(&ctx->conf, mbedtls_ctr_drbg_random, &ctx->ctr_drbg);

  // Force HTTP/1.1 via ALPN - important for modern sites to avoid binary HTTP/2
  static const char *alpn_protocols[] = {"http/1.1", NULL};
  mbedtls_ssl_conf_alpn_protocols(&ctx->conf, alpn_protocols);

  mbedtls_ssl_setup(&ctx->ssl, &ctx->conf);
  mbedtls_ssl_set_hostname(&ctx->ssl, hostname);
  mbedtls_ssl_set_bio(&ctx->ssl, tcp_conn, tls_send_cb, tls_recv_cb, NULL);

  serial_log("TLS: Handshaking...");
  int ret;
  while ((ret = mbedtls_ssl_handshake(&ctx->ssl)) != 0) {
    if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
      serial_log_hex("TLS: Handshake failed! Error: 0x", -ret);
      return nullptr;
    }
    // Log state if we're waiting for data
    if (ctx->ssl.state % 10 == 0) { // Log occasionally to avoid spam
      serial_log_hex("TLS: Still handshaking... State: ", ctx->ssl.state);
    }
    net_poll();
    schedule();
  }
  serial_log("TLS: Handshake successful!");
  return ctx;
}

extern "C" int tls_write(void *context, const void *buf, size_t len) {
  TLSContext *ctx = (TLSContext *)context;
  return mbedtls_ssl_write(&ctx->ssl, (const unsigned char *)buf, len);
}

extern "C" int tls_read(void *context, void *buf, size_t len) {
  TLSContext *ctx = (TLSContext *)context;
  int ret = mbedtls_ssl_read(&ctx->ssl, (unsigned char *)buf, len);
  if (ret == MBEDTLS_ERR_SSL_WANT_READ)
    return 0;
  return ret;
}

extern "C" void tls_close(void *context) {
  TLSContext *ctx = (TLSContext *)context;
  mbedtls_ssl_close_notify(&ctx->ssl);
  mbedtls_ssl_free(&ctx->ssl);
  mbedtls_ssl_config_free(&ctx->conf);
  mbedtls_ctr_drbg_free(&ctx->ctr_drbg);
  mbedtls_entropy_free(&ctx->entropy);
  delete ctx;
}
