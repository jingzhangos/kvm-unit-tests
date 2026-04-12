/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#define __ASSEMBLY__
#include <asm/asm-offsets.h>
#include <asm/sysreg.h>

.global guest_run
guest_run:
	/* x0 = struct guest pointer */

	/* Save Host Callee-Saved Regs */
	stp	x29, x30, [sp, #-16]!
	stp	x27, x28, [sp, #-16]!
	stp	x25, x26, [sp, #-16]!
	stp	x23, x24, [sp, #-16]!
	stp	x21, x22, [sp, #-16]!
	stp	x19, x20, [sp, #-16]!

	/* Cache Guest Pointer in TPIDR_EL2 */
	msr	tpidr_el2, x0

	/* Load Guest System Registers */
	ldr	x1, [x0, #GUEST_ELR_OFFSET]
	msr	elr_el2, x1
	ldr	x1, [x0, #GUEST_SPSR_OFFSET]
	msr	spsr_el2, x1
	ldr	x1, [x0, #GUEST_HCR_OFFSET]
	msr	hcr_el2, x1
	ldr	x1, [x0, #GUEST_VTTBR_OFFSET]
	msr	vttbr_el2, x1
	ldr	x1, [x0, #GUEST_SCTLR_OFFSET]
	msr_s	SYS_SCTLR_EL1, x1
	ldr	x1, [x0, #GUEST_VBAR_OFFSET]
	msr_s	SYS_VBAR_EL1, x1
	ldr	x1, [x0, #GUEST_SP_EL1_OFFSET]
	msr	sp_el1, x1
	ldr	x1, [x0, #GUEST_TPIDR_EL1_OFFSET]
	msr	tpidr_el1, x1

	/* Load Guest GPRs */
	ldp	x1, x2, [x0, #8]
	ldp	x3, x4, [x0, #24]
	ldp	x5, x6, [x0, #40]
	ldp	x7, x8, [x0, #56]
	ldp	x9, x10, [x0, #72]
	ldp	x11, x12, [x0, #88]
	ldp	x13, x14, [x0, #104]
	ldp	x15, x16, [x0, #120]
	ldp	x17, x18, [x0, #136]
	ldp	x19, x20, [x0, #152]
	ldp	x21, x22, [x0, #168]
	ldp	x23, x24, [x0, #184]
	ldp	x25, x26, [x0, #200]
	ldp	x27, x28, [x0, #216]
	ldp	x29, x30, [x0, #232]
	ldr	x0, [x0, #0]

	/* Install Trap Handler */
	adrp	x29, hyp_el2_vectors
	add	x29, x29, :lo12:hyp_el2_vectors
	msr	vbar_el2, x29

	/* Restore x29 from struct (via tpidr_el2) */
	mrs	x29, tpidr_el2
	ldr	x29, [x29, #232]

	isb
	eret

/* EL2 Hypervisor Vector Table */
.macro hyp_el2_vector, vector_num
	.align 7
	stp	x0, x1, [sp, #-16]!
	mrs	x0, tpidr_el2
	mov	x1, \vector_num
	str	x1, [x0, #GUEST_EXIT_CODE_OFFSET]
	b	guest_common_exit
.endm

	.align 11
hyp_el2_vectors:
	.skip 0x200

	hyp_el2_vector #4	/* ELxH_SYNC */
	hyp_el2_vector #5	/* ELxH_IRQ */
	hyp_el2_vector #6	/* ELxH_FIQ */
	hyp_el2_vector #7	/* ELxH_ERROR */

	hyp_el2_vector #8	/* ELx_LOW_SYNC_64 */
	hyp_el2_vector #9	/* ELx_LOW_IRQ_64 */
	hyp_el2_vector #10	/* ELx_LOW_FIQ_64 */
	hyp_el2_vector #11	/* ELx_LOW_ERROR_64 */

	.align 11

guest_common_exit:
	stp	x2, x3, [x0, #16]
	stp	x4, x5, [x0, #32]
	stp	x6, x7, [x0, #48]
	stp	x8, x9, [x0, #64]
	stp	x10, x11, [x0, #80]
	stp	x12, x13, [x0, #96]
	stp	x14, x15, [x0, #112]
	stp	x16, x17, [x0, #128]
	stp	x18, x19, [x0, #144]
	stp	x20, x21, [x0, #160]
	stp	x22, x23, [x0, #176]
	stp	x24, x25, [x0, #192]
	stp	x26, x27, [x0, #208]
	stp	x28, x29, [x0, #224]
	str	x30, [x0, #240]

	ldp	x2, x3, [sp], #16
	stp	x2, x3, [x0, #0]

	mrs	x1, elr_el2
	str	x1, [x0, #GUEST_ELR_OFFSET]
	mrs	x1, spsr_el2
	str	x1, [x0, #GUEST_SPSR_OFFSET]
	mrs	x1, esr_el2
	str	x1, [x0, #GUEST_ESR_OFFSET]
	mrs	x1, far_el2
	str	x1, [x0, #GUEST_FAR_OFFSET]
	mrs	x1, hpfar_el2
	str	x1, [x0, #GUEST_HPFAR_OFFSET]
	mrs	x1, sp_el1
	str	x1, [x0, #GUEST_SP_EL1_OFFSET]
	mrs_s	x1, SYS_VBAR_EL1
	str	x1, [x0, #GUEST_VBAR_OFFSET]

	/* x29 contains vector offset from entry */
	mov	x1, x29
	bl	guest_c_exception_handler
	cbz	x0, guest_resume_guest

	/* EXIT */
	/* Restore Host Callee-Saved Regs */
	ldp	x19, x20, [sp], #16
	ldp	x21, x22, [sp], #16
	ldp	x23, x24, [sp], #16
	ldp	x25, x26, [sp], #16
	ldp	x27, x28, [sp], #16
	ldp	x29, x30, [sp], #16
	ret

	/* RESUME */
guest_resume_guest:
	mrs	x0, tpidr_el2
	ldr	x1, [x0, #GUEST_ELR_OFFSET]
	msr	elr_el2, x1
	ldr	x1, [x0, #GUEST_SPSR_OFFSET]
	msr	spsr_el2, x1
	ldr	x1, [x0, #GUEST_SP_EL1_OFFSET]
	msr	sp_el1, x1

	ldp	x1, x2, [x0, #8]
	ldp	x3, x4, [x0, #24]
	ldp	x5, x6, [x0, #40]
	ldp	x7, x8, [x0, #56]
	ldp	x9, x10, [x0, #72]
	ldp	x11, x12, [x0, #88]
	ldp	x13, x14, [x0, #104]
	ldp	x15, x16, [x0, #120]
	ldp	x17, x18, [x0, #136]
	ldp	x19, x20, [x0, #152]
	ldp	x21, x22, [x0, #168]
	ldp	x23, x24, [x0, #184]
	ldp	x25, x26, [x0, #200]
	ldp	x27, x28, [x0, #216]
	ldp	x29, x30, [x0, #232]
	ldr	x0, [x0, #0]
	eret

/* EL1 Vector Table */
.macro guest_el1_vector, vector_num
	.align 7
	stp     x29, x30, [sp, #-16]!
	mov	x29, \vector_num
	b       guest_el1_common
.endm

.align 11
.global guest_el1_vectors
guest_el1_vectors:
	.skip 0x200

	guest_el1_vector #4	/* Sync */
	guest_el1_vector #5	/* IRQ */
	guest_el1_vector #6	/* FIQ */
	guest_el1_vector #7	/* SError */

	.align 11

guest_el1_common:
	sub	sp, sp, #264
	stp	x0, x1, [sp, #0]
	stp	x2, x3, [sp, #16]
	stp	x4, x5, [sp, #32]
	stp	x6, x7, [sp, #48]
	stp	x8, x9, [sp, #64]
	stp	x10, x11, [sp, #80]
	stp	x12, x13, [sp, #96]
	stp	x14, x15, [sp, #112]
	stp	x16, x17, [sp, #128]
	stp	x18, x19, [sp, #144]
	stp	x20, x21, [sp, #160]
	stp	x22, x23, [sp, #176]
	stp	x24, x25, [sp, #192]
	stp	x26, x27, [sp, #208]
	stp	x28, x30, [sp, #224]

	mrs	x0, elr_el1
	str	x0, [sp, #248]
	mrs	x0, spsr_el1
	str	x0, [sp, #256]

	mov	x0, sp
	mov	x1, x29
	bl	guest_el1_c_handler

	ldr	x0, [sp, #248]
	msr	elr_el1, x0
	ldr	x0, [sp, #256]
	msr	spsr_el1, x0

	ldp	x0, x1, [sp, #0]
	ldp	x2, x3, [sp, #16]
	ldp	x4, x5, [sp, #32]
	ldp	x6, x7, [sp, #48]
	ldp	x8, x9, [sp, #64]
	ldp	x10, x11, [sp, #80]
	ldp	x12, x13, [sp, #96]
	ldp	x14, x15, [sp, #112]
	ldp	x16, x17, [sp, #128]
	ldp	x18, x19, [sp, #144]
	ldp	x20, x21, [sp, #160]
	ldp	x22, x23, [sp, #176]
	ldp	x24, x25, [sp, #192]
	ldp	x26, x27, [sp, #208]
	ldp	x28, x30, [sp, #224]

	add	sp, sp, #264
	ldp	x29, x30, [sp], #16
	eret
