/* SPDX-License-Identifier: BSD-2-Clause */
/* Little-endian, 32-bit wire ABI shared by EE and standalone IOP code. */
#ifndef _PS2_IOP_BOOTSTRAP_H_
#define _PS2_IOP_BOOTSTRAP_H_
#define PS2_IOP_BOOT_COMMAND 0x4e425344
#define PS2_IOP_BOOT_SIGNATURE 0x42524745U
struct ps2_iop_boot_request {
	unsigned int flush_icache;
	unsigned int load_module;
	unsigned int start_module;
	unsigned int image;
	unsigned int argument_size;
	unsigned int stage;
	int module_id;
	int module_result;
	int start_result;
	unsigned int signature;
	char name[8];
	char arguments[16];
};
_Static_assert(sizeof(struct ps2_iop_boot_request) == 64, "IOP bootstrap ABI");
#endif
