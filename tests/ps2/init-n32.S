/* Standalone NetBSD n32 init: no libc, dynamic loader, FPU, or MMI. */
	.set noreorder
	.set noat
	.text
	.globl _start
	.ent _start
_start:
	li $v0, 5                 /* open("/dev/console", O_WRONLY, 0) */
	la $a0, console
	li $a1, 1
	move $a2, $zero
	syscall
	bnez $a3, failed
	nop
	move $s0, $v0
	/* Exercise 64-bit scalar arithmetic as well as the n32 syscall ABI. */
	li $t0, 1
	dsll $t0, $t0, 40
	daddiu $t0, $t0, 7
	dsrl $t1, $t0, 40
	li $t2, 1
	bne $t1, $t2, failed
	nop
	li $v0, 4                 /* write(fd, message, length) */
	move $a0, $s0
	la $a1, message
	li $a2, message_end-message
	syscall
	bnez $a3, failed
	nop
	li $t0, message_end-message
	bne $v0, $t0, failed
	nop
	li $v0, 208               /* reboot(RB_HALT, NULL) */
	li $a0, 8
	move $a1, $zero
	syscall
failed:
	/* PID 1 must not exit; remain inspectable if a syscall fails. */
	b failed
	nop
	.end _start
	.section .rodata
console: .asciz "/dev/console"
message: .ascii "PS2 n32 init: userspace write and 64-bit arithmetic OK\n"
message_end:
	.section .note.netbsd.ident,"a",@note
	.p2align 2
	.long 7, 4, 1
	.asciz "NetBSD"
	.p2align 2
	.long 1199000000
	.section .note.GNU-stack,"",@progbits
