#include <error.h>
#include <lib.h>
#include <mfutex.h>
#include <mthread.h>
#include <mmu.h>

#define MTHREAD_MAX_STACKS (PDMAP / PAGE_SIZE)

struct mthread_start_arg {
	void *(*start_routine)(void *);
	void *arg;
};

static volatile u_int stack_used[MTHREAD_MAX_STACKS];
static volatile mthread_t stack_thread[MTHREAD_MAX_STACKS];
static struct mthread_start_arg start_args[MTHREAD_MAX_STACKS];

static void *mthread_trampoline(void *raw) {
	struct mthread_start_arg *start = (struct mthread_start_arg *)raw;
	void *ret;

	env = &envs[ENVX(syscall_getenvid())];
	ret = start->start_routine(start->arg);
	mthread_exit(ret);
	return 0;
}

static int reserve_stack_slot(void) {
	for (int i = 1; i < MTHREAD_MAX_STACKS; i++) {
		u_int expected = 0;
		if (__atomic_compare_exchange_n(&stack_used[i], &expected, 1, 0, __ATOMIC_ACQUIRE,
						__ATOMIC_RELAXED)) {
			return i;
		}
	}
	return -E_NO_MEM;
}

static void release_stack_slot(int slot) {
	if (slot > 0 && slot < MTHREAD_MAX_STACKS) {
		stack_thread[slot] = 0;
		__atomic_store_n(&stack_used[slot], 0, __ATOMIC_RELEASE);
	}
}

int mthread_create(mthread_t *thread, void *(*start_routine)(void *), void *arg) {
	int slot, r;
	u_int stack_top, stack_bottom;

	if (thread == 0 || start_routine == 0) {
		return -E_INVAL;
	}
	slot = reserve_stack_slot();
	if (slot < 0) {
		return slot;
	}
	stack_top = USTACKTOP - slot * PAGE_SIZE;
	stack_bottom = stack_top - PAGE_SIZE;

	if ((r = syscall_mem_alloc(0, (void *)stack_bottom, PTE_D)) < 0) {
		release_stack_slot(slot);
		return r;
	}
	start_args[slot].start_routine = start_routine;
	start_args[slot].arg = arg;

	r = syscall_create_thread(mthread_trampoline, (void *)stack_top, &start_args[slot]);
	if (r < 0) {
		syscall_mem_unmap(0, (void *)stack_bottom);
		release_stack_slot(slot);
		return r;
	}
	*thread = r;
	stack_thread[slot] = r;
	return 0;
}

int mthread_equal(mthread_t *t1, mthread_t *t2) {
	return t1 != 0 && t2 != 0 && *t1 == *t2;
}

void mthread_exit(void *status) {
	syscall_exit((int)status);
}

int mthread_join(mthread_t *thread, void **status) {
	int r;
	int ret;

	if (thread == 0) {
		return -E_INVAL;
	}
	r = syscall_wait(*thread, &ret);
	if (r < 0) {
		return r;
	}
	if (status != 0) {
		*status = (void *)ret;
	}
	for (int i = 1; i < MTHREAD_MAX_STACKS; i++) {
		if (stack_thread[i] == *thread) {
			syscall_mem_unmap(0, (void *)(USTACKTOP - (i + 1) * PAGE_SIZE));
			release_stack_slot(i);
			break;
		}
	}
	return 0;
}

int mthread_mutex_init(mthread_mutex_t *mutex) {
	if (mutex == 0) {
		return -E_INVAL;
	}
	__atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
	return 0;
}

int mthread_mutex_lock(mthread_mutex_t *mutex) {
	uint32_t expected = 0;
	int r;

	if (mutex == 0) {
		return -E_INVAL;
	}
	if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0, __ATOMIC_ACQUIRE,
					__ATOMIC_RELAXED)) {
		return 0;
	}
	while (__atomic_exchange_n(&mutex->state, 2, __ATOMIC_ACQUIRE) != 0) {
		r = syscall_mfutex(&mutex->state, MFUTEX_WAIT, 2);
		if (r < 0 && r != -E_AGAIN) {
			return r;
		}
	}
	return 0;
}

int mthread_mutex_trylock(mthread_mutex_t *mutex) {
	uint32_t expected = 0;

	if (mutex == 0) {
		return -E_INVAL;
	}
	if (__atomic_compare_exchange_n(&mutex->state, &expected, 1, 0, __ATOMIC_ACQUIRE,
					__ATOMIC_RELAXED)) {
		return 0;
	}
	return -E_BUSY;
}

int mthread_mutex_unlock(mthread_mutex_t *mutex) {
	uint32_t old;

	if (mutex == 0) {
		return -E_INVAL;
	}
	old = __atomic_fetch_sub(&mutex->state, 1, __ATOMIC_RELEASE);
	if (old == 1) {
		return 0;
	}
	__atomic_store_n(&mutex->state, 0, __ATOMIC_RELEASE);
	return syscall_mfutex(&mutex->state, MFUTEX_WAKE, 1);
}
