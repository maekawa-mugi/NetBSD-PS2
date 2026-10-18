/* On-target checks: atomics, TLS, threads, soft-float, libm, dlopen. */
#include <sys/atomic.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(name, cond) do { \
	int ok_ = !!(cond); \
	printf("%-28s %s\n", name, ok_ ? "ok" : "FAIL"); \
	if (!ok_) failures++; \
} while (0)

static __thread int tls_counter = 5;
static __thread char tls_buf[300];
static int tls_dummy_init = 1;
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
static volatile unsigned shared_count;
static atomic_uint c11_count;
static unsigned nbsd_count;
static int ready, tls_ok_threads;

#define NTHREADS 4
#define NITER 20000

static void *
worker(void *arg)
{
	long id = (long)arg;
	int i;

	tls_counter = (int)id * 100;
	memset(tls_buf, (int)id, sizeof(tls_buf));
	for (i = 0; i < NITER; i++) {
		atomic_fetch_add(&c11_count, 1);
		atomic_inc_uint(&nbsd_count);
		pthread_mutex_lock(&mtx);
		shared_count++;
		pthread_mutex_unlock(&mtx);
		tls_counter++;
		if ((i & 0xfff) == 0)
			sched_yield();
	}
	if (tls_counter == (int)id * 100 + NITER && tls_buf[299] == (char)id) {
		pthread_mutex_lock(&mtx);
		tls_ok_threads++;
		pthread_mutex_unlock(&mtx);
	}
	pthread_mutex_lock(&mtx);
	ready++;
	pthread_cond_broadcast(&cv);
	pthread_mutex_unlock(&mtx);
	return (void *)(id * 2);
}

static void
test_float(void)
{
	volatile double a = 1.5, b = 2.25, z = 0.0;
	volatile float f = 3.0f, g = 0.1f;
	char buf[64];
	int64_t big = 1234567890123LL;

	CHECK("double add/mul", a + b == 3.75 && a * b == 3.375);
	CHECK("double div", (a / b) > 0.6666 && (a / b) < 0.6667);
	CHECK("float ops", f * g > 0.2999f && f * g < 0.3001f);
	CHECK("int<->double", (double)big == 1234567890123.0 && (int64_t)(a * 1e12) == 1500000000000LL);
	CHECK("nan/inf", isnan(z / z) && isinf(1.0 / z) && !(z / z == z / z));
	CHECK("sqrt/sin/exp/log", fabs(sqrt(2.0) - 1.41421356237) < 1e-9 &&
	    fabs(sin(1.0) - 0.8414709848) < 1e-9 && fabs(exp(1.0) - 2.718281828) < 1e-8 &&
	    fabs(log(10.0) - 2.302585093) < 1e-8);
	CHECK("pow/atan2/fmod", fabs(pow(2.0, 0.5) - 1.41421356237) < 1e-9 &&
	    fabs(atan2(1.0, 1.0) - 0.7853981634) < 1e-9 && fmod(7.5, 2.0) == 1.5);
	snprintf(buf, sizeof(buf), "%.6f %g", 3.14159265, 1e-5);
	CHECK("printf %f %g", strcmp(buf, "3.141593 1e-05") == 0);
	CHECK("strtod", fabs(strtod("2.5e3", NULL) - 2500.0) < 1e-9);
	CHECK("long double", (long double)a * b == 3.375L);
}

static void
test_atomics(void)
{
	unsigned u = 0, old;
	uint64_t q = 0;
	void *p;

	atomic_inc_uint(&u);
	atomic_add_int((volatile unsigned *)&u, 2);
	CHECK("atomic inc/add", u == 3);
	old = atomic_swap_uint(&u, 9);
	CHECK("atomic swap", old == 3 && u == 9);
	old = atomic_cas_uint(&u, 9, 11);
	CHECK("atomic cas ok", old == 9 && u == 11);
	old = atomic_cas_uint(&u, 9, 12);
	CHECK("atomic cas fail", old == 11 && u == 11);
	atomic_or_uint(&u, 0x100);
	atomic_and_uint(&u, 0x1ff);
	CHECK("atomic or/and", u == 0x10b);
	CHECK("builtin fetch_add", __atomic_fetch_add(&q, 5, __ATOMIC_SEQ_CST) == 0 && q == 5);
	CHECK("builtin 64 cas", __sync_bool_compare_and_swap(&q, 5, 0x100000001ULL) && q == 0x100000001ULL);
	p = atomic_cas_ptr(&p, p, p);
	CHECK("atomic cas ptr", 1);
}

static void
test_threads(void)
{
	pthread_t t[NTHREADS];
	void *rv;
	long i;
	int ok = 1;

	tls_counter = 77;
	CHECK("tls main init", tls_dummy_init == 1 && tls_counter == 77);
	for (i = 0; i < NTHREADS; i++)
		if (pthread_create(&t[i], NULL, worker, (void *)(i + 1)) != 0)
			ok = 0;
	CHECK("pthread_create", ok);
	pthread_mutex_lock(&mtx);
	while (ready < NTHREADS)
		pthread_cond_wait(&cv, &mtx);
	pthread_mutex_unlock(&mtx);
	for (i = 0; i < NTHREADS; i++) {
		pthread_join(t[i], &rv);
		if ((long)rv != (i + 1) * 2)
			ok = 0;
	}
	CHECK("pthread_join retvals", ok);
	CHECK("mutex counter", shared_count == NTHREADS * NITER);
	CHECK("c11 atomic counter", atomic_load(&c11_count) == NTHREADS * NITER);
	CHECK("netbsd atomic counter", nbsd_count == NTHREADS * NITER);
	CHECK("tls per thread", tls_ok_threads == NTHREADS && tls_counter == 77);
}

static void
test_dl(void)
{
	void *h = dlopen("libm.so.0", RTLD_NOW);
	double (*fn)(double);

	if (h == NULL) {
		printf("%-28s skipped (%s)\n", "dlopen libm", dlerror());
		return;
	}
	fn = (double (*)(double))dlsym(h, "cbrt");
	CHECK("dlopen/dlsym cbrt", fn != NULL && fabs(fn(27.0) - 3.0) < 1e-9);
	dlclose(h);
}

int
main(int argc, char **argv)
{
	printf("userland-check: pid %d\n", (int)getpid());
	test_float();
	test_atomics();
	test_threads();
	if (argc > 1 && strcmp(argv[1], "dl") == 0)
		test_dl();
	printf("userland-check: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
	return failures != 0;
}
