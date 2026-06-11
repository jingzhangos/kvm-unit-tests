/*
 * ARM64 Stage-1 MMU Library for Guest
 *
 * Copyright (C) 2026, Google LLC.
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef _ASMARM64_STAGE1_MMU_H_
#define _ASMARM64_STAGE1_MMU_H_

#include <libcflat.h>
#include <bitops.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/stage2_mmu.h>

enum s1_granule {
	S1_GRANULE_4K,
	S1_GRANULE_16K,
	S1_GRANULE_64K,
};



#define S1_PTE_VALID		(1UL << 0)
#define S1_PTE_TABLE_BIT	(1UL << 1)
#define S1_PTE_AF		(1UL << 10)

#define S1_PTE_SH_MASK		GENMASK_ULL(9, 8)
#define S1_PTE_ATTR_INDX_MASK	GENMASK_ULL(4, 2)
#define S1_PTE_ADDR_MASK	GENMASK_ULL(47, 12)

#define S1_PTE_SHARED		((3UL) << 8) /* Inner Shareable */

#define s1_pte_valid(pte)	((pte) & S1_PTE_VALID)
#define s1_pte_is_table(pte)	((pte) & S1_PTE_TABLE_BIT)

#define S1_ATTR_NORMAL_INDEX	4 /* Matches MT_NORMAL */
#define S1_ATTR_DEVICE_INDEX	1 /* Matches MT_DEVICE_nGnRE */

/* Flags for mapping */
#define S1_MAP_RW		(S1_PTE_SHARED | S1_PTE_AF | ((unsigned long)S1_ATTR_NORMAL_INDEX << 2))
#define S1_MAP_DEVICE		(S1_PTE_AF | ((unsigned long)S1_ATTR_DEVICE_INDEX << 2))

#define S1_TCR_FLAGS		(TCR_TxSZ(48) | TCR_IRGN_WBWA | TCR_ORGN_WBWA | TCR_SHARED)

#define S1_MAIR_VAL \
	((0x00ULL << (0 * 8)) | /* MT_DEVICE_nGnRnE = 0 */ \
	 (0x04ULL << (1 * 8)) | /* MT_DEVICE_nGnRE = 1 */ \
	 (0x0cULL << (2 * 8)) | /* MT_DEVICE_GRE = 2 */ \
	 (0x44ULL << (3 * 8)) | /* MT_NORMAL_NC = 3 */ \
	 (0xffULL << (4 * 8)) | /* MT_NORMAL = 4 */ \
	 (0xbbULL << (5 * 8)) | /* MT_NORMAL_WT = 5 */ \
	 (0x08ULL << (6 * 8)) | /* MT_DEVICE_nGRE = 6 */ \
	 (0xf0ULL << (7 * 8)))  /* MT_NORMAL_TAGGED = 7 */

struct s1_mmu {
	struct s2_mmu *s2mmu;
	unsigned long *pgd;
	enum s1_granule granule;
	unsigned int page_shift;
	unsigned int level_shift;
	int root_level;
	unsigned long page_size;
	unsigned long block_size;
};

/* API */
struct s1_mmu *s1mmu_init(struct s2_mmu *s2mmu, enum s1_granule granule);
void s1mmu_destroy(struct s1_mmu *mmu);
int s1mmu_map(struct s1_mmu *mmu, unsigned long va, unsigned long ipa,
	      unsigned long size, unsigned long flags);
int s1mmu_verify_mapping(struct s1_mmu *mmu, unsigned long va,
			 unsigned long size, unsigned long expected_page_size);
void s1mmu_enable(unsigned long pgd_pa, enum s1_granule granule);

#endif /* _ASMARM64_STAGE1_MMU_H_ */


