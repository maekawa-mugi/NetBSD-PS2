/* SPDX-License-Identifier: BSD-2-Clause */
/* Intrman 1.1 ABI directory: suspend/resume only. No SDK headers or objects. */
	.set noreorder
	.section .text.imports,"ax",@progbits
	.balign 4
	.word 0x41e00000, 0, 0x00000101
	.ascii "intrman\0"
	.globl irq_suspend
irq_suspend:
	.word 0x03e00008, 0x24000011
	.globl irq_resume
irq_resume:
	.word 0x03e00008, 0x24000012
	.word 0, 0
