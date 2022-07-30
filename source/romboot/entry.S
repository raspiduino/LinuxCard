.set noreorder
.set noat

//this code carefully written to be position independent

#define RAM_ADDR 	0x80000000

#include "../hypercall.h"

.globl entry
.section .text
entry:
	
	la    $a1, RAM_ADDR & 0x1fffffff
	li    $a0, 0
	li    $at, H_STOR_READ
	.word HYPERCALL
fail_loop:
	beqz  $v0, fail_loop
	li    $s0, RAM_ADDR	//in delay slot, so what...
	jr    $s0


	
	
	
