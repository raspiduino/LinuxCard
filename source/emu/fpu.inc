
.macro fpuRegNoS dst, instr						//also sets Z on regNo
	regNoD		\dst, \instr
.endm

.macro fpuRegNoT dst, instr						//also sets Z on regNo
	regNoT		\dst, \instr
.endm

.macro fpuRegNoD dst, instr
	getShamt	\dst, \instr
.endm

cop1_01000_bc1:
	
	getFcrPtr	t0
	ldr			t0, [t0]
	lsrs		t0, #0 + FPU_FCR_C_SHIFT - 16	//shift FCR.C into "tf" bit positio nin instr
	eors		t0, REG_INSTR					//see if they match
	lsrs		t0, #17							//CPSR.C now ZERO if we need to branch
	bcc			bc1_taken
	endCyNoBra	REG_INSTR

bc1_taken:
	relbra		t0, t1, REG_INSTR

cop1_00010_cfc:
	
	fpuRegNoS	t0, REG_INSTR
	beq			cfc_fir
	cmp			t0, #31
	bne			j_instr_invalid

cfc_fcr:
	getFcrPtr	t0
	ldr			t0, [t0]
	setRegT		t1, REG_INSTR, t0
	endCyNoBra	REG_INSTR

cfc_fir:
	loadImm		t0, FPU_FIR
	setRegT		t1, REG_INSTR, t0
	endCyNoBra	REG_INSTR

cop1_00110_ctc:
	fpuRegNoS	t0, REG_INSTR
	cmp			t0, #31
	bne			j_instr_invalid

ctc_fcr:
	getRegT		t1, REG_INSTR
	getFcrPtr	t0
	str			t1, [t0]
	endCyNoBra	REG_INSTR

cop1_00000_mfc:
	getFpuPtr	t0
	fpuRegNoS	t1, REG_INSTR
	lsls		t1, t1, #2
	ldr			t0, [t0, t1]
	setRegT		t1, REG_INSTR, t0
	endCyNoBra	REG_INSTR

cop1_00100_mtc:
	getRegT		t2, REG_INSTR
	getFpuPtr	t0
	fpuRegNoS	t1, REG_INSTR
	lsls		t1, t1, #2
	str			t2, [t0, t1]
	endCyNoBra	REG_INSTR

cop1_10100_fixed:
	getFpuPtr	t3
	fpuRegNoS	t1, REG_INSTR
	lsls		t1, t1, #2
	ldr			t0, [t3, t1]			//inut val in r0 for func call
	fpuRegNoD	t1, REG_INSTR
	lsls		t1, t1, #2
	adds		p1, t3, t1				//dst ptr
	bfx			t1, REG_INSTR, 0, 6
	cmp			t1, #32
	beq			cop1_cvt_s_fixed
	cmp			t1, #33
	bne			j_instr_invalid

cop1_cvt_d_fixed:
	bl			__aeabi_i2d
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_cvt_s_fixed:
	
	bl			__aeabi_i2f
	str			r0, [p1]
	endCyNoBra	REG_INSTR

j_instr_invalid:
	bl			instr_invalid

cop1_unsupported_trapping:
	loadImm		t2, FCR_UNIMPL
	orrs		t0, t2
	str			t0, [t3]
	movs		r0, #(CP0_EXC_COD_FPE << CP0_CAUSE_EXC_COD_SHIFT)
	bl			cpuPrvTakeException

instr_010001_cop1:
	copCheck	t1, t2, #1
		
	//clear per-op flags, bail if trapping of a type we do not support is requested
	getFcrPtr	t3
	ldr			t2, =FCR_PEROP_FLAGS
	ldr			t0, [t3]
	bics		t0, t2
	str			t0, [t3]
	loadImm		t2, (FCR_INVAL_OP | FCR_CEF_OVERFLOW | FCR_CEF_UDERFLOW | FCR_CEF_INEXACT) << FCR_SHIFT_ENABLES
	ands		t0, t2
	bne			cop1_unsupported_trapping
	
	bfx			t0, REG_INSTR, 20, 6
	add			pc, t0
	nop
	b			cop1_00000_mfc
	b			j_instr_invalid
	b			cop1_00010_cfc
	b			j_instr_invalid
	b			cop1_00100_mtc
	b			j_instr_invalid
	b			cop1_00110_ctc
	b			j_instr_invalid
	b			cop1_01000_bc1
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_10000_float
	b			cop1_10001_double
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_10100_fixed
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid


cop1_10000_float:

	getFpuPtr	t3
	fpuRegNoS	t0, REG_INSTR
	lsls		t0, t0, #2
	fpuRegNoT	t1, REG_INSTR
	lsls		t1, t1, #2
	fpuRegNoD	t2, REG_INSTR
	lsls		t2, t2, #2
	adds		t0, t0, t3	//t0 = &regS
	adds		t1, t1, t3	//t1 = &regT
	adds		p1, t2, t3	//p1 = &regD
	bfx			t3, REG_INSTR, -1, 7
	add			t3, pc
	nop
	b			cop1_add_float
	b			cop1_sub_float
	b			cop1_mul_float
	b			cop1_div_float
	b			j_instr_invalid
	b			cop1_abs_float
	b			cop1_mov_float
	b			cop1_neg_float
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
#ifdef IMPLEMENT_FPU_R4000
	b			cop1_round_w_float
	b			cop1_trunc_w_float
	b			cop1_ceil_w_float
	b			cop1_floor_w_float
#else
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
#endif
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_cvt_d_float
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_cvt_w_float
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_c_f_float
	b			cop1_c_un_float
	b			cop1_c_eq_float
	b			cop1_c_ueq_float
	b			cop1_c_olt_float
	b			cop1_c_ult_float
	b			cop1_c_ole_float
	b			cop1_c_ule_float
	b			cop1_c_sf_float
	b			cop1_c_ngle_float
	b			cop1_c_seq_float
	b			cop1_c_ngl_float
	b			cop1_c_lt_float
	b			cop1_c_nge_float
	b			cop1_c_le_float
	b			cop1_c_ngt_float
	
.ltorg

cop1_add_float:
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_fadd
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_sub_float:
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_fsub
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_mul_float:
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_fmul
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_div_float:
	ldr			r1, [t1]
	lsls		t2, r1, #1
	beq			div0_float

div0_continue_float:
	ldr			r0, [t0]
	bl			__aeabi_fdiv
	str			r0, [p1]
	endCyNoBra	REG_INSTR

div0_float:
	getFcrPtr	t3
	ldr			t2, =(FCR_CEF_DIV0 << FCR_SHIFT_CAUSE) | (FCR_CEF_DIV0 << FCR_SHIFT_FLAGS)
	ldr			REG_INSTR, [t3]
	orrs		REG_INSTR, t2
	str			REG_INSTR, [t3]
	lsrs		REG_INSTR, REG_INSTR, #1 + FCR_CEF_DIV0 + FCR_SHIFT_ENABLES
	bcc			div0_continue_float

div0_exc_float:
	movs		r0, #(CP0_EXC_COD_FPE << CP0_CAUSE_EXC_COD_SHIFT)
	bl			cpuPrvTakeException

cop1_abs_float:
	ldr			r0, [t0]
	bl			fabsf
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_mov_float:
	ldr			r0, [t0]
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_neg_float:
	ldr			r0, [t0]
	bl			__aeabi_fneg
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_round_w_float:
	ldr			r0, [t0]
	bl			roundf
	bl			__aeabi_f2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_trunc_w_float:
	ldr			r0, [t0]
	bl			__aeabi_f2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_ceil_w_float:
	ldr			r0, [t0]
	bl			ceilf
	bl			__aeabi_f2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_floor_w_float:
	ldr			r0, [t0]
	bl			floorf
	bl			__aeabi_f2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_cvt_d_float:
	ldr			r0, [t0]
	bl			__aeabi_f2d
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_cvt_w_float:
	getFcrPtr	t3
	bfx			t3, REG_INSTR, -1, 3
	add			pc, t3
	nop
	b			cop1_round_w_float
	b			cop1_trunc_w_float
	b			cop1_ceil_w_float
	b			cop1_floor_w_float

cop1_c_true_float:
	movs		t0, #1

cop1_c_set_float:
	movs		t1, #1
	lsls		t0, t0, #FPU_FCR_C_SHIFT
	lsls		t1, t1, #FPU_FCR_C_SHIFT
	getFcrPtr	t3
	ldr			t2, [t3]
	bics		t2, t1
	orrs		t2, t0
	str			t2, [t3]
	endCyNoBra	REG_INSTR

cop1_c_f_float:
cop1_c_sf_float:		//we do not support tappng here, so this is the SAME as "f"
cop1_c_false_float:
	movs		t0, #0
	b			cop1_c_set_float

cop1_c_un_float:
cop1_c_ngle_float:		//we do not support tappng here, so this is the SAME as "un"
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_fcmpun
	b			cop1_c_set_float

cop1_c_eq_float:
cop1_c_seq_float:		//we do not support tappng here, so this is the SAME as "eq"
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_fcmpeq
	b			cop1_c_set_float

cop1_c_ueq_float:
cop1_c_ngl_float:		//we do not support tappng here, so this is the SAME as "ueq"
	ldr			r0, [t0]
	ldr			r1, [t1]
	push		{r0, r1}
	bl			__aeabi_fcmpun
	cmp			r0, #0
	pop			{r0, r1}
	bne			cop1_c_true_float
	bl			__aeabi_fcmpeq
	b			cop1_c_set_float

cop1_c_olt_float:
cop1_c_lt_float:		//we do not support tappng here, so this is the SAME as "olt"
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_cdcmple
	bcc			cop1_c_true_float
	b			cop1_c_false_float

cop1_c_ult_float:
cop1_c_nge_float:		//we do not support tappng here, so this is the SAME as "ult"
	ldr			r0, [t0]
	ldr			r1, [t1]
	push		{r0, r1}
	bl			__aeabi_fcmpun
	cmp			r0, #0
	pop			{r0, r1}
	bne			cop1_c_true_float
	bl			__aeabi_fcmplt
	b			cop1_c_set_float

cop1_c_ole_float:
cop1_c_le_float:		//we do not support tappng here, so this is the SAME as "ole"
	ldr			r0, [t0]
	ldr			r1, [t1]
	bl			__aeabi_cdcmple
	beq			cop1_c_true_float
	bcc			cop1_c_true_float
	b			cop1_c_false_float

cop1_c_ule_float:				//le or unordered  -> not ordered & gt
cop1_c_ngt_float:		//we do not support tappng here, so this is the SAME as "ule"
	ldr			r1, [t0]
	ldr			r0, [t1]
	bl			__aeabi_cdcmple
	bcc			cop1_c_true_float
	b			cop1_c_false_float

.ltorg

cop1_10001_double:
	getFpuPtr	t3
	fpuRegNoS	t0, REG_INSTR
	lsls		t0, t0, #2
	fpuRegNoT	t1, REG_INSTR
	lsls		t1, t1, #2
	fpuRegNoD	t2, REG_INSTR
	lsls		t2, t2, #2
	adds		t0, t0, t3	//t0 = &regS
	adds		t1, t1, t3	//t1 = &regT
	adds		p1, t2, t3	//p1 = &regD
	bfx			t3, REG_INSTR, -1, 7
	add			t3, pc
	nop
	b			cop1_add_double
	b			cop1_sub_double
	b			cop1_mul_double
	b			cop1_div_double
	b			j_instr_invalid
	b			cop1_abs_double
	b			cop1_mov_double
	b			cop1_neg_double
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
#ifdef IMPLEMENT_FPU_R4000
	b			cop1_round_w_double
	b			cop1_trunc_w_double
	b			cop1_ceil_w_double
	b			cop1_floor_w_double
#else
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
#endif
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_cvt_s_double
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_cvt_w_double
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			j_instr_invalid
	b			cop1_c_f_double
	b			cop1_c_un_double
	b			cop1_c_eq_double
	b			cop1_c_ueq_double
	b			cop1_c_olt_double
	b			cop1_c_ult_double
	b			cop1_c_ole_double
	b			cop1_c_ule_double
	b			cop1_c_sf_double
	b			cop1_c_ngle_double
	b			cop1_c_seq_double
	b			cop1_c_ngl_double
	b			cop1_c_lt_double
	b			cop1_c_nge_double
	b			cop1_c_le_double
	b			cop1_c_ngt_double
	
.ltorg

cop1_add_double:
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_dadd
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_sub_double:
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_dsub
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_mul_double:
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_dmul
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_div_double:
	ldr			r2, [r1, #4]
	lsls		t2, r1, #1
	bne			div0_continue_double	//not zero
	ldr			r2, [r1, #0]
	cmp			r2, #0
	beq			div0_double

div0_continue_double:
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_ddiv
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

div0_double:
	getFcrPtr	t3
	ldr			t2, =(FCR_CEF_DIV0 << FCR_SHIFT_CAUSE) | (FCR_CEF_DIV0 << FCR_SHIFT_FLAGS)
	ldr			REG_INSTR, [t3]
	orrs		REG_INSTR, t2
	str			REG_INSTR, [t3]
	lsrs		REG_INSTR, REG_INSTR, #1 + FCR_CEF_DIV0 + FCR_SHIFT_ENABLES
	bcc			div0_continue_double

div0_exc_double:
	movs		r0, #(CP0_EXC_COD_FPE << CP0_CAUSE_EXC_COD_SHIFT)
	bl			cpuPrvTakeException

cop1_abs_double:
	ldmia		t0, {r0, r1}
	bl			fabs
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_mov_double:
	ldmia		t0, {r0, r1}
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_neg_double:
	ldmia		t0, {r0, r1}
	bl			__aeabi_dneg
	stmia		p1!, {r0, r1}
	endCyNoBra	REG_INSTR

cop1_round_w_double:
	ldmia		t0, {r0, r1}
	bl			round
	bl			__aeabi_d2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_trunc_w_double:
	ldmia		t0, {r0, r1}
	bl			__aeabi_d2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_ceil_w_double:
	ldmia		t0, {r0, r1}
	bl			ceil
	bl			__aeabi_d2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_floor_w_double:
	ldmia		t0, {r0, r1}
	bl			floor
	bl			__aeabi_d2iz
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_cvt_s_double:
	ldmia		t0, {r0, r1}
	bl			__aeabi_d2f
	str			r0, [p1]
	endCyNoBra	REG_INSTR

cop1_cvt_w_double:
	getFcrPtr	t3
	bfx			t3, REG_INSTR, -1, 3
	add			pc, t3
	nop
	b			cop1_round_w_double
	b			cop1_trunc_w_double
	b			cop1_ceil_w_double
	b			cop1_floor_w_double

cop1_c_true_double:
	movs		t0, #1

cop1_c_set_double:
	movs		t1, #1
	lsls		t0, t0, #FPU_FCR_C_SHIFT
	lsls		t1, t1, #FPU_FCR_C_SHIFT
	getFcrPtr	t3
	ldr			t2, [t3]
	bics		t2, t1
	orrs		t2, t0
	str			t2, [t3]
	endCyNoBra	REG_INSTR

cop1_c_f_double:
cop1_c_sf_double:		//we do not support tappng here, so this is the SAME as "f"
cop1_c_false_double:
	movs		t0, #0
	b			cop1_c_set_double

cop1_c_un_double:
cop1_c_ngle_double:		//we do not support tappng here, so this is the SAME as "un"
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_fcmpun
	b			cop1_c_set_double

cop1_c_eq_double:
cop1_c_seq_double:		//we do not support tappng here, so this is the SAME as "eq"
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_fcmpeq
	b			cop1_c_set_double

cop1_c_ueq_double:
cop1_c_ngl_double:		//we do not support tappng here, so this is the SAME as "ueq"
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	push		{r0, r1, r2, r3}
	bl			__aeabi_fcmpun
	cmp			r0, #0
	pop			{r0, r1, r2, r3}
	bne			cop1_c_true_double
	bl			__aeabi_fcmpeq
	b			cop1_c_set_double

cop1_c_olt_double:
cop1_c_lt_double:		//we do not support tappng here, so this is the SAME as "olt"
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_cdcmple
	bcc			cop1_c_true_double
	b			cop1_c_false_double

cop1_c_ult_double:
cop1_c_nge_double:		//we do not support tappng here, so this is the SAME as "ult"
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	push		{r0, r1, r2, r3}
	bl			__aeabi_fcmpun
	cmp			r0, #0
	pop			{r0, r1, r2, r3}
	bne			cop1_c_true_double
	bl			__aeabi_fcmplt
	b			cop1_c_set_double

cop1_c_ole_double:
cop1_c_le_double:		//we do not support tappng here, so this is the SAME as "ole"
	ldmia		t1!, {r2, r3}
	ldmia		t0, {r0, r1}
	bl			__aeabi_cdcmple
	beq			cop1_c_true_double
	bcc			cop1_c_true_double
	b			cop1_c_false_double

cop1_c_ule_double:				//le or unordered  -> not ordered & gt
cop1_c_ngt_double:		//we do not support tappng here, so this is the SAME as "ule"
	ldmia		t0!, {r2, r3}
	ldmia		t1, {r0, r1}
	bl			__aeabi_cdcmple
	bcc			cop1_c_true_double
	b			cop1_c_false_double

.ltorg

















instr_110001_lwc1:
	copCheck	t1, t2, #1
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t3, t1, t2
	memRdW		t1, t0, t2, t3, 10
	
	regNoT		t1, REG_INSTR
	lsls		t1, t1, #2
	getFpuPtr	t3
	str			t2, [t3, t1]
	endCyNoBra	REG_INSTR


memMiscHandlers 10, t3, 0	//for lwc & ldc

instr_110101_ldc1:
	copCheck	t1, t2, #1
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t3, t1, t2
	memRdW		t1, t0, p1, t3, 10		//p1 is output

	adds		t3, #4
	memRdW		t1, t0, t2, t3, 11		//t2 is output
	
	regNoT		t1, REG_INSTR
	lsls		t1, t1, #2
	getFpuPtr	t3
	adds		t3, t1
	str			p1, [t3, #0]
	str			t2, [t3, #4]
	endCyNoBra	REG_INSTR

memMiscHandlers 11, t3, 0	//for ldc

memMiscHandlers 10, t2, 1	//for sdc
memIscHandler	10

instr_111101_sdc1:
	copCheck	t1, t2, #1
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t2, t1
	regNoT		t1, REG_INSTR
	lsls		t1, t1, #2
	getFpuPtr	t3
	adds		p1, t1, t3
	ldmia		p1, {t1, p1}
	movs		REG_INSTR, t2
	memWrW		t3, t0, t1, t2, 10		//XXX maybe these can be rolled together, then again, FPU is slow enough that who cares?
	adds		REG_INSTR, #4
	memWrW		t3, t0, p1, REG_INSTR, 11
	endCyNoBra	REG_INSTR


memMiscHandlers 11, t2, 1	//for sdc & swc
memIscHandler	11

instr_111001_swc1:
	copCheck	t1, t2, #1
	getRegS		t1, REG_INSTR
	sxth		t2, REG_INSTR
	adds		t2, t1
	regNoT		t1, REG_INSTR
	lsls		t1, t1, #2
	getFpuPtr	t3
	ldr			t1, [t1, t3]
	memWrW		t3, t0, t1, t2, 11
	endCyNoBra	REG_INSTR


	