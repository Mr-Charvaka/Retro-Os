#ifndef PWD_H
#define PWD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Password Database Structure
// ============================================================================
struct passwd {
  char *pw_name;   // Username
  char *pw_passwd; // Password (encrypted, usually 'x' for shadow)
  uint32_t pw_uid; // User ID
  uint32_t pw_gid; // Group ID
  char *pw_gecos;  // User information
  char *pw_dir;    // Home directory
  char *pw_shell;  // Login shell
};

// ============================================================================
// Group Database Structure
// ============================================================================
struct group {
  char *gr_name;   // Group name
  char *gr_passwd; // Group password
  uint32_t gr_gid; // Group ID
  char **gr_mem;   // Group members
};

// ============================================================================
// Password Database Functions
// ============================================================================
struct passwd *getpwnam(const char *name);
struct passwd *getpwuid(uint32_t uid);
struct passwd *getpwent(void);
void setpwent(void);
void endpwent(void);

// ============================================================================
// Group Database Functions
// ============================================================================
struct group *getgrnam(const char *name);
struct group *getgrgid(uint32_t gid);
struct group *getgrent(void);
void setgrent(void);
void endgrent(void);

// ============================================================================
// User/Group ID Functions
// ============================================================================
uint32_t getuid(void);
uint32_t geteuid(void);
uint32_t getgid(void);
uint32_t getegid(void);

int setuid(uint32_t uid);
int seteuid(uint32_t euid);
int setgid(uint32_t gid);
int setegid(uint32_t egid);

int setreuid(uint32_t ruid, uint32_t euid);
int setregid(uint32_t rgid, uint32_t egid);
int setresuid(uint32_t ruid, uint32_t euid, uint32_t suid);
int setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid);

int getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid);
int getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid);

// ============================================================================
// Group Membership
// ============================================================================
int getgroups(int size, uint32_t list[]);
int setgroups(size_t size, const uint32_t *list);
int initgroups(const char *user, uint32_t group);

#ifdef __cplusplus
}
#endif

#endif // PWD_H
