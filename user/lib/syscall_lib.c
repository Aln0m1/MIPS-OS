#include <env.h>
#include <lib.h>
#include <mfutex.h>
#include <mmu.h>
#include <syscall.h>
#include <trap.h>

void syscall_putchar(int ch) {
	msyscall(SYS_putchar, ch);
}

int syscall_print_cons(const void *str, u_int num) {
	return msyscall(SYS_print_cons, str, num);
}

u_int syscall_getenvid(void) {
	return msyscall(SYS_getenvid);
}

void syscall_yield(void) {
	msyscall(SYS_yield);
}

int syscall_env_destroy(u_int envid) {
	return msyscall(SYS_env_destroy, envid);
}

int syscall_set_tlb_mod_entry(u_int envid, void (*func)(struct Trapframe *)) {
	return msyscall(SYS_set_tlb_mod_entry, envid, func);
}

int syscall_mem_alloc(u_int envid, void *va, u_int perm) {
	return msyscall(SYS_mem_alloc, envid, va, perm);
}

int syscall_mem_map(u_int srcid, void *srcva, u_int dstid, void *dstva, u_int perm) {
	return msyscall(SYS_mem_map, srcid, srcva, dstid, dstva, perm);
}

int syscall_mem_unmap(u_int envid, void *va) {
	return msyscall(SYS_mem_unmap, envid, va);
}

int syscall_set_env_status(u_int envid, u_int status) {
	return msyscall(SYS_set_env_status, envid, status);
}

int syscall_set_trapframe(u_int envid, struct Trapframe *tf) {
	return msyscall(SYS_set_trapframe, envid, tf);
}

void syscall_panic(const char *msg) {
	int r = msyscall(SYS_panic, msg);
	user_panic("SYS_panic returned %d", r);
}

int syscall_ipc_try_send(u_int envid, u_int value, const void *srcva, u_int perm) {
	return msyscall(SYS_ipc_try_send, envid, value, srcva, perm);
}

int syscall_ipc_recv(void *dstva) {
	return msyscall(SYS_ipc_recv, dstva);
}

int syscall_cgetc() {
	return msyscall(SYS_cgetc);
}

int syscall_write_dev(void *va, u_int dev, u_int size) {
	/* Exercise 5.2: Your code here. (1/2) */
	return msyscall(SYS_write_dev, va, dev, size);
}

int syscall_read_dev(void *va, u_int dev, u_int size) {
	/* Exercise 5.2: Your code here. (2/2) */
	return msyscall(SYS_read_dev, va, dev, size);
}

int syscall_create_thread(void *(*entry_point)(void *), void *stack, void *arg) {
	return msyscall(SYS_create_thread, entry_point, stack, arg);
}

int syscall_gettgid(void) {
	return msyscall(SYS_gettgid);
}

void syscall_exit(int return_value) {
	msyscall(SYS_exit, return_value);
	user_panic("SYS_exit returned");
}

int syscall_wait(u_int envid, int *return_value_ptr) {
	return msyscall(SYS_wait, envid, return_value_ptr);
}

int syscall_mfutex(uint32_t *uaddr, u_int op, uint32_t val) {
	return msyscall(SYS_mfutex, uaddr, op, val);
}
