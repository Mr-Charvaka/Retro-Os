#ifndef TYPES_H
#define TYPES_H

#include <stddef.h>
#include <stdint.h>

// Use Newlib-compatible guards to prevent redefinition when linking with it
#ifndef _SSIZE_T_DECLARED
typedef int32_t ssize_t;
#define _SSIZE_T_DECLARED
#endif

#ifndef _OFF_T_DECLARED
typedef int32_t off_t;
#define _OFF_T_DECLARED
#endif

#ifndef _MODE_T_DECLARED
typedef uint32_t mode_t;
#define _MODE_T_DECLARED
#endif

#ifndef _UID_T_DECLARED
typedef uint32_t uid_t;
#define _UID_T_DECLARED
#endif

#ifndef _GID_T_DECLARED
typedef uint32_t gid_t;
#define _GID_T_DECLARED
#endif

#ifndef _PID_T_DECLARED
typedef uint32_t pid_t;
#define _PID_T_DECLARED
#endif

#ifndef _TIME_T_DECLARED
typedef int32_t time_t;
#define _TIME_T_DECLARED
#define __time_t_defined
#endif

#ifndef _CLOCKID_T_DECLARED
typedef int clockid_t;
#define _CLOCKID_T_DECLARED
#define __clockid_t_defined
#endif

#ifndef _DEV_T_DECLARED
typedef uint32_t dev_t;
#define _DEV_T_DECLARED
#endif

#ifndef _INO_T_DECLARED
typedef uint32_t ino_t;
#define _INO_T_DECLARED
#endif

#ifndef _NLINK_T_DECLARED
typedef uint32_t nlink_t;
#define _NLINK_T_DECLARED
#endif

// Only define these if Newlib hasn't already defined them
#ifndef _SYS_TYPES_H
typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;
#endif

// Avoid conflict with standard struct timespec
#if !defined(_STRUCT_TIMESPEC) && !defined(_TIMESPEC_DECLARED) && !defined(_SYS__TIMESPEC_H_)
#define _STRUCT_TIMESPEC
#define _TIMESPEC_DECLARED
#define _SYS__TIMESPEC_H_  // Shadow Newlib's guard
struct timespec {
  time_t tv_sec;
  long tv_nsec;
};
#endif

// Retro-OS specific stat structure (internal)
#if !defined(_SYS_STAT_H) && !defined(_STAT_H_)
#define _STAT_H_
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
#endif

struct dirent {
  ino_t d_ino;             
  off_t d_off;             
  unsigned short d_reclen; 
  unsigned char d_type;    
  char d_name[256];        
};

#endif
