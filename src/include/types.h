#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;

typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
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
  dev_t st_dev;        /* ID of device containing file */
  ino_t st_ino;        /* Inode number */
  mode_t st_mode;      /* File type and mode */
  nlink_t st_nlink;    /* Number of hard links */
  uid_t st_uid;        /* User ID of owner */
  gid_t st_gid;        /* Group ID of owner */
  dev_t st_rdev;       /* Device ID (if special file) */
  off_t st_size;       /* Total size, in bytes */
  time_t st_atime;     /* Time of last access */
  time_t st_mtime;     /* Time of last modification */
  time_t st_ctime;     /* Time of last status change */
  uint32_t st_blksize; /* Block size for filesystem I/O */
  uint32_t st_blocks;  /* Number of 512B blocks allocated */
};

struct dirent {
  ino_t d_ino;             /* Inode number */
  off_t d_off;             /* Not an offset */
  unsigned short d_reclen; /* Length of this record */
  unsigned char d_type;    /* Type of file */
  char d_name[256];        /* Null-terminated filename */
};

typedef int64_t i64;

#endif
