#ifndef POLL_H
#define POLL_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// poll Structure
// ============================================================================
struct pollfd {
  int fd;        // File descriptor
  short events;  // Events to poll for
  short revents; // Events that occurred
};

// ============================================================================
// Poll Event Flags
// ============================================================================
#define POLLIN 0x0001     // Data available to read
#define POLLPRI 0x0002    // Urgent data to read
#define POLLOUT 0x0004    // Writing is possible
#define POLLERR 0x0008    // Error condition
#define POLLHUP 0x0010    // Hang up
#define POLLNVAL 0x0020   // Invalid fd
#define POLLRDNORM 0x0040 // Normal data can be read
#define POLLRDBAND 0x0080 // Priority data can be read
#define POLLWRNORM 0x0100 // Normal data can be written
#define POLLWRBAND 0x0200 // Priority data can be written

// ============================================================================
// poll Function
// ============================================================================
int poll(struct pollfd *fds, unsigned int nfds, int timeout);
int ppoll(struct pollfd *fds, unsigned int nfds, const void *timeout,
          const void *sigmask);

// ============================================================================
// select Structures
// ============================================================================
#define FD_SETSIZE 64

typedef struct {
  unsigned long fds_bits[FD_SETSIZE / (8 * sizeof(unsigned long))];
} fd_set;

#define FD_ZERO(set)                                                           \
  do {                                                                         \
    for (unsigned int __i = 0;                                                 \
         __i < sizeof((set)->fds_bits) / sizeof(unsigned long); __i++)         \
      (set)->fds_bits[__i] = 0;                                                \
  } while (0)

#define FD_SET(fd, set)                                                        \
  ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] |=                      \
   (1UL << ((fd) % (8 * sizeof(unsigned long)))))

#define FD_CLR(fd, set)                                                        \
  ((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &=                      \
   ~(1UL << ((fd) % (8 * sizeof(unsigned long)))))

#define FD_ISSET(fd, set)                                                      \
  (((set)->fds_bits[(fd) / (8 * sizeof(unsigned long))] &                      \
    (1UL << ((fd) % (8 * sizeof(unsigned long))))) != 0)

// ============================================================================
// select Function
// ============================================================================
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           void *timeout);
int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const void *timeout, const void *sigmask);

#ifdef __cplusplus
}
#endif

#endif // POLL_H
