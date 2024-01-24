// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 MediaTek Inc.
 */

#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/sched/clock.h>
#include <apusys_secure.h>
#include <apusys_plat.h>


static void set_dbg_sel(int val, int offset, int shift, int mask)
{
	void *target = apu_top + offset;
	u32 tmp;

	if (apu_top == NULL) /* Skip if apu_top is not valid */
		return;

	tmp = ioread32(target);

	tmp = (tmp & ~(mask << shift)) | (val << shift);
	iowrite32(tmp, target);
	tmp = ioread32(target);
}

u32 dbg_read(struct dbg_mux_sel_value sel)
{
	int i;
	int offset;
	int shift;
	int length;
	int mask;
	void *addr = apu_top + sel.status_reg_offset;
	struct dbg_mux_sel_info info;

	if (apu_top == NULL) /* Skip if apu_top is not valid */
		return 0;

	for (i = 0; i < DBG_MUX_SEL_COUNT; ++i) {
		if (sel.dbg_sel[i] >= 0) {
			info = info_table[i];
			offset = info.offset;
			shift = info.end_bit;
			length = info.start_bit - info.end_bit + 1;
			mask = (1 << length) - 1;

			set_dbg_sel(sel.dbg_sel[i], offset, shift, mask);
		}
	}

	return ioread32(addr);
}

static u32 gals_reg[TOTAL_DBG_MUX_COUNT];

void dump_gals_reg(bool dump_vpu)
{
	int i;

	for (i = 0; i < TOTAL_DBG_MUX_COUNT; ++i)
		gals_reg[i] = dbg_read(value_table[i]);
}

void apusys_reg_dump(void)
{
	int i;
	u32 offset, size;
	char *tmp = reg_all_mem;

	if (apu_top == NULL) /* Skip if apu_top is not valid */
		return;

	dump_gals_reg(false);

	for (i = 0; i < SEGMENT_COUNT; ++i) {
		offset = range_table[i].base - APUSYS_BASE;
		size = range_table[i].size;

		memcpy_fromio(tmp, apu_top + offset, size);
		tmp += size;
	}
}
