/* SPDX-License-Identifier: BSD-2-Clause */
OUTPUT_FORMAT("elf32-littlemips")
ENTRY(module_start)
SECTIONS {
	. = 0;
	.text : { *(.text .text.*) }
	. = ALIGN(16);
	.rodata : { *(.rodata .rodata.*) }
	. = ALIGN(16);
	.data : { *(.data .data.*) *(.sdata .sdata.*) }
	. = ALIGN(16);
	_gp = . + 0x7ff0;
	.bss : { *(.bss .bss.*) *(.sbss .sbss.*) *(COMMON) }
	.iopmod 0 (INFO) : {
		LONG(_irx_id) LONG(module_start) LONG(_gp)
		LONG(ADDR(.data)) LONG(SIZEOF(.data)) LONG(SIZEOF(.bss))
		SHORT(0x0100) BYTE(0)
	}
	/DISCARD/ : { *(.comment) *(.note*) *(.reginfo) *(.MIPS.abiflags) }
}
