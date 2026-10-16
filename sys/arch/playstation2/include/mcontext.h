/*	$NetBSD: mcontext.h,v 1.4 2014/03/31 11:25:49 martin Exp $	*/

/* PS2 n32 has additional integer state in signal/getcontext frames. */
#if defined(__mips_n32)
#define _MIPS_EE_CONTEXT 1
#endif
#include <mips/mcontext.h>
