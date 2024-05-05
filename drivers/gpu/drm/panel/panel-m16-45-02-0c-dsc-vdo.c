/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2019 MediaTek Inc.
*/

#include <drm/drmP.h>
#include <drm/drm_mipi_dsi.h>
#include <drm/drm_panel.h>
#include <linux/backlight.h>

#include <linux/gpio/consumer.h>
#include <linux/regulator/consumer.h>

#include <video/mipi_display.h>
#include <video/of_videomode.h>
#include <video/videomode.h>

#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/of_graph.h>
#include <linux/platform_device.h>
#include <linux/of_gpio.h>

#ifdef CONFIG_MI_DISP
#include <uapi/drm/mi_disp.h>
#include "../mediatek/mi_disp/mi_panel_ext.h"
#endif

#define CONFIG_MTK_PANEL_EXT
#if defined(CONFIG_MTK_PANEL_EXT)
#include "../mediatek/mtk_drm_graphics_base.h"
#include "../mediatek/mtk_log.h"
#include "../mediatek/mtk_panel_ext.h"
#endif

#ifdef CONFIG_MTK_ROUND_CORNER_SUPPORT
#include "../mediatek/mtk_corner_pattern/mtk_data_hw_roundedpattern.h"
#endif

//#define ENABLE_ELVSS_CONTROL

#define REGFLAG_CMD			0xFFFA
#define REGFLAG_DELAY			0xFFFC
#define REGFLAG_UDELAY			0xFFFB
#define REGFLAG_END_OF_TABLE    	0xFFFD
#define REGFLAG_RESET_LOW		0xFFFE
#define REGFLAG_RESET_HIGH		0xFFFF

#define DATA_RATE                   831

#define FRAME_WIDTH                 (1080)
#define FRAME_HEIGHT                (2400)

#define DSC_ENABLE                  1
#define DSC_VER                     17
#define DSC_SLICE_MODE              1
#define DSC_RGB_SWAP                0
#define DSC_DSC_CFG                 34
#define DSC_RCT_ON                  1
#define DSC_BIT_PER_CHANNEL         8
#define DSC_DSC_LINE_BUF_DEPTH      9
#define DSC_BP_ENABLE               1
#define DSC_BIT_PER_PIXEL           128
#define DSC_SLICE_HEIGHT            20
#define DSC_SLICE_WIDTH             540
#define DSC_CHUNK_SIZE              540
#define DSC_XMIT_DELAY              512
#define DSC_DEC_DELAY               526
#define DSC_SCALE_VALUE             32
#define DSC_INCREMENT_INTERVAL      488
#define DSC_DECREMENT_INTERVAL      7
#define DSC_LINE_BPG_OFFSET         12
#define DSC_NFL_BPG_OFFSET          1294
#define DSC_SLICE_BPG_OFFSET        1302
#define DSC_INITIAL_OFFSET          6144
#define DSC_FINAL_OFFSET            4336
#define DSC_FLATNESS_MINQP          3
#define DSC_FLATNESS_MAXQP          12
#define DSC_RC_MODEL_SIZE           8192
#define DSC_RC_EDGE_FACTOR          6
#define DSC_RC_QUANT_INCR_LIMIT0    11
#define DSC_RC_QUANT_INCR_LIMIT1    11
#define DSC_RC_TGT_OFFSET_HI        3
#define DSC_RC_TGT_OFFSET_LO        3

static unsigned int rc_buf_thresh[14] = {896, 1792, 2688, 3584, 4480, 5376, 6272, 6720, 7168, 7616, 7744, 7872, 8000, 8064};
static unsigned int range_min_qp[15] = {0, 0, 1, 1, 3, 3, 3, 3, 3, 3, 5, 5, 5, 7, 13};
static unsigned int range_max_qp[15] = {4, 4, 5, 6, 7, 7, 7, 8, 9, 10, 11, 12, 13, 13, 15};
static int range_bpg_ofs[15] = {2, 0, 0, -2, -4, -6, -8, -8, -8, -10, -10, -12, -12, -12, -12};

#define MAX_BRIGHTNESS_CLONE 16383

#define PHYSICAL_WIDTH              69552
#define PHYSICAL_HEIGHT             154560

#define FPS_INIT_INDEX 25
#define GIR_INIT_INDEX1 42
#define GIR_INIT_INDEX2 43
#define GIR_INIT_INDEX3 44
#define GIR_CRC_INDEX 63
#define AOD_TO_NORMAL_FPS_INDEX 0

#define pr_fmt(fmt)	"panel_45_02_0c:" fmt

static const char *panel_name = "panel_name=dsi_m16_45_02_0c_dsc_vdo";
static char oled_wp_cmdline[18] = {0};

static int lcm_panel_vibr30_regulator_init(struct device *dev);
static int lcm_panel_vibr30_enable(struct device *dev);

#ifdef CONFIG_MI_DISP
extern void mi_dsi_panel_tigger_dimming_work(struct mtk_dsi *dsi);
#endif
extern int get_panel_dead_flag(void);

static struct drm_panel * this_panel = NULL;
static struct mtk_ddic_dsi_msg *cmd_msg = NULL;
static int last_bl_level = 0;
static int last_non_zero_bl_level = 511;

struct lcm {
	struct device *dev;
	struct drm_panel panel;
	struct backlight_device *backlight;
	struct gpio_desc *reset_gpio;
	struct gpio_desc *vddi_gpio;
	struct gpio_desc *vdd_gpio;

	struct gpio_desc *pm_enable_gpio;

	bool prepared;
	bool enabled;

	bool dc_status;
	bool hbm_enabled;
	const char *panel_info;
	int dynamic_fps;
	u32 doze_state;
	u32 doze_brightness_state;
	u32 max_brightness_clone;
	int gir_status;
	int crc_status;
	struct mutex panel_lock;
	int error;
};

#define lcm_dcs_write_seq(ctx, seq...)                                         \
	({                                                                     \
		const u8 d[] = {seq};                                          \
		BUILD_BUG_ON_MSG(ARRAY_SIZE(d) > 64,                           \
				 "DCS sequence too big for stack");            \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

#define lcm_dcs_write_seq_static(ctx, seq...)                                  \
	({                                                                     \
		static const u8 d[] = {seq};                                   \
		lcm_dcs_write(ctx, d, ARRAY_SIZE(d));                          \
	})

static inline struct lcm *panel_to_lcm(struct drm_panel *panel)
{
	return container_of(panel, struct lcm, panel);
}

static void lcm_dcs_write(struct lcm *ctx, const void *data, size_t len)
{
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	ssize_t ret;
	char *addr;

	if (ctx->error < 0)
		return;

	addr = (char *)data;
	if ((int)*addr < 0xB0)
		ret = mipi_dsi_dcs_write_buffer(dsi, data, len);
	else
		ret = mipi_dsi_generic_write(dsi, data, len);
	if (ret < 0) {
		dev_err(ctx->dev, "error %zd writing seq: %ph\n", ret, data);
		ctx->error = ret;
	}
}

static void mi_disp_panel_ddic_send_cmd(struct LCM_setting_table *table, unsigned int count)
{
	int i = 0, j = 0, ret = 0;
	struct mtk_ddic_dsi_msg cmd_msg = {
		.channel = 0,
		.flags = 0,
		.tx_cmd_num = count,
	};

	if (table == NULL) {
		pr_err("invalid ddic cmd \n");
		return;
	}

	if (count == 0 || count > 25) {
		pr_err("cmd count invalid, value:%d \n", count);
		return;
	}

	for (i = 0;i < count; i++) {
		cmd_msg.type[i] = table[i].count > 2 ? 0x39 : 0x15;
		cmd_msg.tx_buf[i] = table[i].para_list;
		cmd_msg.tx_len[i] = table[i].count;
		pr_debug("cmd count:%d, cmd_add:%x len:%d\n",count,table[i].cmd,table[i].count);
		for (j = 0;j < table[i].count; j++)
			pr_debug("0x%02hhx ",table[i].para_list[j]);
	}

	ret = mtk_ddic_dsi_send_cmd(&cmd_msg, true, false);
	if (ret != 0) {
		pr_err("%s: failed to send ddic cmd\n", __func__);
	}
	return;
}

static struct regulator *disp_vibr30;
static int lcm_panel_vibr30_regulator_init(struct device *dev)
{
	static int vibr30_regulator_inited;
	int ret = 0;

	if (vibr30_regulator_inited)
               return ret;

	/* please only get regulator once in a driver */
	disp_vibr30 = regulator_get(dev, "vibr30");
	if (IS_ERR(disp_vibr30)) { /* handle return value */
		ret = PTR_ERR(disp_vibr30);
		pr_err("get disp_vibr30 fail, error: %d\n", ret);
		return ret;
	}

	vibr30_regulator_inited = 1;
	return ret; /* must be 0 */
}

static unsigned int vibr30_start_up = 1;
static int lcm_panel_vibr30_enable(struct device *dev)
{
	int ret = 0;
	int retval = 0;
	int status = 0;

	/* set voltage with min & max*/
	ret = regulator_set_voltage(disp_vibr30, 3000000, 3000000);
	if (ret < 0)
		pr_err("set voltage disp_vibr30 fail, ret = %d\n", ret);
	retval |= ret;

	status = regulator_is_enabled(disp_vibr30);
	if (!status || vibr30_start_up){
		/* enable regulator */
		ret = regulator_enable(disp_vibr30);
		if (ret < 0)
			pr_err("enable regulator disp_vibr30 fail, ret = %d\n", ret);
		vibr30_start_up = 0;
		retval |= ret;
	}

	return retval;
}

static int lcm_panel_vibr30_disable(struct device *dev)
{
	int ret = 0;
	int retval = 0;
	int status = 0;

	status = regulator_is_enabled(disp_vibr30);
	if (status){
		ret = regulator_disable(disp_vibr30);
		if (ret < 0)
			pr_err("disable regulator disp_vibr30 fail, ret = %d\n", ret);
	}

	retval |= ret;

	return retval;
}

static struct LCM_setting_table init_setting_vdo[] = {
    {0x3B, 4, {0x00, 0x0C, 0x00, 0x34}},
    {0xFF, 4, {0xAA, 0x55, 0xA5, 0x80}},
    {0x6F, 1, {0x19}},
    {0xF2, 1, {0x00}},
    {0x6F, 1, {0x14}},
    {0xF4, 1, {0x40}},
    {0x6F, 1, {0x17}},
    {0xF4, 1, {0x02}},
    {0x6F, 1, {0x31}},
    {0xFC, 1, {0x30}},
    {0xFF, 4, {0xAA, 0x55, 0xA5, 0x81}},
    {0x6F, 1, {0x3C}},
    {0xF5, 1, {0x81}},
    {0x6F, 1, {0x13}},
    {0xF9, 1, {0x01}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x01}},
    {0x6F, 1, {0x03}},
    {0xC7, 1, {0x07}},
    {0x6F, 1, {0x05}},
    {0xC7, 2, {0x27, 0x08}},
    {0x6F, 1, {0x0A}},
    {0xE4, 1, {0x90}},
    {0x17, 1, {0x03}},
    {0x71, 1, {0x00}},
    {0x8D, 8, {0x00, 0x00, 0x04, 0x37, 0x00, 0x00, 0x09, 0x5F}},
    {0x2F, 1, {0x02}},
    {0x2A, 4, {0x00, 0x00, 0x04, 0x37}},
    {0x2B, 4, {0x00, 0x00, 0x09, 0x5F}},
    {0x03, 1, {0x00}},
    {0x90, 2, {0x03, 0x43}},
    {0x91, 18, {0x89, 0x28, 0x00, 0x14, 0xC2, 0x00, 0x02, 0x0E, 0x01, 0xE8, 0x00, 0x07, 0x05, 0x0E, 0x05, 0x16, 0x10, 0xF0}},
    {0x93, 18, {0x89, 0x28, 0x00, 0x14, 0xC2, 0x00, 0x02, 0x0E, 0x01, 0xE8, 0x00, 0x07, 0x05, 0x0E, 0x05, 0x16, 0x10, 0xF0}},
    {0x95, 18, {0x89, 0x28, 0x00, 0x14, 0xC2, 0x00, 0x02, 0x0E, 0x01, 0xE8, 0x00, 0x07, 0x05, 0x0E, 0x05, 0x16, 0x10, 0xF0}},
    {0x97, 18, {0x89, 0x28, 0x00, 0x14, 0xC2, 0x00, 0x02, 0x0E, 0x01, 0xE8, 0x00, 0x07, 0x05, 0x0E, 0x05, 0x16, 0x10, 0xF0}},
    {0x51, 6, {0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
    {0xB2, 1, {0x09}},
    {0x6F, 1, {0x05}},
    {0xB2, 2, {0x04, 0x04}},
    {0x53, 1, {0x20}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
    {0x6F, 1, {0x03}},
    {0xC0, 1, {0x54}},
    {0x26, 1, {0x03}},
    {0x5F, 1, {0x01}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x00, 0x00}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
    {0x6F, 1, {0x01}},
    {0xBE, 1, {0x47}},
    {0x6F, 1, {0x13}},
    {0xBE, 1, {0x00}},
    {0x6F, 1, {0x05}},
    {0xBE, 1, {0x18}},
    {0x6F, 1, {0x0F}},
    {0xBE, 2, {0xFB, 0xFB}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x01}},
    {0xE8, 1, {0x30}},
    {0xFF, 4, {0xAA, 0x55, 0xA5, 0x84}},
    {0x6F, 1, {0x21}},
    {0xF4, 6, {0xFF, 0xFF, 0xF9, 0xFF, 0xFF, 0xFF}},
    {0x35, 1, {0x00}},
    {0x6F, 1, {0x9C}},
    {0xF4, 2, {0xFF, 0xC0}},
    {0x57, 1, {0x00}},
    {0x11, 0, {}},
    {REGFLAG_DELAY, 80, {}},
    {0x29, 0, {}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x01}},
    {0x6F, 1, {0x03}},
    {0xC7, 1, {0x47}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
    {0x6F, 1, {0x13}},
    {0xBE, 1, {0xC5}},
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x00, 0x00}},
    {0xFF, 4, {0xAA, 0x55, 0xA5, 0x00}},
    {REGFLAG_END_OF_TABLE, 0, {}},
};

static struct LCM_setting_table lcm_suspend_setting[] = {
    {0xF0, 5, {0x55, 0xAA, 0x52, 0x08, 0x00}},
    {0x6F, 1, {0x13}},
    {0xBE, 1, {0x00}},
    {0x28, 0, {}},
    {REGFLAG_DELAY, 10, {}},
    {0x10, 0, {}},
    {REGFLAG_DELAY, 100, {}},
};

static void push_table(struct lcm *ctx, struct LCM_setting_table *table, unsigned int count)
{
	unsigned int i,j;
	unsigned char temp[255] = {0};
	for (i = 0; i < count; i++) {
		unsigned cmd;
		cmd = table[i].cmd;
		memset(temp, 0, sizeof(temp));
		switch (cmd) {
			case REGFLAG_DELAY:
				if (table[i].count <= 10)
					msleep(table[i].count);
				else
					msleep(table[i].count);
				break;
			case REGFLAG_END_OF_TABLE:
				break;
			default:
				temp[0] = cmd;
				for (j = 0; j < table[i].count; j++) {
					temp[j+1] = table[i].para_list[j];
				}
				lcm_dcs_write(ctx, temp, table[i].count+1);
		}
	}
}

static void lcm_panel_init(struct lcm *ctx)
{
	pr_debug("%s+\n", __func__);
	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return;
	}

	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(10 * 1000, (10 * 1000)+20);
	gpiod_set_value(ctx->reset_gpio, 0);
	usleep_range(1 * 1000, (1* 1000)+20);
	gpiod_set_value(ctx->reset_gpio, 1);
	usleep_range(11 * 1000, (11 * 1000)+20);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);


	pr_debug("%s: dynamic_fps:%d, gir_status:%d, crc_status:%d\n", __func__,
		ctx->dynamic_fps, ctx->gir_status, ctx->crc_status);

	mutex_lock(&ctx->panel_lock);
	if (ctx->dynamic_fps == 120  || ctx->dynamic_fps == 30) {
		if (init_setting_vdo[FPS_INIT_INDEX].cmd == 0x2F)
			init_setting_vdo[FPS_INIT_INDEX].para_list[0] = 0x00;
		else
			pr_debug("%s: please check FPS_INIT_INDEX\n", __func__);
	} else if (ctx->dynamic_fps == 90) {
		if (init_setting_vdo[FPS_INIT_INDEX].cmd == 0x2F)
			init_setting_vdo[FPS_INIT_INDEX].para_list[0] = 0x01;
		else
			pr_debug("%s: please check FPS_INIT_INDEX\n", __func__);
	} else if (ctx->dynamic_fps == 60) {
		if (init_setting_vdo[FPS_INIT_INDEX].cmd == 0x2F)
			init_setting_vdo[FPS_INIT_INDEX].para_list[0] = 0x02;
		else
			pr_debug("%s: please check FPS_INIT_INDEX\n", __func__);
	}

	if (ctx->gir_status == 1) {
		if (init_setting_vdo[GIR_INIT_INDEX1].cmd == 0xC0)
					init_setting_vdo[GIR_INIT_INDEX1].para_list[0] = 0x21;
		else
			pr_debug("%s: please check GIR_INIT_INDEX1\n", __func__);
		if (init_setting_vdo[GIR_INIT_INDEX2].cmd == 0x26)
			init_setting_vdo[GIR_INIT_INDEX2].para_list[0] = 0x00;
		else
			pr_debug("%s: please check GIR_INIT_INDEX2\n", __func__);
		if (init_setting_vdo[GIR_INIT_INDEX3].cmd == 0x5F)
			init_setting_vdo[GIR_INIT_INDEX3].para_list[0] = 0x0;
		else
			pr_debug("%s: please check GIR_INIT_INDEX3\n", __func__);
	} else {
		if (init_setting_vdo[GIR_INIT_INDEX1].cmd == 0xC0)
					init_setting_vdo[GIR_INIT_INDEX1].para_list[0] = 0x54;
		else
			pr_debug("%s: please check GIR_INIT_INDEX1\n", __func__);
		if (init_setting_vdo[GIR_INIT_INDEX2].cmd == 0x26)
			init_setting_vdo[GIR_INIT_INDEX2].para_list[0] = 0x03;
		else
			pr_debug("%s: please check GIR_INIT_INDEX2\n", __func__);

		if (init_setting_vdo[GIR_INIT_INDEX3].cmd == 0x5F)
			init_setting_vdo[GIR_INIT_INDEX3].para_list[0] = 0x01;
		else
			pr_debug("%s: please check GIR_INIT_INDEX3\n", __func__);
	}

	if (ctx->crc_status == 1) {
		if (init_setting_vdo[GIR_CRC_INDEX].cmd == 0x57)
			init_setting_vdo[GIR_CRC_INDEX].para_list[0] = 0x80;
		else
			pr_debug("%s: please check GIR_CRC_INDEX\n", __func__);
	} else {
		if (init_setting_vdo[GIR_CRC_INDEX].cmd == 0x57)
			init_setting_vdo[GIR_CRC_INDEX].para_list[0] = 0x0;
		else
			pr_debug("%s: please check GIR_CRC_INDEX\n", __func__);

	}

	push_table(ctx, init_setting_vdo, sizeof(init_setting_vdo) / sizeof(struct LCM_setting_table));
	mutex_unlock(&ctx->panel_lock);

	pr_debug("%s-\n", __func__);
}

static int lcm_disable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_POWERDOWN;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = false;

	return 0;
}

static int panel_power_off(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_debug("%s\n", __func__);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	udelay(1000);
	gpiod_set_value(ctx->reset_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);
	udelay(1000);

	//VCI 3.0V -> 0
	lcm_panel_vibr30_disable(ctx->dev);
	udelay(1000);

	//VDD 1.2V -> 0
	ctx->vdd_gpio = devm_gpiod_get(ctx->dev, "vdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vdd_gpio %ld\n",
			__func__, PTR_ERR(ctx->vdd_gpio));
		return PTR_ERR(ctx->vdd_gpio);
	}
	gpiod_set_value(ctx->vdd_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vdd_gpio);
	udelay(1000);

	//VDDI 1.8V -> 0
	ctx->vddi_gpio = devm_gpiod_get(ctx->dev, "vddi", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddi_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vddi_gpio %ld\n",
			__func__, PTR_ERR(ctx->vddi_gpio));
		return PTR_ERR(ctx->vddi_gpio);
	}
	gpiod_set_value(ctx->vddi_gpio, 0);
	devm_gpiod_put(ctx->dev, ctx->vddi_gpio);
	udelay(1000);

	return 0;
}
static int lcm_unprepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (!ctx->prepared)
		return 0;

	mutex_lock(&ctx->panel_lock);
	push_table(ctx, lcm_suspend_setting, sizeof(lcm_suspend_setting) / sizeof(struct LCM_setting_table));
	mutex_unlock(&ctx->panel_lock);

	ctx->error = 0;
	ctx->prepared = false;

	return 0;
}

static int panel_power_on(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_debug("%s\n", __func__);
	if (ctx->prepared)
		return 0;

	//VDDI 1.8V
	ctx->vddi_gpio = devm_gpiod_get(ctx->dev, "vddi", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vddi_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vddi_gpio %ld\n",
			__func__, PTR_ERR(ctx->vddi_gpio));
		return PTR_ERR(ctx->vddi_gpio);
	}
	gpiod_set_value(ctx->vddi_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vddi_gpio);
	udelay(1000);

	//VDD 1.2V
	ctx->vdd_gpio = devm_gpiod_get(ctx->dev, "vdd", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->vdd_gpio)) {
		dev_err(ctx->dev, "%s: cannot get vdd_gpio %ld\n",
			__func__, PTR_ERR(ctx->vdd_gpio));
		return PTR_ERR(ctx->vdd_gpio);
	}
	gpiod_set_value(ctx->vdd_gpio, 1);
	devm_gpiod_put(ctx->dev, ctx->vdd_gpio);
	udelay(1000);

	//VCI 3.0V
	lcm_panel_vibr30_enable(ctx->dev);
	udelay(10000);

	return 0;
}

static int lcm_prepare(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	int ret;

	pr_debug("%s\n", __func__);
	if (ctx->prepared)
		return 0;

	lcm_panel_init(ctx);

	ret = ctx->error;
	if (ret < 0)
		lcm_unprepare(panel);

	ctx->prepared = true;

	return ret;
}

static int lcm_enable(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	if (ctx->enabled)
		return 0;

	if (ctx->backlight) {
		ctx->backlight->props.power = FB_BLANK_UNBLANK;
		backlight_update_status(ctx->backlight);
	}

	ctx->enabled = true;

	return 0;
}

#if defined(CONFIG_MTK_PANEL_EXT)
static int panel_ext_reset(struct drm_panel *panel, int on)
{
	struct lcm *ctx = panel_to_lcm(panel);

	ctx->reset_gpio =
		devm_gpiod_get(ctx->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(ctx->dev, "%s: cannot get reset_gpio %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	gpiod_set_value(ctx->reset_gpio, on);
	devm_gpiod_put(ctx->dev, ctx->reset_gpio);

	return 0;
}

static int panel_ata_check(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);
	struct mipi_dsi_device *dsi = to_mipi_dsi_device(ctx->dev);
	unsigned char data[3] = {0x00, 0x00, 0x00};
	unsigned char id[3] = {0x40, 0x00, 0x00};
	ssize_t ret;

	ret = mipi_dsi_dcs_read(dsi, 0x4, data, 3);
	if (ret < 0) {
		pr_err("%s error\n", __func__);
		return 0;
	}

	DDPINFO("ATA read data %x %x %x\n", data[0], data[1], data[2]);

	if (data[0] == id[0] &&
			data[1] == id[1] &&
			data[2] == id[2])
		return 1;

	DDPINFO("ATA expect read data is %x %x %x\n",
			id[0], id[1], id[2]);

	return 0;
}

static int lcm_setbacklight_cmdq(void *dsi, dcs_write_gce cb, void *handle,
				 unsigned int level)
{
	char bl_tb0[] = {0x51, 0x00, 0x00};
	struct lcm *ctx = panel_to_lcm(this_panel);

	if (level) {
		bl_tb0[1] = (level >> 8) & 0xFF;
		bl_tb0[2] = level & 0xFF;
	}

	if (!cb)
		return -1;

	pr_debug("%s: last_bl_level = %d,level = %d\n", __func__, last_bl_level, level);

	mutex_lock(&ctx->panel_lock);
	cb(dsi, handle, bl_tb0, ARRAY_SIZE(bl_tb0));
	mutex_unlock(&ctx->panel_lock);

	if (last_bl_level == 0 && level > 0) {
#ifdef CONFIG_MI_DISP
		if (!get_panel_dead_flag() && ctx->doze_state == 0)
			mi_dsi_panel_tigger_dimming_work(dsi);
#endif
	}

	if (level != 0)
		last_non_zero_bl_level = level;
	last_bl_level = level;

	return 0;
}

static int lcm_get_virtual_heigh(void)
{
	return 2400;
}

static int lcm_get_virtual_width(void)
{
	return 1080;
}

#ifdef CONFIG_MI_DISP
static int panel_set_doze_brightness(struct drm_panel *panel, int doze_brightness)
{
	int ret = 0;
	struct lcm *ctx;

	// 0x03,0xFF -> 0x0F,0xFE
	char lcm_aod_high_mode[] = {0x51,0x00,0x00,0x00,0x00,0x0F,0xFE};
	// 0x01,0xFF -> 0x01,0x55
	char lcm_aod_low_mode[] = {0x51,0x00,0x00,0x00,0x00,0x01,0x55};
	char lcm_aod_mode_enter[] = {0x39, 0x00};
	char lcm_aod_mode_exit[] = {0x38, 0x00};

	if (!panel) {
		pr_err("invalid params\n");
		return -1;
	}

	ctx = panel_to_lcm(panel);

	if (ctx->doze_brightness_state == doze_brightness) {
		pr_debug("%s skip same doze_brightness set:%d\n", __func__, doze_brightness);
		return 0;
	}

	if (DOZE_BRIGHTNESS_LBM  == doze_brightness || DOZE_BRIGHTNESS_HBM  == doze_brightness) {
		if (cmd_msg != NULL) {
			cmd_msg->channel = 0;
			cmd_msg->flags = 0;//MIPI_DSI_MSG_USE_LPM;
			cmd_msg->tx_cmd_num = 1;
			cmd_msg->tx_buf[0] = lcm_aod_mode_enter;
			cmd_msg->type[0] = sizeof(lcm_aod_mode_enter) > 2 ? 0x39 : 0x15;
			cmd_msg->tx_len[0] = sizeof(lcm_aod_mode_enter);
			mtk_ddic_dsi_send_cmd(cmd_msg, true, false);
			cmd_msg->type[0] = sizeof(lcm_aod_high_mode) > 2 ? 0x39 : 0x15;;
			if (DOZE_BRIGHTNESS_LBM  == doze_brightness)
				cmd_msg->tx_buf[0] = lcm_aod_low_mode;
			else if (DOZE_BRIGHTNESS_HBM == doze_brightness)
				cmd_msg->tx_buf[0] = lcm_aod_high_mode;
			cmd_msg->tx_len[0] = sizeof(lcm_aod_high_mode);
			mtk_ddic_dsi_send_cmd(cmd_msg, true, false);
		}
	}
	if (DOZE_TO_NORMAL == doze_brightness) {
		if (cmd_msg != NULL) {
			cmd_msg->channel = 0;
			cmd_msg->flags = ARRAY_SIZE(lcm_aod_mode_exit) > 2 ? 0 : MIPI_DSI_MSG_USE_LPM;
			cmd_msg->tx_cmd_num = 1;
			cmd_msg->tx_buf[0] = lcm_aod_mode_exit;
			cmd_msg->type[0] = sizeof(lcm_aod_mode_exit) > 2 ? 0x39 : 0x15;
			cmd_msg->tx_len[0] = sizeof(lcm_aod_mode_exit);
			mtk_ddic_dsi_send_cmd(cmd_msg, true, false);
		}
	}

	ctx->doze_brightness_state = doze_brightness;
	pr_debug("%s set doze_brightness %d end -\n", __func__, doze_brightness);

	return ret;
}

static int panel_get_doze_brightness(struct drm_panel *panel, u32 *doze_brightness)
{
	int count = 0;
	struct lcm *ctx = panel_to_lcm(panel);
	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}
	*doze_brightness = ctx->doze_brightness_state;
	pr_debug("%s get doze_brightness %d end -\n", __func__, *doze_brightness);
	return count;
}
#endif

#ifdef ENABLE_30HZ
static struct drm_display_mode mode_30hz = {
	.clock = 186870, // changed
	.hdisplay = 1080,
	.hsync_start = 1080 + 1424,			//HFP changed
	.hsync_end = 1080 + 1424 + 8,		//HSA changed
	.htotal = 1080 + 1424 + 8+ 16,		//HBP changed
	.vdisplay = 2400,
	.vsync_start = 2400 + 52,			//VFP changed
	.vsync_end = 2400 + 52 + 4,		//VSA changed
	.vtotal = 2400 + 52 + 4 + 8,	//VBP changed
	.vrefresh = 30,
};
#endif

static struct drm_display_mode mode_60hz = {
	.clock = 331162, // changed
	.hdisplay = 1080,
	.hsync_start = 1080 + 20,			//HFP
	.hsync_end = 1080 + 20 + 8, 	//HSA
	.htotal = 1080 + 20 + 8 + 16,		//HBP
	.vdisplay = 2400,
	.vsync_start = 2400 + 2516,			//VFP changed
	.vsync_end = 2400 + 2516 + 4,		//VSA changed
	.vtotal = 2400 + 2516 + 4 + 8,	//VBP changed
	.vrefresh = 60,
};

static struct drm_display_mode mode_90hz = {
	.clock = 328316, // changed
	.hdisplay = 1080,
	.hsync_start = 1080 + 20,			//HFP changed
	.hsync_end = 1080 + 20+ 8,		//HSA changed
	.htotal = 1080 + 20 + 8+ 16,		//HBP changed
	.vdisplay = 2400,
	.vsync_start = 2400 + 822,			//VFP changed
	.vsync_end = 2400 + 822 + 4,		//VSA changed
	.vtotal = 2400 + 822 + 4 + 8,	//VBP changed
	.vrefresh = 90,
};

static struct drm_display_mode mode_120hz = {
	.clock = 331162, // changed
	.hdisplay = 1080,
	.hsync_start = 1080 + 20,			//HFP
	.hsync_end = 1080 + 20+ 8,		//HSA
	.htotal = 1080 + 20 + 8+ 16,		//HBP
	.vdisplay = 2400,
	.vsync_start = 2400 + 52,			//VFP changed
	.vsync_end = 2400 + 52 + 4,		//VSA changed
	.vtotal = 2400 + 52 + 4 + 8,	//VBP changed
	.vrefresh = 120,
};

#ifdef ENABLE_30HZ
static struct mtk_panel_params ext_params_30hz = {
	.lcm_index = 2,
	.pll_clk = DATA_RATE / 2,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x91,
		.count = 2,
		.para_list[0] = 0x89,
		.para_list[1] = 0x28,
	},
	.is_cphy = 0,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.data_rate = DATA_RATE,
#ifdef CONFIG_MI_DISP_FOD_SYNC
	.bl_sync_enable = 1,
	.aod_delay_enable = 1,
#endif
	//.vfp_low_power = 820,//idle 90hz
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 30,
		.cmds_counts_switch_fps = 30,
		.short_dfps_cmds_counts = 4,
		.long_dfps_cmds_counts = 4,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x00} },
		.dfps_cmd_table[1] = {0, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xEA,0x11} },
		.dfps_cmd_table[3] = {0, 6, {0xF0,0x55,0xAA,0x52,0x00,0x00} },
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
};
#endif

static struct mtk_panel_params ext_params_60hz = {
	.lcm_index = 2,
	.pll_clk = DATA_RATE / 2,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
		.cmd = 0x91,
		.count = 2,
		.para_list[0] = 0x89,
		.para_list[1] = 0x28,
	},
	.is_cphy = 0,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.data_rate = DATA_RATE,
#ifdef CONFIG_MI_DISP_FOD_SYNC
	.bl_sync_enable = 1,
	.aod_delay_enable = 1,
#endif
	//.vfp_low_power = 820,//idle 90hz
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 60,
		.cmds_counts_switch_fps = 30,
		.short_dfps_cmds_counts = 1,
		.long_dfps_cmds_counts = 4,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x02} },
		.dfps_cmd_table[1] = {0, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xEA,0x91} },
		.dfps_cmd_table[3] = {0, 6, {0xF0,0x55,0xAA,0x52,0x00,0x00} },
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
};

static struct mtk_panel_params ext_params_90hz = {
	.lcm_index = 2,
	.pll_clk = DATA_RATE / 2,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
			.cmd = 0x91,
			.count = 2,
			.para_list[0] = 0x89,
			.para_list[1] = 0x28,
	},
	.is_cphy = 0,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.data_rate = DATA_RATE,
#ifdef CONFIG_MI_DISP_FOD_SYNC
	.bl_sync_enable = 1,
	.aod_delay_enable = 1,
#endif
	//.vfp_low_power = 820,//idle 90hz
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 90,
		.cmds_counts_switch_fps = 30,
		.short_dfps_cmds_counts = 1,
		.long_dfps_cmds_counts = 4,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x01} },
		.dfps_cmd_table[1] = {0, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xEA,0x91} },
		.dfps_cmd_table[3] = {0, 6, {0xF0,0x55,0xAA,0x52,0x00,0x00} },
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
};

static struct mtk_panel_params ext_params_120hz = {
	.lcm_index = 2,
	.pll_clk = DATA_RATE / 2,
	.cust_esd_check = 0,
	.esd_check_enable = 1,
	.lcm_esd_check_table[0] = {
			.cmd = 0x91,
			.count = 2,
			.para_list[0] = 0x89,
			.para_list[1] = 0x28,
	},
	.is_cphy = 0,
	.lcm_color_mode = MTK_DRM_COLOR_MODE_DISPLAY_P3,
	.output_mode = MTK_PANEL_DSC_SINGLE_PORT,
	.dsc_params = {
		.enable                =  DSC_ENABLE,
		.ver                   =  DSC_VER,
		.slice_mode            =  DSC_SLICE_MODE,
		.rgb_swap              =  DSC_RGB_SWAP,
		.dsc_cfg               =  DSC_DSC_CFG,
		.rct_on                =  DSC_RCT_ON,
		.bit_per_channel       =  DSC_BIT_PER_CHANNEL,
		.dsc_line_buf_depth    =  DSC_DSC_LINE_BUF_DEPTH,
		.bp_enable             =  DSC_BP_ENABLE,
		.bit_per_pixel         =  DSC_BIT_PER_PIXEL,
		.pic_height            =  FRAME_HEIGHT,
		.pic_width             =  FRAME_WIDTH,
		.slice_height          =  DSC_SLICE_HEIGHT,
		.slice_width           =  DSC_SLICE_WIDTH,
		.chunk_size            =  DSC_CHUNK_SIZE,
		.xmit_delay            =  DSC_XMIT_DELAY,
		.dec_delay             =  DSC_DEC_DELAY,
		.scale_value           =  DSC_SCALE_VALUE,
		.increment_interval    =  DSC_INCREMENT_INTERVAL,
		.decrement_interval    =  DSC_DECREMENT_INTERVAL,
		.line_bpg_offset       =  DSC_LINE_BPG_OFFSET,
		.nfl_bpg_offset        =  DSC_NFL_BPG_OFFSET,
		.slice_bpg_offset      =  DSC_SLICE_BPG_OFFSET,
		.initial_offset        =  DSC_INITIAL_OFFSET,
		.final_offset          =  DSC_FINAL_OFFSET,
		.flatness_minqp        =  DSC_FLATNESS_MINQP,
		.flatness_maxqp        =  DSC_FLATNESS_MAXQP,
		.rc_model_size         =  DSC_RC_MODEL_SIZE,
		.rc_edge_factor        =  DSC_RC_EDGE_FACTOR,
		.rc_quant_incr_limit0  =  DSC_RC_QUANT_INCR_LIMIT0,
		.rc_quant_incr_limit1  =  DSC_RC_QUANT_INCR_LIMIT1,
		.rc_tgt_offset_hi      =  DSC_RC_TGT_OFFSET_HI,
		.rc_tgt_offset_lo      =  DSC_RC_TGT_OFFSET_LO,
		.ext_pps_cfg = {
			.enable = 1,
			.rc_buf_thresh = rc_buf_thresh,
			.range_min_qp = range_min_qp,
			.range_max_qp = range_max_qp,
			.range_bpg_ofs = range_bpg_ofs,
		},
	},
	.data_rate = DATA_RATE,
#ifdef CONFIG_MI_DISP_FOD_SYNC
	.bl_sync_enable = 1,
	.aod_delay_enable = 1,
#endif
	//.vfp_low_power = 820,//idle 90hz
	.dyn_fps = {
		.switch_en = 1,
		.vact_timing_fps = 120,
		.cmds_counts_switch_fps = 30,
		.short_dfps_cmds_counts = 1,
		.long_dfps_cmds_counts = 4,
		.dfps_cmd_table[0] = {0, 2, {0x2F, 0x00} },
		.dfps_cmd_table[1] = {0, 6, {0xF0,0x55,0xAA,0x52,0x08,0x01} },
		.dfps_cmd_table[2] = {0, 2, {0xEA,0x91} },
		.dfps_cmd_table[3] = {0, 6, {0xF0,0x55,0xAA,0x52,0x00,0x00} },
	},
	.physical_width_um = PHYSICAL_WIDTH,
	.physical_height_um = PHYSICAL_HEIGHT,
};

static struct drm_display_mode *get_mode_by_id(struct drm_connector *connector,
	unsigned int mode)
{
	struct drm_display_mode *m;
	unsigned int i = 0;

	list_for_each_entry(m, &connector->modes, head) {
		if (i == mode)
			return m;
		i++;
	}
	return NULL;
}

static int mtk_panel_ext_param_set(struct drm_panel *panel, unsigned int mode)
{
	struct mtk_panel_ext *ext = find_panel_ext(panel);
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(panel->connector, mode);
	struct lcm *ctx = panel_to_lcm(panel);

	//pr_debug("%s drm_mode_vrefresh = %d, m->hdisplay = %d\n",
		//__func__, drm_mode_vrefresh(m), m->hdisplay);

#ifdef ENABLE_30HZ
	if (drm_mode_vrefresh(m) == 30)
		ext->params = &ext_params_30hz;
	else if (drm_mode_vrefresh(m) == 60)
#else
	if (drm_mode_vrefresh(m) == 60)
#endif
		ext->params = &ext_params_60hz;
	else if (drm_mode_vrefresh(m) == 90)
		ext->params = &ext_params_90hz;
	else if (drm_mode_vrefresh(m) == 120)
		ext->params = &ext_params_120hz;
	else
		ret = 1;
	if (!ret)
		ctx->dynamic_fps = drm_mode_vrefresh(m);

	return ret;
}

static void mode_switch_to_60(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_debug("%s\n", __func__);

	mutex_lock(&ctx->panel_lock);
	lcm_dcs_write_seq_static(ctx, 0x2F, 0x02);
	mutex_unlock(&ctx->panel_lock);
}

static void mode_switch_to_90(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_debug("%s\n", __func__);

	mutex_lock(&ctx->panel_lock);
	lcm_dcs_write_seq_static(ctx, 0x2F, 0x01);
	mutex_unlock(&ctx->panel_lock);
}

static void mode_switch_to_120(struct drm_panel *panel)
{
	struct lcm *ctx = panel_to_lcm(panel);

	pr_debug("%s\n", __func__);

	mutex_lock(&ctx->panel_lock);
	lcm_dcs_write_seq_static(ctx, 0x2F, 0x01);
	mutex_unlock(&ctx->panel_lock);
}

static int mode_switch(struct drm_panel *panel, unsigned int cur_mode,
		unsigned int dst_mode, enum MTK_PANEL_MODE_SWITCH_STAGE stage)
{
	int ret = 0;
	struct drm_display_mode *m = get_mode_by_id(panel->connector, dst_mode);
	struct lcm *ctx = panel_to_lcm(panel);

	pr_debug("%s cur_mode = %d dst_mode %d\n", __func__, cur_mode, dst_mode);

	if (drm_mode_vrefresh(m) == 60 || drm_mode_vrefresh(m) == 30) {
		mode_switch_to_60(panel);
	} else if (drm_mode_vrefresh(m) == 90) {
		mode_switch_to_90(panel);
	} else if (drm_mode_vrefresh(m) == 120) {
		mode_switch_to_120(panel);
	} else
		ret = 1;
	if(!ret)
		ctx->dynamic_fps = drm_mode_vrefresh(m);

	return ret;
}

#ifdef CONFIG_MI_DISP
static unsigned int bl_level = 2047;
static int panel_get_panel_info(struct drm_panel *panel, char *buf)
{
	int count = 0;
	struct lcm *ctx;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	ctx = panel_to_lcm(panel);
	count = snprintf(buf, PAGE_SIZE, "%s\n", ctx->panel_info);

	return count;
}

static int panel_get_dynamic_fps(struct drm_panel *panel, u32 *fps)
{
	int ret = 0;
	struct lcm *ctx;

	if (!panel || !fps) {
		pr_err("%s: panel or fps is NULL\n", __func__);
		ret = -1;
		goto err;
	}

	ctx = panel_to_lcm(panel);
	*fps = ctx->dynamic_fps;
err:
	return ret;
}

static int panel_get_max_brightness_clone(struct drm_panel *panel, u32 *max_brightness_clone)
{
	struct lcm *ctx;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	ctx = panel_to_lcm(panel);
	*max_brightness_clone = ctx->max_brightness_clone;

	return 0;
}

static int panel_get_panel_thermal_dimming_enable(struct drm_panel *panel, bool *enabled)
{
	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	*enabled = true;

	return 0;
}

static int panel_get_wp_info(struct drm_panel *panel, char *buf, size_t size)
{
	int count = 0;

	pr_debug("%s: got wp info from cmdline: oled_wp_cmdline=%s\n",
		__func__, oled_wp_cmdline);
	count = snprintf(buf, PAGE_SIZE, "%s\n", oled_wp_cmdline);

	return count;
}

static int panel_normal_hbm_control(struct drm_panel *panel, uint32_t level)
{
	struct lcm *ctx = panel_to_lcm(this_panel);

	mutex_lock(&ctx->panel_lock);
	if (level == 1) {
		lcm_dcs_write_seq_static(ctx, 0x51, 0x0F,0xFF);
	} else if (level == 0) {
		lcm_dcs_write_seq_static(ctx, 0x51, 0x07, 0xFF);
	}
	mutex_unlock(&ctx->panel_lock);

	return 0;
}

#ifdef CONFIG_MI_DISP_ESD_CHECK
static void lcm_esd_restore_backlight(struct mtk_dsi *dsi, struct drm_panel *panel)
{
	char bl_tb0[] = {0x51, 0x00, 0x00};
	struct lcm *ctx = panel_to_lcm(panel);

	bl_tb0[1] = (last_non_zero_bl_level >> 8) & 0xFF;
	bl_tb0[2] = last_non_zero_bl_level & 0xFF;

	pr_debug("%s: restore to level = %d\n", __func__, last_non_zero_bl_level);

	mutex_lock(&ctx->panel_lock);
	lcm_dcs_write(ctx, bl_tb0, ARRAY_SIZE(bl_tb0));
	mutex_unlock(&ctx->panel_lock);

#ifdef CONFIG_MI_DISP
	mi_dsi_panel_tigger_dimming_work(dsi);
#endif

	return;
}
#endif
static int lcm_setbacklight_control(struct drm_panel *panel, unsigned int level)
{
	struct lcm *ctx;

	if (!panel) {
		pr_err("invalid params\n");
		return -EAGAIN;
	}

	ctx = panel_to_lcm(panel);
	if (level > 2047) {
		ctx->hbm_enabled = true;
		bl_level = level;
	}
	pr_err("lcm_setbacklight_control backlight %d\n", level);

	return 0;
}

static bool get_lcm_initialized(struct drm_panel *panel)
{
	struct lcm *ctx;
	bool ret = false;

	if (!panel) {
		pr_err(": panel is NULL\n", __func__);
		goto err;
	}

	ctx = panel_to_lcm(panel);
	ret = ctx->prepared;
err:
	return ret;
}

// Disassembly of stock kernel from later firmware indicates that this
// function was stubbed out in all panel drivers, for some reason.
#ifdef ENABLE_ELVSS_CONTROL
static void panel_elvss_control(struct drm_panel *panel, bool en)
{
	struct lcm *ctx;
	char dimming_set[2] = {0x53, 0x20};

	if (!panel) {
		pr_err("invalid params\n");
		return;
	}
	if (last_bl_level == 0) {
		pr_debug("last bl level is 0, skip set dimming %s\n", __func__, en ? "on" : "off");
		return;
	}
	ctx = panel_to_lcm(panel);
	if (!ctx->prepared) {
		pr_err("panel unprepared, skip!!!\n");
		return;
	}
	if (en) {
		dimming_set[1] = 0x28;
	} else {
		dimming_set[1] = 0x20;
	}
	if (cmd_msg != NULL) {
		cmd_msg->channel = 0;
		cmd_msg->flags = 0;//MIPI_DSI_MSG_USE_LPM;
		cmd_msg->tx_cmd_num = 1;
		cmd_msg->type[0] = ARRAY_SIZE(dimming_set) > 2 ? 0x39 : 0x15;
		cmd_msg->tx_buf[0] = dimming_set;
		cmd_msg->tx_len[0] = ARRAY_SIZE(dimming_set);
		mtk_ddic_dsi_send_cmd(cmd_msg, false, false);
	}
}
#endif

static int panel_set_gir_on(struct drm_panel *panel)
{
	char gir_on[] = {0xA9, 0x02, 0x00,
					 0xC0, 0x03, 0x03,
					 0x21, 0x01,
					 0x00, 0x26, 0x00,
					 0x00, 0x03, 0x01,
					 0x00, 0x5f,
					 0x00, 0x00, 0x01};
	struct lcm *ctx;
	int ret = 0;

	pr_debug("%s: + ctx->gir_status = %d  \n", __func__, ctx->gir_status);

	if (!panel) {
		pr_err("%s: panel is NULL\n", __func__);
		ret = -1;
		goto err;
	}

	ctx = panel_to_lcm(panel);
	if (!ctx->enabled) {
		pr_err("%s: panel isn't enabled\n", __func__);
	} else {
		if (cmd_msg != NULL) {
			cmd_msg->channel = 0;
			cmd_msg->flags = ARRAY_SIZE(gir_on) > 2 ? 0 : MIPI_DSI_MSG_USE_LPM;
			cmd_msg->tx_cmd_num = 1;
			cmd_msg->type[0] = ARRAY_SIZE(gir_on) > 2 ? 0x39 : 0x15;
			cmd_msg->tx_buf[0] = gir_on;
			cmd_msg->tx_len[0] = ARRAY_SIZE(gir_on);
			mtk_ddic_dsi_send_cmd(cmd_msg, false, false);
		}
		ctx->gir_status = 1;
	}

err:
	return ret;
}

static int panel_set_gir_off(struct drm_panel *panel)
{
	char gir_on[] = {0xA9, 0x02, 0x00,
					 0xC0, 0x03, 0x03,
					 0x54, 0x01,
					 0x00, 0x26, 0x00,
					 0x00, 0x03, 0x01,
					 0x00, 0x5f,
					 0x00, 0x00, 0x01};
	struct lcm *ctx;
	int ret = 0;

	pr_debug("%s: + ctx->gir_status = %d  \n", __func__, ctx->gir_status);

	if (!panel) {
		pr_err("%s: panel is NULL\n", __func__);
		ret = -1;
		goto err;
	}

	ctx = panel_to_lcm(panel);
	if (!ctx->enabled) {
		pr_err("%s: panel isn't enabled\n", __func__);
	} else {
		if (cmd_msg != NULL) {
			cmd_msg->channel = 0;
			cmd_msg->flags = ARRAY_SIZE(gir_on) > 2 ? 0 : MIPI_DSI_MSG_USE_LPM;
			cmd_msg->tx_cmd_num = 1;
			cmd_msg->type[0] = ARRAY_SIZE(gir_on) > 2 ? 0x39 : 0x15;
			cmd_msg->tx_buf[0] = gir_on;
			cmd_msg->tx_len[0] = ARRAY_SIZE(gir_on);
			mtk_ddic_dsi_send_cmd(cmd_msg, false, false);
		}
		ctx->gir_status = 0;
	}

err:
	return ret;
}

static int panel_get_gir_status(struct drm_panel *panel)
{
	struct lcm *ctx;

	if (!panel) {
		pr_err("%s; panel is NULL\n", __func__);
		return -1;
	}

	ctx = panel_to_lcm(panel);

	return ctx->gir_status;
}
#endif

static struct mtk_panel_funcs ext_funcs = {
	.reset = panel_ext_reset,
	.panel_power_on = panel_power_on,
	.panel_power_off = panel_power_off,
	.ext_param_set = mtk_panel_ext_param_set,
	.mode_switch = mode_switch,
	.set_backlight_cmdq = lcm_setbacklight_cmdq,
	.ata_check = panel_ata_check,
	.get_virtual_heigh = lcm_get_virtual_heigh,
	.get_virtual_width = lcm_get_virtual_width,
#ifdef CONFIG_MI_DISP
	.set_doze_brightness = panel_set_doze_brightness,
	.get_doze_brightness = panel_get_doze_brightness,
	.get_panel_info = panel_get_panel_info,
	.get_panel_dynamic_fps =  panel_get_dynamic_fps,
	.get_panel_max_brightness_clone = panel_get_max_brightness_clone,
	.get_panel_thermal_dimming_enable = panel_get_panel_thermal_dimming_enable,
	.get_wp_info = panel_get_wp_info,
	.normal_hbm_control = panel_normal_hbm_control,
#ifdef CONFIG_MI_DISP_ESD_CHECK
	.esd_restore_backlight = lcm_esd_restore_backlight,
#endif
	.setbacklight_control = lcm_setbacklight_control,
	.get_panel_initialized = get_lcm_initialized,
#ifdef ENABLE_ELVSS_CONTROL
	.panel_elvss_control = panel_elvss_control,
#endif
	.panel_set_gir_on = panel_set_gir_on,
	.panel_set_gir_off = panel_set_gir_off,
	.panel_get_gir_status = panel_get_gir_status,
#endif
};
#endif

struct panel_desc {
	const struct drm_display_mode *modes;
	unsigned int num_modes;

	unsigned int bpc;

	struct {
		unsigned int width;
		unsigned int height;
	} size;

	struct {
		unsigned int prepare;
		unsigned int enable;
		unsigned int disable;
		unsigned int unprepare;
	} delay;
};

static int lcm_get_modes(struct drm_panel *panel)
{
#ifdef ENABLE_30HZ
	struct drm_display_mode *mode_30;
#endif
	struct drm_display_mode *mode_60;
	struct drm_display_mode *mode_90;
	struct drm_display_mode *mode_120;

#ifdef ENABLE_30HZ
	mode_30 = drm_mode_duplicate(panel->drm, &mode_30hz);
	if (!mode_30) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			mode_30hz.hdisplay, mode_30hz.vdisplay,
			mode_30hz.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode_30);
	mode_30->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode_30);
#endif

	mode_60 = drm_mode_duplicate(panel->drm, &mode_60hz);
	if (!mode_60) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			mode_60hz.hdisplay, mode_60hz.vdisplay,
			mode_60hz.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode_60);
	mode_60->type = DRM_MODE_TYPE_DRIVER | DRM_MODE_TYPE_PREFERRED;
	drm_mode_probed_add(panel->connector, mode_60);

	mode_90 = drm_mode_duplicate(panel->drm, &mode_90hz);
	if (!mode_90) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			mode_90hz.hdisplay, mode_90hz.vdisplay,
			mode_90hz.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode_90);
	mode_90->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode_90);

	mode_120 = drm_mode_duplicate(panel->drm, &mode_120hz);
	if (!mode_120) {
		dev_err(panel->drm->dev, "failed to add mode %ux%ux@%u\n",
			mode_120hz.hdisplay, mode_120hz.vdisplay,
			mode_120hz.vrefresh);
		return -ENOMEM;
	}
	drm_mode_set_name(mode_120);
	mode_120->type = DRM_MODE_TYPE_DRIVER;
	drm_mode_probed_add(panel->connector, mode_120);

	panel->connector->display_info.width_mm = PHYSICAL_WIDTH/1000;
	panel->connector->display_info.height_mm = PHYSICAL_HEIGHT/1000;

	return 1;
}

static const struct drm_panel_funcs lcm_drm_funcs = {
	.disable = lcm_disable,
	.unprepare = lcm_unprepare,
	.prepare = lcm_prepare,
	.enable = lcm_enable,
	.get_modes = lcm_get_modes,
};

static int lcm_probe(struct mipi_dsi_device *dsi)
{
	struct device *dev = &dsi->dev;
	struct lcm *ctx;
	struct device_node *backlight;
	int ret;
	struct device_node *dsi_node, *remote_node = NULL, *endpoint = NULL;

	dsi_node = of_get_parent(dev->of_node);
	if (dsi_node) {
		endpoint = of_graph_get_next_endpoint(dsi_node, NULL);
		if (endpoint) {
			remote_node = of_graph_get_remote_port_parent(endpoint);
			if (!remote_node) {
				pr_debug("No panel connected,skip probe lcm\n");
				return -ENODEV;
			}
			pr_debug("device node name:%s\n", remote_node->name);
		}
	}
	if (remote_node != dev->of_node) {
		pr_debug("%s+ skip probe due to not current lcm: %s\n", __func__, panel_name);
		return -ENODEV;
	}
	pr_debug("%s+  probe current lcm: %s\n", __func__, panel_name);

	ctx = devm_kzalloc(dev, sizeof(struct lcm), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	mipi_dsi_set_drvdata(dsi, ctx);

	ctx->dev = dev;
	dsi->lanes = 4;
	dsi->format = MIPI_DSI_FMT_RGB888;
	dsi->mode_flags = MIPI_DSI_MODE_VIDEO | MIPI_DSI_MODE_VIDEO_BURST
			 | MIPI_DSI_MODE_LPM | MIPI_DSI_MODE_EOT_PACKET
			 | MIPI_DSI_CLOCK_NON_CONTINUOUS;

	backlight = of_parse_phandle(dev->of_node, "backlight", 0);
	if (backlight) {
		ctx->backlight = of_find_backlight_by_node(backlight);
		of_node_put(backlight);

		if (!ctx->backlight)
			return -EPROBE_DEFER;
	}

	ctx->reset_gpio = devm_gpiod_get(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(ctx->reset_gpio)) {
		dev_err(dev, "%s: cannot get reset-gpios %ld\n",
			__func__, PTR_ERR(ctx->reset_gpio));
		return PTR_ERR(ctx->reset_gpio);
	}
	devm_gpiod_put(dev, ctx->reset_gpio);

	ret = lcm_panel_vibr30_regulator_init(dev);
	if (!ret)
		lcm_panel_vibr30_enable(dev);
	else
		pr_err("%s init vibr30_aif regulator error\n", __func__);

	ctx->prepared = true;
	ctx->enabled = true;

	ctx->hbm_enabled = false;
	ctx->panel_info = panel_name;
	ctx->dynamic_fps = 60;
	ctx->max_brightness_clone = MAX_BRIGHTNESS_CLONE;

	ext_params_60hz.err_flag_irq_gpio = of_get_named_gpio_flags(
		dev->of_node, "mi,esd-err-irq-gpio",
		0, (enum of_gpio_flags *)&(ext_params_60hz.err_flag_irq_flags));

#ifdef ENABLE_30HZ
	ext_params_30hz.err_flag_irq_gpio = ext_params_60hz.err_flag_irq_gpio;
	ext_params_30hz.err_flag_irq_flags = ext_params_60hz.err_flag_irq_flags;
#endif

	ext_params_90hz.err_flag_irq_gpio = ext_params_60hz.err_flag_irq_gpio;
	ext_params_90hz.err_flag_irq_flags = ext_params_60hz.err_flag_irq_flags;
	ext_params_120hz.err_flag_irq_gpio = ext_params_60hz.err_flag_irq_gpio;
	ext_params_120hz.err_flag_irq_flags = ext_params_60hz.err_flag_irq_flags;

	drm_panel_init(&ctx->panel);
	ctx->panel.dev = dev;
	ctx->panel.funcs = &lcm_drm_funcs;

	mutex_init(&ctx->panel_lock);

	ret = drm_panel_add(&ctx->panel);
	if (ret < 0)
		return ret;

	ret = mipi_dsi_attach(dsi);
	if (ret < 0)
		drm_panel_remove(&ctx->panel);

#if defined(CONFIG_MTK_PANEL_EXT)
	ret = mtk_panel_ext_create(dev, &ext_params_60hz, &ext_funcs, &ctx->panel);
	if (ret < 0)
		return ret;
#endif
	cmd_msg = vmalloc(sizeof(struct mtk_ddic_dsi_msg));
	memset(cmd_msg, 0, sizeof(struct mtk_ddic_dsi_msg));
	this_panel = &ctx->panel;

	pr_debug("%s-\n", __func__);

	return ret;
}

static int lcm_remove(struct mipi_dsi_device *dsi)
{
	struct lcm *ctx = mipi_dsi_get_drvdata(dsi);

	vfree(cmd_msg);
	mipi_dsi_detach(dsi);
	drm_panel_remove(&ctx->panel);

	return 0;
}

static const struct of_device_id lcm_of_match[] = {
	{ .compatible = "m16_45_02_0c_dsc_vdo,lcm", },
	{ }
};

MODULE_DEVICE_TABLE(of, lcm_of_match);

static struct mipi_dsi_driver lcm_driver = {
	.probe = lcm_probe,
	.remove = lcm_remove,
	.driver = {
		.name = "panel-m16-45-02-0c-dsc-vdo",
		.owner = THIS_MODULE,
		.of_match_table = lcm_of_match,
	},
};

module_mipi_dsi_driver(lcm_driver);

module_param_string(oled_wp, oled_wp_cmdline, sizeof(oled_wp_cmdline), 0600);
MODULE_PARM_DESC(oled_wp, "oled_wp=<white_point_info>");

MODULE_AUTHOR("bengris32 <bengris32@protonmail.ch>");
MODULE_DESCRIPTION("panel-m16-45-02-0c-dsc-vdo Panel Driver");
MODULE_LICENSE("GPL v2");
