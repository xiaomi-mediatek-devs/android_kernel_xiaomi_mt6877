// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2021 MediaTek Inc.
 */


#include <linux/of.h>
#include <linux/clk.h>
#include <linux/devfreq.h>
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#include <linux/devfreq_cooling.h>
#endif
#include <linux/version.h>
#include <linux/pm_opp.h>
#include <mali_kbase.h>

#include <platform/mtk_platform_common.h>
#include <backend/gpu/mali_kbase_pm_internal.h>
#include <backend/gpu/mali_kbase_pm_defs.h>
#include "governor.h"
#include <platform/mtk_platform_common/mtk_platform_devfreq.h>
#include <mtk_gpufreq.h>

/* use mali devfreq and adjust freq by gpufreq base on simple_ondemand */
static int mtk_mali_devfreq_target(struct device *dev,
						unsigned long *target_freq, u32 flags)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	struct dev_pm_opp *opp;
	unsigned long nominal_freq;
	int ret = 0;
	int limited_idx = 0;

	nominal_freq = *target_freq;

#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_lock();
#endif
	opp = devfreq_recommended_opp(dev, &nominal_freq, flags);
#if KERNEL_VERSION(4, 11, 0) > LINUX_VERSION_CODE
	rcu_read_unlock();
#endif
	if (IS_ERR_OR_NULL(opp)) {
		dev_err(dev, "Failed to get opp (%d)\n", PTR_ERR_OR_ZERO(opp));
		return IS_ERR(opp) ? PTR_ERR(opp) : -ENODEV;
	}
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
	dev_pm_opp_put(opp);
#endif

	/*
	 * Only update if there is a change of frequency
	 */
	if (kbdev->current_nominal_freq == nominal_freq) {
		*target_freq = nominal_freq;
		return 0;
	}

	limited_idx = mt_gpufreq_get_opp_idx_by_freq(
			(unsigned int)(nominal_freq / 1000));

	/*
	 * Only update when GPU is power on.
	 * need power on GPU and power off after commit.
	 */

	ret = mtk_common_gpufreq_commit(limited_idx);
	if (ret < 0) {
		dev_info(kbdev->dev, "Failed to target freq idx[%d]\n", limited_idx);
		return -EINVAL;
	}

	*target_freq = nominal_freq;
	kbdev->current_nominal_freq = nominal_freq;

	return 0;
}


void mtk_mali_devfreq_update_profile(struct devfreq_dev_profile *dp)
{
	dp->target = mtk_mali_devfreq_target;
}

/* this for svs adjust framework opp */
void mtk_devfreq_update_voltage(void)
{
	int opp_num = 0;
	struct kbase_device *kbdev = (struct kbase_device *)mtk_common_get_kbdev();
	int i;
	unsigned long tmp_volt = 0;
	unsigned long tmp_freq = 0;

	opp_num = mt_gpufreq_get_dvfs_table_num() - 1;

	if (opp_num <= 0)
		return;

	for (i = 0; i < opp_num; i++)
	{
		tmp_volt = (unsigned long)mt_gpufreq_get_volt_by_idx(i) * 10;
		tmp_freq = (unsigned long)mt_gpufreq_get_freq_by_idx(i) * 1000;

		if (tmp_volt == 0 || tmp_freq == 0)
			continue;
		dev_pm_opp_adjust_voltage(kbdev->dev, tmp_freq, tmp_volt);
	}
}
