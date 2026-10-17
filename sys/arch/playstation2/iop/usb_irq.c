/* SPDX-License-Identifier: BSD-2-Clause */
/* Independently authored resident IRQ22 relay. No SDK implementation code. */
#include "usb_irq.h"
extern int irq_register(int, int, int (*)(void *), void *);
extern int irq_release(int);
extern int irq_enable(int);
extern int irq_suspend(int *);
extern int irq_resume(int);
#ifndef IOP_REG
#define IOP_REG(a) (*(volatile unsigned int *)(a))
#endif
#define OHCI_DISABLE 0xbf801614U
#define OHCI_MIE 0x80000000U
#define SMFLAG 0xbd000030U
#define EE_NOTIFY 0xbf801450U

static const char module_name[] = "NetBSD_USB_IRQ";
const struct { const char *name; unsigned short version; } _irx_id = {
	module_name, 0x0100
};
static volatile struct ps2_usb_irq_mailbox *mailbox;

static void
notify(void)
{
	mailbox->sequence++;
	(void)mailbox->sequence;
	IOP_REG(SMFLAG) = 0x400;
	(void)IOP_REG(SMFLAG);
	IOP_REG(EE_NOTIFY) = IOP_REG(EE_NOTIFY) | 2;
}

static int
usb_interrupt(void *arg)
{
	(void)arg;
	/* Mask at the device, not intrman's shared INTC mask. EE alone rearms
	 * MIE after consuming this sequence. Pending OHCI status stays intact. */
	IOP_REG(OHCI_DISABLE) = OHCI_MIE;
	(void)IOP_REG(OHCI_DISABLE);
	mailbox->interrupts++;
	notify();
	return 1; /* Keep IRQ22 enabled in intrman; the device is now masked. */
}

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
	if (argv[1][8] || (address & 15) || address < 0x1000 ||
	    address > 0x200000 - sizeof(*mailbox))
		return 1;
	int state;
	if (irq_suspend(&state) < 0)
		return 1;
	mailbox = (void *)(address | 0xa0000000U);
	IOP_REG(OHCI_DISABLE) = OHCI_MIE;
	(void)IOP_REG(OHCI_DISABLE);
	if (irq_register(22, 1, usb_interrupt, 0) < 0) {
		(void)irq_resume(state);
		return 1;
	}
	if (irq_enable(22) < 0) {
		(void)irq_release(22);
		(void)irq_resume(state);
		return 1;
	}
	mailbox->magic = PS2_USB_IRQ_MAGIC;
	notify(); /* Prove the SBUS path before enabling OHCI MIE on the EE. */
	(void)irq_resume(state);
	return 0; /* Resident: code and mailbox survive for the IOP boot lifetime. */
}
