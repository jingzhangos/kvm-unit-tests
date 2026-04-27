/*
 * Adapted from arch/arm64/kernel/asm-offsets.c
 *
 * Copyright (C) 2017, Red Hat Inc, Andrew Jones <drjones@redhat.com>
 *
 * This work is licensed under the terms of the GNU GPL, version 2.
 */
#include <libcflat.h>
#include <kbuild.h>
#include <asm/ptrace.h>
#include <asm/guest.h>

int main(void)
{
	OFFSET(S_X0, pt_regs, regs[0]);
	OFFSET(S_X1, pt_regs, regs[1]);
	OFFSET(S_X2, pt_regs, regs[2]);
	OFFSET(S_X3, pt_regs, regs[3]);
	OFFSET(S_X4, pt_regs, regs[4]);
	OFFSET(S_X5, pt_regs, regs[5]);
	OFFSET(S_X6, pt_regs, regs[6]);
	OFFSET(S_X7, pt_regs, regs[7]);
	OFFSET(S_LR, pt_regs, regs[30]);
	OFFSET(S_SP, pt_regs, sp);
	OFFSET(S_PC, pt_regs, pc);
	OFFSET(S_PSTATE, pt_regs, pstate);
	OFFSET(S_ORIG_X0, pt_regs, orig_x0);
	OFFSET(S_SYSCALLNO, pt_regs, syscallno);

	/* FP and LR (16 bytes) go on the frame above pt_regs */
	DEFINE(S_FP, sizeof(struct pt_regs));
	DEFINE(S_FRAME_SIZE, (sizeof(struct pt_regs) + 16));

	OFFSET(GUEST_X_OFFSET, guest, x);
	OFFSET(GUEST_ELR_OFFSET, guest, elr_el2);
	OFFSET(GUEST_SPSR_OFFSET, guest, spsr_el2);
	OFFSET(GUEST_HCR_OFFSET, guest, hcr_el2);
	OFFSET(GUEST_VTTBR_OFFSET, guest, vttbr_el2);
	OFFSET(GUEST_SCTLR_OFFSET, guest, sctlr_el1);
	OFFSET(GUEST_VBAR_OFFSET, guest, vbar_el1);
	OFFSET(GUEST_SP_EL1_OFFSET, guest, sp_el1);
	OFFSET(GUEST_ESR_OFFSET, guest, esr_el2);
	OFFSET(GUEST_FAR_OFFSET, guest, far_el2);
	OFFSET(GUEST_HPFAR_OFFSET, guest, hpfar_el2);
	OFFSET(GUEST_EXIT_CODE_OFFSET, guest, exit_code);
	OFFSET(GUEST_TPIDR_EL1_OFFSET, guest, tpidr_el1);
	OFFSET(GUEST_ICH_VMCR_EL2_OFFSET, guest, ich_vmcr_el2);

	return 0;
}
