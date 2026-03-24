#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef int32_t ssize_t;
typedef int32_t off_t;
typedef uint32_t mode_t;
typedef uint32_t uid_t;
typedef uint32_t gid_t;
typedef uint32_t pid_t;
typedef uint32_t key_t;
typedef int32_t time_t;
typedef int clockid_t;

typedef uint32_t dev_t;
typedef uint32_t ino_t;
typedef uint32_t nlink_t;

struct timespec {
  time_t tv_sec;
  long tv_nsec;
};

struct stat {
  dev_t st_dev;
  ino_t st_ino;
  mode_t st_mode;
  nlink_t st_nlink;
  uid_t st_uid;
  gid_t st_gid;
  dev_t st_rdev;
  off_t st_size;
  time_t st_atime;
  time_t st_mtime;
  time_t st_ctime;
  uint32_t st_blksize;
  uint32_t st_blocks;
};

struct dirent {
  ino_t d_ino;
  off_t d_off;
  unsigned short d_reclen;
  unsigned char d_type;
  char d_name[256];
};

#endif
