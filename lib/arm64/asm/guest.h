/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef _ASMARM64_GUEST_H_
#define _ASMARM64_GUEST_H_

#include <libcflat.h>
#include <asm/processor.h>
#include <asm/stage2_mmu.h>

#define HCR_GUEST_FLAGS (HCR_EL2_VM | HCR_EL2_FMO | HCR_EL2_IMO | \
			 HCR_EL2_AMO | HCR_EL2_RW | HCR_EL2_E2H)
/* Guest stack size */
#define GUEST_STACK_SIZE		SZ_64K

struct guest {
	/* General Purpose Registers */
	unsigned long x[31]; /* x0..x30 */

	/* Execution State */
	unsigned long elr_el2;
	unsigned long spsr_el2;

	/* Control Registers */
	unsigned long hcr_el2;
	unsigned long vttbr_el2;
	unsigned long sctlr_el1;
	unsigned long sp_el1;

	/* Exit Information */
	unsigned long esr_el2;
	unsigned long far_el2;
	unsigned long hpfar_el2;
	unsigned long exit_code;

	struct s2_mmu *s2mmu;
};

struct guest *guest_create(int vmid, void (*guest_func)(void), enum s2_granule granule);
void guest_destroy(struct guest *guest);
void guest_run(struct guest *guest);

#endif /* _ASMARM64_GUEST_H_ */
