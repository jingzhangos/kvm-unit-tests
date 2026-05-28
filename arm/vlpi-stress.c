/*
 * Copyright (C) 2026 Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <libcflat.h>
#include <asm/io.h>
#include <asm/smp.h>
#include <asm/gic.h>
#include <asm/arch_gicv3.h>
#include <asm/gic-v3-its.h>
#include <asm/stage2_mmu.h>
#include <asm/guest.h>

#define GITS_TRANSLATER_IPA 0x800000000UL

struct shared_data {
	int vlpi_triggered;
	int vlpi_injected;
	int vlpi_handled;
	int pending_vlpis;
};

static volatile struct shared_data test_data = { 0 };

static void guest_sender(void)
{
	volatile u64 *gits_translater = (void *)GITS_TRANSLATER_IPA;
	u32 event;
	int i;

	for (i = 0; i < 100; i++) {
		for (event = 0; event < 32; event++) {
			 /* To avoid irq storem on QEMU TCG: udelay(300) */
			udelay(300);
			test_data.vlpi_triggered++;
			*gits_translater = event;
		}
	}
	asm ("hvc #0");
}

static void guest_receiver(void)
{
	local_irq_enable();

	while (true)
		asm ("wfi");
}

/* IRQ handler for receiver guest */
static void receiver_irq_handler(struct guest_el1_regs *regs, unsigned int esr)
{
	u32 iar = gic_read_iar();

	test_data.vlpi_handled++;
	gic_write_eoir(iar);
}

/* Receiver's host vLPI injection */
static void receiver_irq_injection(void)
{
	u32 iar = gic_read_iar();
	int irq = iar & GICC_IAR_INT_ID_MASK;
	int local_pending = __atomic_exchange_n(&test_data.pending_vlpis, 0, __ATOMIC_ACQUIRE);
	int num_lr = (read_sysreg(ich_vtr_el2) & 0x1F) + 1;

	if (irq < 1020) {
		gic_write_eoir(iar);

		if (local_pending == 0) {
			gic_disable_irq(25);
			return;
		}
		/* Check pending bits for THIS CPU */
		int i;
		for (i = 0; i < 32; i++) {
			if (local_pending & (1 << i)) {
				u64 lr = (1UL << 62) | (1UL << 60) | (8192 + i);
				u64 elrsr = read_sysreg(ich_elrsr_el2);
				if (!elrsr) {
					gic_enable_irq(25);
					__atomic_fetch_or(&test_data.pending_vlpis, local_pending, __ATOMIC_RELAXED);
					return;
				}

				if (num_lr > 8)
					num_lr = 8;
				switch (fls(elrsr) % num_lr) {
				case 0:
					write_sysreg(lr, ich_lr0_el2);
					break;
				case 1:
					write_sysreg(lr, ich_lr1_el2);
					break;
				case 2:
					write_sysreg(lr, ich_lr2_el2);
					break;
				case 3:
					write_sysreg(lr, ich_lr3_el2);
					break;
				case 4:
					write_sysreg(lr, ich_lr4_el2);
					break;
				case 5:
					write_sysreg(lr, ich_lr5_el2);
					break;
				case 6:
					write_sysreg(lr, ich_lr6_el2);
					break;
				case 7:
					write_sysreg(lr, ich_lr7_el2);
					break;
				}

				local_pending &= ~(1 << i);
				test_data.vlpi_injected++;
			}
		}
	}
}

static int countSetBits(unsigned long n) {
    int count = 0;
    while (n > 0) {
        n &= (n - 1);
        count++;
    }
    return count;
}

/* Helper to decode write value from ESR */
static bool esr_get_write_value(struct guest *guest, unsigned long *val, int *width)
{
    unsigned long esr = guest->esr_el2;

    /* Check if Syndrome is Valid (ISV bit 24) */
    if (!((esr >> 24) & 1)) {
        report_info("Abort ISV is 0. Instruction decoding required (e.g., STP/LDP).");
        return false;
    }

    /* Check if this was a Write (WnR bit 6) */
    if (!((esr >> 6) & 1)) {
        report_info("Abort was a Read, not a Write.");
        return false;
    }

    /* Get the Source Register (SRT bits 20:16) */
    int srt = (esr >> 16) & 0x1F;

    /*
     * Get the Access Size (SAS bits 23:22)
     * 0=Byte, 1=Halfword, 2=Word, 3=Doubleword
     */
    int sas = (esr >> 22) & 0x3;
    *width = 1 << sas; // 1, 2, 4, or 8 bytes

    /* Retrieve the value from the Guest Context */
    if (srt == 31) {
        /* If SRT is 31, for Store operations, it means the Zero Register (XZR/WZR) */
        *val = 0;
    } else {
        *val = guest->x[srt];
    }

    /* Mask value based on width (Optional, but good for emulation) */
    if (*width < 8) {
        unsigned long mask = (1UL << (*width * 8)) - 1;
        *val &= mask;
    }

    return true;
}

static void receiver_guest_loop(void)
{
	struct guest *guest;
	unsigned long data_base;

	guest = guest_create(smp_processor_id(), guest_receiver, S2_PAGE_4K);

	/* Identity map the shared variable */
	data_base = virt_to_phys((void *)&test_data) & PAGE_MASK;
	s2mmu_map(guest->s2mmu, data_base, data_base, PAGE_SIZE, S2_MAP_RW);

	guest_install_el1_handler(guest, ELxH_IRQ, receiver_irq_handler);

	gicv3_enable_defaults();

	report_info("Receiver: starts");
	while (true) {
		guest_run(guest);

		if (guest->exit_code == ELx_LOW_IRQ_64) {
			receiver_irq_injection();
		} else {
			report_fail("Receiver: unknown exit code: 0x%lx", guest->exit_code);
			break;
		}
	}
}

static void sender_guest_loop(void)
{
	struct guest *guest;
	unsigned long far, ec, data_base;
	int cpu = smp_processor_id();

	guest = guest_create(cpu, guest_sender, S2_PAGE_4K);

	/* Identity map the shared variable */
	data_base = virt_to_phys((void *)&test_data) & PAGE_MASK;
	s2mmu_map(guest->s2mmu, data_base, data_base, PAGE_SIZE, S2_MAP_RW);

	gicv3_enable_defaults();

	report_info("Sender: starts");

	while (true) {
		guest_run(guest);

		if (guest->exit_code == ELx_LOW_SYNC_64) {
			ec = guest->esr_el2 >> ESR_ELx_EC_SHIFT;

			if (ec == ESR_ELx_EC_HVC64) {
				report_info("Sender: exits");
				break;
			} else if (ec == ESR_ELx_EC_DABT_LOW) {
				far = guest->far_el2;
				if ((far & PAGE_MASK) == GITS_TRANSLATER_IPA) {
					unsigned long event_id;
					int target_cpu = 1;
					int width;

					/* Receiver queue is full, retry later */
					if (countSetBits(test_data.pending_vlpis) >= 4)
						continue;

					guest->elr_el2 += 4;

					if (!esr_get_write_value(guest, &event_id, &width)) {
						report_fail("Failed to decode written event id.");
						break;
					}

					__atomic_fetch_or(&test_data.pending_vlpis,
							  (1 << event_id), __ATOMIC_RELAXED);
					gic_ipi_send_single(0, target_cpu);
				} else {
					report_fail("Sender: unexpected abort: 0x%lx", far);
					break;
				}
			}
		} else {
			report_fail("Sender: unknown exit code: 0x%lx", guest->exit_code);
			break;
		}
	}
}

int main(int argc, char **argv)
{
	report_prefix_push("gic-v3-vlpi-stress");

	if (gic_init() < 3) {
		report_skip("GIC version is less than 3");
		return report_summary();
	}

	if (nr_cpus < 2) {
		report_skip("Need at least 2 CPUs");
		return report_summary();
	}

	smp_boot_secondary(1, receiver_guest_loop);
	sender_guest_loop();

	/* Wait for receiver to complete */
	for (int count = 0; count < 100; count++) {
		if (test_data.vlpi_handled >= test_data.vlpi_injected && test_data.vlpi_injected > 0)
			break;
		mdelay(1);
	}
	report(test_data.vlpi_triggered > 0, "vLPI Triggered: %d", test_data.vlpi_triggered);
	report(test_data.vlpi_injected > 0, "vLPI Injected: %d", test_data.vlpi_injected);
	report(test_data.vlpi_handled > 0, "vLPI Handled: %d", test_data.vlpi_handled);

	return report_summary();
}
