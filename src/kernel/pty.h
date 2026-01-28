#ifndef PTY_H
#define PTY_H

#include "../include/types.h"
#include "tty.h"
#include "wait_queue.h"

#define PTY_BUFFER_SIZE 4096

typedef struct {
  char buffer[PTY_BUFFER_SIZE];
  int head;
  int tail;
  int count;
  wait_queue_t wait;
} pty_fifo_t;

typedef struct {
  int id;
  pty_fifo_t master_to_slave; // Terminal -> Shell (Input)
  pty_fifo_t slave_to_master; // Shell -> Terminal (Output)
  tty_t slave_tty;            // Shell side (Standard TTY logic)
  bool master_open;
  bool slave_open;
  bool master_nonblock;
  bool slave_nonblock;
  struct vfs_node *master_node;
  struct vfs_node *slave_node;
} pty_t;

#ifdef __cplusplus
extern "C" {
#endif

void pty_init();
int sys_pty_create(int *master_fd, int *slave_fd);

#ifdef __cplusplus
}
#endif

#endif // PTY_H
