// ============================================================================
// select.cpp - I/O Multiplexing Implementation
// Entirely hand-crafted for Retro-OS kernel
// ============================================================================

#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/poll.h"
#include "../include/time.h"
#include "../include/vfs.h"
#include "process.h"


extern "C" {

extern uint32_t tick;
#define TICKS_PER_SEC 100

// ============================================================================
// Check if fd is ready for I/O
// ============================================================================
static int fd_is_readable(int fd) {
  if (!current_process || fd < 0 || fd >= MAX_PROCESS_FILES)
    return 0;
  vfs_node_t *node = current_process->fd_table[fd];
  if (!node)
    return 0;

  // For pipes and sockets, check if there's data
  // For regular files, always readable
  // Simplified: assume always readable for now
  return 1;
}

static int fd_is_writable(int fd) {
  if (!current_process || fd < 0 || fd >= MAX_PROCESS_FILES)
    return 0;
  vfs_node_t *node = current_process->fd_table[fd];
  if (!node)
    return 0;

  // For pipes and sockets, check if buffer has space
  // For regular files, always writable
  // Simplified: assume always writable for now
  return 1;
}

static int fd_has_exception(int fd) {
  (void)fd;
  // No exceptional conditions tracked
  return 0;
}

// ============================================================================
// poll - Wait for events on file descriptors
// ============================================================================
int poll(struct pollfd *fds, unsigned int nfds, int timeout) {
  if (!fds && nfds > 0)
    return -EFAULT;

  uint32_t start_tick = tick;
  uint32_t wait_ticks = (timeout > 0) ? (timeout * TICKS_PER_SEC / 1000) : 0;
  int ready_count = 0;

  // Initialize revents
  for (unsigned int i = 0; i < nfds; i++)
    fds[i].revents = 0;

  do {
    ready_count = 0;

    for (unsigned int i = 0; i < nfds; i++) {
      int fd = fds[i].fd;

      // Check for invalid fd
      if (fd < 0 || !current_process || fd >= MAX_PROCESS_FILES ||
          !current_process->fd_table[fd]) {
        fds[i].revents |= POLLNVAL;
        ready_count++;
        continue;
      }

      // Check for readable
      if (fds[i].events & (POLLIN | POLLRDNORM)) {
        if (fd_is_readable(fd)) {
          fds[i].revents |= POLLIN | POLLRDNORM;
          ready_count++;
        }
      }

      // Check for writable
      if (fds[i].events & (POLLOUT | POLLWRNORM)) {
        if (fd_is_writable(fd)) {
          fds[i].revents |= POLLOUT | POLLWRNORM;
          ready_count++;
        }
      }

      // Check for error/hangup
      if (fd_has_exception(fd)) {
        fds[i].revents |= POLLERR;
        ready_count++;
      }
    }

    if (ready_count > 0 || timeout == 0)
      break;

    // Wait if no events and timeout not reached
    if (timeout < 0 || (tick - start_tick) < wait_ticks) {
      schedule();
    } else {
      break;
    }

  } while (1);

  return ready_count;
}

int ppoll(struct pollfd *fds, unsigned int nfds, const void *timeout_ts,
          const void *sigmask) {
  (void)sigmask; // Signal mask not implemented

  int timeout_ms = -1;
  if (timeout_ts) {
    const struct timespec *ts = (const struct timespec *)timeout_ts;
    timeout_ms = ts->tv_sec * 1000 + ts->tv_nsec / 1000000;
  }

  return poll(fds, nfds, timeout_ms);
}

// ============================================================================
// select - Synchronous I/O multiplexing
// ============================================================================
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           void *timeout) {
  if (nfds < 0 || nfds > FD_SETSIZE)
    return -EINVAL;

  // Convert timeout to milliseconds
  int timeout_ms = -1;
  if (timeout) {
    struct timeval *tv = (struct timeval *)timeout;
    timeout_ms = tv->tv_sec * 1000 + tv->tv_usec / 1000;
  }

  uint32_t start_tick = tick;
  uint32_t wait_ticks =
      (timeout_ms > 0) ? (timeout_ms * TICKS_PER_SEC / 1000) : 0;
  int ready_count = 0;

  // Save original sets for checking
  fd_set read_copy, write_copy, except_copy;
  if (readfds)
    read_copy = *readfds;
  else
    FD_ZERO(&read_copy);
  if (writefds)
    write_copy = *writefds;
  else
    FD_ZERO(&write_copy);
  if (exceptfds)
    except_copy = *exceptfds;
  else
    FD_ZERO(&except_copy);

  do {
    ready_count = 0;

    // Clear result sets
    if (readfds)
      FD_ZERO(readfds);
    if (writefds)
      FD_ZERO(writefds);
    if (exceptfds)
      FD_ZERO(exceptfds);

    for (int fd = 0; fd < nfds; fd++) {
      // Check readability
      if (FD_ISSET(fd, &read_copy)) {
        if (fd_is_readable(fd)) {
          if (readfds)
            FD_SET(fd, readfds);
          ready_count++;
        }
      }

      // Check writability
      if (FD_ISSET(fd, &write_copy)) {
        if (fd_is_writable(fd)) {
          if (writefds)
            FD_SET(fd, writefds);
          ready_count++;
        }
      }

      // Check exceptions
      if (FD_ISSET(fd, &except_copy)) {
        if (fd_has_exception(fd)) {
          if (exceptfds)
            FD_SET(fd, exceptfds);
          ready_count++;
        }
      }
    }

    if (ready_count > 0 || timeout_ms == 0)
      break;

    // Wait if no events and timeout not reached
    if (timeout_ms < 0 || (tick - start_tick) < wait_ticks) {
      schedule();
    } else {
      break;
    }

  } while (1);

  return ready_count;
}

int pselect(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
            const void *timeout, const void *sigmask) {
  (void)sigmask; // Signal mask not implemented

  struct timeval tv;
  void *tv_ptr = 0;

  if (timeout) {
    const struct timespec *ts = (const struct timespec *)timeout;
    tv.tv_sec = ts->tv_sec;
    tv.tv_usec = ts->tv_nsec / 1000;
    tv_ptr = &tv;
  }

  return select(nfds, readfds, writefds, exceptfds, tv_ptr);
}

} // extern "C"
