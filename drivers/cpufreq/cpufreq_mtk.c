/*
 * drivers/cpufreq/cpufreq_mtk.c
 *
 * Copyright (C) 2022-2023 bengris32
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/module.h>
#include <linux/sysfs.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/cpufreq.h>
#include <cpu_ctrl.h>

#define CLUSTER_NUM 2
#define LITTLE 0
#define BIG 1

#define DEFINE_CPUFREQ_ATTR(attr_name, cluster, freq_field)  \
    static ssize_t show_##attr_name(struct kobject *kobj,    \
                    struct kobj_attribute *attr, char *buf)  \
    {                                                        \
        return sprintf(buf, "%d\n", current_cpu_freq[cluster].freq_field); \
    }                                                        \
                                                             \
    static ssize_t store_##attr_name(struct kobject *kobj,   \
                    struct kobj_attribute *attr, const char *buf, \
                    size_t count)                            \
    {                                                        \
        int ret, new_freq;                                   \
                                                             \
        ret = sscanf(buf, "%d", &new_freq);                  \
        if (ret != 1)                                        \
            return -EINVAL;                                  \
                                                             \
        ret = set_##freq_field(cluster, new_freq);           \
                                                             \
        if (ret)                                             \
            return ret;                                      \
                                                             \
        return count;                                        \
    }                                                        \
                                                             \
    static struct kobj_attribute attr_name##_attr =          \
        __ATTR(attr_name, 0644, show_##attr_name, store_##attr_name)

static struct cpu_ctrl_data *current_cpu_freq;
DEFINE_MUTEX(cpufreq_mtk_mutex);

/* Sets current maximum CPU frequency */
int set_max(int cluster, int max)
{
    int ret, old_max;

    mutex_lock(&cpufreq_mtk_mutex);

    old_max = current_cpu_freq[cluster].max;
    current_cpu_freq[cluster].max = max;

    ret = update_userlimit_cpu_freq(CPU_KIR_PERF, CLUSTER_NUM, current_cpu_freq);
    if (ret)
        /*
         * update_userlimit_cpu_freq() does it's own checks, so we
         * don't need to do it here. If it fails, we will just restore
         * the old value. (the frequency change didn't apply)
         */
        current_cpu_freq[cluster].max = old_max;

    mutex_unlock(&cpufreq_mtk_mutex);
    return ret;
}

/* Sets current minimum CPU frequency */
int set_min(int cluster, int min)
{
    int ret, old_min;

    mutex_lock(&cpufreq_mtk_mutex);

    old_min = current_cpu_freq[cluster].min;
    current_cpu_freq[cluster].min = min;

    ret = update_userlimit_cpu_freq(CPU_KIR_PERF, CLUSTER_NUM, current_cpu_freq);
    if (ret)
        /*
         * update_userlimit_cpu_freq() does it's own checks, so we
         * don't need to do it here. If it fails, we will just restore
         * the old value. (the frequency change didn't apply)
         */
        current_cpu_freq[cluster].min = old_min;

    mutex_unlock(&cpufreq_mtk_mutex);
    return ret;
}

DEFINE_CPUFREQ_ATTR(lcluster_min_freq, LITTLE, min);
DEFINE_CPUFREQ_ATTR(lcluster_max_freq, LITTLE, max);
DEFINE_CPUFREQ_ATTR(bcluster_min_freq, BIG, min);
DEFINE_CPUFREQ_ATTR(bcluster_max_freq, BIG, max);

static struct attribute *mtk_param_attributes[] = {
    &lcluster_min_freq_attr.attr,
    &lcluster_max_freq_attr.attr,
    &bcluster_min_freq_attr.attr,
    &bcluster_max_freq_attr.attr,
    NULL,
};

static struct attribute_group mtk_param_attr_group = {
    .attrs = mtk_param_attributes,
    .name = "mtk",
};

static int __init cpufreq_mtk_init(void)
{
    int ret;

    current_cpu_freq = kcalloc(CLUSTER_NUM, sizeof(struct cpu_ctrl_data), GFP_KERNEL);

    if (!current_cpu_freq) {
        pr_err("[%s] Could not allocate memory for current_cpu_freq!\n", __func__);
        ret = -ENOMEM;
        goto out;
    }

    current_cpu_freq[LITTLE].min = -1;
    current_cpu_freq[BIG].min = -1;
    current_cpu_freq[LITTLE].max = -1;
    current_cpu_freq[BIG].max = -1;

    if (!cpufreq_global_kobject) {
        pr_err("[%s] !cpufreq_global_kobject\n", __func__);
        ret = -ENODEV;
        goto out;
    }

    ret = sysfs_create_group(cpufreq_global_kobject, &mtk_param_attr_group);
    if (ret) {
        pr_err("[%s] sysfs_create_group failed!\n", __func__);
        ret = -ENOMEM;
        goto out;
    }

out:
    return ret;
}

static void __exit cpufreq_mtk_exit(void)
{
    pr_debug("[%s] Driver unloading.", __func__);
    sysfs_remove_group(cpufreq_global_kobject, &mtk_param_attr_group);
    kfree(current_cpu_freq);
}

MODULE_DESCRIPTION("CPU frequency control for MediaTek's CPUFreq API");
MODULE_AUTHOR("bengris32");
MODULE_LICENSE("GPL");

module_init(cpufreq_mtk_init);
module_exit(cpufreq_mtk_exit);
