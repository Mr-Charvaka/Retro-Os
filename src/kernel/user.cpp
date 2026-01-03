// ============================================================================
// user.cpp - User & Group Management Implementation
// Entirely hand-crafted for Retro-OS kernel
// ============================================================================

#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/pwd.h"
#include "../include/string.h"
#include "heap.h"
#include "process.h"


extern "C" {

// ============================================================================
// Simple User Database
// ============================================================================
#define MAX_PASSWD_ENTRIES 16

static struct passwd passwd_db[MAX_PASSWD_ENTRIES];
static char passwd_names[MAX_PASSWD_ENTRIES][32];
static char passwd_dirs[MAX_PASSWD_ENTRIES][64];
static char passwd_shells[MAX_PASSWD_ENTRIES][32];
static int passwd_count = 0;
static int passwd_index = 0;

// ============================================================================
// Simple Group Database
// ============================================================================
#define MAX_GROUP_ENTRIES 16
#define MAX_GROUP_MEMBERS 8

static struct group group_db[MAX_GROUP_ENTRIES];
static char group_names[MAX_GROUP_ENTRIES][32];
static char *group_members[MAX_GROUP_ENTRIES][MAX_GROUP_MEMBERS + 1];
static char group_member_names[MAX_GROUP_ENTRIES][MAX_GROUP_MEMBERS][32];
static int group_count = 0;
static int group_index = 0;

// ============================================================================
// Initialize Default Users
// ============================================================================
static int users_initialized = 0;

static void init_users(void) {
  if (users_initialized)
    return;

  // Add root user
  strcpy(passwd_names[0], "root");
  passwd_db[0].pw_name = passwd_names[0];
  passwd_db[0].pw_passwd = (char *)"x";
  passwd_db[0].pw_uid = 0;
  passwd_db[0].pw_gid = 0;
  passwd_db[0].pw_gecos = (char *)"root";
  strcpy(passwd_dirs[0], "/root");
  passwd_db[0].pw_dir = passwd_dirs[0];
  strcpy(passwd_shells[0], "/bin/sh");
  passwd_db[0].pw_shell = passwd_shells[0];
  passwd_count = 1;

  // Add nobody user
  strcpy(passwd_names[1], "nobody");
  passwd_db[1].pw_name = passwd_names[1];
  passwd_db[1].pw_passwd = (char *)"x";
  passwd_db[1].pw_uid = 65534;
  passwd_db[1].pw_gid = 65534;
  passwd_db[1].pw_gecos = (char *)"nobody";
  strcpy(passwd_dirs[1], "/nonexistent");
  passwd_db[1].pw_dir = passwd_dirs[1];
  strcpy(passwd_shells[1], "/bin/false");
  passwd_db[1].pw_shell = passwd_shells[1];
  passwd_count = 2;

  // Add root group
  strcpy(group_names[0], "root");
  group_db[0].gr_name = group_names[0];
  group_db[0].gr_passwd = (char *)"x";
  group_db[0].gr_gid = 0;
  group_members[0][0] = 0;
  group_db[0].gr_mem = group_members[0];
  group_count = 1;

  // Add nogroup
  strcpy(group_names[1], "nogroup");
  group_db[1].gr_name = group_names[1];
  group_db[1].gr_passwd = (char *)"x";
  group_db[1].gr_gid = 65534;
  group_members[1][0] = 0;
  group_db[1].gr_mem = group_members[1];
  group_count = 2;

  users_initialized = 1;
  serial_log("USER: User database initialized");
}

// ============================================================================
// Password Database Functions
// ============================================================================

struct passwd *getpwnam(const char *name) {
  init_users();

  if (!name)
    return 0;

  for (int i = 0; i < passwd_count; i++) {
    if (strcmp(passwd_db[i].pw_name, name) == 0)
      return &passwd_db[i];
  }

  return 0;
}

struct passwd *getpwuid(uint32_t uid) {
  init_users();

  for (int i = 0; i < passwd_count; i++) {
    if (passwd_db[i].pw_uid == uid)
      return &passwd_db[i];
  }

  return 0;
}

struct passwd *getpwent(void) {
  init_users();

  if (passwd_index >= passwd_count)
    return 0;

  return &passwd_db[passwd_index++];
}

void setpwent(void) {
  init_users();
  passwd_index = 0;
}

void endpwent(void) { passwd_index = 0; }

// ============================================================================
// Group Database Functions
// ============================================================================

struct group *getgrnam(const char *name) {
  init_users();

  if (!name)
    return 0;

  for (int i = 0; i < group_count; i++) {
    if (strcmp(group_db[i].gr_name, name) == 0)
      return &group_db[i];
  }

  return 0;
}

struct group *getgrgid(uint32_t gid) {
  init_users();

  for (int i = 0; i < group_count; i++) {
    if (group_db[i].gr_gid == gid)
      return &group_db[i];
  }

  return 0;
}

struct group *getgrent(void) {
  init_users();

  if (group_index >= group_count)
    return 0;

  return &group_db[group_index++];
}

void setgrent(void) {
  init_users();
  group_index = 0;
}

void endgrent(void) { group_index = 0; }

// ============================================================================
// User/Group ID Functions
// ============================================================================

uint32_t getuid(void) {
  if (!current_process)
    return 0;
  return current_process->uid;
}

uint32_t geteuid(void) {
  if (!current_process)
    return 0;
  return current_process->euid;
}

uint32_t getgid(void) {
  if (!current_process)
    return 0;
  return current_process->gid;
}

uint32_t getegid(void) {
  if (!current_process)
    return 0;
  return current_process->egid;
}

// ============================================================================
// Set User/Group ID Functions
// ============================================================================

int setuid(uint32_t uid) {
  if (!current_process)
    return -1;

  // Only root can set uid arbitrarily
  if (current_process->euid != 0) {
    // Can only set to current real or saved uid
    if (uid != current_process->uid && uid != current_process->suid) {
      // errno = EPERM;
      return -1;
    }
  }

  current_process->uid = uid;
  current_process->euid = uid;
  current_process->suid = uid;
  return 0;
}

int seteuid(uint32_t euid) {
  if (!current_process)
    return -1;

  // Can set euid to real uid, saved uid, or if root, any
  if (current_process->euid != 0) {
    if (euid != current_process->uid && euid != current_process->suid) {
      // errno = EPERM;
      return -1;
    }
  }

  current_process->euid = euid;
  return 0;
}

int setgid(uint32_t gid) {
  if (!current_process)
    return -1;

  if (current_process->euid != 0) {
    if (gid != current_process->gid && gid != current_process->sgid) {
      // errno = EPERM;
      return -1;
    }
  }

  current_process->gid = gid;
  current_process->egid = gid;
  current_process->sgid = gid;
  return 0;
}

int setegid(uint32_t egid) {
  if (!current_process)
    return -1;

  if (current_process->euid != 0) {
    if (egid != current_process->gid && egid != current_process->sgid) {
      // errno = EPERM;
      return -1;
    }
  }

  current_process->egid = egid;
  return 0;
}

int setreuid(uint32_t ruid, uint32_t euid) {
  if (ruid != (uint32_t)-1) {
    if (setuid(ruid) < 0)
      return -1;
  }
  if (euid != (uint32_t)-1) {
    if (seteuid(euid) < 0)
      return -1;
  }
  return 0;
}

int setregid(uint32_t rgid, uint32_t egid) {
  if (rgid != (uint32_t)-1) {
    if (setgid(rgid) < 0)
      return -1;
  }
  if (egid != (uint32_t)-1) {
    if (setegid(egid) < 0)
      return -1;
  }
  return 0;
}

int setresuid(uint32_t ruid, uint32_t euid, uint32_t suid) {
  if (!current_process)
    return -1;

  if (current_process->euid != 0) {
    // Can only set to current values
    uint32_t cur_uid = current_process->uid;
    uint32_t cur_euid = current_process->euid;
    uint32_t cur_suid = current_process->suid;

    if (ruid != (uint32_t)-1 && ruid != cur_uid && ruid != cur_euid &&
        ruid != cur_suid)
      return -EPERM;
    if (euid != (uint32_t)-1 && euid != cur_uid && euid != cur_euid &&
        euid != cur_suid)
      return -EPERM;
    if (suid != (uint32_t)-1 && suid != cur_uid && suid != cur_euid &&
        suid != cur_suid)
      return -EPERM;
  }

  if (ruid != (uint32_t)-1)
    current_process->uid = ruid;
  if (euid != (uint32_t)-1)
    current_process->euid = euid;
  if (suid != (uint32_t)-1)
    current_process->suid = suid;

  return 0;
}

int setresgid(uint32_t rgid, uint32_t egid, uint32_t sgid) {
  if (!current_process)
    return -1;

  if (current_process->euid != 0) {
    uint32_t cur_gid = current_process->gid;
    uint32_t cur_egid = current_process->egid;
    uint32_t cur_sgid = current_process->sgid;

    if (rgid != (uint32_t)-1 && rgid != cur_gid && rgid != cur_egid &&
        rgid != cur_sgid)
      return -EPERM;
    if (egid != (uint32_t)-1 && egid != cur_gid && egid != cur_egid &&
        egid != cur_sgid)
      return -EPERM;
    if (sgid != (uint32_t)-1 && sgid != cur_gid && sgid != cur_egid &&
        sgid != cur_sgid)
      return -EPERM;
  }

  if (rgid != (uint32_t)-1)
    current_process->gid = rgid;
  if (egid != (uint32_t)-1)
    current_process->egid = egid;
  if (sgid != (uint32_t)-1)
    current_process->sgid = sgid;

  return 0;
}

int getresuid(uint32_t *ruid, uint32_t *euid, uint32_t *suid) {
  if (!current_process)
    return -1;

  if (ruid)
    *ruid = current_process->uid;
  if (euid)
    *euid = current_process->euid;
  if (suid)
    *suid = current_process->suid;

  return 0;
}

int getresgid(uint32_t *rgid, uint32_t *egid, uint32_t *sgid) {
  if (!current_process)
    return -1;

  if (rgid)
    *rgid = current_process->gid;
  if (egid)
    *egid = current_process->egid;
  if (sgid)
    *sgid = current_process->sgid;

  return 0;
}

// ============================================================================
// Supplementary Groups (simplified)
// ============================================================================
#define MAX_GROUPS 16
static uint32_t supplementary_groups[MAX_GROUPS];
static int supplementary_group_count = 0;

int getgroups(int size, uint32_t list[]) {
  if (size == 0)
    return supplementary_group_count;

  if (size < supplementary_group_count) {
    // errno = EINVAL;
    return -1;
  }

  for (int i = 0; i < supplementary_group_count; i++)
    list[i] = supplementary_groups[i];

  return supplementary_group_count;
}

int setgroups(size_t size, const uint32_t *list) {
  if (!current_process || current_process->euid != 0) {
    // errno = EPERM;
    return -1;
  }

  if (size > MAX_GROUPS) {
    // errno = EINVAL;
    return -1;
  }

  supplementary_group_count = size;
  for (size_t i = 0; i < size; i++)
    supplementary_groups[i] = list[i];

  return 0;
}

int initgroups(const char *user, uint32_t group) {
  (void)user;

  // Simplified: just set the primary group
  supplementary_groups[0] = group;
  supplementary_group_count = 1;

  return 0;
}

} // extern "C"
