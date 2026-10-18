/* SPDX-License-Identifier: BSD-2-Clause */
/* Uncached IOP RAM; one writer per field, no RPC from interrupt context. */
#ifndef _PS2_IOP_DEV9_IRQ_H_
#define _PS2_IOP_DEV9_IRQ_H_
#define PS2_DEV9_IRQ_MAGIC 0x39564544U
#define PS2_DEV9_IRQ_GUARD 0x47554152U
#define PS2_DEV9_IRQ_GUARDS 6
/* SIF user command (cid) accepted by the module. Payload words 4..7:
 * operation, IOP address, byte count (multiple of 128), request sequence. */
#define PS2_DEV9_CMD_CID 1
#define PS2_DEV9_DMA_READ 1  /* SPEED -> IOP RAM (DMAC ch8, CHCR 0x01000200) */
#define PS2_DEV9_DMA_WRITE 2 /* IOP RAM -> SPEED (CHCR 0x01000201) */
#define PS2_DEV9_DMA_ABORT 3
#define PS2_DEV9_DMA_IDLE 0
#define PS2_DEV9_DMA_RUNNING 1
#define PS2_DEV9_DMA_DONE 2
struct ps2_dev9_irq_mailbox {
	unsigned int magic;        /* IOP: published after handler installation */
	unsigned int sequence;     /* IOP: notification, including startup probe */
	unsigned int interrupts;   /* IOP: actual IRQ13 entries */
	unsigned int acknowledged; /* EE: last serviced sequence */
	unsigned int status;       /* IOP: SPEED INTR_STAT sampled at the IRQ */
	unsigned int dma_cmds;     /* IOP: SIF DMA commands handled */
	unsigned int dma_ack;      /* IOP: request sequence last handled */
	unsigned int dma_state;    /* IOP: PS2_DEV9_DMA_* */
	unsigned int dma_bytes;    /* IOP: bytes of the active/last request */
	unsigned int reserved;
	unsigned int guard[PS2_DEV9_IRQ_GUARDS]; /* EE initializes */
};
#endif
