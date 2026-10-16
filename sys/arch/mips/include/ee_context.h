/* R5900 integer context helpers. Included only by locore assembly. */
#ifndef _MIPS_EE_CONTEXT_H_
#define _MIPS_EE_CONTEXT_H_

#ifdef MIPS3_5900
/*
 * Capture before any non-reserved GPR is changed. v0 is scratch only after
 * its full value is stored, and is restored before the scalar save code.
 * k0/k1 belong to the exception machinery, not to the user ABI.
 */
.macro EE_SAVE base, off
 .set push
 .set arch=r5900
 .irp r,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,29,30,31
 sq $\r, \off+TF_EE_GPR+16*\r(\base)
 .endr
 sq zero, \off+TF_EE_GPR+16*26(\base)
 sq zero, \off+TF_EE_GPR+16*27(\base)
 mfhi1 v0
 sd v0, \off+TF_EE_HI1(\base)
 mflo1 v0
 sd v0, \off+TF_EE_LO1(\base)
 mfsa v0
 sd v0, \off+TF_EE_SA(\base)
 sd zero, \off+TF_EE_SA+8(\base)
 lq v0, \off+TF_EE_GPR+16*2(\base)
 .set pop
.endm

/*
 * Merge upper halves after scalar restoration: syscall results, PC changes,
 * signal arguments, etc. in the existing low-half slots remain authoritative.
 * base must be sp or k1; k0 is scratch. PCPYLD leaves the low half unchanged.
 * No SA instruction occurs in the three instructions preceding MTSA.
 */
.macro EE_RESTORE base, off
 .set push
 .set arch=r5900
 ld k0, \off+TF_EE_SA(\base)
 nop
 nop
 nop
 mtsa k0
 ld k0, \off+TF_EE_HI1(\base)
 mthi1 k0
 ld k0, \off+TF_EE_LO1(\base)
 mtlo1 k0
 .irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,29,30,31
 ld k0, \off+TF_EE_GPR+16*\r+8(\base)
 pcpyld $\r, k0, $\r
 .endr
 .set pop
.endm
#else
.macro EE_SAVE base, off
.endm
.macro EE_RESTORE base, off
.endm
#endif
#endif
