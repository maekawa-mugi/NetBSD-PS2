/* SPDX-License-Identifier: BSD-2-Clause */
/* Independently authored resident IRQ13 (DEV9/SPEED) relay. No SDK code. */
#include "dev9_irq.h"
extern int irq_register(int, int, int (*)(void *), void *);
extern int irq_release(int);
extern int irq_enable(int);
extern int irq_suspend(int *);
extern int irq_resume(int);
extern void sif_set_cmd_buffer(void *, int);
#ifndef IOP_REG
#define IOP_REG(a) (*(volatile unsigned int *)(a))
#endif
#define SPD_INTR_STAT 0xbf400028U
#define SPD_INTR_MASK 0xbf40002aU
#define SMFLAG 0xbd000030U
#define EE_NOTIFY 0xbf801450U
#define SMFLAG_DEV9 0x800U
#define DMA8_MADR 0xbf801510U
#define DMA8_BCR 0xbf801514U
#define DMA8_CHCR 0xbf801518U
#define DMAC_DPCR2 0xbf801570U
#define DMA8_ENABLE 0x80U      /* DPCR2 channel 8 master enable */
#define CHCR_BUSY 0x01000000U

static const char module_name[] = "NetBSD_DEV9_IRQ";
const struct { const char *name; unsigned short version; } _irx_id = {
	module_name, 0x0100
};
static volatile struct ps2_dev9_irq_mailbox *mailbox;
/* User SIF command table (cid -> handler); installed with sceSifSetCmdBuffer. */
static struct { void (*handler)(void *, void *); void *harg; } cmd_table[2];

static void
dma_poll_done(void)
{
	if (mailbox->dma_state == PS2_DEV9_DMA_RUNNING &&
	    !(IOP_REG(DMA8_CHCR) & CHCR_BUSY))
		mailbox->dma_state = PS2_DEV9_DMA_DONE;
}

/* Runs in SIF1 interrupt context; programs DMAC channel 8 and returns. */
static void
dma_command(void *packet, void *arg)
{
	const volatile unsigned int *p = packet;
	unsigned int op = p[4], addr = p[5], bytes = p[6], seq = p[7];
	(void)arg;
	mailbox->dma_cmds++;
	if (op == PS2_DEV9_DMA_ABORT) {
		IOP_REG(DMA8_CHCR) = 0;
		mailbox->dma_state = PS2_DEV9_DMA_IDLE;
	} else if ((op == PS2_DEV9_DMA_READ || op == PS2_DEV9_DMA_WRITE) &&
	    (addr & 15) == 0 && addr >= 0x1000 && addr < 0x200000 &&
	    bytes != 0 && (bytes & 127) == 0 && bytes <= 0x20000 &&
	    addr + bytes <= 0x200000) {
		mailbox->dma_bytes = bytes;
		mailbox->dma_state = PS2_DEV9_DMA_RUNNING;
		IOP_REG(DMAC_DPCR2) = IOP_REG(DMAC_DPCR2) | DMA8_ENABLE;
		IOP_REG(DMA8_MADR) = addr;
		IOP_REG(DMA8_BCR) = ((bytes >> 7) << 16) | 0x20;
		IOP_REG(DMA8_CHCR) = op == PS2_DEV9_DMA_READ ?
		    0x01000200U : 0x01000201U;
		dma_poll_done();
	}
	mailbox->dma_ack = seq;
}

static void
notify(void)
{
	mailbox->sequence++;
	(void)mailbox->sequence;
	IOP_REG(SMFLAG) = SMFLAG_DEV9;
	(void)IOP_REG(SMFLAG);
	IOP_REG(EE_NOTIFY) = IOP_REG(EE_NOTIFY) | 2;
}

static int
dev9_interrupt(void *arg)
{
	(void)arg;
	/* Mask all SPEED sources at the device; the EE restores its own shadow
	 * of the enable register after servicing, which rearms IRQ13. */
	dma_poll_done();
	mailbox->status = *(volatile unsigned short *)SPD_INTR_STAT;
	*(volatile unsigned short *)SPD_INTR_MASK = 0;
	(void)*(volatile unsigned short *)SPD_INTR_MASK;
	mailbox->interrupts++;
	notify();
	return 1;
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
	if (irq_register(13, 1, dev9_interrupt, 0) < 0) {
		(void)irq_resume(state);
		return 1;
	}
	if (irq_enable(13) < 0) {
		(void)irq_release(13);
		(void)irq_resume(state);
		return 1;
	}
	cmd_table[PS2_DEV9_CMD_CID].handler = dma_command;
	cmd_table[PS2_DEV9_CMD_CID].harg = 0;
	sif_set_cmd_buffer(cmd_table, 2);
	mailbox->magic = PS2_DEV9_IRQ_MAGIC;
	notify();
	(void)irq_resume(state);
	return 0; /* Resident. */
}
