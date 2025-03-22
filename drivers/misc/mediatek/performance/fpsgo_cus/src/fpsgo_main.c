#include <linux/module.h>

#if defined (CONFIG_MTK_FPSGO) || defined (CONFIG_MTK_FPSGO_V3)
#include "xgf.h"
#endif
#include "fpsgo_ko.h"

#line __LINE__ "vendor/mediatek/kernel_modules/fpsgo_cus/src/fpsgo_main.c"

static void __exit fpsgo_exit(void) {}

static int __init fpsgo_init(void)
{
#ifdef CONFIG_MTK_FPSGO
	xgf_est_slptime_fp = xgf_est_slptime;
#endif

#ifdef CONFIG_MTK_FPSGO_V3
	int ret;

	ret = xgf_ko_init();

	pr_debug("%s %d: xgf_ko_init %d", __func__, __LINE__, ret);

	if (ret)
		return -1;

	xgf_est_runtime_fp = xgf_est_runtime;
	fpsgo_xgf2ko_calculate_target_fps_fp = fpsgo_xgf2ko_calculate_target_fps;
	fpsgo_xgf2ko_do_recycle_fp = fpsgo_xgf2ko_do_recycle;

	xgff_est_runtime_fp = xgff_est_runtime;
	xgff_update_start_prev_index_fp = xgff_update_start_prev_index;

	xgf_ema2_predict_fp = xgf_ema2_predict;
	xgf_ema2_init_fp = xgf_ema2_init;

	notify_xgf_ko_ready();

	pr_debug("%s %d: finish", __func__, __LINE__);
#endif

	return 0;
}

module_init(fpsgo_init);
module_exit(fpsgo_exit);

MODULE_LICENSE("Proprietary");
MODULE_DESCRIPTION("MediaTek FPSGO");
MODULE_AUTHOR("MediaTek Inc.");
