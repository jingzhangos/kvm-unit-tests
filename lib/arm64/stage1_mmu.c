/*
 * ARM64 Stage-1 MMU Library for Guest
 *
 * Copyright (C) 2026, Google LLC.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <libcflat.h>
#include <alloc.h>
#include <alloc_page.h>
#include <asm/processor.h>
#include <asm/sysreg.h>
#include <asm/io.h>
#include <asm/stage1_mmu.h>
#include <asm/stage2_mmu.h>
#include <linux/compiler.h>



struct s1_mmu *s1mmu_init(struct s2_mmu *s2mmu, enum s1_granule granule)
{
	struct s1_mmu *mmu;
	int order = 0;

	if (!s2mmu)
		return NULL;

	mmu = calloc(1, sizeof(struct s1_mmu));
	if (!mmu)
		return NULL;


	mmu->s2mmu = s2mmu;
	mmu->granule = granule;

	switch (granule) {
	case S1_GRANULE_4K:
		mmu->page_shift = 12;
		mmu->level_shift = 9;
		mmu->root_level = (VA_BITS > 39) ? 0 : 1;
		break;
	case S1_GRANULE_16K:
		mmu->page_shift = 14;
		mmu->level_shift = 11;
		mmu->root_level = (VA_BITS > 36) ? 1 : 2;
		break;
	case S1_GRANULE_64K:
		mmu->page_shift = 16;
		mmu->level_shift = 13;
		mmu->root_level = (VA_BITS > 42) ? 1 : 2;
		break;
	}

	mmu->page_size = 1UL << mmu->page_shift;
	mmu->block_size = 1UL << (mmu->page_shift + mmu->level_shift);

	/* Alloc PGD. Use order for allocation size */
	if (mmu->page_size > PAGE_SIZE) {
		order = __builtin_ctz(mmu->page_size / PAGE_SIZE);
	}
	mmu->pgd = (unsigned long *)alloc_pages(order);
	if (!mmu->pgd) {
		free(mmu);
		return NULL;
	}
	memset(mmu->pgd, 0, mmu->page_size);

	// Map the root page table in Stage 2 so the CPU walk can access it
	unsigned long pgd_pa = virt_to_phys(mmu->pgd);
	s2mmu_map(s2mmu, pgd_pa, pgd_pa, mmu->page_size, S2_MAP_RW);

	return mmu;
}

static void s1mmu_free_tables(struct s1_mmu *mmu, unsigned long *table, int level)
{
	unsigned long entries = 1UL << mmu->level_shift;
	unsigned long mask = GENMASK_ULL(47, mmu->page_shift);

	if (level < 3) {
		for (int i = 0; i < entries; i++) {
			unsigned long entry = table[i];
			if (s1_pte_valid(entry) && s1_pte_is_table(entry)) {
				unsigned long *next = (unsigned long *)phys_to_virt(entry & mask);
				s1mmu_free_tables(mmu, next, level + 1);
			}
		}
	}
	unsigned long table_pa = virt_to_phys(table);
	s2mmu_unmap(mmu->s2mmu, table_pa, mmu->page_size);

	free_pages(table);
}


void s1mmu_destroy(struct s1_mmu *mmu)
{
	if (mmu->pgd) {
		s1mmu_free_tables(mmu, mmu->pgd, mmu->root_level);
	}
	free(mmu);
}

static unsigned long *s1mmu_get_pte(struct s1_mmu *mmu, unsigned long *table, unsigned long idx, bool alloc)
{
	unsigned long entry = table[idx];
	unsigned long *next_table;
	unsigned long mask = GENMASK_ULL(47, mmu->page_shift);
	int order = 0;

	if (s1_pte_valid(entry)) {
		if (s1_pte_is_table(entry)) {
			return (unsigned long *)phys_to_virt(entry & mask);
		}
		return NULL;
	}

	if (!alloc)
		return NULL;

	if (mmu->page_size > PAGE_SIZE)
		order = __builtin_ctz(mmu->page_size / PAGE_SIZE);

	next_table = (unsigned long *)alloc_pages(order);
	if (!next_table)
		return NULL;
	memset(next_table, 0, mmu->page_size);

	unsigned long next_pa = virt_to_phys(next_table);

	// Map the new table page in Stage 2 so the hardware page table walker can read it
	s2mmu_map(mmu->s2mmu, next_pa, next_pa, mmu->page_size, S2_MAP_RW);

	table[idx] = next_pa | S1_PTE_TABLE_BIT | S1_PTE_VALID;
	return next_table;
}


int s1mmu_map(struct s1_mmu *mmu, unsigned long va, unsigned long ipa,
	      unsigned long size, unsigned long flags)
{
	unsigned long level_mask = (1UL << mmu->level_shift) - 1;
	unsigned long start_va = va;
	unsigned long end_va = va + size;
	unsigned long cur_ipa = ipa;

	while (start_va < end_va) {
		unsigned long *table = mmu->pgd;
		unsigned long level;

		for (level = mmu->root_level; level < 3; level++) {
			unsigned long level_shift = mmu->page_shift + (3 - level) * mmu->level_shift;
			unsigned long idx = (start_va >> level_shift) & level_mask;
			unsigned long level_size = 1UL << level_shift;

			/* Check for Block Mapping */
			bool is_block_level = (level == 2) ||
					      (mmu->granule == S1_GRANULE_4K && level == 1);

			if (is_block_level) {
				if ((start_va & (level_size - 1)) == 0 &&
				    (cur_ipa & (level_size - 1)) == 0 &&
				    (start_va + level_size) <= end_va) {
					/* Map Block */
					table[idx] = (cur_ipa & ~(level_size - 1)) | flags | S1_PTE_VALID;
					start_va += level_size;
					cur_ipa += level_size;
					goto next_chunk;
				}
			}

			table = s1mmu_get_pte(mmu, table, idx, true);
			if (!table)
				return -1;
		}

		/* Level 3 (Page Mapping) */
		if (level == 3) {
			unsigned long idx = (start_va >> mmu->page_shift) & level_mask;
			table[idx] = (cur_ipa & ~(mmu->page_size - 1)) | flags | S1_PTE_TABLE_BIT | S1_PTE_VALID;
			start_va += mmu->page_size;
			cur_ipa += mmu->page_size;
		}

next_chunk:
		continue;
	}

	return 0;
}

int s1mmu_verify_mapping(struct s1_mmu *mmu, unsigned long va,
			 unsigned long size, unsigned long expected_page_size)
{
	unsigned long level_mask = (1UL << mmu->level_shift) - 1;
	unsigned long start_va = va;
	unsigned long end_va = va + size;
	unsigned long mask = GENMASK_ULL(47, mmu->page_shift);

	while (start_va < end_va) {
		unsigned long *table = mmu->pgd;
		unsigned long level;

		for (level = mmu->root_level; level < 3; level++) {
			unsigned long level_shift = mmu->page_shift + (3 - level) * mmu->level_shift;
			unsigned long idx = (start_va >> level_shift) & level_mask;
			unsigned long level_size = 1UL << level_shift;
			unsigned long entry = table[idx];

			if (!s1_pte_valid(entry))
				return -1;

			if (!s1_pte_is_table(entry)) {
				/* Block mapping */
				if (level_size != expected_page_size)
					return -2;
				start_va += level_size;
				goto next_chunk;
			}

			table = (unsigned long *)phys_to_virt(entry & mask);
		}

		if (level == 3) {
			unsigned long idx = (start_va >> mmu->page_shift) & level_mask;
			unsigned long entry = table[idx];

			if (!s1_pte_valid(entry) || !s1_pte_is_table(entry))
				return -1;
			if (mmu->page_size != expected_page_size)
				return -2;
			start_va += mmu->page_size;
		}

next_chunk:
		continue;
	}

	return 0;
}

void s1mmu_enable(unsigned long pgd_pa, enum s1_granule granule)
{
	unsigned long tcr = S1_TCR_FLAGS;

	switch (granule) {
	case S1_GRANULE_4K:
		tcr |= (0UL << 14); /* TCR_TG0_4K */
		break;
	case S1_GRANULE_16K:
		tcr |= (2UL << 14); /* TCR_TG0_16K */
		break;
	case S1_GRANULE_64K:
		tcr |= (1UL << 14); /* TCR_TG0_64K */
		break;
	}

	write_sysreg(S1_MAIR_VAL, mair_el1);
	write_sysreg(tcr, tcr_el1);
	write_sysreg(pgd_pa, ttbr0_el1);
	isb();

	unsigned long sctlr = read_sysreg(sctlr_el1);
	sctlr |= SCTLR_EL1_M | SCTLR_EL1_C | SCTLR_EL1_I;
	write_sysreg(sctlr, sctlr_el1);
	isb();
}



