// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * hp_victus_rgb.c - HP Victus keyboard RGB control via HP WMI (sysfs interface)
 *
 * Stage 2 of the hp-victus-16-rgb project. Unlike the stage1 probe which
 * performs a single WMI write on insmod, this module stays loaded and exposes
 * a sysfs interface so userspace (the GUI / CLI) can change the keyboard color
 * live without reloading the module.
 *
 * The WMI protocol is documented in stage1/hp_rgb_probe.c and docs/wmi-protocol.md:
 *
 *   GUID          : 5FB7F034-2C63-45E9-BE91-3D44E2C707E4
 *   Signature     : 0x55434553  ("SEQU")
 *   Write command : 0x00020008, commandtype 0x05, method 1, 4 data bytes
 *                   data = [zone, red, green, blue]
 *   Read  command : 0x00020008, commandtype 0x04, method 2, 1 data byte
 *                   response buffer[0..3] = "PASS", buffer[8..10] = R G B
 *
 * The tested HP Victus keyboard behaves as a single physical zone. The ACPI
 * firmware exposes four logical zone fields (Z01..Z04), but only zone 1 maps to
 * a visible change on this hardware. We therefore fix the active zone to 1 and
 * do not expose zone selection to userspace. If future hardware turns out to
 * have multiple physical zones, expose a `zone` sysfs attribute and rewrite
 * this driver accordingly.
 *
 * Brightness is implemented in this driver by scaling the stored 0-255 RGB
 * values by a 0-100 percent factor before issuing the WMI write. The firmware
 * accepts 0-255 per channel; a 0-100 userspace knob is a convenience that maps
 * to 0-255 internally.
 *
 * Author: Janindu (stage1), Stage 2 driver derived from the same WMI protocol.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <linux/sysfs.h>
#include <linux/mutex.h>

#define HP_WMI_GUID "5FB7F034-2C63-45E9-BE91-3D44E2C707E4"

/* The physical keyboard on the tested Victus is a single zone. */
#define HP_VICTUS_RGB_ZONE 1

struct hp_wmi_input {
	u32 signature;
	u32 command;
	u32 commandtype;
	u32 datasize;
	u8 data[];
} __packed;

struct hp_victus_rgb {
	struct platform_device *pdev;
	struct mutex lock;

	/* Last RGB written by userspace, 0-255 per channel. */
	u8 r;
	u8 g;
	u8 b;

	/* Brightness 0-100 (percent). Stored separately so that changing
	 * brightness re-applies the same hue without the GUI having to resend. */
	u8 brightness;
};

static struct hp_victus_rgb *g_rgb;

/* ----------------------------------------------------------------- WMI glue */

static int hp_wmi_rgb_write(u8 zone, u8 r, u8 g, u8 b)
{
	struct hp_wmi_input *input;
	struct acpi_buffer in;
	struct acpi_buffer out;
	acpi_status status;

	input = kzalloc(sizeof(*input) + 4, GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input->signature   = 0x55434553;
	input->command     = 0x00020008;
	input->commandtype = 0x05;
	input->datasize    = 4;
	input->data[0]     = zone;
	input->data[1]     = r;
	input->data[2]     = g;
	input->data[3]     = b;

	in.length  = sizeof(*input) + 4;
	in.pointer = input;

	out.length  = ACPI_ALLOCATE_BUFFER;
	out.pointer = NULL;

	status = wmi_evaluate_method(HP_WMI_GUID, 0, 1, &in, &out);

	kfree(input);
	if (out.pointer)
		kfree(out.pointer);

	return ACPI_SUCCESS(status) ? 0 : -EIO;
}

static int hp_wmi_rgb_read(u8 zone, u8 *r, u8 *g, u8 *b)
{
	struct hp_wmi_input *input;
	struct acpi_buffer in;
	struct acpi_buffer out;
	acpi_status status;
	int ret = -EIO;

	input = kzalloc(sizeof(*input) + 1, GFP_KERNEL);
	if (!input)
		return -ENOMEM;

	input->signature   = 0x55434553;
	input->command     = 0x00020008;
	input->commandtype = 0x04;
	input->datasize    = 1;
	input->data[0]     = zone;

	in.length  = sizeof(*input) + 1;
	in.pointer = input;

	out.length  = ACPI_ALLOCATE_BUFFER;
	out.pointer = NULL;

	status = wmi_evaluate_method(HP_WMI_GUID, 0, 2, &in, &out);
	kfree(input);

	if (ACPI_SUCCESS(status) && out.pointer) {
		union acpi_object *obj = out.pointer;

		if (obj->type == ACPI_TYPE_BUFFER &&
		    obj->buffer.length >= 12 &&
		    memcmp(obj->buffer.pointer, "PASS", 4) == 0) {
			if (r)
				*r = obj->buffer.pointer[8];
			if (g)
				*g = obj->buffer.pointer[9];
			if (b)
				*b = obj->buffer.pointer[10];
			ret = 0;
		}
	}

	if (out.pointer)
		kfree(out.pointer);

	return ret;
}

/* Apply the currently stored (r,g,b,brightness) to the hardware. Caller holds
 * the mutex. */
static int hp_victus_rgb_apply_locked(struct hp_victus_rgb *rgb)
{
	/* Scale the stored 0-255 color by brightness 0-100 percent before
	 * sending to the EC. Single-stage scaling with rounding so 100% is
	 * exactly the stored value and 0% is exactly off. */
	u32 r = ((u32)rgb->r * rgb->brightness + 50) / 100;
	u32 g = ((u32)rgb->g * rgb->brightness + 50) / 100;
	u32 b = ((u32)rgb->b * rgb->brightness + 50) / 100;

	return hp_wmi_rgb_write(HP_VICTUS_RGB_ZONE, (u8)r, (u8)g, (u8)b);
}

/* --------------------------------------------------------------- sysfs attrs */

/* color: "RRGGBB" hex string, e.g. "ff8800". Write triggers an immediate WMI
 * write to zone 1 with the brightness-scaled values. Read returns the last
 * stored (unscaled) color. */
static ssize_t color_show(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	struct hp_victus_rgb *rgb = dev_get_drvdata(dev);

	mutex_lock(&rgb->lock);
	snprintf(buf, PAGE_SIZE, "%02x%02x%02x\n", rgb->r, rgb->g, rgb->b);
	mutex_unlock(&rgb->lock);
	return strlen(buf);
}

static ssize_t color_store(struct device *dev,
			   struct device_attribute *attr,
			   const char *buf, size_t count)
{
	struct hp_victus_rgb *rgb = dev_get_drvdata(dev);
	unsigned int r, g, b;
	int ret;

	if (sscanf(buf, "%02x%02x%02x", &r, &g, &b) != 3)
		return -EINVAL;

	mutex_lock(&rgb->lock);
	rgb->r = (u8)r;
	rgb->g = (u8)g;
	rgb->b = (u8)b;
	ret = hp_victus_rgb_apply_locked(rgb);
	mutex_unlock(&rgb->lock);

	return ret ? ret : count;
}

/* brightness: 0-100 percent. Stored separately from color so that moving the
 * brightness slider alone re-applies the same hue. */
static ssize_t brightness_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct hp_victus_rgb *rgb = dev_get_drvdata(dev);
	int len;

	mutex_lock(&rgb->lock);
	len = snprintf(buf, PAGE_SIZE, "%u\n", rgb->brightness);
	mutex_unlock(&rgb->lock);
	return len;
}

static ssize_t brightness_store(struct device *dev,
			       struct device_attribute *attr,
			       const char *buf, size_t count)
{
	struct hp_victus_rgb *rgb = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	if (kstrtouint(buf, 10, &val) < 0)
		return -EINVAL;
	if (val > 100)
		return -EINVAL;

	mutex_lock(&rgb->lock);
	rgb->brightness = (u8)val;
	ret = hp_victus_rgb_apply_locked(rgb);
	mutex_unlock(&rgb->lock);

	return ret ? ret : count;
}

/* raw: a debugging hook. "RRGGBB" written here bypasses the brightness scaling
 * and writes the exact 0-255 values to zone 1. Useful for verifying the
 * firmware's own brightness behaviour vs. our scaling. */
static ssize_t raw_show(struct device *dev,
		       struct device_attribute *attr, char *buf)
{
	u8 r, g, b;
	int ret;

	ret = hp_wmi_rgb_read(HP_VICTUS_RGB_ZONE, &r, &g, &b);
	if (ret)
		return ret;
	return snprintf(buf, PAGE_SIZE, "%02x%02x%02x\n", r, g, b);
}

static ssize_t raw_store(struct device *dev,
			struct device_attribute *attr,
			const char *buf, size_t count)
{
	unsigned int r, g, b;

	if (sscanf(buf, "%02x%02x%02x", &r, &g, &b) != 3)
		return -EINVAL;
	return hp_wmi_rgb_write(HP_VICTUS_RGB_ZONE, (u8)r, (u8)g, (u8)b) ? -EIO : count;
}

static DEVICE_ATTR_RW(color);
static DEVICE_ATTR_RW(brightness);
static DEVICE_ATTR_RW(raw);

static struct attribute *hp_victus_rgb_attrs[] = {
	&dev_attr_color.attr,
	&dev_attr_brightness.attr,
	&dev_attr_raw.attr,
	NULL
};

static const struct attribute_group hp_victus_rgb_group = {
	.attrs = hp_victus_rgb_attrs,
};

/* --------------------------------------------------------------- platform drv */

static int hp_victus_rgb_probe(struct platform_device *pdev)
{
	struct hp_victus_rgb *rgb;
	u8 r, g, b;
	int ret;

	if (!wmi_has_guid(HP_WMI_GUID)) {
		dev_err(&pdev->dev, "HP WMI GUID not present on this system\n");
		return -ENODEV;
	}

	rgb = devm_kzalloc(&pdev->dev, sizeof(*rgb), GFP_KERNEL);
	if (!rgb)
		return -ENOMEM;

	rgb->pdev = pdev;
	mutex_init(&rgb->lock);

	/* Read the current hardware state so the GUI opens on the color that's
	 * actually on the keyboard. Fall back to white at 50% if read fails. */
	if (hp_wmi_rgb_read(HP_VICTUS_RGB_ZONE, &r, &g, &b) == 0) {
		rgb->r = r;
		rgb->g = g;
		rgb->b = b;
		rgb->brightness = 100;
	} else {
		rgb->r = 0xff;
		rgb->g = 0xff;
		rgb->b = 0xff;
		rgb->brightness = 50;
	}

	platform_set_drvdata(pdev, rgb);
	g_rgb = rgb;

	ret = sysfs_create_group(&pdev->dev.kobj, &hp_victus_rgb_group);
	if (ret) {
		dev_err(&pdev->dev, "failed to create sysfs group: %d\n", ret);
		return ret;
	}

	dev_info(&pdev->dev,
		 "HP Victus RGB ready: zone %d, current %02x%02x%02x @ %u%%\n",
		 HP_VICTUS_RGB_ZONE, rgb->r, rgb->g, rgb->b, rgb->brightness);
	return 0;
}

static void hp_victus_rgb_remove(struct platform_device *pdev)
{
	sysfs_remove_group(&pdev->dev.kobj, &hp_victus_rgb_group);
	g_rgb = NULL;
}

static struct platform_driver hp_victus_rgb_driver = {
	.driver = {
		.name  = "hp-victus-rgb",
	},
	.probe  = hp_victus_rgb_probe,
	.remove = hp_victus_rgb_remove,
};

/* The platform device is created manually so the sysfs path is stable and
 * predictable: /sys/devices/platform/hp-victus-rgb/ */
static struct platform_device *hp_victus_rgb_pdev;

static int __init hp_victus_rgb_init(void)
{
	int ret;

	if (!wmi_has_guid(HP_WMI_GUID))
		return -ENODEV;

	ret = platform_driver_register(&hp_victus_rgb_driver);
	if (ret)
		return ret;

	hp_victus_rgb_pdev = platform_device_register_simple("hp-victus-rgb", -1, NULL, 0);
	if (IS_ERR(hp_victus_rgb_pdev)) {
		platform_driver_unregister(&hp_victus_rgb_driver);
		return PTR_ERR(hp_victus_rgb_pdev);
	}
	return 0;
}

static void __exit hp_victus_rgb_exit(void)
{
	platform_device_unregister(hp_victus_rgb_pdev);
	platform_driver_unregister(&hp_victus_rgb_driver);
}

module_init(hp_victus_rgb_init);
module_exit(hp_victus_rgb_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Janindu (stage1) / Stage 2 driver");
MODULE_DESCRIPTION("HP Victus keyboard RGB control via HP WMI (sysfs interface)");
