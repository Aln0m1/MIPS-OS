#include <env.h>
#include <error.h>
#include <mfutex.h>
#include <mmu.h>
#include <pmap.h>
#include <sched.h>
#include <types.h>

extern struct Env envs[];

static int futex_word_pa(uint32_t *uaddr, u_int *pa_store, uint32_t **kaddr_store) {
	u_int va = (u_int)uaddr;
	Pte *pte;
	struct Page *pp;

	if ((va & 3) || va < UTEXT || va >= KSEG0) {
		return -E_INVAL;
	}
	pp = page_lookup(curenv->env_pgdir, va, &pte);
	if (pp == NULL) {
		return -E_INVAL;
	}
	*pa_store = page2pa(pp) + (va & (PAGE_SIZE - 1));
	if (kaddr_store != NULL) {
		*kaddr_store = (uint32_t *)(KADDR(page2pa(pp)) + (va & (PAGE_SIZE - 1)));
	}
	return 0;
}

int sys_mfutex(uint32_t *uaddr, u_int op, uint32_t val) {
	u_int pa;
	uint32_t *word;
	u_int woken = 0;

	if (op != MFUTEX_WAIT && op != MFUTEX_WAKE) {
		return -E_INVAL;
	}
	try(futex_word_pa(uaddr, &pa, &word));

	if (op == MFUTEX_WAIT) {
		if (*word != val) {
			return -E_AGAIN;
		}
		curenv->env_wait_type = ENV_WAIT_FUTEX;
		curenv->env_futex_pa = pa;
		curenv->env_status = ENV_NOT_RUNNABLE;
		TAILQ_REMOVE(&env_sched_list, curenv, env_sched_link);
		((struct Trapframe *)KSTACKTOP - 1)->regs[2] = 0;
		schedule(1);
	}

	for (struct Env *e = envs; e < envs + NENV && woken < val; e++) {
		if (e->env_status == ENV_NOT_RUNNABLE && e->env_wait_type == ENV_WAIT_FUTEX &&
		    e->env_futex_pa == pa) {
			e->env_wait_type = ENV_WAIT_NONE;
			e->env_futex_pa = 0;
			e->env_tf.regs[2] = 0;
			e->env_status = ENV_RUNNABLE;
			TAILQ_INSERT_TAIL(&env_sched_list, e, env_sched_link);
			woken++;
		}
	}
	return 0;
}
