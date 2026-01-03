#ifndef TIME_H
#define TIME_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Time Structures
// ============================================================================

// Redefinitions removed as they are now in types.h

struct timeval {
  long tv_sec;  // Seconds
  long tv_usec; // Microseconds (0 to 999,999)
};

struct timezone {
  int tz_minuteswest; // Minutes west of Greenwich
  int tz_dsttime;     // DST correction type
};

struct itimerspec {
  struct timespec it_interval; // Timer interval
  struct timespec it_value;    // Timer expiration
};

struct itimerval {
  struct timeval it_interval; // Interval for periodic timer
  struct timeval it_value;    // Time until next expiration
};

// ============================================================================
// Clock IDs
// ============================================================================
// typedef int clockid_t; // Now in types.h

#define CLOCK_REALTIME 0           // System-wide realtime clock
#define CLOCK_MONOTONIC 1          // Monotonic clock (no adjustments)
#define CLOCK_PROCESS_CPUTIME_ID 2 // Process CPU time
#define CLOCK_THREAD_CPUTIME_ID 3  // Thread CPU time
#define CLOCK_MONOTONIC_RAW 4      // Raw hardware monotonic clock
#define CLOCK_REALTIME_COARSE 5    // Fast but less precise realtime
#define CLOCK_MONOTONIC_COARSE 6   // Fast but less precise monotonic
#define CLOCK_BOOTTIME 7           // Time since boot (includes suspend)

// ============================================================================
// Timer Types
// ============================================================================
typedef int timer_t;

// Timer flags
#define TIMER_ABSTIME 0x01 // Absolute time flag for timer_settime

// ============================================================================
// Time Functions
// ============================================================================
int clock_gettime(clockid_t clk_id, struct timespec *tp);
int clock_settime(clockid_t clk_id, const struct timespec *tp);
int clock_getres(clockid_t clk_id, struct timespec *res);
int clock_nanosleep(clockid_t clk_id, int flags, const struct timespec *request,
                    struct timespec *remain);

int nanosleep(const struct timespec *req, struct timespec *rem);
unsigned int sleep(unsigned int seconds);
int usleep(unsigned int usec);

int gettimeofday(struct timeval *tv, struct timezone *tz);
int settimeofday(const struct timeval *tv, const struct timezone *tz);

// Timer functions
int timer_create(clockid_t clockid, void *sevp, timer_t *timerid);
int timer_delete(timer_t timerid);
int timer_settime(timer_t timerid, int flags,
                  const struct itimerspec *new_value,
                  struct itimerspec *old_value);
int timer_gettime(timer_t timerid, struct itimerspec *curr_value);
int timer_getoverrun(timer_t timerid);

// Interval timer (legacy)
int getitimer(int which, struct itimerval *curr_value);
int setitimer(int which, const struct itimerval *new_value,
              struct itimerval *old_value);

// Interval timer types
#define ITIMER_REAL 0    // Decrements in real time
#define ITIMER_VIRTUAL 1 // Decrements in process virtual time
#define ITIMER_PROF 2    // Decrements in process time + system time

// ============================================================================
// Process Times
// ============================================================================
struct tms {
  long tms_utime;  // User CPU time
  long tms_stime;  // System CPU time
  long tms_cutime; // User CPU time of children
  long tms_cstime; // System CPU time of children
};

long times(struct tms *buf);

#ifdef __cplusplus
}
#endif

#endif // TIME_H
