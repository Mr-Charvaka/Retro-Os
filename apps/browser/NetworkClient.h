#ifndef NETWORK_CLIENT_H
#define NETWORK_CLIENT_H

#include <stddef.h>
#include <stdint.h>

class NetworkClient {
public:
    NetworkClient();
    ~NetworkClient();

    bool connect(const char *host, int port);
    int read(unsigned char *buf, size_t len);
    int write(const unsigned char *buf, size_t len);
    void close();

    bool is_connected() const { return connected; }

private:
    // We use void* to hide mbedtls types from the header
    void *entropy;  
    void *ctr_drbg; 
    void *ssl;      
    void *conf;     
    void *cacert;     
    
    int server_fd; // File descriptor
    bool connected;
};

#endif // NETWORK_CLIENT_H
