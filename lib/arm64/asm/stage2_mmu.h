/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef _ASMARM64_STAGE2_MMU_H_
#define _ASMARM64_STAGE2_MMU_H_

#include <libcflat.h>
#include <asm/page.h>
#include <asm/pgtable.h>

#define pte_is_table(pte)	(pte_val(pte) & PTE_TABLE_BIT)

/* Stage-2 Memory Attributes (MemAttr[3:0]) */
#define S2_MEMATTR_NORMAL	(0xFUL << 2) /* Normal Memory, Outer/Inner Write-Back */
#define S2_MEMATTR_DEVICE	(0x0UL << 2) /* Device-nGnRnE */

#define ESR_ELx_EC_SHIFT	(26)
#define ESR_ELx_EC_HVC64	UL(0x16)
#define ESR_ELx_EC_DABT_LOW	UL(0x24)

/* Stage-2 Access Permissions (S2AP[1:0]) */
#define S2AP_NONE	(0UL << 6)
#define S2AP_RO		(1UL << 6) /* Read-only */
#define S2AP_WO		(2UL << 6) /* Write-only */
#define S2AP_RW		(3UL << 6) /* Read-Write */

/* Flags for mapping */
#define S2_MAP_RW	(S2AP_RW | S2_MEMATTR_NORMAL | PTE_AF | PTE_SHARED)
#define S2_MAP_DEVICE	(S2AP_RW | S2_MEMATTR_DEVICE | PTE_AF)

enum s2_granule {
	S2_PAGE_4K,
	S2_PAGE_16K,
	S2_PAGE_64K,
};

/* Main Stage-2 MMU Structure */
struct s2_mmu {
	pgd_t *pgd;
	int vmid;

	/* Configuration */
	enum s2_granule granule;
	bool allow_block_mappings;

	/* Internal helpers calculated from granule & VA_BITS */
	unsigned int page_shift;
	unsigned int level_shift;
	int root_level; /* 0, 1, or 2 */
	unsigned long page_size;
	unsigned long block_size;
};

/* API */
/* Initialize an s2_mmu struct with specific settings */
struct s2_mmu *s2mmu_init(int vmid, enum s2_granule granule, bool allow_block_mappings);

/* Management */
void s2mmu_destroy(struct s2_mmu *mmu);
void s2mmu_map(struct s2_mmu *mmu, unsigned long ipa, unsigned long pa,
	       unsigned long size, unsigned long flags);
void s2mmu_unmap(struct s2_mmu *mmu, unsigned long ipa, unsigned long size);

/* Activation */
void s2mmu_enable(struct s2_mmu *mmu);
void s2mmu_disable(struct s2_mmu *mmu);

/* Debug */
void s2mmu_print_fault_info(void);

#endif /* _ASMARM64_STAGE2_MMU_H_ */
