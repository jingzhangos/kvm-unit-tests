/*
 * Copyright (C) 2026 Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <libcflat.h>
#include <asm/gic.h>
#include <asm/gic-v3-its.h>
#include <asm/smp.h>
#include <asm/gic-v4.h>
#include <asm/guest.h>

#define TEST_VPE_ID	3
#define TEST_DEV_ID	2
#define TEST_EVENT_ID	4
#define TEST_VLPI_ID	8199

static volatile int irq_received = -1;
static volatile bool send_done = false;

static void guest_irq_handler(struct guest_el1_regs *regs, unsigned int esr)
{
	u32 iar = gic_read_iar();

	(void)regs;
	(void)esr;
	irq_received++;

	gic_write_eoir(iar);
}


static void vlpi_receiver(void)
{
	local_irq_enable();

	irq_received = 0;
	while (!send_done)
		cpu_relax();

	asm volatile ("hvc #0");
}

static void vlpi_sender(void)
{
	struct its_device *dev;

	gicv3_enable_defaults();

	while (irq_received < 0)
		cpu_relax();

	dev = its_get_device(TEST_DEV_ID);
	report_info("Injecting vLPI %d...", TEST_VLPI_ID);
	for (int i = 0; i < 10000; i++) {
		its_send_int_nv(dev, TEST_EVENT_ID);
		udelay(1);
	}
	mdelay(1);
	send_done = true;
	while (true)
		cpu_relax();
}

int main(void)
{
	struct its_device *dev;
	struct gicv4_vpe *vpe;
	struct guest *guest;
	unsigned long ec;

	if (gic_init() < 3) {
		report_skip("No supported gic present");
		return report_summary();
	}

	if (!is_gicv4()) {
		report_skip("No supported gicv4 present");
		return report_summary();
	}

	if (!gicv3_its_base()) {
		report_skip("No ITS detected");
		return report_summary();
	}

	report_prefix_push("gic-v4-vlpi");

	gicv3_enable_defaults();
	smp_boot_secondary(1, vlpi_sender);
	its_enable_defaults();

	guest = guest_create(0, vlpi_receiver, S2_PAGE_4K);

	/* Map Shared Flag */
	unsigned long flag_pa = virt_to_phys((void *)&irq_received);
	s2mmu_map(guest->s2mmu, flag_pa & PAGE_MASK, flag_pa & PAGE_MASK, PAGE_SIZE,
		  S2_MAP_RW);

	guest_install_el1_handler(guest, ELxH_IRQ, guest_irq_handler);

	vpe = gicv4_alloc_vpe(TEST_VPE_ID);

	/* Map vPE to current Redistributor (VMAPP) */
	its_send_vmapp(vpe, true);

	/* Map Device */
	dev = its_create_device(TEST_DEV_ID, 8);
	its_send_mapd(dev, true);

	/* Map Event to vLPI (VMAPTI) */
	its_send_vmapti(dev, TEST_EVENT_ID, TEST_VPE_ID, TEST_VLPI_ID);

	/* Configure vLPI (Enable and set Priority) */
	gicv4_vlpi_set_config(TEST_VLPI_ID, LPI_PROP_DEFAULT);

	/* Schedule vPE (Make resident) */
	gicv4_schedule_vpe(vpe);

	guest_run(guest);

	ec = guest->esr_el2 >> 26;
	if (ec != ESR_ELx_EC_HVC64) {
		report_fail("Guest crashed with ESR: 0x%lx", guest->esr_el2);
	} else {
		report(irq_received, "%d out of 10000 vLPIs Received in guest", irq_received);
	}

	return report_summary();
}
