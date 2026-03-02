#include "syscall.h"
#include "console.h"
#include "defs.h"
#include "loader.h"
#include "syscall_ids.h"
#include "timer.h"
#include "trap.h"
#include "vm.h"

uint64 sys_write(int fd, uint64 va, uint len)
{
	debugf("sys_write fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDOUT)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	int size = copyinstr(p->pagetable, str, va, MIN(len, MAX_STR_LEN));
	debugf("size = %d", size);
	for (int i = 0; i < size; ++i) {
		console_putchar(str[i]);
	}
	return size;
}

uint64 sys_read(int fd, uint64 va, uint64 len)
{
	debugf("sys_read fd = %d str = %x, len = %d", fd, va, len);
	if (fd != STDIN)
		return -1;
	struct proc *p = curr_proc();
	char str[MAX_STR_LEN];
	for (int i = 0; i < len; ++i) {
		int c = consgetc();
		str[i] = c;
	}
	copyout(p->pagetable, va, str, len);
	return len;
}

__attribute__((noreturn)) void sys_exit(int code)
{
	exit(code);
	__builtin_unreachable();
}

uint64 sys_sched_yield()
{
	yield();
	return 0;
}

uint64 sys_gettimeofday(uint64 val, int _tz)
{
	struct proc *p = curr_proc();
	uint64 cycle = get_cycle();
	TimeVal t;
	t.sec = cycle / CPU_FREQ;
	t.usec = (cycle % CPU_FREQ) * 1000000 / CPU_FREQ;
	copyout(p->pagetable, val, (char *)&t, sizeof(TimeVal));
	return 0;
}

uint64 sys_task_info(uint64 va) {
	struct proc *current_proc = curr_proc();
	TaskInfo local_ti;

	uint64 cycle = get_cycle();
	local_ti.time = (cycle * 1000 / CPU_FREQ) - current_proc->task_info.time;
	local_ti.status = Running;
	for (int i = 0; i < MAX_SYSCALL_NUM; i++) {
		local_ti.syscall_times[i] = current_proc->task_info.syscall_times[i];
	}

	if (copyout(current_proc->pagetable, va, (void *) &local_ti, sizeof(TaskInfo)) < 0) {
		return -1;
	}
	return 0;
}

uint64 sys_munmap(void* start, uint64 len) {
	struct proc *current_proc = curr_proc();
	uint64 a;
	pte_t* pte;
	uint64 pa;

	start = (void *) PGROUNDDOWN((uint64) start);
	for (a = (uint64) start; a < (uint64) start + len; a += PGSIZE) {
		pte = walk(current_proc->pagetable, a, 0);
		if (pte == 0) {
			return -1;
		}

		if ((*pte & PTE_V) == 0 || (*pte & PTE_U) == 0) {
			return -1;
		}

		pa = PTE2PA(*pte);
		kfree((void *) pa);
		*pte = 0;
	}
	return 0;
}

uint64 sys_mmap(void* start, uint64 len, int port, int flag, int fd) {
	if ((port & ~0x7) != 0) {
		return -1;
	}

	if ((port & 0x7) == 0) {
		return -1;
	}

	if (!PGALIGNED((uint64) start)) {
		return -1;
	}
	
	struct proc *current_proc = curr_proc();
	uint64 a;
	uint64 mem;
	uint64 page;
	int perm;

	len = PGROUNDUP(len);

	perm = PTE_U | PTE_V;
	if (port & (1 << 0)) perm |= PTE_R;
	if (port & (1 << 1)) perm |= PTE_W;
	if (port & (1 << 2)) perm |= PTE_X;

	for (a = (uint64) start; a < (uint64) start + len; a += PGSIZE) {
		page = useraddr(current_proc->pagetable, a);
		if (page != 0) {
			return -1;
		}

		mem = (uint64) kalloc();
		if (mem == 0) {
			return -1;
		}

		if (mappages(current_proc->pagetable, a, PGSIZE, mem, perm) == -1) {
			return -1;
		}
	}

	return 0;
}

uint64 sys_getpid()
{
	return curr_proc()->pid;
}

uint64 sys_getppid()
{
	struct proc *p = curr_proc();
	return p->parent == NULL ? IDLE_PID : p->parent->pid;
}

uint64 sys_clone()
{
	debugf("fork!\n");
	return fork();
}

uint64 sys_exec(uint64 va)
{
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_exec %s\n", name);
	return exec(name);
}

uint64 sys_wait(int pid, uint64 va)
{
	struct proc *p = curr_proc();
	int *code = (int *)useraddr(p->pagetable, va);
	return wait(pid, code);
}

uint64 sys_spawn(uint64 va)
{
	// TODO: your job is to complete the sys call
	struct proc *p = curr_proc();
	char name[200];
	copyinstr(p->pagetable, name, va, 200);
	debugf("sys_spawn %s\n", name);
	return spawn(name);
}

uint64 sys_set_priority(long long prio){
	if (prio < 2) return -1;
  struct proc *p = curr_proc();
	p->priority = prio;
  return prio;
}


extern char trap_page[];

void syscall()
{
	struct trapframe *trapframe = curr_proc()->trapframe;
	int id = trapframe->a7, ret;
	uint64 args[6] = { trapframe->a0, trapframe->a1, trapframe->a2,
			   trapframe->a3, trapframe->a4, trapframe->a5 };
	tracef("syscall %d args = [%x, %x, %x, %x, %x, %x]", id, args[0],
	       args[1], args[2], args[3], args[4], args[5]);
	switch (id) {
	case SYS_write:
		ret = sys_write(args[0], args[1], args[2]);
		break;
	case SYS_read:
		ret = sys_read(args[0], args[1], args[2]);
		break;
	case SYS_exit:
		sys_exit(args[0]);
		// __builtin_unreachable();
	case SYS_sched_yield:
		ret = sys_sched_yield();
		break;
	case SYS_gettimeofday:
		ret = sys_gettimeofday(args[0], args[1]);
		break;
	case SYS_getpid:
		ret = sys_getpid();
		break;
	case SYS_getppid:
		ret = sys_getppid();
		break;
	case SYS_clone: // SYS_fork
		ret = sys_clone();
		break;
	case SYS_execve:
		ret = sys_exec(args[0]);
		break;
	case SYS_wait4:
		ret = sys_wait(args[0], args[1]);
		break;
	case SYS_spawn:
		ret = sys_spawn(args[0]);
		break;
	case SYS_setpriority:
		ret = sys_set_priority(args[0]);
		break;
	case SYS_mmap:
		ret = sys_mmap((void *) args[0], args[1], args[2], args[3], args[4]);
		break;
	case SYS_munmap:
		ret = sys_munmap((void *) args[0], args[1]);
		break;
	case SYS_task_info:
		ret = sys_task_info(args[0]);
		break;
	default:
		ret = -1;
		errorf("unknown syscall %d", id);
	}
	trapframe->a0 = ret;
	tracef("syscall ret %d", ret);
}
