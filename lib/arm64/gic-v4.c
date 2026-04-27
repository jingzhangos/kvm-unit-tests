/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#include <asm/smp.h>
#include <asm/gic-v4.h>
#include <alloc_page.h>

struct gicv4_vpe *gicv4_alloc_vpe(u16 vpe_id)
{
	struct gicv4_vpe *vpe;

	vpe = malloc(sizeof(struct gicv4_vpe));
	if (!vpe) {
		printf("Error: VPE allocation failed!\n");
		return vpe;
	}

	vpe->vpt_page = gicv3_data.vlpi_pend[smp_processor_id()];
	vpe->vpe_id = vpe_id;
	vpe->resident = false;
	return vpe;
}

void gicv4_free_vpe(struct gicv4_vpe *vpe)
{
	free(vpe);
}

void gicv4_schedule_vpe(struct gicv4_vpe *vpe)
{
	u64 val;
	void *rdist_base = gicv3_redist_base();


	val = readq(rdist_base + GICR_VPENDBASER);
	val |= GICR_VPENDBASER_VALID;

	writeq(val, rdist_base + GICR_VPENDBASER);

	while (readq(rdist_base + GICR_VPENDBASER) & GICR_VPENDBASER_DIRTY)
		cpu_relax();

	vpe->resident = true;
}

void gicv4_deschedule_vpe(struct gicv4_vpe *vpe)
{
	void *rdist_base = gicv3_redist_base();

	/* To deschedule, we can simply clear the Valid bit */
	u64 val = readq(rdist_base + GICR_VPENDBASER);
	val &= ~GICR_VPENDBASER_VALID;

	writeq(val, rdist_base + GICR_VPENDBASER);
	while (readq(rdist_base + GICR_VPENDBASER) & GICR_VPENDBASER_DIRTY)
		cpu_relax();
	vpe->resident = false;
}

static void its_build_vmapp_cmd(struct its_cmd_block *cmd, struct its_cmd_desc *desc)
{
	struct its_cmd_desc_v4 *d = (struct its_cmd_desc_v4 *)desc;
	struct gicv4_vpe *vpe = d->vmapp.vpe;
	bool valid = d->vmapp.valid;

	its_encode_cmd(cmd, GITS_CMD_VMAPP);
	its_encode_valid(cmd, valid);
	its_encode_vpeid(cmd, vpe->vpe_id);
	its_encode_target(cmd, smp_processor_id());
	if (valid) {
		its_encode_vpt_addr(cmd, virt_to_phys(vpe->vpt_page));
		its_encode_vpt_size(cmd, 13);
	} else {
		cmd->raw_cmd[3] = 0;
	}
}

static void its_build_vmapti_cmd(struct its_cmd_block *cmd, struct its_cmd_desc *desc)
{
	struct its_cmd_desc_v4 *d = (struct its_cmd_desc_v4 *)desc;
	struct its_vmapti_cmd *args = &d->vmapti;

	its_encode_cmd(cmd, GITS_CMD_VMAPTI);
	its_encode_devid(cmd, args->dev->device_id);
	its_encode_event_id(cmd, args->event_id);
	its_encode_vpeid(cmd, args->vpe_id);
	its_encode_virt_id(cmd, args->vlpi_id);
	its_encode_db_phys_id(cmd, 1023);
}

static void its_build_vsync_cmd(struct its_cmd_block *cmd, struct its_cmd_desc *desc)
{
	struct its_cmd_desc_v4 *d = (struct its_cmd_desc_v4 *)desc;

	its_encode_cmd(cmd, GITS_CMD_VSYNC);
	its_encode_vpeid(cmd, d->vsync.vpe->vpe_id);
}

static void its_build_vinvall_cmd(struct its_cmd_block *cmd, struct its_cmd_desc *desc)
{
	struct its_cmd_desc_v4 *d = (struct its_cmd_desc_v4 *)desc;

	its_encode_cmd(cmd, GITS_CMD_VINVALL);
	its_encode_vpeid(cmd, d->vsync.vpe->vpe_id);
}

/* Public Send Wrappers */
void its_send_vmapp(struct gicv4_vpe *vpe, bool valid)
{
	struct its_cmd_desc_v4 desc;
	desc.vmapp.vpe = vpe;
	desc.vmapp.valid = valid;
	its_send_single_command(its_build_vmapp_cmd, (struct its_cmd_desc *)&desc);
}

void its_send_vmapti(struct its_device *dev, u32 id, u32 vpe_id, u32 vlpi_id)
{
	struct its_cmd_desc_v4 desc;
	desc.vmapti.dev = dev;
	desc.vmapti.event_id = id;
	desc.vmapti.vpe_id = vpe_id;
	desc.vmapti.vlpi_id = vlpi_id;
	its_send_single_command(its_build_vmapti_cmd, (struct its_cmd_desc *)&desc);
}

void its_send_vsync(struct gicv4_vpe *vpe)
{
	struct its_cmd_desc_v4 desc;
	desc.vsync.vpe = vpe;
	its_send_single_command(its_build_vsync_cmd, (struct its_cmd_desc *)&desc);
}

void its_send_vinvall(struct gicv4_vpe *vpe)
{
	struct its_cmd_desc_v4 desc;
	desc.vinvall.vpe = vpe;
	its_send_single_command(its_build_vinvall_cmd, (struct its_cmd_desc *)&desc);
}
