/*
 * All ITS* defines are lifted from include/linux/irqchip/arm-gic-v3.h
 *
 * Copyright (C) 2020, Red Hat Inc, Eric Auger <eric.auger@redhat.com>
 *
 * This work is licensed under the terms of the GNU LGPL, version 2.
 */
#ifndef _ASMARM64_GIC_V3_ITS_H_
#define _ASMARM64_GIC_V3_ITS_H_

#ifndef _ASMARM_GIC_H_
#error Do not directly include <asm/gic-v3-its.h>. Include <asm/gic.h>
#endif

struct its_typer {
	unsigned int ite_size;
	unsigned int eventid_bits;
	unsigned int deviceid_bits;
	unsigned int collid_bits;
	bool pta;
	bool phys_lpi;
	bool virt_lpi;
	bool virt_sgi;
};

struct its_baser {
	int index;
	size_t psz;
	int esz;
	bool indirect;
	void *table_addr;
};

#define GITS_BASER_NR_REGS		8
#define GITS_MAX_DEVICES		8
#define GITS_MAX_COLLECTIONS		8

struct its_device {
	u32 device_id;	/* device ID */
	u32 nr_ites;	/* Max Interrupt Translation Entries */
	void *itt;	/* Interrupt Translation Table GVA */
};

struct its_collection {
	u64 target_address;
	u16 col_id;
};

struct its_data {
	void *base;
	struct its_typer typer;
	struct its_baser device_baser;
	struct its_baser vpe_baser;
	struct its_baser coll_baser;
	struct its_cmd_block *cmd_base;
	struct its_cmd_block *cmd_write;
	struct its_device devices[GITS_MAX_DEVICES];
	u32 nr_devices;		/* Allocated Devices */
	struct its_collection collections[GITS_MAX_COLLECTIONS];
	u16 nr_collections;	/* Allocated Collections */
};

extern struct its_data its_data;

#define gicv3_its_base()		(its_data.base)

#define GITS_CTLR			0x0000
#define GITS_IIDR			0x0004
#define GITS_TYPER			0x0008
#define GITS_CBASER			0x0080
#define GITS_CWRITER			0x0088
#define GITS_CREADR			0x0090
#define GITS_BASER			0x0100

#define GITS_TYPER_PLPIS		BIT(0)
#define GITS_TYPER_VLPIS		BIT(1)
#define GITS_TYPER_ITT_ENTRY_SIZE	GENMASK_ULL(7, 4)
#define GITS_TYPER_ITT_ENTRY_SIZE_SHIFT	4
#define GITS_TYPER_IDBITS		GENMASK_ULL(12, 8)
#define GITS_TYPER_IDBITS_SHIFT		8
#define GITS_TYPER_DEVBITS		GENMASK_ULL(17, 13)
#define GITS_TYPER_DEVBITS_SHIFT	13
#define GITS_TYPER_PTA			BIT(19)
#define GITS_TYPER_CIDBITS		GENMASK_ULL(35, 32)
#define GITS_TYPER_CIDBITS_SHIFT	32
#define GITS_TYPER_CIL			BIT(36)
#define GITS_TYPER_VSGI			BIT(39)

#define GITS_CTLR_ENABLE		(1U << 0)

#define GITS_CBASER_VALID		(1UL << 63)

#define GITS_BASER_VALID		BIT(63)
#define GITS_BASER_INDIRECT		BIT(62)
#define GITS_BASER_TYPE_SHIFT		(56)
#define GITS_BASER_TYPE(r)		(((r) >> GITS_BASER_TYPE_SHIFT) & 7)
#define GITS_BASER_ENTRY_SIZE_SHIFT	(48)
#define GITS_BASER_ENTRY_SIZE(r)	((((r) >> GITS_BASER_ENTRY_SIZE_SHIFT) & 0x1f) + 1)
#define GITS_BASER_PAGE_SIZE_SHIFT	(8)
#define GITS_BASER_PAGE_SIZE_4K		(0UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGE_SIZE_16K	(1UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGE_SIZE_64K	(2UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGE_SIZE_MASK	(3UL << GITS_BASER_PAGE_SIZE_SHIFT)
#define GITS_BASER_PAGES_MAX		256
#define GITS_BASER_PAGES_SHIFT		(0)
#define GITS_BASER_NR_PAGES(r)		(((r) & 0xff) + 1)
#define GITS_BASER_PHYS_ADDR_MASK	0xFFFFFFFFF000
#define GITS_BASER_TYPE_NONE		0
#define GITS_BASER_TYPE_DEVICE		1
#define GITS_BASER_TYPE_VPE		2
#define GITS_BASER_TYPE_COLLECTION	4

/*
 * ITS commands
 */
#define GITS_CMD_MAPD			0x08
#define GITS_CMD_MAPC			0x09
#define GITS_CMD_MAPTI			0x0a
#define GITS_CMD_MAPI			0x0b
#define GITS_CMD_MOVI			0x01
#define GITS_CMD_DISCARD		0x0f
#define GITS_CMD_INV			0x0c
#define GITS_CMD_MOVALL			0x0e
#define GITS_CMD_INVALL			0x0d
#define GITS_CMD_INT			0x03
#define GITS_CMD_CLEAR			0x04
#define GITS_CMD_SYNC			0x05

struct its_cmd_block {
	u64 raw_cmd[4];
};

struct its_cmd_desc {
	union {
		struct {
			struct its_device *dev;
			u32 event_id;
		} its_inv_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_int_cmd;

		struct {
			struct its_device *dev;
			bool valid;
		} its_mapd_cmd;

		struct {
			struct its_collection *col;
			bool valid;
		} its_mapc_cmd;

		struct {
			struct its_device *dev;
			u32 phys_id;
			u32 event_id;
			u32 col_id;
		} its_mapti_cmd;

		struct {
			struct its_device *dev;
			struct its_collection *col;
			u32 event_id;
		} its_movi_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_discard_cmd;

		struct {
			struct its_device *dev;
			u32 event_id;
		} its_clear_cmd;

		struct {
			struct its_collection *col;
		} its_invall_cmd;

		struct {
			struct its_collection *col;
		} its_sync_cmd;
	};
	bool verbose;
};

typedef void (*its_cmd_builder_t)(struct its_cmd_block *, struct its_cmd_desc *);

extern void its_encode_cmd(struct its_cmd_block *cmd, u8 cmd_nr);
extern void its_encode_devid(struct its_cmd_block *cmd, u32 devid);
extern void its_encode_event_id(struct its_cmd_block *cmd, u32 id);
extern void its_encode_valid(struct its_cmd_block *cmd, int valid);
extern void its_encode_vpeid(struct its_cmd_block *cmd, u16 vpeid);
extern void its_encode_target(struct its_cmd_block *cmd, u64 target_addr);
extern void its_encode_vpt_addr(struct its_cmd_block *cmd, u64 vpt_pa);
extern void its_encode_vpt_size(struct its_cmd_block *cmd, u8 vpt_size);
extern void its_encode_virt_id(struct its_cmd_block *cmd, u32 virt_id);
extern void its_encode_db_phys_id(struct its_cmd_block *cmd, u32 db_phys_id);

extern void its_parse_typer(void);
extern void its_init(void);
extern int its_baser_lookup(int i, struct its_baser *baser);
extern void its_enable_defaults(void);
extern struct its_device *its_create_device(u32 dev_id, int nr_ites);
extern struct its_collection *its_create_collection(u16 col_id, u32 target_pe);

extern void its_send_single_command(its_cmd_builder_t builder, struct its_cmd_desc *desc);
extern void __its_send_mapd(struct its_device *dev, int valid, bool verbose);
extern void __its_send_mapc(struct its_collection *col, int valid, bool verbose);
extern void __its_send_mapti(struct its_device *dev, u32 irq_id, u32 event_id,
			     struct its_collection *col, bool verbose);
extern void __its_send_int(struct its_device *dev, u32 event_id, bool verbose);
extern void __its_send_inv(struct its_device *dev, u32 event_id, bool verbose);
extern void __its_send_discard(struct its_device *dev, u32 event_id, bool verbose);
extern void __its_send_clear(struct its_device *dev, u32 event_id, bool verbose);
extern void __its_send_invall(struct its_collection *col, bool verbose);
extern void __its_send_movi(struct its_device *dev, struct its_collection *col,
			    u32 id, bool verbose);
extern void __its_send_sync(struct its_collection *col, bool verbose);

#define its_send_mapd(dev, valid)			__its_send_mapd(dev, valid, true)
#define its_send_mapc(col, valid)			__its_send_mapc(col, valid, true)
#define its_send_mapti(dev, irqid, eventid, col)	__its_send_mapti(dev, irqid, eventid, col, true)
#define its_send_int(dev, eventid)			__its_send_int(dev, eventid, true)
#define its_send_inv(dev, eventid)			__its_send_inv(dev, eventid, true)
#define its_send_discard(dev, eventid)			__its_send_discard(dev, eventid, true)
#define its_send_clear(dev, eventid)			__its_send_clear(dev, eventid, true)
#define its_send_invall(col)				__its_send_invall(col, true)
#define its_send_movi(dev, col, id)			__its_send_movi(dev, col, id, true)
#define its_send_sync(col)				__its_send_sync(col, true)

#define its_send_mapd_nv(dev, valid)			__its_send_mapd(dev, valid, false)
#define its_send_mapc_nv(col, valid)			__its_send_mapc(col, valid, false)
#define its_send_mapti_nv(dev, irqid, eventid, col)	__its_send_mapti(dev, irqid, eventid, col, false)
#define its_send_int_nv(dev, eventid)			__its_send_int(dev, eventid, false)
#define its_send_inv_nv(dev, eventid)			__its_send_inv(dev, eventid, false)
#define its_send_discard_nv(dev, eventid)		__its_send_discard(dev, eventid, false)
#define its_send_clear_nv(dev, eventid)			__its_send_clear(dev, eventidn false)
#define its_send_invall_nv(col)				__its_send_invall(col, false)
#define its_send_movi_nv(dev, col, id)			__its_send_movi(dev, col, id, false)
#define its_send_sync_nv(col)				__its_send_sync(col, false)

extern struct its_device *its_get_device(u32 id);
extern struct its_collection *its_get_collection(u32 id);

#define is_gicv4()	(its_data.typer.virt_lpi)

#endif /* _ASMARM64_GIC_V3_ITS_H_ */
