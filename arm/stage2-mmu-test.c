/*
 * ARM64 Stage-2 MMU Demand Paging Test
 *
 * This test validates stage-2 data abort handling by purposefully
 * accessing unmapped memory in the guest and verifying that the
 * host correctly handles the fault by mapping the page.
 *
 * Copyright (C) 2026 Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <libcflat.h>
#include <alloc_page.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/guest.h>
#include <asm/stage2_mmu.h>

#define TEST_PAGE_IPA		0x40000000UL
#define FAULT_ADDR_IPA		0x50000000UL
#define TEST_DATA		0xBEEFCAFEUL

static volatile bool handled = false;

static void guest_code(void)
{
	volatile unsigned long *test_va = (void *)TEST_PAGE_IPA;
	volatile unsigned long *fault_va = (void *)FAULT_ADDR_IPA;

	*fault_va = *test_va;

	if (*fault_va == *test_va)
		handled = true;

	asm("hvc #0");
}

static enum guest_handler_result guest_exception_handler(struct guest *guest)
{
	unsigned long far, ec;
	unsigned long *fixup_page;

	ec = guest->esr_el2 >> ESR_ELx_EC_SHIFT;

	if (ec == ESR_ELx_EC_HVC64) {
		report_info("CPU%d: Guest exited via HVC.", smp_processor_id());
		return GUEST_ACTION_EXIT;
	}

	if (ec == ESR_ELx_EC_DABT_LOW) {
		far = guest->far_el2;
		if (far == FAULT_ADDR_IPA) {
			fixup_page = alloc_page();
			s2mmu_map(guest->s2mmu, FAULT_ADDR_IPA,
				  virt_to_phys(fixup_page), PAGE_SIZE, S2_MAP_RW);
			report(true, "Caught stage-2 fault at 0x%lx", far);
		} else {
			report(false, "Unexpected fault address: 0x%lx", far);
		}
	} else {
		report(false, "Unexpected exception class: 0x%lx", ec);
	}

	return GUEST_ACTION_RESUME;
}

int main(int argc, char **argv)
{
	struct guest *guest;
	unsigned long *test_page;
	unsigned long code_va_base, code_pa_base, data_base;

	report_prefix_push("stage2-mmu");

	guest = guest_create(smp_processor_id(), guest_code, S2_PAGE_4K);
	if (!guest) {
		report(false, "Failed to create guest");
		return report_summary();
	}


	/* Map host code: IPA(VA) -> PA */
	/* We use the host VA as the Guest IPA because guest stage 1 is disabled. */
	code_va_base = (unsigned long)guest_code;
	code_pa_base = virt_to_phys((void *)guest_code);

	/* Align to 2MB to use block descriptors where possible */
	code_va_base = code_va_base & ~(SZ_2M - 1);
	code_pa_base = code_pa_base & ~(SZ_2M - 1);
	s2mmu_map(guest->s2mmu, code_va_base, code_pa_base, SZ_2M, S2_MAP_RW);

	/* Identity map the shared variable */
	data_base = virt_to_phys((void *)&handled) & PAGE_MASK;
	s2mmu_map(guest->s2mmu, data_base, data_base, PAGE_SIZE, S2_MAP_RW);

	/* Map test data page */
	test_page = alloc_page();
	*test_page = TEST_DATA;
	s2mmu_map(guest->s2mmu, TEST_PAGE_IPA, virt_to_phys(test_page), PAGE_SIZE, S2_MAP_RW);

	guest_install_handler(guest, ELx_LOW_SYNC_64, guest_exception_handler);

	report_info("CPU%d: entering guest...", smp_processor_id());

	guest_run(guest);

	report(handled, "Stage-2 fault handling test completed");
	guest_destroy(guest);

	return report_summary();
}
