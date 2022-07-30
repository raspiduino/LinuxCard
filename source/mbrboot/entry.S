.set noreorder
.set noat

//this code carefully written to be position independent
//we need to find the partition with type "bb" and load it to ram at 0x80001000, then run it
//we are somewhere in RAM, but we ARE the MBR so part table is here already

#define RAM_ADDR 	0x80001000

#include "../hypercall.h"

.globl entry
.section .text
entry:
	bal   get_self
	nop
	li    $t0, 4
	addiu $s0, $v0, 0x1be
	
partloop:
	lbu   $a0, 4($s0)		//part type
	addiu $a0, -0xbb
	beqz  $a0, found
	
	addiu $t0, -1
	bnez  $t0, partloop
	addiu $s0, $s0, 16

fail_part_find:
	bal   get_self
	nop
	addiu $s0, $v0, fail_part_find_msg - entry
	b     puts_s0
	nop

fail_read:
	bal   get_self
	nop
	addiu $s0, $v0, fail_read_msg - entry
	b     puts_s0
	nop

puts_s0:
	lbu   $a0, 0($s0)
	beqz  $a0, puts_s0
	nop
	addiu $s0, $s0, 1
	li    $at, H_CONSOLE_WRITE
	b     puts_s0
	.word HYPERCALL

found:
	lwl   $s2, 15($s0)	//part len
	lwr   $s2, 12($s0)
	lwl   $s1, 11($s0)	//part start
	beqz  $s2, fail_part_find
	lwr   $s1, 8($s0)
	
	la    $s3, RAM_ADDR & 0x1fffffff
	la    $s4, RAM_ADDR
	
read_loop:
	move  $a0, $s1
	move  $a1, $s3
	bal   readblock
	addiu $s2, -1
	beqz  $v0, fail_read
	addiu $s1, $s1, 1
	
	bnez  $s2, read_loop
	addiu $s3, $s3, 512

boot:
	jr    $s4
	nop

readblock:		//(a0 = block number, a1 = destination PA)
	li    $at, H_STOR_READ
	jr    $ra
	.word HYPERCALL

get_self:
	move  $a0, $ra
	bal   jmplbl
	nop
jmplbl:
	jr    $a0
	addiu $v0, $ra, entry - jmplbl
	

fail_read_msg:
	.asciiz "Failed to bootload"

fail_part_find_msg:
	.asciiz "Failed to find a valid bootable partition"
	
	
	
