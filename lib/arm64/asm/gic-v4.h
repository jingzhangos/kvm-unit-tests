/*
 * Copyright (C) 2026, Google LLC.
 * Author: Jing Zhang <jingzhangos@google.com>
 *
 * SPDX-License-Identifier: LGPL-2.0-or-later
 */
#ifndef _ASMARM_GIC_V4_H_
#define _ASMARM_GIC_V4_H_

#include <asm/gic.h>
#include <asm/gic-v3-its.h>

/* ITS GICv4 Commands */
#define GITS_CMD_VMAPP			0x29
#define GITS_CMD_VMAPTI			0x2A
#define GITS_CMD_VSYNC			0x25
#define GITS_CMD_VINVALL		0x2D

struct gicv4_vpe {
	u16 vpe_id;
	void *vpt_page;		/* Virtual Pending Table */
	void *conf_table;	/* Pointer to shared Config Table */
	bool resident;		/* Is currently scheduled on a Redistributor? */
};

/* Command Descriptors for GICv4 */
struct its_vmapp_cmd {
	struct gicv4_vpe *vpe;
	bool valid;
};

struct its_vmapti_cmd {
	struct its_device *dev;
	u32 event_id;
	u32 vpe_id;
	u32 vlpi_id;
};

struct its_vinvall_cmd {
	struct gicv4_vpe *vpe;
};

struct its_vsync_cmd {
	struct gicv4_vpe *vpe;
};

struct its_cmd_desc_v4 {
	union {
		struct its_vmapp_cmd vmapp;
		struct its_vmapti_cmd vmapti;
		struct its_vinvall_cmd vinvall;
		struct its_vsync_cmd vsync;
	};
};

void its_send_vmapp(struct gicv4_vpe *vpe, bool valid);
void its_send_vmapti(struct its_device *dev, u32 id, u32 vpe_id, u32 vlpi_id);
void its_send_vsync(struct gicv4_vpe *vpe);
void its_send_vinvall(struct gicv4_vpe *vpe);

#endif /* _ASMARM_GIC_V4_H_ */
