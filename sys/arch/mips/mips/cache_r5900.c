/*
 * R5900 EE cache operations.
 *
 * EE Core User's Manual, chapter 6: 16 KiB I-cache, 8 KiB D-cache,
 * two ways, 64-byte lines. The CACHE instruction reference specifies
 * index bits [12:6] (I) / [11:6] (D), and bit 0 selects the way.
 * R4K CACHE op numbers and way-address strides do not apply to the EE.
 *
 * The existing cache_op_r5900_line_64 macro places SYNC.L and SYNC.P
 * before and after each operation, including the required SYNC.L
 * immediately after each writeback. All op arguments below are literals.
 */
#include <sys/cdefs.h>
#include <sys/param.h>

#include <mips/cache.h>
#include <mips/cache_r5900.h>
#include <mips/cpuregs.h>

#define EE_LINE 64
#define EE_IWAY (CACHE_R5900_SIZE_I / 2)
#define EE_DWAY (CACHE_R5900_SIZE_D / 2)

/* Widen before adding so even a vsize_t-sized range cannot wrap here. */
static uint64_t
r5900_lines(vaddr_t va, vsize_t size)
{
	if (size == 0)
		return 0;
	return ((uint64_t)size + (va & (EE_LINE - 1)) + EE_LINE - 1) /
	    EE_LINE;
}

void
r5900_pdcache_wbinv_all_64(void)
{
	for (vaddr_t index = 0; index < EE_DWAY; index += EE_LINE) {
		vaddr_t va = MIPS_PHYS_TO_KSEG0(index);
		cache_op_r5900_line_64(va, CACHEOP_R5900_IWBINV_D);
		cache_op_r5900_line_64(va | 1, CACHEOP_R5900_IWBINV_D);
	}
}

void
r5900_pdcache_wbinv_range_index_64(vaddr_t va, vsize_t size)
{
	uint64_t lines = r5900_lines(va, size);
	if (lines >= EE_DWAY / EE_LINE) {
		r5900_pdcache_wbinv_all_64();
		return;
	}
	va &= ~(vaddr_t)(EE_LINE - 1);
	for (; lines != 0; lines--, va += EE_LINE) {
		vaddr_t index = MIPS_PHYS_TO_KSEG0(va & (EE_DWAY - 1));
		cache_op_r5900_line_64(index, CACHEOP_R5900_IWBINV_D);
		cache_op_r5900_line_64(index | 1, CACHEOP_R5900_IWBINV_D);
	}
}

/* Hit operations compare both ways, so their address must remain intact. */
#define EE_HIT_RANGE(name, op) \
void \
name(register_t address, vsize_t size) \
{ \
	vaddr_t va = (vaddr_t)address; \
	uint64_t lines = r5900_lines(va, size); \
	va &= ~(vaddr_t)(EE_LINE - 1); \
	for (; lines != 0; lines--, va += EE_LINE) \
		cache_op_r5900_line_64(va, op); \
}

EE_HIT_RANGE(r5900_pdcache_wbinv_range_64, CACHEOP_R5900_HWBINV_D)
EE_HIT_RANGE(r5900_pdcache_inv_range_64, CACHEOP_R5900_HINV_D)
EE_HIT_RANGE(r5900_pdcache_wb_range_64, CACHEOP_R5900_HWB_D)

void
r5900_icache_sync_all_64(void)
{
	/* Publish stored instructions before invalidating the instruction cache. */
	r5900_pdcache_wbinv_all_64();
	for (vaddr_t index = 0; index < EE_IWAY; index += EE_LINE) {
		vaddr_t va = MIPS_PHYS_TO_KSEG0(index);
		cache_op_r5900_line_64(va, CACHEOP_R5900_IINV_I);
		cache_op_r5900_line_64(va | 1, CACHEOP_R5900_IINV_I);
	}
}

void
r5900_icache_sync_range_64(register_t address, vsize_t size)
{
	vaddr_t va = (vaddr_t)address;
	uint64_t lines = r5900_lines(va, size);

	r5900_pdcache_wb_range_64(address, size);
	va &= ~(vaddr_t)(EE_LINE - 1);
	for (; lines != 0; lines--, va += EE_LINE)
		cache_op_r5900_line_64(va, CACHEOP_R5900_HINV_I);
}

void
r5900_icache_sync_range_index_64(vaddr_t va, vsize_t size)
{
	uint64_t lines = r5900_lines(va, size);
	if (lines >= EE_IWAY / EE_LINE) {
		r5900_icache_sync_all_64();
		return;
	}
	r5900_pdcache_wbinv_range_index_64(va, size);
	va &= ~(vaddr_t)(EE_LINE - 1);
	for (; lines != 0; lines--, va += EE_LINE) {
		vaddr_t index = MIPS_PHYS_TO_KSEG0(va & (EE_IWAY - 1));
		cache_op_r5900_line_64(index, CACHEOP_R5900_IINV_I);
		cache_op_r5900_line_64(index | 1, CACHEOP_R5900_IINV_I);
	}
}
