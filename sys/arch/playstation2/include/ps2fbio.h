/* SPDX-License-Identifier: BSD-2-Clause */
/* PlayStation 2 GS framebuffer (ps2fb) private wsdisplay ioctls. */
#ifndef _PLAYSTATION2_PS2FBIO_H_
#define _PLAYSTATION2_PS2FBIO_H_

#include <sys/ioccom.h>

/*
 * Resolution of the mapped (WSDISPLAYIO_MODE_DUMBFB) framebuffer. The text
 * console always runs at the boot resolution (640x480); a different mode is
 * accepted only while a client holds the display in DUMBFB mode and is
 * dropped when the display returns to emulation mode.
 */
struct ps2fb_mode {
	u_int	width;
	u_int	height;
	u_int	depth;		/* 32: PSMCT32 (0xAABBGGRR); 8: palette index (PSMT8 + CLUT) */
};

#define PS2FBIO_GMODE	_IOWR('W', 192, struct ps2fb_mode) /* index in width */
#define PS2FBIO_SMODE	_IOW('W', 193, struct ps2fb_mode) /* applies to DUMBFB sessions */
/* Upload rate (Hz) of the mapped framebuffer; 0 disables the periodic copy. */
#define PS2FBIO_SRATE	_IOW('W', 194, u_int)
/* Make the GS show the mapped framebuffer contents now. */
#define PS2FBIO_FLUSH	_IO('W', 195)

#endif /* _PLAYSTATION2_PS2FBIO_H_ */
