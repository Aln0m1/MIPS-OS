#ifndef MTHREAD_H
#define MTHREAD_H

#include <types.h>

typedef u_int mthread_t;
typedef struct {
	// 0: unlocked
	// 1: locked, no known waiters
	// 2: locked, maybe has waiters
	uint32_t state;
} mthread_mutex_t;

int mthread_create(mthread_t *thread, void *(*start_routine)(void *), void *arg);
int mthread_equal(mthread_t *t1, mthread_t *t2);
void mthread_exit(void *status);
int mthread_join(mthread_t *thread, void **status);
int mthread_mutex_init(mthread_mutex_t *mutex);
int mthread_mutex_lock(mthread_mutex_t *mutex);
int mthread_mutex_trylock(mthread_mutex_t *mutex);
int mthread_mutex_unlock(mthread_mutex_t *mutex);

#endif
