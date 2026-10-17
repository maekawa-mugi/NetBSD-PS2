/* SPDX-License-Identifier: BSD-2-Clause */
OUTPUT_FORMAT("elf32-littlemips")
ENTRY(ps2_iop_bootstrap)
SECTIONS {
	. = 0;
	.text : { *(.text .text.*) }
	/DISCARD/ : { *(.pdr) *(.reginfo) *(.MIPS.abiflags) *(.comment) *(.note*) }
}
