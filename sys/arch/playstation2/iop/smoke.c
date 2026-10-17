/* SPDX-License-Identifier: BSD-2-Clause */
/* Nonresident test module: write a signature into an EE-owned IOP mailbox. */
extern int irq_suspend(int *);
extern int irq_resume(int);
static const char module_name[] = "NetBSD_IOP_smoke";
const struct { const char *name; unsigned short version; } _irx_id = {
	module_name, 0x0100
};

int
module_start(int argc, char **argv)
{
	unsigned int address = 0;
	if (argc != 2)
		return 1;
	for (unsigned int i = 0; i < 8; i++) {
		unsigned int c = (unsigned char)argv[1][i];
		if (c >= '0' && c <= '9')
			c -= '0';
		else if (c >= 'a' && c <= 'f')
			c -= 'a' - 10;
		else
			return 1;
		address = address * 16 + c;
	}
	if (argv[1][8] != 0 || (address & 15) || address < 0x1000 ||
	    address > 0x00200000 - 64)
		return 1;
	int state;
	if (irq_suspend(&state) < 0)
		return 1;
	volatile unsigned int *p = (volatile unsigned int *)(address | 0xa0000000U);
	p[0] = 0x4e425344U;
	p[1] = 0x494f5031U;
	p[2] = address;
	p[3] = argc;
	(void)p[3];
	(void)irq_resume(state);
	return 1; /* Nonresident: no handlers or references survive this call. */
}
