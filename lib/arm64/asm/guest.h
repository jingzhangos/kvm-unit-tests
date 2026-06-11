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
#include <asm/stage1_mmu.h>


#define HCR_GUEST_FLAGS (HCR_EL2_VM | HCR_EL2_FMO | HCR_EL2_IMO | \
			 HCR_EL2_AMO | HCR_EL2_RW | HCR_EL2_E2H)
/* Guest stack size */
#define GUEST_STACK_SIZE		SZ_64K

/* ICH_VMCR_EL2 bit definition */
#define ICH_VMCR_PMR_SHIFT	24
#define ICH_VMCR_PMR_MASK	(0xffUL << ICH_VMCR_PMR_SHIFT)
#define ICH_VMCR_ENG0_SHIFT	0
#define ICH_VMCR_ENG0_MASK	(1 << ICH_VMCR_ENG0_SHIFT)
#define ICH_VMCR_ENG1_SHIFT	1
#define ICH_VMCR_ENG1_MASK	(1 << ICH_VMCR_ENG1_SHIFT)

/*
 * Result from Handler:
 * RESUME: Keep guest running (ERET immediately)
 * EXIT:   Return to Host C caller
 */
enum guest_handler_result {
	GUEST_ACTION_RESUME,
	GUEST_ACTION_EXIT
};

struct guest;
typedef enum guest_handler_result (*guest_handler_t)(struct guest *guest);

/*
 * Guest EL1 Exception Frame (pushed to guest stack by asm stub)
 * We use a simplified frame: x0-x30, elr, spsr. size = 33*8
 */
struct guest_el1_regs {
	unsigned long regs[31];
	unsigned long elr;
	unsigned long spsr;
};

typedef void (*guest_el1_handler_t)(struct guest_el1_regs *regs, unsigned int esr);

/*
 * Guest Context Structure
 * This will be pointed to by TPIDR_EL1 while the guest is running.
 */
struct guest_context {
	guest_el1_handler_t handlers[VECTOR_MAX];
};

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
	unsigned long vbar_el1;

	/* Exit Information */
	unsigned long esr_el2;
	unsigned long far_el2;
	unsigned long hpfar_el2;
	unsigned long exit_code;
	unsigned long tpidr_el1;

	/* GIC Registers */
	unsigned long ich_vmcr_el2;

	/* Exception Handlers in EL2 */
	guest_handler_t handlers[VECTOR_MAX];
	struct guest_context *guest_context;

	struct s2_mmu *s2mmu;
	struct s1_mmu *s1mmu;
};


struct guest *guest_create(int vmid, void (*guest_func)(void), enum s2_granule granule);
void guest_destroy(struct guest *guest);
void guest_run(struct guest *guest);

unsigned long guest_c_exception_handler(struct guest *guest, unsigned long vector_offset);
void guest_install_handler(struct guest *guest, enum vector v, guest_handler_t handler);

void guest_el1_c_handler(struct guest_el1_regs *regs, unsigned int vector);
void guest_install_el1_handler(struct guest *guest, enum vector v, guest_el1_handler_t handler);

#endif /* _ASMARM64_GUEST_H_ */
