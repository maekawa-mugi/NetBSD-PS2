/* Prove expanded RAM is usable: keep 80 MiB resident and verify every word. */
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/sysctl.h>
#include <uvm/uvm_extern.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static void
check(int good, const char *stage)
{
	if (!good) {
		perror(stage);
		printf("RAM128: FAIL %s\n", stage);
		exit(1);
	}
}

static uint32_t
pattern(size_t i)
{
	return (uint32_t)i * UINT32_C(0x9e3779b1) ^ UINT32_C(0x5ac369e7);
}

int
main(void)
{
	const size_t bytes = 80U * 1024 * 1024;
	const size_t words = bytes / sizeof(uint32_t);
	uint64_t total = 0;
	size_t len = sizeof(total);
	struct rusage usage;
	struct uvmexp_sysctl vm;

	setvbuf(stdout, NULL, _IONBF, 0);
	check(sysctlbyname("hw.physmem64", &total, &len, NULL, 0) == 0,
	    "hw.physmem64");
	printf("RAM128: physical=%" PRIu64 " MiB\n", total / (1024 * 1024));
	check(total == 128U * 1024 * 1024, "physical RAM size");
	len = sizeof(vm);
	check(sysctlbyname("vm.uvmexp2", &vm, &len, NULL, 0) == 0,
	    "vm.uvmexp2");
	check(vm.nswapdev == 0, "test requires no swap");
	volatile uint32_t *buf = mmap(NULL, bytes, PROT_READ|PROT_WRITE,
	    MAP_PRIVATE|MAP_ANON, -1, 0);
	check(buf != MAP_FAILED, "mmap 80 MiB");
	puts("RAM128: writing 80 MiB");
	for (size_t i = 0; i < words; i++)
		buf[i] = pattern(i);
	size_t pages = bytes / (size_t)getpagesize();
	char *resident = malloc(pages);
	check(resident != NULL, "residency vector");
	check(mincore((void *)buf, bytes, resident) == 0, "mincore");
	for (size_t i = 0; i < pages; i++)
		check(resident[i] & 1, "page not resident");
	free(resident);
	check(getrusage(RUSAGE_SELF, &usage) == 0, "getrusage");
	printf("RAM128: resident=%zu pages (80 MiB), swap=0, maxrss=%ld KiB\n",
	    pages, usage.ru_maxrss);
	/* The buffer exceeds all possible physical storage in a 32 MiB target.
	 * mincore confirms residency; no swap can evict dirty anonymous pages.
	 * Unique per-word data catches RAM aliases. */
	for (size_t i = 0; i < words; i++) {
		check(buf[i] == pattern(i), "forward pattern");
		buf[i] = ~pattern(i);
	}
	for (size_t i = words; i-- != 0;)
		check(buf[i] == ~pattern(i), "reverse inverse pattern");
	puts("RAM128: resident 80 MiB forward/inverse/reverse OK");
	check(munmap((void *)buf, bytes) == 0, "munmap");
	puts("RAM128: ALL CHECKS PASSED");
	return 0;
}
