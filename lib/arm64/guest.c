/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <libcflat.h>
#include <asm/guest.h>
#include <asm/io.h>
#include <asm/sysreg.h>
#include <asm/barrier.h>
#include <alloc_page.h>
#include <alloc.h>

/*
 * C-Entry for Exception Handling
 * Returns 0 to Resume Guest, 1 to Exit to Host Caller
 */
unsigned long guest_c_exception_handler(struct guest *guest, unsigned long vector_offset)
{
	enum vector vector = (enum vector)guest->exit_code;

	/* Save Trap Info */
	guest->esr_el2 = read_sysreg(esr_el2);
	guest->far_el2 = read_sysreg(far_el2);
	guest->hpfar_el2 = read_sysreg(hpfar_el2);

	/* Invoke Handler if registered */
	if (guest->handlers[vector]) {
		if (guest->handlers[vector](guest) == GUEST_ACTION_RESUME) {
			return 0; /* ASM stub will restore and ERET */
		}
	}

	/* Default: Exit to caller */
	return 1;
}

/* --- EL1 (Guest-Internal) Vector Handling --- */

void guest_install_el1_handler(struct guest *guest, enum vector v, guest_el1_handler_t handler)
{
	if (guest && guest->guest_context && v < VECTOR_MAX)
		guest->guest_context->handlers[v] = handler;
}

void guest_el1_c_handler(struct guest_el1_regs *regs, unsigned int vector)
{
	struct guest_context *ctx = (struct guest_context *)read_sysreg(tpidr_el1);
	unsigned int esr = read_sysreg(esr_el1);

	if (ctx && vector < VECTOR_MAX && ctx->handlers[vector]) {
		ctx->handlers[vector](regs, esr);
	} else {
		printf("Guest: Unhandled Exception Vector %d, ESR=0x%x\n", vector, esr);
		asm volatile("hvc #0xFFFF");
	}
}

extern void guest_el1_vectors(void);

static struct guest *__guest_create(struct s2_mmu *s2_ctx, void *entry_point)
{
	struct guest *guest = calloc(1, sizeof(struct guest));
	struct guest_context *guest_ctx;
	unsigned long guest_ctx_pa;

	/* Allocate the internal context table */
	guest_ctx = (void *)alloc_page();
	memset(guest_ctx, 0, PAGE_SIZE);
	guest->guest_context = guest_ctx;

	guest_ctx_pa = virt_to_phys(guest_ctx);
	if (s2_ctx)
		s2mmu_map(s2_ctx, guest_ctx_pa, guest_ctx_pa, PAGE_SIZE, S2_MAP_RW);

	guest->tpidr_el1 = guest_ctx_pa;

	guest->elr_el2 = (unsigned long)entry_point;
	guest->spsr_el2 = 0x3C5; /* M=EL1h, DAIF=Masked */
	guest->hcr_el2 = HCR_GUEST_FLAGS;

	if (s2_ctx) {
		guest->vttbr_el2 = virt_to_phys(s2_ctx->pgd);
		guest->vttbr_el2 |= ((unsigned long)s2_ctx->vmid << 48);
	} else {
		printf("Stage 2 MMU context missing!");
	}

	guest->sctlr_el1 = read_sysreg(sctlr_el1);
	/* Disable guest stage 1 translation */
	guest->sctlr_el1 &= ~(SCTLR_EL1_M | SCTLR_EL1_C);
	guest->sctlr_el1 |= SCTLR_EL1_I;

	guest->ich_vmcr_el2 = read_sysreg(ich_vmcr_el2);
	guest->ich_vmcr_el2 |= (0xFFUL << ICH_VMCR_PMR_SHIFT) | (1UL << ICH_VMCR_ENG1_SHIFT);

	guest->vbar_el1 = (unsigned long)guest_el1_vectors;
	guest->s2mmu = s2_ctx;

	return guest;
}

struct guest *guest_create(int vmid, void (*guest_func)(void), enum s2_granule granule)
{
	unsigned long guest_pa, code_base, stack_pa;
	unsigned long *stack_page;
	struct guest *guest;
	struct s2_mmu *ctx;

	ctx = s2mmu_init(vmid, granule, true);
	/*
	 * Map the Host's code segment Identity Mapped (IPA=PA).
	 * To be safe, we map a large chunk (e.g., 2MB) around the function
	 * to capture any helper functions the compiler might generate calls to.
	 */
	guest_pa = virt_to_phys((void *)guest_func);
	code_base = guest_pa & ~(SZ_2M - 1);
	s2mmu_map(ctx, code_base, code_base, SZ_2M, S2_MAP_RW);

	/*
	 * Map Stack
	 * Allocate 16 pages (64K) in Host, get its PA, and map it for Guest.
	 */
	stack_page = alloc_pages(get_order(GUEST_STACK_SIZE >> PAGE_SHIFT));
	stack_pa = virt_to_phys(stack_page);
	/* Identity Map it (IPA = PA) */
	s2mmu_map(ctx, stack_pa, stack_pa, GUEST_STACK_SIZE, S2_MAP_RW);

	s2mmu_enable(ctx);

	/* Create Guest */
	/* Entry point is the PA of the function (Identity Mapped) */
	guest = __guest_create(ctx, (void *)guest_pa);

	/*
	 * Setup Guest Stack Pointer
	 * Must match where we mapped the stack + Offset
	 */
	guest->sp_el1 = stack_pa + GUEST_STACK_SIZE;

	/* Map UART identity mapped, printf() available to guest */
	s2mmu_map(ctx, 0x09000000, 0x09000000, PAGE_SIZE, S2_MAP_DEVICE);

	return guest;
}

void guest_destroy(struct guest *guest)
{
	s2mmu_disable(guest->s2mmu);
	s2mmu_destroy(guest->s2mmu);
	if (guest->guest_context)
		free_page(guest->guest_context);
	free(guest);
}

void guest_install_handler(struct guest *guest, enum vector v, guest_handler_t handler)
{
	if (v < VECTOR_MAX)
		guest->handlers[v] = handler;
}
