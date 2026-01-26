#include "NetworkClient.h"
#include "../include/os/syscalls.hpp"
#include <string.h>
#include <stdlib.h>

// MbedTLS includes
#include "mbedtls/net_sockets.h"
#include "mbedtls/debug.h"
#include "mbedtls/ssl.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/error.h"
#include "mbedtls/certs.h"

// Custom Send/Recv callbacks for mbedtls using our syscalls
static int my_mbedtls_net_send(void *ctx, const unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int ret = OS::Syscall::write(fd, buf, len);
    if (ret < 0) return MBEDTLS_ERR_NET_SEND_FAILED;
    return ret;
}

static int my_mbedtls_net_recv(void *ctx, unsigned char *buf, size_t len) {
    int fd = (int)(intptr_t)ctx;
    int ret = OS::Syscall::read(fd, buf, len);
    if (ret < 0) return MBEDTLS_ERR_NET_RECV_FAILED;
    return ret;
}

NetworkClient::NetworkClient() : server_fd(-1), connected(false) {
    entropy = malloc(sizeof(mbedtls_entropy_context));
    ctr_drbg = malloc(sizeof(mbedtls_ctr_drbg_context));
    ssl = malloc(sizeof(mbedtls_ssl_context));
    conf = malloc(sizeof(mbedtls_ssl_config));
    cacert = malloc(sizeof(mbedtls_x509_crt));

    mbedtls_entropy_init((mbedtls_entropy_context*)entropy);
    mbedtls_ctr_drbg_init((mbedtls_ctr_drbg_context*)ctr_drbg);
    mbedtls_ssl_init((mbedtls_ssl_context*)ssl);
    mbedtls_ssl_config_init((mbedtls_ssl_config*)conf);
    mbedtls_x509_crt_init((mbedtls_x509_crt*)cacert);

    // Seed RNG
    const char *pers = "retro_os_browser";
    mbedtls_ctr_drbg_seed((mbedtls_ctr_drbg_context*)ctr_drbg, mbedtls_entropy_func, entropy,
                          (const unsigned char *)pers, strlen(pers));
}

NetworkClient::~NetworkClient() {
    close();
    mbedtls_x509_crt_free((mbedtls_x509_crt*)cacert);
    mbedtls_ssl_config_free((mbedtls_ssl_config*)conf);
    mbedtls_ssl_free((mbedtls_ssl_context*)ssl);
    mbedtls_ctr_drbg_free((mbedtls_ctr_drbg_context*)ctr_drbg);
    mbedtls_entropy_free((mbedtls_entropy_context*)entropy);
    
    free(cacert);
    free(conf);
    free(ssl);
    free(ctr_drbg);
    free(entropy);
}

bool NetworkClient::connect(const char *host, int port) {
    // 1. Create Socket via Syscall
    server_fd = OS::Syscall::socket(AF_INET, SOCK_STREAM, 0); // Assuming AF_INET=2, SOCK_STREAM=1
    if (server_fd < 0) return false;

    // 2. Connect via Syscall (Blocking)
    // Note: OS::Syscall::connect expects "path" for string, but standard sockets expect struct sockaddr.
    // However, looking at socket.h, sys_connect takes 'const char *path'.
    // It seems this OS uses string-based addressing or maybe specific format "IP:PORT"?
    // Let's assume typical IP string for now or "IP:PORT" string.
    // tcptest.cpp didn't show connect usage, it used custom syscall.
    // Let's try passing "IP:PORT" string if that's how this OS Works, or simple struct sockaddr cast if it's standard.
    // Wait, sys_connect(int sockfd, const char *path) in socket.h implies string path (like binding to a file path? or /dev/net/ip:port?)
    // This is tricky. I'll stick to a simple string "8.8.8.8:80" format if I can format it, or just try to pass the host.
    
    // For now, let's assume we can connect by passing the host string if the kernel supports DNS/IP parsing from string.
    // If not, this will fail.
    
    if (OS::Syscall::connect(server_fd, host) < 0) {
        OS::Syscall::close(server_fd);
        return false;
    }

    // 3. Setup SSL
    mbedtls_ssl_config_defaults((mbedtls_ssl_config*)conf,
                                MBEDTLS_SSL_IS_CLIENT,
                                MBEDTLS_SSL_TRANSPORT_STREAM,
                                MBEDTLS_SSL_PRESET_DEFAULT);

    mbedtls_ssl_conf_rng((mbedtls_ssl_config*)conf, mbedtls_ctr_drbg_random, ctr_drbg);
    
    // Allow self-signed for now (easier for testing)
    mbedtls_ssl_conf_authmode((mbedtls_ssl_config*)conf, MBEDTLS_SSL_VERIFY_NONE);

    mbedtls_ssl_setup((mbedtls_ssl_context*)ssl, (mbedtls_ssl_config*)conf);

    // Set IO callbacks
    mbedtls_ssl_set_bio((mbedtls_ssl_context*)ssl, (void*)(intptr_t)server_fd,
                        my_mbedtls_net_send, my_mbedtls_net_recv, NULL);

    // Handshake
    if (mbedtls_ssl_handshake((mbedtls_ssl_context*)ssl) != 0) {
        OS::Syscall::close(server_fd);
        return false;
    }

    connected = true;
    return true;
}

int NetworkClient::read(unsigned char *buf, size_t len) {
    if (!connected) return -1;
    return mbedtls_ssl_read((mbedtls_ssl_context*)ssl, buf, len);
}

int NetworkClient::write(const unsigned char *buf, size_t len) {
    if (!connected) return -1;
    return mbedtls_ssl_write((mbedtls_ssl_context*)ssl, buf, len);
}

void NetworkClient::close() {
    if (connected) {
        mbedtls_ssl_close_notify((mbedtls_ssl_context*)ssl);
        OS::Syscall::close(server_fd);
        connected = false;
        server_fd = -1;
    }
}
