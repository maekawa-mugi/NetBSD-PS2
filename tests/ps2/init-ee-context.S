/* Standalone n32 integer-context integration test; no libc or FPU. */
	.set noreorder
	.set noat
	.data
	.p2align 4
data:
fd: .word 0
pid: .word 0
childpid: .word 0
waitstatus: .word -1
signals: .word 0
	.p2align 3
delay: .dword 0
	.word 50000000,0
action: .word handler,0,0,0,0,0x40
	.p2align 3
patterns:
	.irp r,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
	.dword 0x1020304050607000+\r
	.endr
special:
	.dword 0x1122334455667788,0x8877665544332211
	.dword 0x123456789abcdef0,0xfedcba9876543210
sa_expected: .dword 0
special_seen: .space 32
sa_seen: .dword 0
	.p2align 4
observed: .space 512
.equ O_childpid, childpid-data
.equ O_fd, fd-data
.equ O_observed, observed-data
.equ O_patterns, patterns-data
.equ O_pid, pid-data
.equ O_sa_expected, sa_expected-data
.equ O_sa_seen, sa_seen-data
.equ O_signals, signals-data
.equ O_special, special-data
.equ O_special_seen, special_seen-data
.equ O_waitstatus, waitstatus-data
	.text
	.globl _start
	.ent _start
_start:
	li $v0,5
	la $a0,console
	li $a1,1
	move $a2,$zero
	syscall
	bnez $a3,failed
	nop
	la $t9,data
	sw $v0,%lo(O_fd)($t9)
	/* Register version-2 siginfo handlers for SIGUSR1 and BREAK/SIGTRAP. */
	li $a0,30
	jal install
	nop
	li $a0,5
	jal install
	nop
	jal seed
	nop
	li $v0,20
	syscall
	bnez $a3,failed
	nop
	sw $v0,%lo(O_pid)($t9)
	jal check
	nop
	/* Spend time in user mode with no syscalls, allowing timer interrupts. */
	li $t0,5000000
1:	addiu $t0,$t0,-1
	bnez $t0,1b
	nop
	jal check
	nop
	jal sleep
	nop
	jal check
	nop
	/* A synchronous user exception: handler advances EPC past BREAK. */
	break 0
	jal check
	nop
	lw $a0,%lo(O_pid)($t9)
	li $a1,30
	li $v0,37
	syscall
	bnez $a3,failed
	nop
	jal check
	nop
	lw $t0,%lo(O_signals)($t9)
	li $t1,2
	bne $t0,$t1,failed
	nop
	li $v0,2                 /* fork: parent and child have identical EE state */
	syscall
	bnez $a3,failed
	nop
	bnez $v1,child
	nop
	sw $v0,%lo(O_childpid)($t9)
	jal check
	nop
	lw $a0,%lo(O_childpid)($t9)
	la $a1,waitstatus
	move $a2,$zero
	move $a3,$zero
	li $v0,449               /* __wait450 */
	syscall
	bnez $a3,failed
	nop
	jal check
	nop
	lw $t0,%lo(O_waitstatus)($t9)
	bnez $t0,failed
	nop
	la $a1,success
	li $a2,success_end-success
	jal print
	nop
	li $v0,208
	li $a0,8
	move $a1,$zero
	syscall
failed:
	la $t9,data
	la $a1,failure
	li $a2,failure_end-failure
	jal print
	nop
2:	b 2b
	nop
child:
	jal check
	nop
	/* Different live state while the parent blocks in wait. */
	jal clobber
	nop
	jal sleep
	nop
	li $v0,1
	move $a0,$zero
	syscall
	b failed
	nop
	.end _start

install:
	li $v0,340
	la $a1,action
	move $a2,$zero
	la $a3,trampoline
	li $8,2                 /* n32 a4: trampoline version */
	syscall
	bnez $a3,failed
	nop
	jr $ra
	nop
sleep:
	la $a0,delay
	move $a1,$zero
	li $v0,430
	syscall
	bnez $a3,failed
	nop
	jr $ra
	nop
print:
	lw $a0,%lo(O_fd)($t9)
	li $v0,4
	syscall
	jr $ra
	nop

/* Seed all non-reserved upper halves, preserving valid stack/return pointers. */
seed:
	la $t9,data
	.irp r,1,2,3,4,5,6,7,8,9,10,11,13,14,15,16,17,18,19,20,21,22,23,24,25,28,29,30,31
	ld $t0,%lo(O_patterns+8*\r)($t9)
	pcpyld $\r,$t0,$\r
	.endr
	ld $t0,%lo(O_special+0)($t9)
	mthi $t0
	ld $t0,%lo(O_special+8)($t9)
	mtlo $t0
	ld $t0,%lo(O_special+16)($t9)
	mthi1 $t0
	ld $t0,%lo(O_special+24)($t9)
	mtlo1 $t0
	li $t0,11
	mtsab $t0,0
	mfsa $t0
	sd $t0,%lo(O_sa_expected)($t9)
	ld $t0,%lo(O_patterns+8*12)($t9)
	pcpyld $t0,$t0,$t0
	jr $ra
	nop

/* Snapshot before using scratch registers, then compare every upper half. */
check:
	.irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,29,30,31
	sq $\r,%lo(O_observed+16*\r)($t9)
	.endr
	mfhi $t0
	sd $t0,%lo(O_special_seen+0)($t9)
	mflo $t0
	sd $t0,%lo(O_special_seen+8)($t9)
	mfhi1 $t0
	sd $t0,%lo(O_special_seen+16)($t9)
	mflo1 $t0
	sd $t0,%lo(O_special_seen+24)($t9)
	mfsa $t0
	sd $t0,%lo(O_sa_seen)($t9)
	.irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,29,30,31
	ld $t0,%lo(O_observed+16*\r+8)($t9)
	ld $t1,%lo(O_patterns+8*\r)($t9)
	bne $t0,$t1,failed
	li $t2,\r               /* failure diagnostics: register number */
	.endr
	.irp n,0,8,16,24
	ld $t0,%lo(O_special_seen+\n)($t9)
	ld $t1,%lo(O_special+\n)($t9)
	bne $t0,$t1,failed
	li $t2,100+\n
	.endr
	ld $t0,%lo(O_sa_seen)($t9)
	ld $t1,%lo(O_sa_expected)($t9)
	bne $t0,$t1,failed
	li $t2,200
	jr $ra
	nop

handler:
	la $t9,data
	lw $t0,%lo(O_signals)($t9)
	addiu $t0,$t0,1
	sw $t0,%lo(O_signals)($t9)
	li $t0,5
	bne $a0,$t0,clobber
	nop
	/* ucontext.uc_mcontext.__gregs[EPC], checked by layout.c. */
	ld $t0,320($a2)
	addiu $t0,$t0,4
	sd $t0,320($a2)
clobber:
	.irp r,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,28,29,30,31
	pcpyld $\r,$zero,$\r
	.endr
	mthi $zero
	mtlo $zero
	mthi1 $zero
	mtlo1 $zero
	mtsab $zero,0
	jr $ra
	nop
trampoline:
	addiu $a0,$sp,128        /* sizeof(siginfo_t), checked by layout.c */
	li $v0,308               /* setcontext restores the signal frame */
	syscall
	b failed
	nop

	.section .rodata
console: .asciz "/dev/console"
success: .ascii "PS2 EE context: GPR128 HI/LO HI1/LO1 SA syscall timer sleep signal fork/wait OK\n"
success_end:
failure: .ascii "PS2 EE context: FAILED (inspect t0/t1/t2 and observed)\n"
failure_end:
	.section .note.netbsd.ident,"a",@note
	.p2align 2
	.long 7,4,1
	.asciz "NetBSD"
	.p2align 2
	.long 1199000000
	.section .note.GNU-stack,"",@progbits
