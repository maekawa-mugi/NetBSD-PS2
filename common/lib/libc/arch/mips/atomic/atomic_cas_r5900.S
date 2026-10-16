/* UP-only R5900 atomics: EE has no LL/SC. libc registers these RAS ranges. */
#include <sys/ras.h>
#include "atomic_op_asm.h"

	.text
	.set noat
	.set noreorder
LEAF(_atomic_cas_32)
RAS_START_ASM_HIDDEN(_r5900_cas32)
	lw v0,0(a0)
	bne v0,a1,1f
	 nop
	sw a2,0(a0)
RAS_END_ASM_HIDDEN(_r5900_cas32)
1:	jr ra
	 nop
END(_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_32,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_32_ni,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_32_ni,_atomic_cas_32)
STRONG_ALIAS(__sync_val_compare_and_swap_4,_atomic_cas_32)

LEAF(_atomic_cas_64)
RAS_START_ASM_HIDDEN(_r5900_cas64)
	ld v0,0(a0)
	bne v0,a1,1f
	 nop
	sd a2,0(a0)
RAS_END_ASM_HIDDEN(_r5900_cas64)
1:	jr ra
	 nop
END(_atomic_cas_64)
ATOMIC_OP_ALIAS(atomic_cas_64,_atomic_cas_64)
ATOMIC_OP_ALIAS(atomic_cas_64_ni,_atomic_cas_64)
STRONG_ALIAS(_atomic_cas_64_ni,_atomic_cas_64)
STRONG_ALIAS(__sync_val_compare_and_swap_8,_atomic_cas_64)

STRONG_ALIAS(_atomic_cas_uint,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_uint_ni,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_ulong,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_ulong_ni,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_ptr,_atomic_cas_32)
STRONG_ALIAS(_atomic_cas_ptr_ni,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_uint,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_uint_ni,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_ulong,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_ulong_ni,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_ptr,_atomic_cas_32)
ATOMIC_OP_ALIAS(atomic_cas_ptr_ni,_atomic_cas_32)
