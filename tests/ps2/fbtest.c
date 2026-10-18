/* SPDX-License-Identifier: BSD-2-Clause */
/* ps2fb test: query, map and draw on the GS wsdisplay, switch modes, restore.
 *   fbtest [-d wsdisplay-device] [-t seconds] [-m widthxheight] [-b bpp(8|32)] [-s (set mode only)]
 */
#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <sys/mman.h>
#include <dev/wscons/wsconsio.h>
#include <ps2fbio.h>
#include <err.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void
paint8(uint8_t *fb, u_int w, u_int h)
{
	/* Palette: 0..7 bars, 16..271-ish grey ramp mapped to 32..255. */
	u_char r[256], g[256], b[256];
	static const uint32_t bars[8] = { 0xffffff, 0x00ffff, 0xffff00, 0x00ff00,
	    0xff00ff, 0x0000ff, 0xff0000, 0x000000 };
	struct wsdisplay_cmap cm;

	memset(r, 0, sizeof r); memset(g, 0, sizeof g); memset(b, 0, sizeof b);
	for (u_int i = 0; i < 8; i++) {
		r[i] = bars[i] & 0xff; g[i] = (bars[i] >> 8) & 0xff;
		b[i] = (bars[i] >> 16) & 0xff;
	}
	for (u_int i = 32; i < 256; i++)
		r[i] = g[i] = b[i] = i;
	cm.index = 0; cm.count = 256; cm.red = r; cm.green = g; cm.blue = b;
	for (u_int y = 0; y < h; y++)
		for (u_int x = 0; x < w; x++) {
			uint8_t c;
			if (y < h * 3 / 4)
				c = x * 8 / w;
			else
				c = 32 + x * 223 / w;
			if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
				c = 0;
			fb[y * w + x] = c;
		}
	/* caller issues PUTCMAP with the same tables */
	extern int fbtest_fd;
	if (ioctl(fbtest_fd, WSDISPLAYIO_PUTCMAP, &cm) < 0)
		err(1, "PUTCMAP");
}

static void
paint(uint32_t *fb, u_int w, u_int h)
{
	/* 8 vertical colour bars over a horizontal grey ramp, R at bit 0. */
	static const uint32_t bars[8] = { 0xffffff, 0x00ffff, 0xffff00, 0x00ff00,
	    0xff00ff, 0x0000ff, 0xff0000, 0x000000 };
	for (u_int y = 0; y < h; y++)
		for (u_int x = 0; x < w; x++) {
			uint32_t c;
			if (y < h * 3 / 4)
				c = bars[x * 8 / w];
			else
				c = (x * 255 / w) * 0x010101;
			if (x < 2 || y < 2 || x >= w - 2 || y >= h - 2)
				c = 0xffffff;
			fb[y * w + x] = c;
		}
}

int fbtest_fd;

int
main(int argc, char **argv)
{
	const char *dev = "/dev/ttyE1";
	u_int secs = 5, w = 0, h = 0, rate, bpp = 32;
	int ch, fd, mode, setonly = 0, nflush = 0;
	struct wsdisplay_fbinfo fi;
	struct wsdisplayio_fbinfo fbi;
	struct ps2fb_mode pm;
	u_int type, lb;
	size_t size;
	uint32_t *fb;

	while ((ch = getopt(argc, argv, "d:t:m:b:sn:")) != -1) {
		switch (ch) {
		case 'd': dev = optarg; break;
		case 't': secs = atoi(optarg); break;
		case 'm': if (sscanf(optarg, "%ux%u", &w, &h) != 2)
				errx(1, "bad mode"); break;
		case 's': setonly = 1; break;
		case 'n': nflush = atoi(optarg); break;
		case 'b': bpp = atoi(optarg); break;
		default: errx(1, "usage");
		}
	}
	if ((fd = open(dev, O_RDWR)) < 0)
		err(1, "open %s", dev);
	if (ioctl(fd, WSDISPLAYIO_GTYPE, &type) < 0)
		err(1, "GTYPE");
	if (ioctl(fd, WSDISPLAYIO_GINFO, &fi) < 0)
		err(1, "GINFO");
	if (ioctl(fd, WSDISPLAYIO_LINEBYTES, &lb) < 0)
		err(1, "LINEBYTES");
	printf("type %u info %ux%u depth %u linebytes %u\n", type, fi.width,
	    fi.height, fi.depth, lb);
	for (u_int i = 0;; i++) {
		pm.width = i; pm.height = pm.depth = 0;
		if (ioctl(fd, PS2FBIO_GMODE, &pm) < 0)
			break;
		printf("mode[%u] %ux%u depth %u\n", i, pm.width, pm.height,
		    pm.depth);
	}
	fbtest_fd = fd;
	if (w == 0) { w = 640; h = 480; }
	pm.width = w; pm.height = h; pm.depth = bpp;
	if (ioctl(fd, PS2FBIO_SMODE, &pm) < 0)
		err(1, "PS2FBIO_SMODE %ux%u", w, h);
	if (setonly) {
		printf("next DUMBFB mode %ux%ux%u\n", w, h, bpp);
		return 0;
	}
	mode = WSDISPLAYIO_MODE_DUMBFB;
	if (ioctl(fd, WSDISPLAYIO_SMODE, &mode) < 0)
		err(1, "SMODE dumbfb");
	if (ioctl(fd, WSDISPLAYIO_GET_FBINFO, &fbi) < 0)
		err(1, "GET_FBINFO");
	printf("fbinfo %ux%u stride %u bpp %u size %lu\n", fbi.fbi_width,
	    fbi.fbi_height, fbi.fbi_stride, fbi.fbi_bitsperpixel,
	    (unsigned long)fbi.fbi_fbsize);
	size = fbi.fbi_fbsize;
	fb = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED)
		err(1, "mmap");
	if (fbi.fbi_bitsperpixel == 8)
		paint8((uint8_t *)fb, fbi.fbi_width, fbi.fbi_height);
	else
		paint(fb, fbi.fbi_width, fbi.fbi_height);
	rate = 20;
	if (ioctl(fd, PS2FBIO_SRATE, &rate) < 0)
		err(1, "SRATE");
	if (ioctl(fd, PS2FBIO_FLUSH) < 0)
		err(1, "FLUSH");
	if (nflush > 0) {
		struct timeval t0, t1;
		double ms;

		rate = 0;
		(void)ioctl(fd, PS2FBIO_SRATE, &rate);
		gettimeofday(&t0, NULL);
		for (int i = 0; i < nflush; i++)
			(void)ioctl(fd, PS2FBIO_FLUSH);
		gettimeofday(&t1, NULL);
		ms = (t1.tv_sec - t0.tv_sec) * 1000.0 +
		    (t1.tv_usec - t0.tv_usec) / 1000.0;
		printf("%d flushes: %.1f ms each\n", nflush, ms / nflush);
	}
	printf("painted; holding %u s\n", secs);
	fflush(stdout);
	sleep(secs);
	munmap(fb, size);
	mode = WSDISPLAYIO_MODE_EMUL;
	if (ioctl(fd, WSDISPLAYIO_SMODE, &mode) < 0)
		err(1, "SMODE emul");
	printf("restored emulation mode\n");
	return 0;
}
