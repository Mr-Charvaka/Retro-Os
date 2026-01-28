// ============================================================================
// clock.cpp - POSIX Time aur Clock ka sara implementation yahan hai
// Poora hand-crafted hai apne Retro-OS ke liye
// ============================================================================

#include "../drivers/rtc.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/signal.h"
#include "../include/time.h"
#include "heap.h"
#include "process.h"

extern "C" {

// Global tick counter (100Hz maan ke chal rahe hain)
extern uint32_t tick;
#define TICKS_PER_SEC 100

// Boot time (init ke waqt set hota hai)
static uint32_t boot_time_sec = 0;

// ============================================================================
// clock_gettime - High-resolution time nikalne ke liye
// ============================================================================

int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  if (!tp)
    return -EFAULT;

  uint32_t current_tick = tick;

  switch (clk_id) {
  case CLOCK_REALTIME:
  case CLOCK_REALTIME_COARSE: {
    // Get RTC time
    rtc_time_t rtc;
    rtc_read(&rtc);

    // Convert to seconds since epoch (simplified - assumes 2000 base)
    uint32_t days = 0;
    for (int y = 2000; y < 2000 + rtc.year; y++) {
      bool leap = ((y % 4 == 0) && (y % 100 != 0)) || (y % 400 == 0);
      days += leap ? 366 : 365;
    }

    int month_days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    bool is_leap = ((2000 + rtc.year) % 4 == 0);
    if (is_leap)
      month_days[1] = 29;

    for (int m = 0; m < rtc.month - 1; m++) {
      days += month_days[m];
    }
    days += rtc.day - 1;

    uint32_t secs =
        days * 86400 + rtc.hour * 3600 + rtc.minute * 60 + rtc.second;
    secs += 946684800; // Seconds from 1970 to 2000

    tp->tv_sec = secs;
    tp->tv_nsec = (current_tick % TICKS_PER_SEC) * (1000000000 / TICKS_PER_SEC);
    return 0;
  }

  case CLOCK_MONOTONIC:
  case CLOCK_MONOTONIC_RAW:
  case CLOCK_MONOTONIC_COARSE:
  case CLOCK_BOOTTIME: {
    // Time since boot
    tp->tv_sec = current_tick / TICKS_PER_SEC;
    tp->tv_nsec = (current_tick % TICKS_PER_SEC) * (1000000000 / TICKS_PER_SEC);
    return 0;
  }

  case CLOCK_PROCESS_CPUTIME_ID: {
    // Process CPU time
    if (current_process) {
      uint32_t total = current_process->utime + current_process->stime;
      tp->tv_sec = total / TICKS_PER_SEC;
      tp->tv_nsec = (total % TICKS_PER_SEC) * (1000000000 / TICKS_PER_SEC);
    } else {
      tp->tv_sec = 0;
      tp->tv_nsec = 0;
    }
    return 0;
  }

  case CLOCK_THREAD_CPUTIME_ID:
    // Thread CPU time (same as process for now)
    return clock_gettime(CLOCK_PROCESS_CPUTIME_ID, tp);

  default:
    return -EINVAL;
  }
}

// ============================================================================
// clock_settime - Time set karne ke liye (aukat chahiye iske liye)
// ============================================================================

int clock_settime(clockid_t clk_id, const struct timespec *tp) {
  if (!tp)
    return -EFAULT;

  // Only CLOCK_REALTIME can be set
  if (clk_id != CLOCK_REALTIME)
    return -EINVAL;

  // Check privileges (only root can set time)
  if (current_process && current_process->euid != 0)
    return -EPERM;

  // Setting time would require RTC write - not implemented
  serial_log("CLOCK: clock_settime not fully implemented");
  return -ENOSYS;
}

// ============================================================================
// clock_getres - Get clock resolution
// ============================================================================

int clock_getres(clockid_t clk_id, struct timespec *res) {
  if (!res)
    return 0; // NULL is valid, just don't fill anything

  switch (clk_id) {
  case CLOCK_REALTIME:
  case CLOCK_MONOTONIC:
  case CLOCK_BOOTTIME:
  case CLOCK_REALTIME_COARSE:
  case CLOCK_MONOTONIC_COARSE:
    // 10ms resolution (100Hz timer)
    res->tv_sec = 0;
    res->tv_nsec = 10000000; // 10ms in nanoseconds
    return 0;

  case CLOCK_MONOTONIC_RAW:
  case CLOCK_PROCESS_CPUTIME_ID:
  case CLOCK_THREAD_CPUTIME_ID:
    res->tv_sec = 0;
    res->tv_nsec = 10000000;
    return 0;

  default:
    return -EINVAL;
  }
}

// ============================================================================
// nanosleep - Chhoti si neend (High-resolution sleep)
// ============================================================================

int nanosleep(const struct timespec *req, struct timespec *rem) {
  if (!req)
    return -EFAULT;

  if (req->tv_sec < 0 || req->tv_nsec < 0 || req->tv_nsec >= 1000000000)
    return -EINVAL;

  // Convert to ticks
  uint32_t sleep_ticks = req->tv_sec * TICKS_PER_SEC;
  sleep_ticks += (req->tv_nsec + 9999999) / 10000000; // Round up

  if (sleep_ticks == 0)
    sleep_ticks = 1; // Minimum 1 tick

  uint32_t start = tick;
  current_process->sleep_until = tick + sleep_ticks;
  current_process->state = PROCESS_SLEEPING;
  schedule();

  // Check karo agar jaldi jag gaye (kisi signal ki wajah se)
  uint32_t elapsed = tick - start;
  if (elapsed < sleep_ticks) {
    // Interrupted
    if (rem) {
      uint32_t remaining = sleep_ticks - elapsed;
      rem->tv_sec = remaining / TICKS_PER_SEC;
      rem->tv_nsec = (remaining % TICKS_PER_SEC) * 10000000;
    }
    return -EINTR;
  }

  if (rem) {
    rem->tv_sec = 0;
    rem->tv_nsec = 0;
  }
  return 0;
}

// ============================================================================
// clock_nanosleep - Sleep with specified clock
// ============================================================================

int clock_nanosleep(clockid_t clk_id, int flags, const struct timespec *request,
                    struct timespec *remain) {
  if (!request)
    return -EFAULT;

  if (clk_id != CLOCK_REALTIME && clk_id != CLOCK_MONOTONIC)
    return -ENOTSUP;

  if (flags & TIMER_ABSTIME) {
    // Absolute time - calculate relative time
    struct timespec now;
    clock_gettime(clk_id, &now);

    struct timespec rel;
    rel.tv_sec = request->tv_sec - now.tv_sec;
    rel.tv_nsec = request->tv_nsec - now.tv_nsec;
    if (rel.tv_nsec < 0) {
      rel.tv_sec--;
      rel.tv_nsec += 1000000000;
    }

    if (rel.tv_sec < 0)
      return 0; // Time already passed

    return nanosleep(&rel, remain);
  }

  return nanosleep(request, remain);
}

// ============================================================================
// sleep - Sleep for seconds
// ============================================================================

unsigned int sleep(unsigned int seconds) {
  struct timespec req, rem;
  req.tv_sec = seconds;
  req.tv_nsec = 0;

  if (nanosleep(&req, &rem) == -EINTR) {
    return (unsigned int)rem.tv_sec;
  }
  return 0;
}

// ============================================================================
// usleep - Sleep for microseconds
// ============================================================================

int usleep(unsigned int usec) {
  struct timespec req;
  req.tv_sec = usec / 1000000;
  req.tv_nsec = (usec % 1000000) * 1000;
  return nanosleep(&req, 0);
}

// ============================================================================
// gettimeofday - Get time of day
// ============================================================================

int gettimeofday(struct timeval *tv, struct timezone *tz) {
  if (tv) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tv->tv_sec = ts.tv_sec;
    tv->tv_usec = ts.tv_nsec / 1000;
  }

  if (tz) {
    tz->tz_minuteswest = 0;
    tz->tz_dsttime = 0;
  }

  return 0;
}

// ============================================================================
// settimeofday - Set time of day
// ============================================================================

int settimeofday(const struct timeval *tv, const struct timezone *tz) {
  (void)tz;

  if (!tv)
    return -EFAULT;

  struct timespec ts;
  ts.tv_sec = tv->tv_sec;
  ts.tv_nsec = tv->tv_usec * 1000;

  return clock_settime(CLOCK_REALTIME, &ts);
}

// ============================================================================
// times - Get process times
// ============================================================================

long times(struct tms *buf) {
  if (!buf)
    return -EFAULT;

  if (current_process) {
    buf->tms_utime = current_process->utime;
    buf->tms_stime = current_process->stime;
    buf->tms_cutime = current_process->cutime;
    buf->tms_cstime = current_process->cstime;
  } else {
    buf->tms_utime = 0;
    buf->tms_stime = 0;
    buf->tms_cutime = 0;
    buf->tms_cstime = 0;
  }

  return tick;
}

// ============================================================================
// Timer Implementation
// ============================================================================

#define MAX_TIMERS 16

struct kernel_timer {
  int in_use;
  clockid_t clock_id;
  int pid;   // Process that owns this timer
  int signo; // Signal to send
  struct itimerspec value;
  uint32_t next_fire; // Is tick pe timer bajega
};

static struct kernel_timer timers[MAX_TIMERS];

int timer_create(clockid_t clockid, void *sevp, timer_t *timerid) {
  (void)sevp; // Signal event structure - simplified

  if (!timerid)
    return -EFAULT;

  if (clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC)
    return -EINVAL;

  // Find free timer
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (!timers[i].in_use) {
      timers[i].in_use = 1;
      timers[i].clock_id = clockid;
      timers[i].pid = current_process ? current_process->id : 0;
      timers[i].signo = SIGALRM;
      timers[i].next_fire = 0;
      timers[i].value.it_interval.tv_sec = 0;
      timers[i].value.it_interval.tv_nsec = 0;
      timers[i].value.it_value.tv_sec = 0;
      timers[i].value.it_value.tv_nsec = 0;
      *timerid = i;
      return 0;
    }
  }

  return -EAGAIN;
}

int timer_delete(timer_t timerid) {
  if (timerid < 0 || timerid >= MAX_TIMERS || !timers[timerid].in_use)
    return -EINVAL;

  timers[timerid].in_use = 0;
  return 0;
}

int timer_settime(timer_t timerid, int flags,
                  const struct itimerspec *new_value,
                  struct itimerspec *old_value) {
  if (timerid < 0 || timerid >= MAX_TIMERS || !timers[timerid].in_use)
    return -EINVAL;

  if (!new_value)
    return -EFAULT;

  struct kernel_timer *t = &timers[timerid];

  if (old_value)
    *old_value = t->value;

  t->value = *new_value;

  // Calculate next fire time
  if (new_value->it_value.tv_sec == 0 && new_value->it_value.tv_nsec == 0) {
    // Disarm timer
    t->next_fire = 0;
  } else {
    uint32_t fire_ticks = new_value->it_value.tv_sec * TICKS_PER_SEC;
    fire_ticks += (new_value->it_value.tv_nsec + 9999999) / 10000000;

    if (flags & TIMER_ABSTIME) {
      // Absolute time - need to convert
      t->next_fire = fire_ticks;
    } else {
      // Relative time
      t->next_fire = tick + fire_ticks;
    }
  }

  return 0;
}

int timer_gettime(timer_t timerid, struct itimerspec *curr_value) {
  if (timerid < 0 || timerid >= MAX_TIMERS || !timers[timerid].in_use)
    return -EINVAL;

  if (!curr_value)
    return -EFAULT;

  struct kernel_timer *t = &timers[timerid];

  curr_value->it_interval = t->value.it_interval;

  // Calculate remaining time
  if (t->next_fire == 0 || tick >= t->next_fire) {
    curr_value->it_value.tv_sec = 0;
    curr_value->it_value.tv_nsec = 0;
  } else {
    uint32_t remaining = t->next_fire - tick;
    curr_value->it_value.tv_sec = remaining / TICKS_PER_SEC;
    curr_value->it_value.tv_nsec =
        (remaining % TICKS_PER_SEC) * (1000000000 / TICKS_PER_SEC);
  }

  return 0;
}

int timer_getoverrun(timer_t timerid) {
  if (timerid < 0 || timerid >= MAX_TIMERS || !timers[timerid].in_use)
    return -EINVAL;
  return 0; // Overrun tracking not implemented
}

// ============================================================================
// check_timers - Timer interrupt se bar-bar call hota hai
// ============================================================================

void check_timers(void) {
  for (int i = 0; i < MAX_TIMERS; i++) {
    if (!timers[i].in_use || timers[i].next_fire == 0)
      continue;

    if (tick >= timers[i].next_fire) {
      // Timer fired - send signal to owner
      sys_kill(timers[i].pid, timers[i].signo);

      // Check for periodic timer
      if (timers[i].value.it_interval.tv_sec != 0 ||
          timers[i].value.it_interval.tv_nsec != 0) {
        // Reset for next interval
        uint32_t interval_ticks =
            timers[i].value.it_interval.tv_sec * TICKS_PER_SEC +
            (timers[i].value.it_interval.tv_nsec + 9999999) / 10000000;
        timers[i].next_fire = tick + interval_ticks;
      } else {
        // One-shot timer
        timers[i].next_fire = 0;
      }
    }
  }
}

// ============================================================================
// clock_init - Initialize clock subsystem
// ============================================================================

void clock_init(void) {
  for (int i = 0; i < MAX_TIMERS; i++) {
    timers[i].in_use = 0;
  }
  serial_log("CLOCK: Subsystem initialized");
}

// ============================================================================
// timer_now_ms - Get current time in milliseconds (for network stack)
// ============================================================================

uint32_t timer_now_ms(void) {
  // tick runs at 100Hz, so each tick is 10ms
  return tick * 10;
}

} // extern "C"
