/*
 * Copyright (c) 2001 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * PCRTC setup derived from code contributed to The NetBSD Foundation
 * by UCHIYAMA Yasushi.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Direct EE/GS early text console. No SBIOS, RTE, IOP RPC, DMA or VU use.
 * VGA PCRTC timings follow this port's gs.c; GIF/GS transfer formats follow
 * the EE User Manual ch.7 and GS User Manual ch.4/5/7.
 */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <dev/wscons/wsconsio.h>
#include <dev/wsfont/sony8x16.h>
#include <mips/cpuregs.h>
#include <machine/intr.h>
#include <playstation2/ee/gsreg.h>

#define WIDTH 640
#define HEIGHT 480
#define COLS (WIDTH / 8)
#define ROWS (HEIGHT / 16)
#define BG 0x80201810U
#define FG 0x80e8e8e8U
#define GIF_STAT (*(volatile uint32_t *)MIPS_PHYS_TO_KSEG1(0x10003020))
#define GIF_CTRL (*(volatile uint32_t *)MIPS_PHYS_TO_KSEG1(0x10003000))
#define GSWRITE(reg, value) (*(volatile uint64_t *)(reg) = (value))

struct gs_qword { uint64_t lo, hi; } __aligned(16);
static bool ready;
static unsigned col, row;

void ps2_gs_init(void);
void ps2_gs_putc(int);

static bool
send_qword(uint64_t lo, uint64_t hi)
{
	struct gs_qword q = { lo, hi };
	/* A failed GS must not prevent the independent SIO console working. */
	unsigned timeout = 100000;
	while (((GIF_STAT >> 24) & 31) >= 16) {
		if (--timeout == 0) {
			ready = false;
			return false;
		}
	}
	/* $8 is caller-saved in n32; SQ requires the entire 128-bit value. */
	__asm volatile("lq $8,0(%0); sq $8,0(%1); sync.p"
	    :: "r"(&q), "r"(MIPS_PHYS_TO_KSEG1(0x10006000))
	    : "$8", "memory");
	return true;
}

static bool
transfer(unsigned x, unsigned y, unsigned w, unsigned h, unsigned sy,
    bool copy)
{
	/* PACKED A+D: BITBLTBUF, TRXPOS, TRXREG, TRXDIR. */
	return send_qword(4 | (1ULL << 60) | (copy ? 0x8000 : 0), 0xe) &&
	    send_qword((10ULL << 48) | (copy ? 10ULL << 16 : 0), 0x50) &&
	    send_qword(((uint64_t)y << 48) | ((uint64_t)x << 32) |
	        ((uint64_t)sy << 16), 0x51) &&
	    send_qword(((uint64_t)h << 32) | w, 0x52) &&
	    send_qword(copy ? 2 : 0, 0x53);
}

static bool
fill(unsigned y, unsigned h)
{
	if (!transfer(0, y, WIDTH, h, 0, false))
		return false;
	unsigned n = WIDTH * h / 4;
	/* IMAGE tag, EOP; largest call is 640*16/4 = 2560 qwords. */
	if (!send_qword(n | 0x8000 | (2ULL << 58), 0))
		return false;
	while (n--)
		if (!send_qword(((uint64_t)BG << 32) | BG,
		    ((uint64_t)BG << 32) | BG))
			return false;
	return true;
}

static void
glyph(unsigned c)
{
	if (c < 32 || c > 255)
		c = '?';
	if (!transfer(col * 8, row * 16, 8, 16, 0, false) ||
	    !send_qword(32 | 0x8000 | (2ULL << 58), 0))
		return;
	const uint8_t *bits = sony8x16_data + (c - 32) * 16;
	for (unsigned y = 0; y < 16; y++) {
		uint32_t pixels[8];
		for (unsigned x = 0; x < 8; x++)
			pixels[x] = bits[y] & (0x80 >> x) ? FG : BG;
		for (unsigned x = 0; x < 8; x += 4)
			if (!send_qword(((uint64_t)pixels[x+1] << 32) | pixels[x],
			    ((uint64_t)pixels[x+3] << 32) | pixels[x+2]))
				return;
	}
}

static void
newline(void)
{
	col = 0;
	if (++row == ROWS) {
		/* Top-to-bottom copy is safe for upward overlapping scrolling. */
		if (transfer(0, 0, WIDTH, HEIGHT - 16, 16, true))
			fill(HEIGHT - 16, 16);
		row--;
	}
}

void
ps2_gs_putc(int c)
{
	if (!ready)
		return;
	/* Serialize packet fragments against interrupts on this UP machine. */
	/* consinit precedes mips_vector_init: the SPL switch is not ready yet. */
	int s = _intr_suspend();
	switch (c) {
	case '\r': col = 0; break;
	case '\n': newline(); break;
	case '\f':
		for (unsigned y = 0; y < HEIGHT && ready; y += 16)
			fill(y, 16);
		col = row = 0;
		break;
	case '\b': if (col) col--; break;
	case '\t':
		do { glyph(' '); if (++col == COLS) newline(); }
		while (col % 8);
		break;
	default:
		if (c >= 32 && c != 127) {
			glyph((unsigned char)c);
			if (++col == COLS)
				newline();
		}
		break;
	}
	_intr_resume(s);
}

void
ps2_gs_init(void)
{
	const uint64_t smode1 = SMODE1(1,0,1,1,1,0,0,0,0,0,
	    2,0,0,1,0,0,0,0,1,15,2);
	GSWRITE(GS_S_CSR_REG, 1 << 9);
	GSWRITE(GS_S_PMODE_REG, 0);
	GSWRITE(GS_S_IMR_REG, 0x7f00);
	GSWRITE(GS_S_BUSDIR_REG, 0);
	GSWRITE(GS_S_SMODE1_REG, smode1 | (1ULL << 16));
	GSWRITE(GS_S_SYNCH1_REG, SYNCH1(192,608,192,84,32));
	GSWRITE(GS_S_SYNCH2_REG, SYNCH2(768,524));
	GSWRITE(GS_S_SYNCV_REG, SYNCV(2,480,0,33,0,10));
	GSWRITE(GS_S_SMODE2_REG, SMODE2(0,0,0));
	GSWRITE(GS_S_SRFSH_REG, SRFSH(4));
	GSWRITE(GS_S_SMODE1_REG, smode1 & ~(1ULL << 16));
	/* PLL stabilization, 2.5 ms at the EE's 147.456 MHz Count clock.
	 * This early path cannot use delay() before clock initialization. */
	uint32_t start, now;
	__asm volatile("mfc0 %0,$9" : "=r"(start));
	do {
		__asm volatile("mfc0 %0,$9" : "=r"(now));
	} while ((uint32_t)(now - start) < 368640);
	GSWRITE(GS_S_SMODE1_REG, smode1 & ~((1ULL << 16) | (1ULL << 17)));
	GSWRITE(GS_S_DISPLAY2_REG, DISPLAY(HEIGHT - 1,1279,0,1,34,276));
	GSWRITE(GS_S_DISPFB2_REG, 10 << 9); /* FBP=0, FBW=640/64, PSMCT32 */
	GSWRITE(GS_S_BGCOLOR_REG, BG & 0xffffff);
	GIF_CTRL = 1;
	__asm volatile("sync.p" ::: "memory");
	ready = true;
	col = row = 0;
	for (unsigned y = 0; y < HEIGHT && ready; y += 16)
		fill(y, 16);
	/* Circuit 2 only, ALP=0x80, select constant alpha. */
	GSWRITE(GS_S_PMODE_REG, 0x8022);
	const char *banner = "NetBSD/PS2 - direct GS console (SIO input)\n";
	while (*banner)
		ps2_gs_putc(*banner++);
}
