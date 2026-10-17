/* SPDX-License-Identifier: BSD-2-Clause */
/* Uncached IOP RAM; one writer per field, no RPC from interrupt context. */
#ifndef _PS2_IOP_USB_IRQ_H_
#define _PS2_IOP_USB_IRQ_H_
#define PS2_USB_IRQ_MAGIC 0x49525132U
#define PS2_USB_IRQ_GUARD 0x47554152U
struct ps2_usb_irq_mailbox {
	unsigned int magic;       /* IOP: published after handler installation */
	unsigned int sequence;    /* IOP: notification, including startup probe */
	unsigned int interrupts;  /* IOP: actual IRQ22 entries */
	unsigned int acknowledged; /* EE: last serviced sequence */
	unsigned int guard[12];   /* EE initializes; neither side modifies */
};
#endif
