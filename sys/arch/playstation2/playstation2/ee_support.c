/* Single-CPU EE support. R5900 has no LL/SC instructions. */
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/intr.h>
#include <machine/intr.h>
#include <mips/locore.h>
#include <playstation2/playstation2/interrupt.h>

uint64_t _atomic_cas_64(volatile uint64_t *, uint64_t, uint64_t);

uint64_t
_atomic_cas_64(volatile uint64_t *ptr, uint64_t old, uint64_t new)
{
	int s = _intr_suspend();
	uint64_t value = *ptr;
	__asm volatile("sync.l" ::: "memory");
	if (value == old)
		*ptr = new;
	__asm volatile("sync.l" ::: "memory");
	_intr_resume(s);
	return value;
}
