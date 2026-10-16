/* User-mode regressions for translated code and precise R5900 exceptions. */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <mips/cachectl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>
#include <unistd.h>

static uintptr_t expected_pc, bad_address, resume_pc;
static unsigned expected_bd;
static volatile sig_atomic_t caught;
static volatile uint32_t seen_cause;

static int
sync_code(void *address, size_t length)
{
	return _cacheflush(address, length, BCACHE);
}

static void
segv(int signo, siginfo_t *info, void *arg)
{
	ucontext_t *uc = arg;
	uint32_t cause = uc->uc_mcontext.__gregs[_REG_CAUSE];
	uintptr_t pc = uc->uc_mcontext.__gregs[_REG_EPC];
	if (signo != SIGSEGV || (uintptr_t)info->si_addr != bad_address ||
	    pc != expected_pc || (cause >> 31) != expected_bd) {
		static const char msg[] = "JIT regression: exception PC/BD/address FAIL\n";
		(void)write(1, msg, sizeof(msg)-1);
		_exit(2);
	}
	seen_cause = cause;
	caught++;
	uc->uc_mcontext.__gregs[_REG_EPC] = resume_pc;
}

static void
check(int good, const char *stage)
{
	if (!good) {
		printf("JIT regression: FAIL %s\n", stage);
		exit(1);
	}
}

int
main(void)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	size_t page = (size_t)getpagesize();
	uint32_t *code = mmap(NULL, page, PROT_READ|PROT_WRITE|PROT_EXEC,
	    MAP_PRIVATE|MAP_ANON, -1, 0);
	check(code != MAP_FAILED, "map code");
	/* Rewrite the same already-executed virtual address repeatedly. */
	for (unsigned i = 1; i <= 64; i++) {
		code[0] = 0x24020000 | i; /* addiu v0,zero,i */
		code[1] = 0x03e00008;     /* jr ra */
		code[2] = 0;
		check(sync_code(code, 12) == 0, "cacheflush");
		check(((int (*)(void))code)() == (int)i, "rewritten code result");
	}
	puts("JIT regression: code rewrite/cacheflush x64 OK");
	/* A second process gets different instructions at the same VA (COW). */
	pid_t child = fork();
	check(child >= 0, "fork COW code");
	if (child == 0) {
		code[0] = 0x2402007b;
		if (sync_code(code, 12) != 0 ||
		    ((int (*)(void))code)() != 123)
			_exit(3);
		_exit(0);
	}
	int status;
	check(waitpid(child, &status, 0) == child && status == 0, "child code COW");
	check(((int (*)(void))code)() == 64, "parent code after child");
	puts("JIT regression: fork/COW code isolation OK");

	void *bad = mmap(NULL, page, PROT_NONE, MAP_PRIVATE|MAP_ANON, -1, 0);
	check(bad != MAP_FAILED, "map protected page");
	bad_address = (uintptr_t)bad;
	struct sigaction sa = { .sa_sigaction = segv, .sa_flags = SA_SIGINFO };
	sigemptyset(&sa.sa_mask);
	check(sigaction(SIGSEGV, &sa, NULL) == 0, "sigaction");
	code[0] = 0x10000001; /* beq zero,zero,+1 */
	code[1] = 0x8c820000; /* lw v0,0(a0), branch delay slot */
	code[2] = 0x03e00008; /* jr ra */
	code[3] = 0;
	check(sync_code(code, 16) == 0, "cacheflush fault code");
	resume_pc = (uintptr_t)&code[2];
	expected_pc = (uintptr_t)&code[1];
	expected_bd = 0;
	(void)((int (*)(void *))&code[1])(bad);
	check(caught == 1, "ordinary fault signal");
	printf("JIT regression: load exception EPC/BD=0 OK (cause=%08" PRIx32 ")\n",
	    seen_cause);
	expected_pc = (uintptr_t)code;
	expected_bd = 1;
	(void)((int (*)(void *))code)(bad);
	check(caught == 2, "delay-slot fault signal");
	printf("JIT regression: delay-slot exception EPC/BD=1 OK (cause=%08" PRIx32 ")\n",
	    seen_cause);
	check(munmap(bad, page) == 0 && munmap(code, page) == 0, "unmap");
	puts("JIT regression: ALL CHECKS PASSED");
	return 0;
}
