/* SPDX-License-Identifier: BSD-2-Clause */
/* ps2fb test: query, map and draw on the GS wsdisplay, switch modes, restore.
 *   fbtest [-d wsdisplay-device] [-t seconds] [-m widthxheight]
 */
#include <sys/types.h>
#include <sys/ioctl.h>
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

int
main(int argc, char **argv)
{
	const char *dev = "/dev/ttyE1";
	u_int secs = 5, w = 0, h = 0, rate;
	int ch, fd, mode;
	struct wsdisplay_fbinfo fi;
	struct wsdisplayio_fbinfo fbi;
	struct ps2fb_mode pm;
	u_int type, lb;
	size_t size;
	uint32_t *fb;

	while ((ch = getopt(argc, argv, "d:t:m:")) != -1) {
		switch (ch) {
		case 'd': dev = optarg; break;
		case 't': secs = atoi(optarg); break;
		case 'm': if (sscanf(optarg, "%ux%u", &w, &h) != 2)
				errx(1, "bad mode"); break;
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
	mode = WSDISPLAYIO_MODE_DUMBFB;
	if (ioctl(fd, WSDISPLAYIO_SMODE, &mode) < 0)
		err(1, "SMODE dumbfb");
	if (w != 0) {
		pm.width = w; pm.height = h; pm.depth = 32;
		if (ioctl(fd, PS2FBIO_SMODE, &pm) < 0)
			err(1, "PS2FBIO_SMODE %ux%u", w, h);
	}
	if (ioctl(fd, WSDISPLAYIO_GET_FBINFO, &fbi) < 0)
		err(1, "GET_FBINFO");
	printf("fbinfo %ux%u stride %u bpp %u size %lu\n", fbi.fbi_width,
	    fbi.fbi_height, fbi.fbi_stride, fbi.fbi_bitsperpixel,
	    (unsigned long)fbi.fbi_fbsize);
	size = fbi.fbi_fbsize;
	fb = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (fb == MAP_FAILED)
		err(1, "mmap");
	paint(fb, fbi.fbi_width, fbi.fbi_height);
	rate = 20;
	if (ioctl(fd, PS2FBIO_SRATE, &rate) < 0)
		err(1, "SRATE");
	if (ioctl(fd, PS2FBIO_FLUSH) < 0)
		err(1, "FLUSH");
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
