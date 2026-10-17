/* SPDX-License-Identifier: BSD-2-Clause */
#ifndef _PS2_SIF_NATIVE_H_
#define _PS2_SIF_NATIVE_H_
#include <machine/bus.h>
struct ps2sif_attach_args {
	bus_dma_tag_t dmat;
};
/* RPC APIs: sleepable thread/autoconfiguration context, never interrupt. */
int ps2sif_alloc(size_t, uint32_t *);
int ps2sif_free(uint32_t);
struct ps2sif_module_result {
	int32_t module_id;
	int32_t entry_result;
	int32_t start_result;
	uint32_t signature;
};
/* PS2_IOP_BOOTSTRAP only. The image and bootstrap are retained on timeout.
 * Thread/autoconfiguration context only; no other EE RPC owner may exist. */
int ps2sif_exec_irx(const uint32_t *, size_t, const char *, size_t,
    struct ps2sif_module_result *);
/* Single USB owner. Establish is sleepable; poll/stop never issue RPC.
 * Stop is terminal until IOP reset; resident code/mailbox are retained. */
int ps2sif_usb_intr_establish(int (*)(void *), void *);
void ps2sif_usb_intr_poll(void);
void ps2sif_usb_intr_stop(void);
bool ps2sif_usb_intr_active(void);
/* Reserve once at attach; USB suballocations need no further RPC. */
bus_dma_tag_t ps2_iopdma_init(uint32_t, size_t);
#endif
