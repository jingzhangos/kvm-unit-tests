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
#include <stdlib.h>
#include <alloc_page.h>
#include <bitops.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/smp.h>
#include <asm/guest.h>
#include <asm/stage1_mmu.h>
#include <asm/stage2_mmu.h>

#define TEST_PAGE_IPA		0x200000000UL
#define FAULT_ADDR_IPA		0x400000000UL
#define TEST_DATA		0xBEEFCAFEUL
#define MAX_MAP_SIZE		(8UL * SZ_1G)

static volatile bool handled = false;
static unsigned long map_size = SZ_4K;

static unsigned long parse_size(const char *str)
{
	char *end;
	unsigned long val = strtoul(str, &end, 10);
	unsigned long size = 0;

	if (!val || end == str)
		return 0;

	if (!strcasecmp(end, "k"))
		size = val * 1024UL;
	else if (!strcasecmp(end, "m"))
		size = val * SZ_1M;
	else if (!strcasecmp(end, "g"))
		size = val * SZ_1G;

	if (!size || size > MAX_MAP_SIZE)
		return 0;

	return size;
}

static bool parse_s1_granule(const char *str, enum s1_granule *granule)
{
	if (!strcmp(str, "4k") || !strcmp(str, "4K")) {
		*granule = S1_GRANULE_4K;
		return true;
	}
	if (!strcmp(str, "16k") || !strcmp(str, "16K")) {
		*granule = S1_GRANULE_16K;
		return true;
	}
	if (!strcmp(str, "64k") || !strcmp(str, "64K")) {
		*granule = S1_GRANULE_64K;
		return true;
	}
	if (!strcmp(str, "2m") || !strcmp(str, "2M")) {
		*granule = S1_GRANULE_2M;
		return true;
	}
	if (!strcmp(str, "1g") || !strcmp(str, "1G")) {
		*granule = S1_GRANULE_1G;
		return true;
	}
	return false;
}

static bool parse_s2_granule(const char *str, enum s2_granule *granule)
{
	if (!strcmp(str, "4k") || !strcmp(str, "4K")) {
		*granule = S2_PAGE_4K;
		return true;
	}
	if (!strcmp(str, "16k") || !strcmp(str, "16K")) {
		*granule = S2_PAGE_16K;
		return true;
	}
	if (!strcmp(str, "64k") || !strcmp(str, "64K")) {
		*granule = S2_PAGE_64K;
		return true;
	}
	if (!strcmp(str, "2m") || !strcmp(str, "2M")) {
		*granule = S2_PAGE_2M;
		return true;
	}
	if (!strcmp(str, "1g") || !strcmp(str, "1G")) {
		*granule = S2_PAGE_1G;
		return true;
	}
	return false;
}

static unsigned long s1_granule_size(enum s1_granule granule)
{
	switch (granule) {
	case S1_GRANULE_16K:
		return SZ_16K;
	case S1_GRANULE_64K:
		return SZ_64K;
	case S1_GRANULE_2M:
		return SZ_2M;
	case S1_GRANULE_1G:
		return SZ_1G;
	case S1_GRANULE_4K:
	default:
		return SZ_4K;
	}
}

static unsigned long s2_granule_size(enum s2_granule granule)
{
	switch (granule) {
	case S2_PAGE_16K:
		return SZ_16K;
	case S2_PAGE_64K:
		return SZ_64K;
	case S2_PAGE_2M:
		return SZ_2M;
	case S2_PAGE_1G:
		return SZ_1G;
	case S2_PAGE_4K:
	default:
		return SZ_4K;
	}
}

static void guest_code(unsigned long s1_pgd_pa, enum s1_granule granule)
{
	volatile unsigned long *test_va = (void *)TEST_PAGE_IPA;
	volatile unsigned long *fault_va = (void *)FAULT_ADDR_IPA;

	s1mmu_enable(s1_pgd_pa, granule);

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
			unsigned long alloc_size = MAX(map_size, s2_granule_size(guest->s2mmu->granule));

			fixup_page = alloc_pages(get_order(alloc_size >> PAGE_SHIFT));
			if (!fixup_page)
				fixup_page = (unsigned long *)PHYS_OFFSET;
			s2mmu_map(guest->s2mmu, FAULT_ADDR_IPA,
				  virt_to_phys(fixup_page), map_size, S2_MAP_RW);
			report(true, "Caught stage-2 fault at 0x%lx", far);
		} else {
			report(false, "Unexpected fault address: 0x%lx", far);
			return GUEST_ACTION_EXIT;
		}
	} else {
		report(false, "Unexpected exception class: 0x%lx", ec);
		return GUEST_ACTION_EXIT;
	}

	return GUEST_ACTION_RESUME;
}

int main(int argc, char **argv)
{
	enum s1_granule s1_granule = S1_GRANULE_4K;
	enum s2_granule s2_granule = S2_PAGE_4K;
	struct guest *guest;
	unsigned long *test_page;
	unsigned long code_va_base, code_pa_base, data_base, alloc_size, min_map_size;
	int i;

	report_prefix_push("stage2-mmu");

	for (i = 1; i < argc; i++) {
		if (!strncmp(argv[i], "s1=", 3)) {
			if (!parse_s1_granule(argv[i] + 3, &s1_granule))
				report_abort("Invalid s1 granule: %s", argv[i] + 3);
		} else if (!strncmp(argv[i], "s2=", 3)) {
			if (!parse_s2_granule(argv[i] + 3, &s2_granule))
				report_abort("Invalid s2 granule: %s", argv[i] + 3);
		} else if (!strncmp(argv[i], "size=", 5)) {
			map_size = parse_size(argv[i] + 5);
			if (!map_size)
				report_abort("Invalid mapping size: %s", argv[i] + 5);
		} else if (!strncmp(argv[i], "map_size=", 9)) {
			map_size = parse_size(argv[i] + 9);
			if (!map_size)
				report_abort("Invalid mapping size: %s", argv[i] + 9);
		} else {
			report_abort("Unknown parameter: %s", argv[i]);
		}
	}

	min_map_size = MAX(s1_granule_size(s1_granule), s2_granule_size(s2_granule));
	map_size = ALIGN(MAX(map_size, min_map_size), min_map_size);

	guest = guest_create(smp_processor_id(), (void *)guest_code, s2_granule, s1_granule);
	if (!guest) {
		report(false, "Failed to create guest");
		return report_summary();
	}


	/* Map host code: VA -> IPA -> PA */
	code_va_base = (unsigned long)guest_code;
	code_pa_base = virt_to_phys((void *)guest_code);

	/* Align to 2MB to use block descriptors where possible */
	code_va_base = code_va_base & ~(SZ_2M - 1);
	code_pa_base = code_pa_base & ~(SZ_2M - 1);
	s2mmu_map(guest->s2mmu, code_va_base, code_pa_base, SZ_2M, S2_MAP_RW);
	s1mmu_map(guest->s1mmu, code_va_base, code_pa_base, SZ_2M, S1_MAP_RW);

	/* Identity map the shared variable */
	data_base = virt_to_phys((void *)&handled) & PAGE_MASK;
	s2mmu_map(guest->s2mmu, data_base, data_base, PAGE_SIZE, S2_MAP_RW);
	s1mmu_map(guest->s1mmu, data_base, data_base, PAGE_SIZE, S1_MAP_RW);

	/* Map test data page */
	alloc_size = MAX(map_size, s2_granule_size(s2_granule));
	test_page = alloc_pages(get_order(alloc_size >> PAGE_SHIFT));
	if (test_page)
		*test_page = TEST_DATA;
	else
		test_page = (unsigned long *)PHYS_OFFSET;
	s2mmu_map(guest->s2mmu, TEST_PAGE_IPA, virt_to_phys(test_page), map_size, S2_MAP_RW);
	s1mmu_map(guest->s1mmu, TEST_PAGE_IPA, TEST_PAGE_IPA, map_size, S1_MAP_RW);

	/* Map fault address in Stage 1 so the access faults at Stage 2 */
	s1mmu_map(guest->s1mmu, FAULT_ADDR_IPA, FAULT_ADDR_IPA, map_size, S1_MAP_RW);

	guest_install_handler(guest, ELx_LOW_SYNC_64, guest_exception_handler);

	report_info("CPU%d: entering guest...", smp_processor_id());

	guest_run(guest);

	report(handled, "Stage-2 fault handling test completed");
	guest_destroy(guest);

	return report_summary();
}
