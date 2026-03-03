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
	ldr	x1, [x0, #GUEST_SP_EL1_OFFSET]
	msr	sp_el1, x1

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

	isb
	eret
