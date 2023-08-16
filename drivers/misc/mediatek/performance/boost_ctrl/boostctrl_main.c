// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 MediaTek Inc.
 */

#include <linux/proc_fs.h>

#include "boost_ctrl.h"

int init_boostctrl(struct proc_dir_entry *parent)
{
	struct proc_dir_entry *bstctrl_root = NULL;
#ifdef CONFIG_MTK_EAS_CTRL
	struct proc_dir_entry *easctrl_root = NULL;
#endif

	pr_debug("__init %s\n", __func__);


	bstctrl_root = proc_mkdir("boost_ctrl", parent);

#ifdef CONFIG_MTK_TOPO_CTRL
    /* init topology info first */
	topo_ctrl_init(bstctrl_root);
#endif

#ifdef CONFIG_MTK_CPU_CTRL
	cpu_ctrl_init(bstctrl_root);
#endif

	dram_ctrl_init(bstctrl_root);

#ifdef CONFIG_MTK_EAS_CTRL
	/* EAS */
	easctrl_root = proc_mkdir("eas_ctrl", bstctrl_root);
	uclamp_ctrl_init(easctrl_root);
	eas_ctrl_init(easctrl_root);
#endif

	return 0;
}
