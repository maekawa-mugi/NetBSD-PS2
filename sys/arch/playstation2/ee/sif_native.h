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
/* Reserve once at attach; USB suballocations need no further RPC. */
bus_dma_tag_t ps2_iopdma_init(uint32_t, size_t);
#endif
