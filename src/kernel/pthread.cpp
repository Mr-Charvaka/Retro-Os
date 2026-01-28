// ============================================================================
// pthread.cpp - POSIX Threads Implementation
// Entirely hand-crafted for Retro-OS kernel
// ============================================================================

#include "../include/pthread.h"
#include "../drivers/serial.h"
#include "../include/errno.h"
#include "../include/signal.h"
#include "../include/string.h"
#include "heap.h"
#include "memory.h"
#include "process.h"

extern void create_kernel_thread(void (*fn)());

extern "C" {

// ============================================================================
// Thread Control Block
// ============================================================================
#define MAX_THREADS 64
#define DEFAULT_STACK_SIZE 16384
#define MAX_TSD_KEYS 64

struct thread_t {
  pthread_t tid;
  int in_use;
  int detached;
  int canceled;
  int cancel_enabled;
  int cancel_type;
  void *(*start_routine)(void *);
  void *arg;
  void *retval;
  void *stack;
  uint32_t stack_size;
  int joined;
  int exited;

  // Thread-specific data
  void *tsd[MAX_TSD_KEYS];

  // Kernel process/thread reference
  uint32_t process_id;
};

static struct thread_t threads[MAX_THREADS];
static pthread_t next_tid = 1;
static int threads_initialized = 0;

// TSD destructors
static void (*tsd_destructors[MAX_TSD_KEYS])(void *);
static int tsd_key_used[MAX_TSD_KEYS];
static pthread_key_t next_key = 0;

// Current thread TID (per-process)
static __thread pthread_t current_thread_id = 0;

// ============================================================================
// Initialize threading subsystem
// ============================================================================
static void init_threads(void) {
  if (threads_initialized)
    return;

  for (int i = 0; i < MAX_THREADS; i++) {
    threads[i].in_use = 0;
    threads[i].tid = 0;
  }

  for (int i = 0; i < MAX_TSD_KEYS; i++) {
    tsd_destructors[i] = 0;
    tsd_key_used[i] = 0;
  }

  // Create main thread entry
  threads[0].in_use = 1;
  threads[0].tid = 0;
  threads[0].detached = 0;
  threads[0].exited = 0;
  threads[0].joined = 0;

  threads_initialized = 1;
  serial_log("PTHREAD: Subsystem initialized");
}

// ============================================================================
// Find thread by TID
// ============================================================================
static struct thread_t *find_thread(pthread_t tid) {
  for (int i = 0; i < MAX_THREADS; i++) {
    if (threads[i].in_use && threads[i].tid == tid)
      return &threads[i];
  }
  return 0;
}

// ============================================================================
// Allocate a thread slot
// ============================================================================
static struct thread_t *alloc_thread(void) {
  init_threads();

  for (int i = 0; i < MAX_THREADS; i++) {
    if (!threads[i].in_use) {
      threads[i].in_use = 1;
      threads[i].tid = next_tid++;
      threads[i].detached = 0;
      threads[i].canceled = 0;
      threads[i].cancel_enabled = 1;
      threads[i].cancel_type = PTHREAD_CANCEL_DEFERRED;
      threads[i].retval = 0;
      threads[i].joined = 0;
      threads[i].exited = 0;

      for (int j = 0; j < MAX_TSD_KEYS; j++)
        threads[i].tsd[j] = 0;

      return &threads[i];
    }
  }
  return 0;
}

// ============================================================================
// Thread wrapper function
// ============================================================================
static void thread_wrapper(struct thread_t *t) {
  current_thread_id = t->tid;

  // Call the actual thread function
  t->retval = t->start_routine(t->arg);

  // Thread exiting normally
  pthread_exit(t->retval);
}

// ============================================================================
// pthread_create - Create a new thread
// ============================================================================
int pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                   void *(*start_routine)(void *), void *arg) {
  init_threads();

  if (!thread || !start_routine)
    return EINVAL;

  struct thread_t *t = alloc_thread();
  if (!t)
    return EAGAIN;

  // Set up thread
  t->start_routine = start_routine;
  t->arg = arg;

  // Determine stack size
  uint32_t stack_size = DEFAULT_STACK_SIZE;
  if (attr && attr->stacksize > 0)
    stack_size = attr->stacksize;

  // Allocate stack
  t->stack = kmalloc(stack_size);
  if (!t->stack) {
    t->in_use = 0;
    return ENOMEM;
  }
  t->stack_size = stack_size;

  // Handle detach state
  if (attr && attr->detachstate == PTHREAD_CREATE_DETACHED)
    t->detached = 1;

  // Create kernel thread
  // In a real implementation, this would use clone() or kernel_thread()
  // For now, we use a simplified approach with the existing scheduler

  // Set up stack for the new thread
  uint32_t *stack_top = (uint32_t *)((uint8_t *)t->stack + stack_size);

  // Push arguments for thread_wrapper
  *(--stack_top) = (uint32_t)(uintptr_t)t;
  *(--stack_top) = 0; // Return address (not used)

  // Create as kernel thread (simplified)
  // extern void create_kernel_thread(void (*fn)()); // Linked from C++ kernel
  // Note: Real implementation would pass arguments properly
  // For now, we'll store the thread info and create a simple thread

  *thread = t->tid;

  serial_log_hex("PTHREAD: Created thread ", t->tid);

  // In a complete implementation, we'd start the thread here
  // For now, we note that full kernel integration is needed

  return 0;
}

// ============================================================================
// pthread_exit - Terminate calling thread
// ============================================================================
void pthread_exit(void *retval) {
  struct thread_t *t = find_thread(current_thread_id);

  if (!t) {
    // Main thread exiting
    exit_process((int)(intptr_t)retval);
    return;
  }

  t->retval = retval;
  t->exited = 1;

  // Call TSD destructors
  for (int i = 0; i < MAX_TSD_KEYS; i++) {
    if (tsd_key_used[i] && t->tsd[i] && tsd_destructors[i]) {
      void *value = t->tsd[i];
      t->tsd[i] = 0;
      tsd_destructors[i](value);
    }
  }

  // If detached, free resources immediately
  if (t->detached) {
    if (t->stack)
      kfree(t->stack);
    t->in_use = 0;
  }

  // Exit thread (would call scheduler)
  schedule();
}

// ============================================================================
// pthread_join - Wait for thread termination
// ============================================================================
int pthread_join(pthread_t thread, void **retval) {
  struct thread_t *t = find_thread(thread);

  if (!t)
    return ESRCH;

  if (t->detached)
    return EINVAL;

  if (t->tid == current_thread_id)
    return EDEADLK;

  if (t->joined)
    return EINVAL;

  t->joined = 1;

  // Wait for thread to exit
  while (!t->exited) {
    schedule();
  }

  if (retval)
    *retval = t->retval;

  // Free resources
  if (t->stack)
    kfree(t->stack);
  t->in_use = 0;

  return 0;
}

// ============================================================================
// pthread_detach - Detach a thread
// ============================================================================
int pthread_detach(pthread_t thread) {
  struct thread_t *t = find_thread(thread);

  if (!t)
    return ESRCH;

  if (t->detached)
    return EINVAL;

  t->detached = 1;

  // If already exited, free resources now
  if (t->exited) {
    if (t->stack)
      kfree(t->stack);
    t->in_use = 0;
  }

  return 0;
}

// ============================================================================
// pthread_self - Get current thread ID
// ============================================================================
pthread_t pthread_self(void) { return current_thread_id; }

// ============================================================================
// pthread_equal - Compare thread IDs
// ============================================================================
int pthread_equal(pthread_t t1, pthread_t t2) { return t1 == t2; }

// ============================================================================
// pthread_cancel - Cancel a thread
// ============================================================================
int pthread_cancel(pthread_t thread) {
  struct thread_t *t = find_thread(thread);

  if (!t)
    return ESRCH;

  t->canceled = 1;
  return 0;
}

// ============================================================================
// pthread_setcancelstate - Set cancellation state
// ============================================================================
int pthread_setcancelstate(int state, int *oldstate) {
  struct thread_t *t = find_thread(current_thread_id);

  if (!t)
    return EINVAL;

  if (state != PTHREAD_CANCEL_ENABLE && state != PTHREAD_CANCEL_DISABLE)
    return EINVAL;

  if (oldstate)
    *oldstate =
        t->cancel_enabled ? PTHREAD_CANCEL_ENABLE : PTHREAD_CANCEL_DISABLE;

  t->cancel_enabled = (state == PTHREAD_CANCEL_ENABLE);
  return 0;
}

// ============================================================================
// pthread_setcanceltype - Set cancellation type
// ============================================================================
int pthread_setcanceltype(int type, int *oldtype) {
  struct thread_t *t = find_thread(current_thread_id);

  if (!t)
    return EINVAL;

  if (type != PTHREAD_CANCEL_DEFERRED && type != PTHREAD_CANCEL_ASYNCHRONOUS)
    return EINVAL;

  if (oldtype)
    *oldtype = t->cancel_type;

  t->cancel_type = type;
  return 0;
}

// ============================================================================
// pthread_testcancel - Create cancellation point
// ============================================================================
void pthread_testcancel(void) {
  struct thread_t *t = find_thread(current_thread_id);

  if (t && t->canceled && t->cancel_enabled) {
    pthread_exit(PTHREAD_CANCELED);
  }
}

// ============================================================================
// Thread Attribute Functions
// ============================================================================
int pthread_attr_init(pthread_attr_t *attr) {
  if (!attr)
    return EINVAL;

  attr->stacksize = DEFAULT_STACK_SIZE;
  attr->stackaddr = 0;
  attr->detachstate = PTHREAD_CREATE_JOINABLE;
  attr->schedpolicy = SCHED_OTHER;
  attr->schedpriority = 0;
  attr->scope = PTHREAD_SCOPE_SYSTEM;
  attr->inheritsched = PTHREAD_INHERIT_SCHED;

  return 0;
}

int pthread_attr_destroy(pthread_attr_t *attr) {
  if (!attr)
    return EINVAL;
  return 0;
}

int pthread_attr_setdetachstate(pthread_attr_t *attr, int detachstate) {
  if (!attr)
    return EINVAL;
  if (detachstate != PTHREAD_CREATE_JOINABLE &&
      detachstate != PTHREAD_CREATE_DETACHED)
    return EINVAL;

  attr->detachstate = detachstate;
  return 0;
}

int pthread_attr_getdetachstate(const pthread_attr_t *attr, int *detachstate) {
  if (!attr || !detachstate)
    return EINVAL;

  *detachstate = attr->detachstate;
  return 0;
}

int pthread_attr_setstacksize(pthread_attr_t *attr, size_t stacksize) {
  if (!attr)
    return EINVAL;
  if (stacksize < 4096)
    return EINVAL;

  attr->stacksize = stacksize;
  return 0;
}

int pthread_attr_getstacksize(const pthread_attr_t *attr, size_t *stacksize) {
  if (!attr || !stacksize)
    return EINVAL;

  *stacksize = attr->stacksize;
  return 0;
}

int pthread_attr_setstackaddr(pthread_attr_t *attr, void *stackaddr) {
  if (!attr)
    return EINVAL;

  attr->stackaddr = stackaddr;
  return 0;
}

int pthread_attr_getstackaddr(const pthread_attr_t *attr, void **stackaddr) {
  if (!attr || !stackaddr)
    return EINVAL;

  *stackaddr = attr->stackaddr;
  return 0;
}

int pthread_attr_setschedpolicy(pthread_attr_t *attr, int policy) {
  if (!attr)
    return EINVAL;

  attr->schedpolicy = policy;
  return 0;
}

int pthread_attr_getschedpolicy(const pthread_attr_t *attr, int *policy) {
  if (!attr || !policy)
    return EINVAL;

  *policy = attr->schedpolicy;
  return 0;
}

// ============================================================================
// Mutex Functions
// ============================================================================

// Atomic compare-and-swap helper
static inline int atomic_cas(volatile int *ptr, int expected, int desired) {
  int result;
  asm volatile("lock cmpxchgl %2, %1"
               : "=a"(result), "+m"(*ptr)
               : "r"(desired), "0"(expected)
               : "memory");
  return result == expected;
}

int pthread_mutex_init(pthread_mutex_t *mutex,
                       const pthread_mutexattr_t *attr) {
  if (!mutex)
    return EINVAL;

  mutex->locked = 0;
  mutex->owner = 0;
  mutex->recursive_count = 0;
  mutex->type = attr ? attr->type : PTHREAD_MUTEX_DEFAULT;

  return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex) {
  if (!mutex)
    return EINVAL;

  if (mutex->locked)
    return EBUSY;

  return 0;
}

int pthread_mutex_lock(pthread_mutex_t *mutex) {
  if (!mutex)
    return EINVAL;

  pthread_t self = pthread_self();

  // Handle recursive mutex
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->owner == self) {
    mutex->recursive_count++;
    return 0;
  }

  // Error check for deadlock
  if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutex->owner == self) {
    return EDEADLK;
  }

  // Spin until we acquire the lock
  while (!atomic_cas(&mutex->locked, 0, 1)) {
    // Yield to other threads
    schedule();
  }

  mutex->owner = self;
  mutex->recursive_count = 1;
  return 0;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex) {
  if (!mutex)
    return EINVAL;

  pthread_t self = pthread_self();

  // Handle recursive mutex
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE && mutex->owner == self) {
    mutex->recursive_count++;
    return 0;
  }

  if (atomic_cas(&mutex->locked, 0, 1)) {
    mutex->owner = self;
    mutex->recursive_count = 1;
    return 0;
  }

  return EBUSY;
}

int pthread_mutex_unlock(pthread_mutex_t *mutex) {
  if (!mutex)
    return EINVAL;

  pthread_t self = pthread_self();

  // Error check: not owner
  if (mutex->type == PTHREAD_MUTEX_ERRORCHECK && mutex->owner != self) {
    return EPERM;
  }

  // Handle recursive mutex
  if (mutex->type == PTHREAD_MUTEX_RECURSIVE) {
    if (mutex->owner != self)
      return EPERM;

    mutex->recursive_count--;
    if (mutex->recursive_count > 0)
      return 0;
  }

  mutex->owner = 0;
  mutex->locked = 0;

  return 0;
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const void *abstime) {
  (void)abstime; // Timed locking not fully implemented
  return pthread_mutex_lock(mutex);
}

int pthread_mutexattr_init(pthread_mutexattr_t *attr) {
  if (!attr)
    return EINVAL;

  attr->type = PTHREAD_MUTEX_DEFAULT;
  attr->pshared = 0;
  attr->protocol = 0;
  attr->prioceiling = 0;
  attr->robust = 0;

  return 0;
}

int pthread_mutexattr_destroy(pthread_mutexattr_t *attr) {
  if (!attr)
    return EINVAL;
  return 0;
}

int pthread_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
  if (!attr)
    return EINVAL;
  if (type < 0 || type > PTHREAD_MUTEX_RECURSIVE)
    return EINVAL;

  attr->type = type;
  return 0;
}

int pthread_mutexattr_gettype(const pthread_mutexattr_t *attr, int *type) {
  if (!attr || !type)
    return EINVAL;

  *type = attr->type;
  return 0;
}

// ============================================================================
// Condition Variable Functions
// ============================================================================
int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr) {
  (void)attr;

  if (!cond)
    return EINVAL;

  cond->waiters = 0;
  cond->signal_count = 0;
  cond->mutex = 0;

  return 0;
}

int pthread_cond_destroy(pthread_cond_t *cond) {
  if (!cond)
    return EINVAL;

  if (cond->waiters > 0)
    return EBUSY;

  return 0;
}

int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex) {
  if (!cond || !mutex)
    return EINVAL;

  cond->mutex = mutex;
  int my_signal = cond->signal_count;
  cond->waiters = cond->waiters + 1;

  // Release mutex while waiting
  pthread_mutex_unlock(mutex);

  // Wait for signal
  while (cond->signal_count == my_signal) {
    schedule();
  }

  cond->waiters = cond->waiters - 1;

  // Re-acquire mutex
  pthread_mutex_lock(mutex);

  return 0;
}

int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const void *abstime) {
  (void)abstime; // Timed wait not fully implemented
  return pthread_cond_wait(cond, mutex);
}

int pthread_cond_signal(pthread_cond_t *cond) {
  if (!cond)
    return EINVAL;

  if (cond->waiters > 0)
    cond->signal_count = cond->signal_count + 1;

  return 0;
}

int pthread_cond_broadcast(pthread_cond_t *cond) {
  if (!cond)
    return EINVAL;

  // Wake all waiters
  cond->signal_count += cond->waiters;

  return 0;
}

int pthread_condattr_init(pthread_condattr_t *attr) {
  if (!attr)
    return EINVAL;

  attr->pshared = 0;
  attr->clock = 0;

  return 0;
}

int pthread_condattr_destroy(pthread_condattr_t *attr) {
  if (!attr)
    return EINVAL;
  return 0;
}

// ============================================================================
// Read-Write Lock Functions
// ============================================================================
int pthread_rwlock_init(pthread_rwlock_t *rwlock,
                        const pthread_rwlockattr_t *attr) {
  (void)attr;

  if (!rwlock)
    return EINVAL;

  rwlock->readers = 0;
  rwlock->writers = 0;
  rwlock->write_waiters = 0;
  rwlock->write_owner = 0;
  pthread_mutex_init(&rwlock->lock, 0);

  return 0;
}

int pthread_rwlock_destroy(pthread_rwlock_t *rwlock) {
  if (!rwlock)
    return EINVAL;

  if (rwlock->readers > 0 || rwlock->writers > 0)
    return EBUSY;

  pthread_mutex_destroy(&rwlock->lock);
  return 0;
}

int pthread_rwlock_rdlock(pthread_rwlock_t *rwlock) {
  if (!rwlock)
    return EINVAL;

  pthread_mutex_lock(&rwlock->lock);

  // Wait while there are writers or write waiters
  while (rwlock->writers > 0 || rwlock->write_waiters > 0) {
    pthread_mutex_unlock(&rwlock->lock);
    schedule();
    pthread_mutex_lock(&rwlock->lock);
  }

  rwlock->readers = rwlock->readers + 1;
  pthread_mutex_unlock(&rwlock->lock);

  return 0;
}

int pthread_rwlock_tryrdlock(pthread_rwlock_t *rwlock) {
  if (!rwlock)
    return EINVAL;

  if (pthread_mutex_trylock(&rwlock->lock) != 0)
    return EBUSY;

  if (rwlock->writers > 0 || rwlock->write_waiters > 0) {
    pthread_mutex_unlock(&rwlock->lock);
    return EBUSY;
  }

  rwlock->readers = rwlock->readers + 1;
  pthread_mutex_unlock(&rwlock->lock);

  return 0;
}

int pthread_rwlock_wrlock(pthread_rwlock_t *rwlock) {
  if (!rwlock)
    return EINVAL;

  pthread_mutex_lock(&rwlock->lock);
  rwlock->write_waiters = rwlock->write_waiters + 1;

  // Wait while there are readers or writers
  while (rwlock->readers > 0 || rwlock->writers > 0) {
    pthread_mutex_unlock(&rwlock->lock);
    schedule();
    pthread_mutex_lock(&rwlock->lock);
  }

  rwlock->write_waiters = rwlock->write_waiters - 1;
  rwlock->writers = rwlock->writers + 1;
  rwlock->write_owner = pthread_self();
  pthread_mutex_unlock(&rwlock->lock);

  return 0;
}

int pthread_rwlock_trywrlock(pthread_rwlock_t *rwlock) {
  if (!rwlock)
    return EINVAL;

  if (pthread_mutex_trylock(&rwlock->lock) != 0)
    return EBUSY;

  if (rwlock->readers > 0 || rwlock->writers > 0) {
    pthread_mutex_unlock(&rwlock->lock);
    return EBUSY;
  }

  rwlock->writers = rwlock->writers + 1;
  rwlock->write_owner = pthread_self();
  pthread_mutex_unlock(&rwlock->lock);

  return 0;
}

int pthread_rwlock_unlock(pthread_rwlock_t *rwlock) {
  if (!rwlock)
    return EINVAL;

  pthread_mutex_lock(&rwlock->lock);

  if (rwlock->writers > 0) {
    rwlock->writers = rwlock->writers + 1;
    rwlock->write_owner = 0;
  } else if (rwlock->readers > 0) {
    rwlock->readers--;
  }

  pthread_mutex_unlock(&rwlock->lock);

  return 0;
}

// ============================================================================
// Barrier Functions
// ============================================================================
int pthread_barrier_init(pthread_barrier_t *barrier,
                         const pthread_barrierattr_t *attr,
                         unsigned int count) {
  (void)attr;

  if (!barrier || count == 0)
    return EINVAL;

  barrier->count = count;
  barrier->current = 0;
  barrier->_phase = 0;
  pthread_mutex_init(&barrier->lock, 0);
  pthread_cond_init(&barrier->cond, 0);

  return 0;
}

int pthread_barrier_destroy(pthread_barrier_t *barrier) {
  if (!barrier)
    return EINVAL;

  pthread_mutex_destroy(&barrier->lock);
  pthread_cond_destroy(&barrier->cond);

  return 0;
}

int pthread_barrier_wait(pthread_barrier_t *barrier) {
  if (!barrier)
    return EINVAL;

  pthread_mutex_lock(&barrier->lock);

  int my_phase = barrier->_phase;
  barrier->current++;

  if (barrier->current >= barrier->count) {
    // Last thread to arrive
    barrier->current = 0;
    barrier->_phase++;
    pthread_cond_broadcast(&barrier->cond);
    pthread_mutex_unlock(&barrier->lock);
    return PTHREAD_BARRIER_SERIAL_THREAD;
  }

  // Wait for other threads
  while (barrier->_phase == my_phase) {
    pthread_cond_wait(&barrier->cond, &barrier->lock);
  }

  pthread_mutex_unlock(&barrier->lock);
  return 0;
}

// ============================================================================
// Spinlock Functions
// ============================================================================
int pthread_spin_init(pthread_spinlock_t *lock, int pshared) {
  (void)pshared;

  if (!lock)
    return EINVAL;

  *lock = 0;
  return 0;
}

int pthread_spin_destroy(pthread_spinlock_t *lock) {
  if (!lock)
    return EINVAL;
  return 0;
}

int pthread_spin_lock(pthread_spinlock_t *lock) {
  if (!lock)
    return EINVAL;

  while (!atomic_cas(lock, 0, 1)) {
    // Spin
    asm volatile("pause");
  }

  return 0;
}

int pthread_spin_trylock(pthread_spinlock_t *lock) {
  if (!lock)
    return EINVAL;

  if (atomic_cas(lock, 0, 1))
    return 0;

  return EBUSY;
}

int pthread_spin_unlock(pthread_spinlock_t *lock) {
  if (!lock)
    return EINVAL;

  *lock = 0;
  return 0;
}

// ============================================================================
// Thread-Specific Data Functions
// ============================================================================
int pthread_key_create(pthread_key_t *key, void (*destructor)(void *)) {
  if (!key)
    return EINVAL;

  for (int i = 0; i < MAX_TSD_KEYS; i++) {
    if (!tsd_key_used[i]) {
      tsd_key_used[i] = 1;
      tsd_destructors[i] = destructor;
      *key = i;
      return 0;
    }
  }

  return EAGAIN;
}

int pthread_key_delete(pthread_key_t key) {
  if (key >= MAX_TSD_KEYS || !tsd_key_used[key])
    return EINVAL;

  tsd_key_used[key] = 0;
  tsd_destructors[key] = 0;

  return 0;
}

void *pthread_getspecific(pthread_key_t key) {
  if (key >= MAX_TSD_KEYS || !tsd_key_used[key])
    return 0;

  struct thread_t *t = find_thread(current_thread_id);
  if (!t)
    return 0;

  return t->tsd[key];
}

int pthread_setspecific(pthread_key_t key, const void *value) {
  if (key >= MAX_TSD_KEYS || !tsd_key_used[key])
    return EINVAL;

  struct thread_t *t = find_thread(current_thread_id);
  if (!t)
    return EINVAL;

  t->tsd[key] = (void *)value;
  return 0;
}

// ============================================================================
// Once Functions
// ============================================================================
int pthread_once(pthread_once_t *once_control, void (*init_routine)(void)) {
  if (!once_control || !init_routine)
    return EINVAL;

  if (once_control->done)
    return 0;

  pthread_mutex_lock(&once_control->lock);

  if (!once_control->done) {
    init_routine();
    once_control->done = 1;
  }

  pthread_mutex_unlock(&once_control->lock);

  return 0;
}

// ============================================================================
// Yield
// ============================================================================
int pthread_yield(void) {
  schedule();
  return 0;
}

int sched_yield(void) {
  schedule();
  return 0;
}

} // extern "C"
