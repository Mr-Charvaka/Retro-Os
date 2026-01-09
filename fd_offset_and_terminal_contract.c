/**************************************************************
 * fd_offset_and_terminal_contract.c
 * AUTHORITATIVE REFERENCE IMPLEMENTATION
 * FOR RETRO-OS PERSISTENCE AND SHELL READINESS
 *
 * This file contains the "Source of Truth" for the
 * File Descriptor (OFS) and PTY/Terminal implementation.
 *************************************************************/

#include <stddef.h>
#include <stdint.h>


/* ============================================================
 * 🔴 SECTION 1 — KERNEL: FILE DESCRIPTOR OFFSETS (REAL FIX)
 * This solves the "hardcoded offset 0" bug permanently.
 * ============================================================ */

#define MAX_FD 256
#define O_RDONLY 0x00
#define O_WRONLY 0x01
#define O_RDWR 0x02
#define O_APPEND 0x08
#define O_CREAT 0x200

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

typedef struct vnode {
  uint64_t size;
  // In Retro-OS, these correspond to vfs_node_t->read/write
  int (*read)(struct vnode *node, uint64_t offset, void *buf, size_t size);
  int (*write)(struct vnode *node, uint64_t offset, const void *buf,
               size_t size);
} vnode_t;

/**
 * FILE DESCRIPTION (OFD - Open File Description)
 * This structure lives in the kernel heap and represents the "session"
 * of an open file. It tracks the cursor (offset).
 */
typedef struct file_description {
  vnode_t *vnode;  /* The actual file on disk/driver */
  uint64_t offset; /* THE CRITICAL PIECE: Current seek position */
  int flags;       /* O_RDONLY, O_WRONLY, O_APPEND */
  int ref_count;   /* Number of FDs pointing here (for fork/dup) */
} file_description_t;

typedef struct process {
  /* Process-specific handle table. Index is the 'fd' integer. */
  file_description_t *fd_table[MAX_FD];
} process_t;

extern process_t *current_process;

/* Syscall: open() */
int sys_open(vnode_t *vnode, int flags) {
  if (!vnode)
    return -1;

  file_description_t *desc =
      (file_description_t *)kmalloc(sizeof(file_description_t));
  desc->vnode = vnode;
  desc->flags = flags;
  desc->ref_count = 1;

  /* Handle O_APPEND: Start cursor at the end of the file */
  desc->offset = (flags & O_APPEND) ? vnode->size : 0;

  /* Find free slot in process table */
  for (int i = 0; i < MAX_FD; i++) {
    if (!current_process->fd_table[i]) {
      current_process->fd_table[i] = desc;
      return i;
    }
  }
  return -1;
}

/* Syscall: read() - Now uses the offset from the description */
int sys_read(int fd, void *buf, size_t size) {
  if (fd < 0 || fd >= MAX_FD)
    return -1;
  file_description_t *f = current_process->fd_table[fd];
  if (!f)
    return -1;

  int n = f->vnode->read(f->vnode, f->offset, buf, size);
  if (n > 0) {
    f->offset += n; /* Advance the cursor */
  }
  return n;
}

/* Syscall: write() - Handles O_APPEND and cursor advancing */
int sys_write(int fd, const void *buf, size_t size) {
  if (fd < 0 || fd >= MAX_FD)
    return -1;
  file_description_t *f = current_process->fd_table[fd];
  if (!f)
    return -1;

  /* If opened with O_APPEND, force cursor to end before every write */
  if (f->flags & O_APPEND) {
    f->offset = f->vnode->size;
  }

  int n = f->vnode->write(f->vnode, f->offset, buf, size);
  if (n > 0) {
    f->offset += n; /* Advance the cursor */
  }
  return n;
}

/* Syscall: lseek() - Allows shell/apps to move the cursor */
int sys_lseek(int fd, int64_t off, int whence) {
  if (fd < 0 || fd >= MAX_FD)
    return -1;
  file_description_t *f = current_process->fd_table[fd];
  if (!f)
    return -1;

  uint64_t new_offset = f->offset;
  if (whence == SEEK_SET)
    new_offset = off;
  else if (whence == SEEK_CUR)
    new_offset += off;
  else if (whence == SEEK_END)
    new_offset = f->vnode->size + off;

  f->offset = new_offset;
  return (int)f->offset;
}

/* ============================================================
 * 🔴 SECTION 2 — KERNEL: PTY (PSEUDO TERMINAL) DRIVER
 * The bridge between the Terminal App and the Shell.
 * ============================================================ */

typedef struct pty {
  char buffer[4096];
  int head;
  int tail;
  int master_open;
  int slave_open;
} pty_t;

pty_t pty_pool[8]; /* Retro-OS supports 8 concurrent terminals */

/**
 * Terminal App reads from PTY Master
 * Shell/Process reads from PTY Slave
 */
int pty_read(pty_t *p, char *buf, int size) {
  int i = 0;
  while (p->tail != p->head && i < size) {
    buf[i++] = p->buffer[p->tail % 4096];
    p->tail++;
  }
  return i;
}

/**
 * Keystrokes pushed into PTY buffer
 */
int pty_write(pty_t *p, const char *buf, int size) {
  for (int i = 0; i < size; i++) {
    p->buffer[p->head % 4096] = buf[i];
    p->head++;
  }
  return size;
}

/* ============================================================
 * 🔴 SECTION 3 — USERLAND: REAL SHELL (NOT FAKE)
 * A shell that uses real standard I/O (fd 0 and 1).
 * ============================================================ */

void shell_main() {
  char line[256];
  char prompt[] = "$ ";

  while (1) {
    /* Write prompt to stdout (fd 1) */
    write(1, prompt, 2);

    /* Read command from stdin (fd 0) */
    int n = read(0, line, sizeof(line) - 1);
    if (n <= 0)
      break;
    line[n] = 0;

    /* Basic Command: cat */
    if (strncmp(line, "cat ", 4) == 0) {
      const char *filename = line + 4;
      int fd = open(filename, O_RDONLY);
      if (fd >= 0) {
        char buf[128];
        int bytes;
        /* This now works even for large files thanks to FD Offsets! */
        while ((bytes = read(fd, buf, sizeof(buf))) > 0) {
          write(1, buf, bytes);
        }
        close(fd);
      }
    }
    /* More commands (ls, cd, etc) using the same FD logic... */
  }
}

/* ============================================================
 * 🔴 SECTION 4 — USERLAND GUI TERMINAL (THE “HUMAN PIECE”)
 * The visual window that you see on the desktop.
 * ============================================================ */

/* The Terminal App owns the "Master" end of the pipe */
int pty_master_fd;

/**
 * Called by GUI System when a key is pressed in the terminal window
 */
void terminal_on_key_event(char c) {
  /* Send character to the Slave's stdin */
  write(pty_master_fd, &c, 1);
}

/**
 * Called by GUI Refresh loop (Timer) to update the window
 */
void terminal_on_paint() {
  char incoming[128];
  int n = read(pty_master_fd, incoming, sizeof(incoming));
  if (n > 0) {
    /* Render these characters into the Terminal GUI window */
    gui_draw_text_stream(incoming, n);
  }
}

/* ============================================================
 * 🔴 SECTION 5 — BOOTSTRAP
 * How the kernel wires it all together.
 * ============================================================ */

void start_system_shell() {
  int master, slave;

  /* 1. Create a PTY pair */
  pty_create(&master, &slave);

  /* 2. Fork the shell process */
  if (fork() == 0) {
    /* --- CHILD PROCESS (SHELL) --- */

    /* Replace stdin/out/err with the PTY Slave */
    dup2(slave, 0); // stdin
    dup2(slave, 1); // stdout
    dup2(slave, 2); // stderr

    /* Start the shell */
    exec("/bin/sh");
  } else {
    /* --- PARENT PROCESS (UI THREAD) --- */

    /* The UI thread keeps the Master FD to send keys/read output */
    pty_master_fd = master;

    /* Launch the Terminal Window */
    gui_create_window("Terminal", terminal_on_paint, terminal_on_key_event);
  }
}
