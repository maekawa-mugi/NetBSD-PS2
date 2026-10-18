/* SPDX-License-Identifier: BSD-2-Clause */
/*
 * __atomic_test_and_set(): GCC emits a call to it for std::atomic_flag
 * (libstdc++) on targets without a native byte test-and-set.
 */
#include "atomic_op_namespace.h"

#include <sys/types.h>
#include <stdint.h>
#include <stdbool.h>

bool __atomic_test_and_set(volatile void *, int);

bool
__atomic_test_and_set(volatile void *ptr, int memorder)
{
	(void)memorder;
	return __sync_lock_test_and_set((volatile uint8_t *)ptr, 1) != 0;
}
