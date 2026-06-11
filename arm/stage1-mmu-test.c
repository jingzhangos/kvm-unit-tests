/*
 * ARM64 Stage-1 MMU Guest Test
 *
 * This test validates Stage-1 MMU translation and translation fault handling
 * in the guest. It configures Stage-1 translation tables, enables Stage-1 MMU
 * in guest EL1, writes to mapped/unmapped virtual addresses, and verifies
 * that a Stage-1 translation fault triggers the guest's exception vector.
 *
 * Copyright (C) 2026 Google LLC.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <libcflat.h>
#include <alloc_page.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/guest.h>
#include <asm/stage1_mmu.h>
#include <asm/stage2_mmu.h>

#define TEST_PAGE_VA		0x20000000UL
#define TEST_PAGE_IPA		0x30000000UL
#define FAULT_ADDR_VA		0x50000000UL
#define TEST_DATA		0xBEEFCAFEUL




struct test_shared_data {
	volatile bool s1_mapped_ok;
	volatile bool stage1_fault_caught;
};

static struct test_shared_data shared_data = {
	.s1_mapped_ok = false,
	.stage1_fault_caught = false,
};


static void guest_code(unsigned long s1_pgd_pa, enum s1_granule granule)
{
	s1mmu_enable(s1_pgd_pa, granule);

	/* Test mapped translation */
	volatile unsigned long *test_va = (void *)TEST_PAGE_VA;
	if (*test_va == TEST_DATA) {
		*test_va = 0xDEADBEEF;
		if (*test_va == 0xDEADBEEF) {
			shared_data.s1_mapped_ok = true;
		}
	}

	/* Force Stage-1 Translation Fault by accessing unmapped virtual address */
	volatile unsigned long *fault_va = (void *)FAULT_ADDR_VA;
	*fault_va = 0xBAADF00D;

	/* Exit guest via HVC */
	asm("hvc #0");
}

static void guest_el1_sync_handler(struct guest_el1_regs *regs, unsigned int esr)
{
	unsigned int ec = esr >> ESR_ELx_EC_SHIFT;

	if (ec == ESR_EL1_EC_DABT_EL1) {
		unsigned long far = read_sysreg(far_el1);
		if (far == FAULT_ADDR_VA) {
			shared_data.stage1_fault_caught = true;
			/* Skip faulting instruction (STR/LDR is 4 bytes) */
			regs->elr += 4;
			return;
		}
	}

	/* Trigger exit with failure code since printing is not available without UART mapping */
	asm volatile("hvc #0xFFFF");
}

static enum guest_handler_result guest_el2_exception_handler(struct guest *guest)
{
	unsigned long ec = guest->esr_el2 >> ESR_ELx_EC_SHIFT;

	if (ec == ESR_ELx_EC_HVC64) {
		if (guest->exit_code == 0xFFFF) {
			report(false, "Guest aborted with unexpected EL1 exception");
		} else {
			report_info("CPU%d: Guest exited via HVC successfully.", smp_processor_id());
		}
		return GUEST_ACTION_EXIT;
	}

	report(false, "Unexpected guest EL2 trap: EC=0x%lx, ESR=0x%lx", ec, guest->esr_el2);
	return GUEST_ACTION_EXIT;
}

int main(int argc, char **argv)
{
	struct guest *guest;
	unsigned long *test_page;
	unsigned long data_base;

	report_prefix_push("stage1-mmu");

	/* Create guest with Stage 2 and Stage 1 configured */
	guest = guest_create(smp_processor_id(), (void *)guest_code, S2_PAGE_4K);
	if (!guest) {
		report(false, "Failed to create guest");
		return report_summary();
	}


	/* Map shared test data in Stage 1 & Stage 2 */
	data_base = virt_to_phys((void *)&shared_data) & PAGE_MASK;
	s2mmu_map(guest->s2mmu, data_base, data_base, PAGE_SIZE, S2_MAP_RW);
	s1mmu_map(guest->s1mmu, data_base, data_base, PAGE_SIZE, S1_MAP_RW);

	/* Map test data page in Stage 1 & Stage 2 */
	test_page = alloc_page();
	*test_page = TEST_DATA;
	s2mmu_map(guest->s2mmu, TEST_PAGE_IPA, virt_to_phys(test_page), PAGE_SIZE, S2_MAP_RW);
	s1mmu_map(guest->s1mmu, TEST_PAGE_VA, TEST_PAGE_IPA, PAGE_SIZE, S1_MAP_RW);


	/* Set up EL1 and EL2 guest exception handlers */
	guest_install_handler(guest, ELx_LOW_SYNC_64, guest_el2_exception_handler);
	guest_install_el1_handler(guest, ELxH_SYNC, guest_el1_sync_handler);

	report_info("CPU%d: entering guest with Stage 1 MMU test...", smp_processor_id());
	guest_run(guest);

	report(shared_data.s1_mapped_ok, "Stage-1 MMU mapped read/write check");
	report(shared_data.stage1_fault_caught, "Stage-1 MMU translation fault caught");

	guest_destroy(guest);
	free_page(test_page);

	return report_summary();
}

