/* SPDX-License-Identifier: BSD-2-Clause */
/* Public intrman 1.1 ABI ordinals, no SDK headers or objects. */
	.set noreorder
	.section .text.imports,"ax",@progbits
	.balign 4
	.word 0x41e00000, 0, 0x00000101
	.ascii "intrman\0"
	.globl irq_register
irq_register:
	.word 0x03e00008, 0x24000004
	.globl irq_release
irq_release:
	.word 0x03e00008, 0x24000005
	.globl irq_enable
irq_enable:
	.word 0x03e00008, 0x24000006
	.globl irq_suspend
irq_suspend:
	.word 0x03e00008, 0x24000011
	.globl irq_resume
irq_resume:
	.word 0x03e00008, 0x24000012
	.word 0, 0
	.word 0x41e00000, 0, 0x00000101
	.ascii "sifcmd\0\0"
	.globl sif_set_cmd_buffer
sif_set_cmd_buffer:
	.word 0x03e00008, 0x24000008
	.word 0, 0
