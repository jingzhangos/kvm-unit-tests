/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef _ASMARM64_GUEST_H_
#define _ASMARM64_GUEST_H_

/* Offsets for assembly (Must match struct guest) */
#define GUEST_X_OFFSET			0
#define GUEST_ELR_OFFSET		248
#define GUEST_SPSR_OFFSET		256
#define GUEST_HCR_OFFSET		264
#define GUEST_VTTBR_OFFSET		272
#define GUEST_SCTLR_OFFSET		280
#define GUEST_VBAR_OFFSET		288
#define GUEST_SP_EL1_OFFSET		296
#define GUEST_ESR_OFFSET		304
#define GUEST_FAR_OFFSET		312
#define GUEST_HPFAR_OFFSET		320
#define GUEST_EXIT_CODE_OFFSET		328
#define GUEST_TPIDR_EL1_OFFSET		336
#define GUEST_ICH_VMCR_EL2_OFFSET	344

#ifndef __ASSEMBLY__

#include <libcflat.h>
#include <asm/stage2_mmu.h>

/* HCR_EL2 Definitions */
#define HCR_VM		(1UL << 0)	/* Virtualization Enable */
#define HCR_FMO		(1UL << 3)	/* Physical FIQ Routing */
#define HCR_IMO		(1UL << 4)	/* Physical IRQ Routing */
#define HCR_AMO		(1UL << 5)	/* Physical SError Interrupt Routing */
#define HCR_RW		(1UL << 31)	/* Execution State: AArch64 */
#define HCR_DC		(1UL << 12)	/* Default Cacheable */
#define HCR_E2H		(1UL << 34)	/* EL2 Host */

#define HCR_GUEST_FLAGS (HCR_VM | HCR_FMO | HCR_IMO | HCR_AMO | HCR_RW | \
			 HCR_DC | HCR_E2H)

/* ICH_VMCR_EL2 bit definition */
#define ICH_VMCR_PMR_SHIFT	24
#define ICH_VMCR_PMR_MASK	(0xffUL << ICH_VMCR_PMR_SHIFT)
#define ICH_VMCR_ENG0_SHIFT	0
#define ICH_VMCR_ENG0_MASK	(1 << ICH_VMCR_ENG0_SHIFT)
#define ICH_VMCR_ENG1_SHIFT	1
#define ICH_VMCR_ENG1_MASK	(1 << ICH_VMCR_ENG1_SHIFT)

/* Guest stack size */
#define GUEST_STACK_SIZE		SZ_64K

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

/* EL1 (Guest-internal) Exception Vector */
enum guest_el1_vector {
	GUEST_EL1_SYNC,
	GUEST_EL1_IRQ,
	GUEST_EL1_FIQ,
	GUEST_EL1_SERROR,
	GUEST_EL1_MAX
};

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

/* Exceptions from the Guest (Lower EL using AArch64) */
enum guest_vector {
	GUEST_VECTOR_SYNC,
	GUEST_VECTOR_IRQ,
	GUEST_VECTOR_FIQ,
	GUEST_VECTOR_SERROR,
	GUEST_VECTOR_MAX
};

/*
 * Guest Context Structure
 * This will be pointed to by TPIDR_EL1 while the guest is running.
 */
struct guest_context {
	guest_el1_handler_t handlers[GUEST_EL1_MAX];
};

struct guest {
	/* 0x000: General Purpose Registers */
	unsigned long x[31]; /* x0..x30 */

	/* 0x0F8: Execution State */
	unsigned long elr_el2;
	unsigned long spsr_el2;

	/* 0x108: Control Registers */
	unsigned long hcr_el2;
	unsigned long vttbr_el2;
	unsigned long sctlr_el1;
	unsigned long vbar_el1;
	unsigned long sp_el1;

	/* 0x130: Exit Information */
	unsigned long esr_el2;
	unsigned long far_el2;
	unsigned long hpfar_el2;
	unsigned long exit_code; /* enum guest_vector */
	unsigned long tpidr_el1;

	/* 0x158: GIC Registers */
	unsigned long ich_vmcr_el2;

	/* 0x160: Exception Handlers */
	guest_handler_t handlers[GUEST_VECTOR_MAX];
	struct guest_context *guest_context;

	struct s2_mmu *s2mmu;
};

/* API */
struct guest *guest_create(int vmid, void (*guest_func)(void), enum s2_granule granule);
void guest_destroy(struct guest *guest);

/* Configuration */
void guest_set_vector(struct guest *guest, void *vector_table);
void guest_set_stack(struct guest *guest, void *stack_top);
void guest_install_handler(struct guest *guest, enum guest_vector v, guest_handler_t handler);

/* Install handler for exceptions INSIDE EL1 */
void guest_install_el1_handler(struct guest *guest, enum guest_el1_vector v, guest_el1_handler_t handler);

unsigned long guest_c_exception_handler(struct guest *guest, unsigned long vector_offset);
void guest_el1_c_handler(struct guest_el1_regs *regs, unsigned int vector);

/* Core Run Loop */
void guest_run(struct guest *guest);

#endif /* __ASSEMBLY__ */
#endif /* _ASMARM64_GUEST_H_ */
