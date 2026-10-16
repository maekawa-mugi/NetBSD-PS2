/* SPDX-License-Identifier: BSD-2-Clause */
/* USB DMA lives in an exclusively reserved IOP RAM arena, not EE RAM. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/vmem.h>
#include <sys/proc.h>
#define _PLAYSTATION2_BUS_DMA_PRIVATE
#include <machine/bus.h>
#include <mips/cpuregs.h>
#include <playstation2/ee/sif_native.h>

#define IOP_WINDOW MIPS_PHYS_TO_KSEG1(0x1c000000U)
static vmem_t *arena;
static bus_addr_t pool_base;
static bus_size_t pool_size;

static bool
in_pool(bus_addr_t a, bus_size_t n)
{
	return n > 0 && a >= pool_base && a - pool_base < pool_size &&
	    n <= pool_size - (a - pool_base);
}

static int
iop_alloc(bus_dma_tag_t t, bus_size_t size, bus_size_t alignment,
    bus_size_t boundary, bus_dma_segment_t *segs, int nsegs, int *rsegs,
    int flags)
{
	vmem_addr_t a;
	if (size == 0 || nsegs < 1 || size > pool_size ||
	    (alignment && !powerof2(alignment)) ||
	    (boundary && (!powerof2(boundary) || size > boundary)))
		return EINVAL;
	int error = vmem_xalloc(arena, size, MAX(alignment, 16), 0,
	    boundary, VMEM_ADDR_MIN, VMEM_ADDR_MAX, VM_NOSLEEP | VM_BESTFIT, &a);
	if (error)
		return error;
	segs[0].ds_addr = a;
	segs[0].ds_len = size;
	segs[0]._ds_vaddr = IOP_WINDOW + a;
	*rsegs = 1;
	return 0;
}

static void
iop_free(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs)
{
	KASSERT(nsegs == 1 && in_pool(segs[0].ds_addr, segs[0].ds_len));
	vmem_xfree(arena, segs[0].ds_addr, segs[0].ds_len);
}

static int
iop_map(bus_dma_tag_t t, bus_dma_segment_t *segs, int nsegs,
    size_t size, void **kvap, int flags)
{
	if (nsegs != 1 || size > segs[0].ds_len ||
	    !in_pool(segs[0].ds_addr, size))
		return EINVAL;
	*kvap = (void *)(IOP_WINDOW + segs[0].ds_addr);
	return 0;
}

static void
iop_unmap(bus_dma_tag_t t, void *kva, size_t size)
{
	/* IOP RAM is permanently mapped in uncached KSEG1. */
}

static int
iop_load(bus_dma_tag_t t, bus_dmamap_t map, void *buf,
    bus_size_t size, struct proc *p, int flags)
{
	uintptr_t va = (uintptr_t)buf;
	map->dm_nsegs = 0;
	map->dm_mapsize = 0;
	if (p != NULL || va < IOP_WINDOW || size > map->_dm_size ||
	    !in_pool(va - IOP_WINDOW, size))
		return EINVAL;
	bus_addr_t a = va - IOP_WINDOW;
	bus_size_t left = size;
	int n = 0;
	while (left) {
		if (n == map->_dm_segcnt)
			return EFBIG;
		bus_size_t len = MIN(left, map->dm_maxsegsz);
		if (map->_dm_boundary)
			len = MIN(len, map->_dm_boundary - (a & (map->_dm_boundary-1)));
		if (len == 0)
			return EINVAL;
		map->dm_segs[n].ds_addr = a;
		map->dm_segs[n].ds_len = len;
		map->dm_segs[n]._ds_vaddr = IOP_WINDOW + a;
		a += len;
		left -= len;
		n++;
	}
	map->dm_nsegs = n;
	map->dm_mapsize = size;
	return 0;
}

static int
iop_mbuf(bus_dma_tag_t t, bus_dmamap_t m, struct mbuf *b, int f)
{
	return EOPNOTSUPP;
}
static int
iop_uio(bus_dma_tag_t t, bus_dmamap_t m, struct uio *u, int f)
{
	return EOPNOTSUPP;
}
static int
iop_raw(bus_dma_tag_t t, bus_dmamap_t m, bus_dma_segment_t *s,
    int n, bus_size_t z, int f)
{
	return EOPNOTSUPP;
}

static paddr_t
iop_mmap(bus_dma_tag_t t, bus_dma_segment_t *s, int n,
    off_t o, int p, int f)
{
	return (paddr_t)-1;
}

static void
iop_sync(bus_dma_tag_t t, bus_dmamap_t map, bus_addr_t off,
    bus_size_t len, int ops)
{
	KASSERT(off <= map->dm_mapsize && len <= map->dm_mapsize - off);
	/* No EE cache aliases exist for the IOP window. Order CPU accesses
	 * and drain posted stores before publishing OHCI descriptors. */
	__asm volatile("sync.l; sync.p" ::: "memory");
	(void)*(volatile uint32_t *)IOP_WINDOW;
}

static struct playstation2_bus_dma_tag iop_tag = {
	._dmamap_create = _bus_dmamap_create,
	._dmamap_destroy = _bus_dmamap_destroy,
	._dmamap_load = iop_load,
	._dmamap_load_mbuf = iop_mbuf,
	._dmamap_load_uio = iop_uio,
	._dmamap_load_raw = iop_raw,
	._dmamap_unload = _bus_dmamap_unload,
	._dmamap_sync = iop_sync,
	._dmamem_alloc = iop_alloc,
	._dmamem_free = iop_free,
	._dmamem_map = iop_map,
	._dmamem_unmap = iop_unmap,
	._dmamem_mmap = iop_mmap,
};

bus_dma_tag_t
ps2_iopdma_init(uint32_t base, size_t size)
{
	if (arena || (base & 15) || base == 0 || base >= 0x200000U ||
	    size == 0 || size > 0x200000U - base)
		return NULL;
	pool_base = base;
	pool_size = size;
	arena = vmem_create("iopdma", base, size, 16, NULL, NULL, NULL,
	    0, VM_SLEEP, IPL_VM);
	return arena ? &iop_tag : NULL;
}
