/* Static n32 libc integration smoke test. Installed as PID 1 on the ramdisk. */
#include <sys/types.h>
#include <sys/atomic.h>
#include <sys/reboot.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <inttypes.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static volatile sig_atomic_t caught;
static __thread volatile uint64_t tls_value = UINT64_C(0x1020304050607080);

static void
handler(int signo)
{
	caught = signo;
}

static void __attribute__((__noreturn__))
fail(const char *stage)
{
	printf("PS2 n32 libc: FAILED at %s\n", stage);
	fflush(stdout);
	for (;;)
		(void)pause();
}

int
main(void)
{
	int fd = open("/dev/console", O_WRONLY);
	if (fd == -1)
		for (;;)
			(void)pause();
	for (int i = 0; i < 3; i++)
		if (dup2(fd, i) == -1)
			fail("dup2");
	if (fd > 2)
		(void)close(fd);
	(void)setvbuf(stdout, NULL, _IONBF, 0);
	printf("PS2 n32 libc: main pid=%d ptr=%zu long=%zu context=%zu\n",
	    (int)getpid(), sizeof(void *), sizeof(long), sizeof(ucontext_t));
	if (getpid() != 1 || sizeof(void *) != 4 || sizeof(long) != 4 ||
	    sizeof(ucontext_t) != 952)
		fail("ABI");
	if (tls_value != UINT64_C(0x1020304050607080))
		fail("TLS initial value");
	tls_value++;
	puts("PS2 n32 libc: TLS OK");

	char *buf = malloc(8192);
	if (buf == NULL)
		fail("malloc");
	memset(buf, 0x5a, 8192);
	for (size_t i = 0; i < 8192; i++)
		if ((unsigned char)buf[i] != 0x5a)
			fail("heap contents");
	free(buf);
	buf = malloc(32);
	if (buf == NULL)
		fail("malloc for realloc");
	buf = realloc(buf, 0);
	free(buf);
	puts("PS2 n32 libc: malloc/string OK");

	volatile uint64_t numerator = UINT64_C(0x123456789abcdef0);
	volatile uint64_t divisor = 97;
	if (numerator / divisor * divisor + numerator % divisor != numerator)
		fail("64-bit division");
	volatile double x = 1.25, y = 2.5;
	buf = malloc(64);
	if (buf == NULL)
		fail("malloc for printf");
	if (snprintf(buf, 64, "%.2f", x * y) != 4 ||
	    strcmp(buf, "3.12") != 0)
		fail("soft-float/printf");
	free(buf);
	puts("PS2 n32 libc: integer/soft-float/printf OK");

	volatile uint32_t a32 = UINT32_C(0x87654321);
	volatile uint64_t a64 = UINT64_C(0xfedcba9876543210);
	if (atomic_cas_32(&a32, 0x87654321, 7) != 0x87654321 || a32 != 7 ||
	    atomic_cas_32(&a32, 8, 9) != 7 || a32 != 7 ||
	    atomic_cas_64(&a64, UINT64_C(0xfedcba9876543210), 11) !=
	    UINT64_C(0xfedcba9876543210) || a64 != 11 ||
	    atomic_cas_64(&a64, 12, 13) != 11 || a64 != 11)
		fail("CAS");
	for (int i = 0; i < 10000; i++) {
		atomic_inc_32(&a32);
		atomic_inc_64(&a64);
	}
	if (a32 != 10007 || a64 != 10011)
		fail("atomic increment");
	puts("PS2 n32 libc: RAS atomic32/64 OK");
	_Atomic uint32_t c32 = 17;
	_Atomic uint64_t c64 = UINT64_C(0x1234567800000000);
	if (atomic_fetch_add_explicit(&c32, 2, memory_order_seq_cst) != 17 ||
	    atomic_load_explicit(&c32, memory_order_relaxed) != 19 ||
	    atomic_fetch_add_explicit(&c64, 3, memory_order_seq_cst) !=
	    UINT64_C(0x1234567800000000) ||
	    atomic_load_explicit(&c64, memory_order_relaxed) !=
	    UINT64_C(0x1234567800000003))
		fail("C11 atomics");
	puts("PS2 n32 libc: C11 atomic32/64 OK");

	struct sigaction sa;
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = handler;
	(void)sigemptyset(&sa.sa_mask);
	if (sigaction(SIGUSR1, &sa, NULL) == -1 || raise(SIGUSR1) != 0 ||
	    caught != SIGUSR1)
		fail("signal");
	puts("PS2 n32 libc: signal OK");

	pid_t child = fork();
	if (child == -1)
		fail("fork");
	if (child == 0) {
		if (tls_value != UINT64_C(0x1020304050607081))
			_exit(24);
		tls_value = 0;
		_exit(23);
	}
	int status;
	if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
	    WEXITSTATUS(status) != 23 ||
	    tls_value != UINT64_C(0x1020304050607081))
		fail("waitpid");
	puts("PS2 n32 libc: fork/wait OK");
	puts("PS2 n32 libc: ALL CHECKS PASSED");
	(void)reboot(RB_HALT, NULL);
	fail("reboot");
}
