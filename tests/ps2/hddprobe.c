/*
 * HDD bring-up probe.
 *   hddprobe raw <chardev> <MiB>        destructive pattern write/verify
 *   hddprobe fs  <blockdev> <dir> <tag> mount FFS, create/verify files, unmount
 */
#include <sys/param.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <ufs/ufs/ufsmount.h>
#include <sys/swap.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static uint32_t
pat(uint64_t off, uint32_t seed)
{
	uint32_t x = (uint32_t)(off >> 2) * 0x9e3779b1U ^ seed;
	return x ^ (x >> 15);
}

static void
fill(uint32_t *b, size_t n, uint64_t off, uint32_t seed)
{
	for (size_t i = 0; i < n / 4; i++)
		b[i] = pat(off + i * 4, seed);
}

static double
now(void)
{
	struct timeval tv;
	gettimeofday(&tv, NULL);
	return tv.tv_sec + tv.tv_usec / 1e6;
}

static int
rawtest(const char *dev, unsigned mib)
{
	static const size_t chunks[] = { 512, 4096, 65536, 131072 };
	uint64_t total = (uint64_t)mib << 20;
	uint32_t *buf = malloc(131072), *chk = malloc(131072);
	int fd = open(dev, O_RDWR);

	if (fd < 0 || buf == NULL || chk == NULL) {
		perror("open/malloc");
		return 1;
	}
	for (unsigned c = 0; c < 4; c++) {
		size_t cs = chunks[c];
		uint64_t base = (uint64_t)c * total, off;
		uint32_t seed = 0x1000 + c;
		double t0 = now();
		/* 512 B chunks over the whole region would be slow; cap them. */
		uint64_t len = cs == 512 ? total / 8 : total;

		for (off = 0; off < len; off += cs) {
			fill(buf, cs, base + off, seed);
			if (pwrite(fd, buf, cs, base + off) != (ssize_t)cs) {
				printf("HDD: FAIL write chunk=%zu off=%llu: %s\n",
				    cs, (unsigned long long)off, strerror(errno));
				return 1;
			}
		}
		double t1 = now();
		for (off = 0; off < len; off += cs) {
			fill(buf, cs, base + off, seed);
			if (pread(fd, chk, cs, base + off) != (ssize_t)cs ||
			    memcmp(buf, chk, cs) != 0) {
				printf("HDD: FAIL verify chunk=%zu off=%llu\n",
				    cs, (unsigned long long)off);
				return 1;
			}
		}
		double t2 = now();
		printf("HDD: raw chunk=%zu MiB=%llu write=%.1fs read=%.1fs OK\n",
		    cs, (unsigned long long)(len >> 20), t1 - t0, t2 - t1);
	}
	/* pseudo-random 4 KiB scatter */
	uint32_t r = 12345;
	for (int i = 0; i < 2000; i++) {
		r = r * 1103515245 + 12345;
		uint64_t off = ((uint64_t)(r >> 8) % (total * 4 / 4096)) * 4096;
		fill(buf, 4096, off + 77, 0x77);
		if (pwrite(fd, buf, 4096, off) != 4096 ||
		    pread(fd, chk, 4096, off) != 4096) {
			printf("HDD: FAIL scatter io off=%llu\n", (unsigned long long)off);
			return 1;
		}
		if (memcmp(buf, chk, 4096)) {
			printf("HDD: FAIL scatter verify off=%llu\n", (unsigned long long)off);
			return 1;
		}
	}
	fsync(fd);
	close(fd);
	puts("HDD: raw PASS");
	return 0;
}

static int
fstest(const char *dev, const char *dir, const char *tag)
{
	struct ufs_args ua = { .fspec = __UNCONST(dev) };
	char path[128], line[64];
	uint32_t *buf = malloc(65536), *chk = malloc(65536);
	int fd;

	if (mount("ffs", dir, 0, &ua, sizeof(ua)) != 0) {
		printf("HDD: FAIL mount %s on %s: %s\n", dev, dir, strerror(errno));
		return 1;
	}
	printf("HDD: mounted %s on %s\n", dev, dir);
	snprintf(path, sizeof(path), "%s/persist.txt", dir);
	fd = open(path, O_RDONLY);
	if (fd >= 0) {
		ssize_t n = read(fd, line, sizeof(line) - 1);
		line[n > 0 ? n : 0] = 0;
		printf("HDD: persisted from earlier boot: %s", line);
		close(fd);
	} else
		puts("HDD: no persist.txt yet (first boot)");
	fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0 || dprintf(fd, "tag=%s\n", tag) < 0 || fsync(fd)) {
		printf("HDD: FAIL write persist.txt: %s\n", strerror(errno));
		return 1;
	}
	close(fd);
	/* 4 MiB data file */
	snprintf(path, sizeof(path), "%s/data.bin", dir);
	fd = open(path, O_RDWR | O_CREAT | O_TRUNC, 0644);
	for (int i = 0; i < 64 && fd >= 0; i++) {
		fill(buf, 65536, (uint64_t)i * 65536, 0xf5);
		if (write(fd, buf, 65536) != 65536) {
			printf("HDD: FAIL data write: %s\n", strerror(errno));
			return 1;
		}
	}
	fsync(fd);
	lseek(fd, 0, SEEK_SET);
	for (int i = 0; i < 64; i++) {
		fill(buf, 65536, (uint64_t)i * 65536, 0xf5);
		if (read(fd, chk, 65536) != 65536 || memcmp(buf, chk, 65536)) {
			printf("HDD: FAIL data verify block %d\n", i);
			return 1;
		}
	}
	close(fd);
	if (unmount(dir, 0) != 0) {
		printf("HDD: FAIL unmount: %s\n", strerror(errno));
		return 1;
	}
	if (mount("ffs", dir, 0, &ua, sizeof(ua)) != 0) {
		printf("HDD: FAIL remount: %s\n", strerror(errno));
		return 1;
	}
	fd = open(path, O_RDONLY);
	for (int i = 0; i < 64 && fd >= 0; i++) {
		fill(buf, 65536, (uint64_t)i * 65536, 0xf5);
		if (read(fd, chk, 65536) != 65536 || memcmp(buf, chk, 65536)) {
			printf("HDD: FAIL remount verify block %d\n", i);
			return 1;
		}
	}
	close(fd);
	unmount(dir, 0);
	puts("HDD: fs PASS");
	return 0;
}

/* hddprobe file <path> <MiB> <w|v>: write or verify a pattern file (root FS). */
static int
filetest(const char *path, unsigned mib, int write_mode)
{
	uint32_t *buf = malloc(65536), *chk = malloc(65536);
	int fd = open(path, write_mode ? O_WRONLY | O_CREAT | O_TRUNC : O_RDONLY, 0644);
	double t0 = now();

	if (fd < 0 || buf == NULL || chk == NULL) {
		printf("HDD: FAIL open %s: %s\n", path, strerror(errno));
		return 1;
	}
	for (uint64_t off = 0; off < ((uint64_t)mib << 20); off += 65536) {
		fill(buf, 65536, off, 0xabc);
		if (write_mode) {
			if (write(fd, buf, 65536) != 65536) {
				printf("HDD: FAIL write at %llu: %s\n",
				    (unsigned long long)off, strerror(errno));
				return 1;
			}
		} else if (read(fd, chk, 65536) != 65536 ||
		    memcmp(buf, chk, 65536) != 0) {
			printf("HDD: FAIL verify at %llu\n", (unsigned long long)off);
			return 1;
		}
	}
	if (write_mode)
		fsync(fd);
	close(fd);
	printf("HDD: file %s %u MiB %s OK in %.1fs\n", path, mib,
	    write_mode ? "written" : "verified", now() - t0);
	return 0;
}

static int
swaptest(const char *dev)
{
	struct swapent se[4];
	int n;

	if (swapctl(SWAP_ON, __UNCONST(dev), 0) != 0) {
		printf("HDD: FAIL swapon %s: %s\n", dev, strerror(errno));
		return 1;
	}
	n = swapctl(SWAP_STATS, se, 4);
	for (int i = 0; i < n; i++)
		printf("HDD: swap %s blocks=%d\n", se[i].se_path, se[i].se_nblks);
	puts("HDD: swap PASS");
	return 0;
}

int
main(int argc, char **argv)
{
	setvbuf(stdout, NULL, _IONBF, 0);
	if (argc == 4 && strcmp(argv[1], "raw") == 0)
		return rawtest(argv[2], (unsigned)atoi(argv[3]));
	if (argc == 5 && strcmp(argv[1], "fs") == 0)
		return fstest(argv[2], argv[3], argv[4]);
	if (argc == 5 && strcmp(argv[1], "file") == 0)
		return filetest(argv[2], (unsigned)atoi(argv[3]), argv[4][0] == 'w');
	if (argc == 3 && strcmp(argv[1], "swap") == 0)
		return swaptest(argv[2]);
	fputs("usage: hddprobe raw dev MiB | fs dev dir tag | swap dev\n", stderr);
	return 2;
}
