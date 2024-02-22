// SPDX-License-Identifier: GPL-2.0-only
/*
 * Backlight emulation LED trigger
 *
 * Copyright 2008 (C) Rodolfo Giometti <giometti@linux.it>
 * Copyright 2008 (C) Eurotech S.p.A. <info@eurotech.it>
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/leds.h>
#include "../leds.h"

#include "mi_disp_notifier.h"

#define BLANK		1
#define UNBLANK		0

struct bl_drm_trig_notifier {
	struct led_classdev *led;
	int brightness;
	bool old_status;
	struct notifier_block notifier;
	unsigned invert;
};

static int drm_notifier_callback(struct notifier_block *p,
				unsigned long event, void *data)
{
	struct bl_drm_trig_notifier *n = container_of(p,
					struct bl_drm_trig_notifier, notifier);
	struct led_classdev *led = n->led;
	struct mi_disp_notifier *evdata = data;
	unsigned int *blank;
	bool new_status;

	/* If we aren't interested in this event, skip it immediately ... */
	if (event != MI_DISP_DPMS_EVENT)
		return 0;

	if (evdata->disp_id != MI_DISPLAY_PRIMARY)
		return 0;

	blank = (unsigned int *)evdata->data;
	if (*blank == MI_DISP_DPMS_ON)
		new_status = UNBLANK;
	else if (*blank == MI_DISP_DPMS_POWERDOWN)
		new_status = BLANK;
	else
		/* ignore other events */
		return 0;

	if (new_status == n->old_status)
		return 0;

	if ((n->old_status == UNBLANK) ^ n->invert) {
		n->brightness = led->brightness;
		led_set_brightness_nosleep(led, LED_OFF);
	} else {
		led_set_brightness_nosleep(led, n->brightness);
	}

	n->old_status = new_status;

	return 0;
}

static ssize_t bl_drm_trig_invert_show(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct bl_drm_trig_notifier *n = led_trigger_get_drvdata(dev);

	return sprintf(buf, "%u\n", n->invert);
}

static ssize_t bl_drm_trig_invert_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t num)
{
	struct led_classdev *led = led_trigger_get_led(dev);
	struct bl_drm_trig_notifier *n = led_trigger_get_drvdata(dev);
	unsigned long invert;
	int ret;

	ret = kstrtoul(buf, 10, &invert);
	if (ret < 0)
		return ret;

	if (invert > 1)
		return -EINVAL;

	n->invert = invert;

	/* After inverting, we need to update the LED. */
	if ((n->old_status == BLANK) ^ n->invert)
		led_set_brightness_nosleep(led, LED_OFF);
	else
		led_set_brightness_nosleep(led, n->brightness);

	return num;
}
static DEVICE_ATTR(inverted, 0644, bl_drm_trig_invert_show, bl_drm_trig_invert_store);

static struct attribute *bl_drm_trig_attrs[] = {
	&dev_attr_inverted.attr,
	NULL,
};
ATTRIBUTE_GROUPS(bl_drm_trig);

static int bl_drm_trig_activate(struct led_classdev *led)
{
	int ret;

	struct bl_drm_trig_notifier *n;

	n = kzalloc(sizeof(struct bl_drm_trig_notifier), GFP_KERNEL);
	if (!n)
		return -ENOMEM;
	led_set_trigger_data(led, n);

	n->led = led;
	n->brightness = led->brightness;
	n->old_status = UNBLANK;
	n->notifier.notifier_call = drm_notifier_callback;

	ret = mi_disp_register_client(&n->notifier);
	if (ret)
		dev_err(led->dev, "unable to register backlight trigger\n");

	return 0;
}

static void bl_drm_trig_deactivate(struct led_classdev *led)
{
	struct bl_drm_trig_notifier *n = led_get_trigger_data(led);

	mi_disp_unregister_client(&n->notifier);
	kfree(n);
}

static struct led_trigger bl_drm_led_trigger = {
	.name		= "backlight-drm",
	.activate	= bl_drm_trig_activate,
	.deactivate	= bl_drm_trig_deactivate,
	.groups		= bl_drm_trig_groups,
};
module_led_trigger(bl_drm_led_trigger);

MODULE_AUTHOR("Rodolfo Giometti <giometti@linux.it>");
MODULE_DESCRIPTION("Backlight emulation LED trigger using DRM notifier");
MODULE_LICENSE("GPL v2");
