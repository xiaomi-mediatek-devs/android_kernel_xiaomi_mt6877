// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2022 MediaTek Inc.
 */

#ifndef __MTK_GPU_DEVFREQ_GOVERNOR_H__
#define __MTK_GPU_DEVFREQ_GOVERNOR_H__

#include <linux/devfreq.h>

void mtk_mali_devfreq_update_profile(struct devfreq_dev_profile *dp);
void mtk_devfreq_update_voltage(void);

#endif /* __MTK_GPU_DEVFREQ_GOVERNOR_H__ */
