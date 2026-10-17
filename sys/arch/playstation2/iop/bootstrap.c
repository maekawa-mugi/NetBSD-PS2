/* SPDX-License-Identifier: BSD-2-Clause */
/* No globals, import stubs, gp references or absolute internal branches.
 * Enter through the IOP's uncached alias; the first call flushes its I-cache.
 * The EE owns this temporary callback exclusively until the RPC completes. */
#include "bootstrap.h"

void *
ps2_iop_bootstrap(int operation, void *buffer, int length)
{
	volatile struct ps2_iop_boot_request *p = buffer;
	if (operation != PS2_IOP_BOOT_COMMAND || length != sizeof(*p) ||
	    p->argument_size > sizeof(p->arguments))
		return buffer;
	p->signature = PS2_IOP_BOOT_SIGNATURE;
	p->stage = 1;
	((void (*)(void))p->flush_icache)();
	p->stage = 2;
	p->module_id = ((int (*)(void *))p->load_module)((void *)p->image);
	p->stage = 3;
	if (p->module_id >= 0) {
		p->start_result = ((int (*)(int, const char *, int, const char *, int *))
		    p->start_module)(p->module_id, (const char *)p->name,
		    p->argument_size, (const char *)p->arguments,
		    (int *)&p->module_result);
		p->stage = 4;
	}
	return (void *)&p->module_id;
}
