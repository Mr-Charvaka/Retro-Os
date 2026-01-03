#ifndef SEMAPHORE_H
#define SEMAPHORE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// POSIX Semaphore Types
// ============================================================================
typedef struct {
  volatile int value;
  volatile int waiters;
} sem_t;

// Failed semaphore value
#define SEM_FAILED ((sem_t *)-1)

// ============================================================================
// Semaphore Functions
// ============================================================================
int sem_init(sem_t *sem, int pshared, unsigned int value);
int sem_destroy(sem_t *sem);
int sem_wait(sem_t *sem);
int sem_trywait(sem_t *sem);
int sem_timedwait(sem_t *sem, const void *abs_timeout);
int sem_post(sem_t *sem);
int sem_getvalue(sem_t *sem, int *sval);

// Named semaphores
sem_t *sem_open(const char *name, int oflag, ...);
int sem_close(sem_t *sem);
int sem_unlink(const char *name);

// ============================================================================
// POSIX Message Queue Types
// ============================================================================
typedef int mqd_t; // Message queue descriptor

struct mq_attr {
  long mq_flags;   // Message queue flags
  long mq_maxmsg;  // Maximum number of messages
  long mq_msgsize; // Maximum message size
  long mq_curmsgs; // Number of messages currently in queue
};

// ============================================================================
// Message Queue Functions
// ============================================================================
mqd_t mq_open(const char *name, int oflag, ...);
int mq_close(mqd_t mqdes);
int mq_unlink(const char *name);
int mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
            unsigned int msg_prio);
int mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len,
                 unsigned int msg_prio, const void *abs_timeout);
ssize_t mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                   unsigned int *msg_prio);
ssize_t mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len,
                        unsigned int *msg_prio, const void *abs_timeout);
int mq_getattr(mqd_t mqdes, struct mq_attr *attr);
int mq_setattr(mqd_t mqdes, const struct mq_attr *newattr,
               struct mq_attr *oldattr);
int mq_notify(mqd_t mqdes, const void *sevp);

// ============================================================================
// POSIX Shared Memory Functions
// ============================================================================
int shm_open(const char *name, int oflag, int mode);
int shm_unlink(const char *name);

#ifdef __cplusplus
}
#endif

#endif // SEMAPHORE_H
