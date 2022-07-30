.globl stack_start
.globl start
.globl __bss_start
.globl __bss_end
.set noreorder
.set noat


#include "../hypercall.h"

.globl entry
.section .vecs
entry:
	la    $sp, stack_start	//setup SP
	mfc0  $t0, $12		//12 = status
	ori   $t0, $t0, 3	//ints off, kernel mode
	xori  $t0, $t0, 3
	mtc0  $t0, $12		//12 = status
	la    $t0, __bss_start
	la    $t1, __bss_end
1:
	beq   $t0, $t1, start
	addiu $t0, 4
	bal   1b
	sw    $zero, -4($t0)


.section .text
.globl getStoreSz
getStoreSz:
	li    $at, H_STOR_GET_SZ
	jr    $ra
	.word HYPERCALL

.globl getMemMap
getMemMap:
	li    $at, H_GET_MEM_MAP
	jr    $ra
	.word HYPERCALL

.globl consoleWrite
consoleWrite:
	li    $at, H_CONSOLE_WRITE
	jr    $ra
	.word HYPERCALL

.globl writeblock
writeblock:		//(a0 = block number, a1 = source PA)
	li    $at, H_STOR_WRITE
	jr    $ra
	.word HYPERCALL
	

.globl readblock
readblock:		//(a0 = block number, a1 = destination PA)
	li    $at, H_STOR_READ
	jr    $ra
	.word HYPERCALL


.balign 32

.globl memcpy
memcpy:		//(a0 = dst, a1 = src, a2 = len)
	beqz  $a2, 1f
	move  $v0, $a0
	
	or    $v1, $a0, $a1
	andi  $v1, $v1, 3
	bnez  $v1, byteloop

	srl   $v1, $a2, 2
	andi  $a2, $a2, 3

.balign 32	//should be a single nop

wordloop:
	beqz  $v1, byteloop
	addiu $v1, $v1, -1
	lw    $a3, 0($a1)
	sw    $a3, 0($a0)
	addiu $a1, $a1, 4
	b     wordloop
	addiu $a0, $a0, 4

byteloop:
	beqz  $a2, 1f
	addiu $a2, $a2, -1
	lbu   $a3, 0($a1)
	sb    $a3, 0($a0)
	addiu $a1, $a1, 1
	b     byteloop
	addiu $a0, $a0, 1
1:
	jr    $ra
	nop

.globl strcmp
strcmp:		//(a0 = str1, a1 = str2)
	lbu   $a2, 0($a0)
	beqz  $a2, 1f
	lbu   $a3, 0($a1)
	beqz  $a3, 1f
	addiu $a0, $a0, 1
	bne   $a2, $a3, 1f
	addiu $a1, $a1, 1
	b     strcmp
	nop
	
1:
	jr    $ra
	sub   $v0, $a2, $a3

.globl memset
memset:		//a0 = dst, a1 = byte, a2 = len
	move  $v0, $a0
2:
	beqz  $a2, 1f
	addiu $a2, $a2, -1
	sb    $a1, 0($a0)
	b     2b
	addiu $a0, $a0, 1
1:
	jr    $ra
	nop
	
	
