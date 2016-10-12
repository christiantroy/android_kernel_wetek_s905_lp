/*
 * Meson Power Management Routines
 *
 * Copyright (C) 2010 Amlogic, Inc. http://www.amlogic.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pm.h>
#include <linux/suspend.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/clk.h>
#include <linux/fs.h>

#include <asm/cacheflush.h>
#include <linux/delay.h>
#include <linux/uaccess.h>
#include <linux/io.h>
#include <linux/amlogic/iomap.h>

#include <linux/init.h>
#include <linux/of.h>

#include <asm/compiler.h>
#include <linux/errno.h>
#include <asm/psci.h>
#include <linux/suspend.h>

#include <asm/suspend.h>
#include <linux/of_address.h>
#include <linux/input.h>
#include <linux/amlogic/pm.h>
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend early_suspend;
static int early_suspend_flag;
#endif
static DEFINE_MUTEX(late_suspend_lock);
static LIST_HEAD(late_suspend_handlers);
static void __iomem *debug_reg;
static void __iomem *exit_reg;
int has_sysled = 1;//from drivers/amlogic/led/aml_led_sys.c

enum {
	DEBUG_USER_STATE = 1U << 0,
	DEBUG_SUSPEND = 1U << 2,
};
static int debug_mask = DEBUG_USER_STATE | DEBUG_SUSPEND;
struct late_suspend {
	struct list_head link;
	int level;
	void (*suspend)(struct late_suspend *h);
	void (*resume)(struct late_suspend *h);
	void *param;
};

void register_late_suspend(struct late_suspend *handler)
{
	struct list_head *pos;

	mutex_lock(&late_suspend_lock);
	list_for_each(pos, &late_suspend_handlers) {
		struct late_suspend *e;
		e = list_entry(pos, struct late_suspend, link);
		if (e->level > handler->level)
			break;
	}
	list_add_tail(&handler->link, pos);
	mutex_unlock(&late_suspend_lock);
}
EXPORT_SYMBOL(register_late_suspend);

void unregister_late_suspend(struct late_suspend *handler)
{
	mutex_lock(&late_suspend_lock);
	list_del(&handler->link);
	mutex_unlock(&late_suspend_lock);
}
EXPORT_SYMBOL(unregister_late_suspend);


static void late_suspend(void)
{
	struct late_suspend *pos;

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_suspend: call handlers\n");
	list_for_each_entry(pos, &late_suspend_handlers, link) {
		if (pos->suspend != NULL) {
			pr_info("%pf\n", pos->suspend);
			pos->suspend(pos);
		}
	}

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_suspend: sync\n");

}

static void early_resume(void)
{
	struct late_suspend *pos;

	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume: call handlers\n");
	list_for_each_entry_reverse(pos, &late_suspend_handlers, link)
	    if (pos->resume != NULL) {
		pr_info("%pf\n", pos->resume);
		pos->resume(pos);
	}
	if (debug_mask & DEBUG_SUSPEND)
		pr_info("late_resume: done\n");
}

#define     AO_RTI_PIN_MUX_REG	((0x05 << 2))
#define     AO_RTI_PIN_MUX_REG2	((0x06 << 2))
#define     P_AO_GPIO_O_EN_N	((0x09 << 2))
#define     P_AO_GPIO_I	((0x00 << 10) | (0x0A << 2))
#define AO_RTI_PULL_UP_REG ((0x00 << 10) | (0x0B << 2))

static void meson_system_led_control(int onoff)
{
	if(has_sysled != true)
		return;

/*gpio ao 13*/
	if(onoff)	//green led
	{
		aml_aobus_update_bits(AO_RTI_PIN_MUX_REG, BIT(3)|BIT(4), 0);
		aml_aobus_update_bits(AO_RTI_PIN_MUX_REG2, BIT(1)|BIT(31), 0);
		aml_aobus_update_bits(P_AO_GPIO_O_EN_N, BIT(13), 0);
		aml_aobus_update_bits(P_AO_GPIO_O_EN_N, BIT(29), BIT(29));
	}
	else	//red led
	{
		aml_aobus_update_bits(AO_RTI_PIN_MUX_REG, BIT(3)|BIT(4), 0);
		aml_aobus_update_bits(AO_RTI_PIN_MUX_REG2, BIT(1)|BIT(31), 0);
		aml_aobus_update_bits(P_AO_GPIO_O_EN_N, BIT(13), 0);
		aml_aobus_update_bits(P_AO_GPIO_O_EN_N, BIT(29), 0);
	}
}


#ifdef CONFIG_HAS_EARLYSUSPEND
static void meson_system_early_suspend(struct early_suspend *h)
{
	if (!early_suspend_flag) {
		pr_info(KERN_INFO "%s\n", __func__);
		do_aml_poweroff();
		early_suspend_flag = 1;
	}
}

static void meson_system_late_resume(struct early_suspend *h)
{
	if (early_suspend_flag) {
		/* early_power_gate_switch(ON); */
		/* early_clk_switch(ON); */
		early_suspend_flag = 0;
		pr_info(KERN_INFO "%s\n", __func__);
	}
}
#endif

/*
 *0x10000 : bit[16]=1:control cpu suspend to power down
 *
 */
static void meson_gx_suspend(void)
{
	pr_info(KERN_INFO "enter meson_pm_suspend!\n");
	late_suspend();
	cpu_suspend(0x0010000);
	early_resume();
	pr_info(KERN_INFO "... wake up\n");

}

static int meson_pm_prepare(void)
{
	pr_info(KERN_INFO "enter meson_pm_prepare!\n");
	return 0;
}

static int meson_gx_enter(suspend_state_t state)
{
	int ret = 0;
	switch (state) {
	case PM_SUSPEND_STANDBY:
	case PM_SUSPEND_MEM:
		meson_gx_suspend();
		break;
	default:
		ret = -EINVAL;
	}
	return ret;
}

static void meson_pm_finish(void)
{
	pr_info(KERN_INFO "enter meson_pm_finish!\n");
}
unsigned int get_resume_method(void)
{
	unsigned int val = 0;
	if (exit_reg)
		val = (readl(exit_reg) >> 28) & 0xf;
	return val;
}
EXPORT_SYMBOL(get_resume_method);

static const struct platform_suspend_ops meson_gx_ops = {
	.enter = meson_gx_enter,
	.prepare = meson_pm_prepare,
	.finish = meson_pm_finish,
	.valid = suspend_valid_only_mem,
};

ssize_t time_out_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	unsigned val = 0, len;

	val = readl(debug_reg);

	len = sprintf(buf, "%d\n", val);

	return len;
}
ssize_t time_out_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	unsigned time_out;
	int ret;

	ret = sscanf(buf, "%d", &time_out);

	switch (ret) {
	case 1:
		writel(time_out, debug_reg);
		break;
	default:
		return -EINVAL;
	}

	return count;
}


DEVICE_ATTR(time_out, 0666, time_out_show, time_out_store);

static int suspend_reason;
static noinline int __invoke_psci_fn_smc(u64 function_id, u64 arg0, u64 arg1,
					 u64 arg2)
{
	register long x0 asm("x0") = function_id;
	register long x1 asm("x1") = arg0;
	register long x2 asm("x2") = arg1;
	register long x3 asm("x3") = arg2;
	asm volatile(
			__asmeq("%0", "x0")
			__asmeq("%1", "x1")
			__asmeq("%2", "x2")
			__asmeq("%3", "x3")
			"smc	#0\n"
		: "+r" (x0)
		: "r" (x1), "r" (x2), "r" (x3));

	return x0;
}

ssize_t suspend_reason_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	unsigned  len;

	len = sprintf(buf, "%d\n", suspend_reason);

	return len;
}
ssize_t suspend_reason_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	int ret;

	ret = sscanf(buf, "%d", &suspend_reason);

	switch (ret) {
	case 1:
		__invoke_psci_fn_smc(0x82000042, suspend_reason, 0, 0);
		break;
	default:
		return -EINVAL;
	}
	return count;
}

DEVICE_ATTR(suspend_reason, 0666, suspend_reason_show, suspend_reason_store);

ssize_t led_test_show(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	return 0;
}
ssize_t led_test_store(struct device *dev, struct device_attribute *attr,
		 const char *buf, size_t count)
{
	int ret;
	int led_test = 0;

	ret = sscanf(buf, "%d", &led_test);

	switch (led_test) {
	case 0:
		meson_system_led_control(0);
		break;
	case 1:
		meson_system_led_control(1);
		break;
	default:
		return -EINVAL;
	}
	return count;
}

DEVICE_ATTR(led_test, 0666, led_test_show, led_test_store);

static int __init meson_pm_probe(struct platform_device *pdev)
{
	pr_info(KERN_INFO "enter meson_pm_probe!\n");
#ifdef CONFIG_HAS_EARLYSUSPEND
	early_suspend.level = EARLY_SUSPEND_LEVEL_DISABLE_FB;
	early_suspend.suspend = meson_system_early_suspend;
	early_suspend.resume = meson_system_late_resume;
	register_early_suspend(&early_suspend);
#endif

	if (of_property_read_bool(pdev->dev.of_node, "gxbaby-suspend"))
		suspend_set_ops(&meson_gx_ops);

	debug_reg = of_iomap(pdev->dev.of_node, 0);
	exit_reg = of_iomap(pdev->dev.of_node, 1);
	writel(0x0, debug_reg);
	device_create_file(&pdev->dev, &dev_attr_time_out);
	device_create_file(&pdev->dev, &dev_attr_suspend_reason);
	device_create_file(&pdev->dev, &dev_attr_led_test);
	device_rename(&pdev->dev, "aml_pm");
	pr_info("meson_pm_probe done\n");
	return 0;
}

static int __exit meson_pm_remove(struct platform_device *pdev)
{
#ifdef CONFIG_HAS_EARLYSUSPEND
	unregister_early_suspend(&early_suspend);
#endif
	return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id amlogic_pm_dt_match[] = {
	{.compatible = "amlogic, pm",
	 },
};
#else
#define amlogic_nand_dt_match NULL
#endif

static struct platform_driver meson_pm_driver = {
	.driver = {
		   .name = "pm-meson",
		   .owner = THIS_MODULE,
		   .of_match_table = amlogic_pm_dt_match,
		   },
	.remove = __exit_p(meson_pm_remove),
};

static int __init meson_pm_init(void)
{
	return platform_driver_probe(&meson_pm_driver, meson_pm_probe);
}

late_initcall(meson_pm_init);
