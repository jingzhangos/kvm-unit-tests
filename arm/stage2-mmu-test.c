/*
 * ARM64 Stage-2 MMU Demand Paging Test
 *
 * This test validates two-stage (Stage-1 + Stage-2) MMU translation and
 * Stage-2 data abort handling by purposefully accessing unmapped Stage-2
 * memory in the guest, mapping the region on demand in the host fault
 * handler, and verifying the mapped guest memory.
 *
 * Parameters (all optional):
 *   s1=<4k|16k|64k|2m|1g>          Stage-1 page/block size (default: 4k)
 *   s2=<4k|16k|64k|2m|1g>          Stage-2 page/block size (default: 4k)
 *   size=<N><k|m|g>                Total mapping size up to 8g (default: 4k;
 *                                  also accepts map_size=<N><k|m|g>, rounded
 *                                  up to the largest of s1 and s2 sizes)
 *   mode=<seq|skip|rand>           Memory verification mode (default: seq;
 *                                  also accepts type= or verify=):
 *                                    seq  - sequentially write/read/compare
 *                                           every word across the mapped range
 *                                    skip - sequentially write/read/compare
 *                                           every N pages across the range
 *                                    rand - randomly write/read/compare across
 *                                           the mapped range
 *   skip=<N>                       Page stride for skip mode (default: 1;
 *                                  also accepts stride=<N> and implies
 *                                  mode=skip)
 *
 * Usage examples:
 *   ./arm/run arm/stage2-mmu-test.flat
 *   ./arm/run arm/stage2-mmu-test.flat -append "s1=4k s2=4k size=2m mode=seq"
 *   ./arm/run arm/stage2-mmu-test.flat -append "s1=16k s2=2m size=3g mode=skip skip=16"
 *   ./arm/run arm/stage2-mmu-test.flat -append "s1=64k s2=2m size=5g mode=rand"
 *   ./arm/run arm/stage2-mmu-test.flat -append "s1=1g s2=1g size=8g skip=64"
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

enum verify_mode {
	VERIFY_SEQ,
	VERIFY_SKIP,
	VERIFY_RANDOM,
};

static volatile bool handled = false;
static unsigned long map_size = SZ_4K;
static enum verify_mode verify_mode = VERIFY_SEQ;
static unsigned long skip_pages = 1;

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

static bool parse_verify_mode(const char *str, enum verify_mode *mode)
{
	if (!strcasecmp(str, "seq") || !strcasecmp(str, "sequential")) {
		*mode = VERIFY_SEQ;
		return true;
	}
	if (!strcasecmp(str, "skip") || !strcasecmp(str, "stride") ||
	    !strcasecmp(str, "page")) {
		*mode = VERIFY_SKIP;
		return true;
	}
	if (!strcasecmp(str, "rand") || !strcasecmp(str, "random")) {
		*mode = VERIFY_RANDOM;
		return true;
	}
	return false;
}

static unsigned long next_rand(unsigned long *state)
{
	unsigned long x = *state;

	x ^= x >> 12;
	x ^= x << 25;
	x ^= x >> 27;
	*state = x;
	return x * 0x2545F4914F6CDD1DUL;
}

static void guest_code(unsigned long s1_pgd_pa, enum s1_granule granule)
{
	volatile unsigned long *test_va = (void *)TEST_PAGE_IPA;
	volatile unsigned long *fault_va = (void *)FAULT_ADDR_IPA;
	unsigned long count = map_size / sizeof(unsigned long);
	unsigned long i, step, iters, seed;

	s1mmu_enable(s1_pgd_pa, granule);

	switch (verify_mode) {
	case VERIFY_SEQ:
		for (i = 0; i < count; i++) {
			test_va[i] = TEST_DATA;
			fault_va[i] = test_va[i];
		}
		for (i = 0; i < count; i++) {
			if (fault_va[i] != TEST_DATA)
				goto out;
		}
		break;
	case VERIFY_SKIP:
		step = MAX(skip_pages, 1UL) * (PAGE_SIZE / sizeof(unsigned long));
		for (i = 0; i < count; i += step) {
			test_va[i] = TEST_DATA;
			fault_va[i] = test_va[i];
		}
		for (i = 0; i < count; i += step) {
			if (fault_va[i] != TEST_DATA)
				goto out;
		}
		break;
	case VERIFY_RANDOM:
		seed = 0x123456789ABCDEF0UL;
		iters = MAX(map_size / PAGE_SIZE, 64UL);
		for (i = 0; i < iters; i++) {
			unsigned long idx = (i == 0) ? 0 : (next_rand(&seed) % count);

			test_va[idx] = TEST_DATA;
			fault_va[idx] = test_va[idx];
		}
		for (i = 0; i < iters; i++) {
			unsigned long idx = (i == 0) ? 0 : (next_rand(&seed) % count);
			unsigned long val = fault_va[idx];

			if (val != TEST_DATA && val != 0)
				goto out;
		}
		break;
	}

	handled = true;
out:
	asm("hvc #0");
}

static void map_s2_region(struct s2_mmu *s2mmu, unsigned long ipa,
			  unsigned long size)
{
	unsigned long min_chunk = s2_granule_size(s2mmu->granule);
	unsigned long chunk_size = is_power_of_2(size) ?
				   MAX(size, min_chunk) :
				   (1UL << fls(MAX(size, min_chunk)));
	unsigned long *pages = NULL;
	unsigned long offset;

	while (chunk_size >= min_chunk) {
		unsigned int order = get_order(chunk_size >> PAGE_SHIFT);

		if (order < BITS_PER_LONG - PAGE_SHIFT)
			pages = alloc_pages(order);
		if (pages)
			break;
		if (chunk_size == min_chunk)
			break;
		chunk_size >>= 1;
		if (chunk_size < min_chunk)
			chunk_size = min_chunk;
	}

	if (!pages) {
		pages = (unsigned long *)(PHYS_OFFSET + SZ_1G);
		chunk_size = size;
	}

	for (offset = 0; offset < size; offset += chunk_size) {
		unsigned long map_len = MIN(chunk_size, size - offset);

		s2mmu_map(s2mmu, ipa + offset, virt_to_phys(pages),
			  map_len, S2_MAP_RW);
	}
}

static enum guest_handler_result guest_exception_handler(struct guest *guest)
{
	unsigned long far, ec;

	ec = guest->esr_el2 >> ESR_ELx_EC_SHIFT;

	if (ec == ESR_ELx_EC_HVC64) {
		report_info("CPU%d: Guest exited via HVC.", smp_processor_id());
		return GUEST_ACTION_EXIT;
	}

	if (ec == ESR_ELx_EC_DABT_LOW) {
		far = guest->far_el2;
		if (far >= FAULT_ADDR_IPA && far < FAULT_ADDR_IPA + map_size) {
			map_s2_region(guest->s2mmu, FAULT_ADDR_IPA, map_size);
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
	unsigned long code_va_base, code_pa_base, data_base, min_map_size;
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
		} else if (!strncmp(argv[i], "mode=", 5) || !strncmp(argv[i], "type=", 5)) {
			if (!parse_verify_mode(argv[i] + 5, &verify_mode))
				report_abort("Invalid verification mode: %s", argv[i] + 5);
		} else if (!strncmp(argv[i], "verify=", 7)) {
			if (!parse_verify_mode(argv[i] + 7, &verify_mode))
				report_abort("Invalid verification mode: %s", argv[i] + 7);
		} else if (!strncmp(argv[i], "skip=", 5)) {
			char *end;

			skip_pages = strtoul(argv[i] + 5, &end, 10);
			if (!skip_pages || *end != '\0')
				report_abort("Invalid skip pages: %s", argv[i] + 5);
			verify_mode = VERIFY_SKIP;
		} else if (!strncmp(argv[i], "stride=", 7)) {
			char *end;

			skip_pages = strtoul(argv[i] + 7, &end, 10);
			if (!skip_pages || *end != '\0')
				report_abort("Invalid stride pages: %s", argv[i] + 7);
			verify_mode = VERIFY_SKIP;
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
	map_s2_region(guest->s2mmu, TEST_PAGE_IPA, map_size);
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
