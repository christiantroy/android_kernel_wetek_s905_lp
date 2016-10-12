/*
 * drivers/amlogic/display/vout/lcd/lcd_extern/i2c_tc101.c
 *
 * Copyright (C) 2015 Amlogic, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
*/


#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/amlogic/i2c-amlogic.h>
#include <linux/miscdevice.h>
#include <linux/mutex.h>
#include <linux/mm.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/delay.h>
#include <linux/sysctl.h>
#include <linux/uaccess.h>
#include <linux/amlogic/vout/lcd_extern.h>

static struct lcd_extern_config_s *ext_config;
static struct i2c_client *aml_tc101_i2c_client;

/* #define LCD_EXT_DEBUG_INFO */
#ifdef LCD_EXT_DEBUG_INFO
#define DBG_PRINT(...)		pr_info(__VA_ARGS__)
#else
#define DBG_PRINT(...)
#endif

#define LCD_EXTERN_NAME			"lcd_i2c_tc101"

static unsigned char i2c_init_table[][3] = {
	/* {0xff, 0xff, 20},//delay mark(20ms) */
	{0xf8, 0x30, 0xb2},
	{0xf8, 0x33, 0xc2},
	{0xf8, 0x31, 0xf0},
	{0xf8, 0x40, 0x80},
	{0xf8, 0x81, 0xec},
	{0xff, 0xff, 0xff},/* end mark */
};

static int aml_i2c_write(struct i2c_client *i2client, unsigned char *buff,
		unsigned len)
{
	int ret = 0;
	struct i2c_msg msg[] = {
		{
			.addr = i2client->addr,
			.flags = 0,
			.len = len,
			.buf = buff,
		},
	};

	ret = i2c_transfer(i2client->adapter, msg, 1);
	if (ret < 0) {
		pr_info("%s: i2c transfer failed [addr 0x%02x]\n",
			__func__, i2client->addr);
	}

	return ret;
}
#if 0
static int aml_i2c_read(struct i2c_client *i2client, unsigned char *buff,
		unsigned len)
{
	int ret = 0;
	struct i2c_msg msgs[] = {
		{
			.addr = i2client->addr,
			.flags = 0,
			.len = 1,
			.buf = buff,
		},
		{
			.addr = i2client->addr,
			.flags = I2C_M_RD,
			.len = len,
			.buf = buff,
		},
	};

	ret = i2c_transfer(i2client->adapter, msgs, 2);
	if (ret < 0) {
		pr_info("%s: i2c transfer failed [addr 0x%02x]\n",
			__func__, i2client->addr);
	}

	return ret;
}
#endif

static int i2c_reg_read(unsigned char reg, unsigned char *buf)
{
	int ret = 0;

	return ret;
}

static int i2c_reg_write(unsigned char reg, unsigned char value)
{
	int ret = 0;

	return ret;
}

static int lcd_extern_i2c_init(void)
{
	unsigned char tData[4];
	int i = 0, ending_flag = 0;
	int ret = 0;

	while (ending_flag == 0) {
		if ((i2c_init_table[i][0] == 0xff) &&
			(i2c_init_table[i][1] == 0xff)) { /* special mark */
			if (i2c_init_table[i][2] == 0xff) /* ending flag */
				ending_flag = 1;
			else /* delay flag */
				mdelay(i2c_init_table[i][2]);
		} else {
			tData[0] = i2c_init_table[i][0];
			tData[1] = i2c_init_table[i][1];
			tData[2] = i2c_init_table[i][2];
			aml_i2c_write(aml_tc101_i2c_client, tData, 3);
		}
		i++;
	}
	pr_info("%s\n", __func__);
	return ret;
}

static int lcd_extern_i2c_remove(void)
{
	int ret = 0;

	return ret;
}

static int get_lcd_extern_config(struct platform_device *pdev,
		struct lcd_extern_config_s *ext_cfg)
{
	int ret = 0;
	struct aml_lcd_extern_driver_s *lcd_ext;

	ret = get_lcd_extern_dt_data(pdev, ext_cfg);
	if (ret) {
		pr_info("[error] %s: failed to get dt data\n", LCD_EXTERN_NAME);
		return ret;
	}

	/* lcd extern driver update */
	lcd_ext = aml_lcd_extern_get_driver();
	if (lcd_ext) {
		lcd_ext->type      = ext_cfg->type;
		lcd_ext->name      = ext_cfg->name;
		lcd_ext->reg_read  = i2c_reg_read;
		lcd_ext->reg_write = i2c_reg_write;
		lcd_ext->power_on  = lcd_extern_i2c_init;
		lcd_ext->power_off = lcd_extern_i2c_remove;
	} else {
		pr_info("[error] %s get lcd_extern_driver failed\n",
			ext_cfg->name);
		ret = -1;
	}

	return ret;
}

static int aml_tc101_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		pr_info("[error] %s: functionality check failed\n", __func__);
		return -ENODEV;
	} else {
		aml_tc101_i2c_client = client;
	}

	pr_info("%s OK\n", __func__);
	return 0;
}

static int aml_tc101_i2c_remove(struct i2c_client *client)
{
	return 0;
}

static const struct i2c_device_id aml_tc101_i2c_id[] = {
	{LCD_EXTERN_NAME, 0},
	{ }
};
/* MODULE_DEVICE_TABLE(i2c, aml_tc101_id); */

static struct i2c_driver aml_tc101_i2c_driver = {
	.probe    = aml_tc101_i2c_probe,
	.remove   = aml_tc101_i2c_remove,
	.id_table = aml_tc101_i2c_id,
	.driver = {
		.name = LCD_EXTERN_NAME,
		.owner = THIS_MODULE,
	},
};

static int aml_tc101_probe(struct platform_device *pdev)
{
	struct i2c_board_info i2c_info;
	struct i2c_adapter *adapter;
	struct i2c_client *i2c_client;
	/* int i = 0; */
	int ret = 0;

	if (lcd_extern_driver_check() == FALSE)
		return -1;
	if (ext_config == NULL)
		ext_config = kzalloc(sizeof(*ext_config), GFP_KERNEL);
	if (ext_config == NULL) {
		pr_info("[error] %s probe: failed to alloc data\n",
			LCD_EXTERN_NAME);
		return -1;
	}

	pdev->dev.platform_data = ext_config;
	ret = get_lcd_extern_config(pdev, ext_config);
	if (ret)
		goto lcd_extern_probe_failed;

	memset(&i2c_info, 0, sizeof(i2c_info));

	adapter = i2c_get_adapter(ext_config->i2c_bus);
	if (!adapter) {
		pr_info("[error] %s��failed to get i2c adapter\n",
			LCD_EXTERN_NAME);
		goto lcd_extern_probe_failed;
	}

	strncpy(i2c_info.type, ext_config->name, I2C_NAME_SIZE);
	i2c_info.addr = ext_config->i2c_addr;
	i2c_info.platform_data = ext_config;
	i2c_info.flags = 0;
	if (i2c_info.addr > 0x7f)
		i2c_info.flags = 0x10;
	i2c_client = i2c_new_device(adapter, &i2c_info);
	if (!i2c_client) {
		pr_info("[error] %s :failed to new i2c device\n",
			LCD_EXTERN_NAME);
		goto lcd_extern_probe_failed;
	} else {
		DBG_PRINT("%s: new i2c device succeed\n", LCD_EXTERN_NAME);
	}

	if (!aml_tc101_i2c_client) {
		ret = i2c_add_driver(&aml_tc101_i2c_driver);
		if (ret) {
			pr_info("[error] lcd_extern probe: ");
			pr_info("add i2c_driver failed\n");
			goto lcd_extern_probe_failed;
		}
	}

	pr_info("%s ok\n", __func__);
	return ret;

lcd_extern_probe_failed:
	kfree(ext_config);
	ext_config = NULL;
	return -1;
}

static int aml_tc101_remove(struct platform_device *pdev)
{
	kfree(pdev->dev.platform_data);
	return 0;
}

#ifdef CONFIG_USE_OF
static const struct of_device_id aml_tc101_dt_match[] = {
	{
		.compatible = "amlogic, lcd_i2c_tc101",
	},
	{},
};
#else
#define aml_tc101_dt_match NULL
#endif

static struct platform_driver aml_tc101_driver = {
	.probe  = aml_tc101_probe,
	.remove = aml_tc101_remove,
	.driver = {
		.name  = LCD_EXTERN_NAME,
		.owner = THIS_MODULE,
#ifdef CONFIG_USE_OF
		.of_match_table = aml_tc101_dt_match,
#endif
	},
};

static int __init aml_tc101_init(void)
{
	int ret;
	DBG_PRINT("%s\n", __func__);

	ret = platform_driver_register(&aml_tc101_driver);
	if (ret) {
		pr_info("[error] %s failed ", __func__);
		pr_info("to register lcd extern driver module\n");
		return -ENODEV;
	}
	return ret;
}

static void __exit aml_tc101_exit(void)
{
	platform_driver_unregister(&aml_tc101_driver);
}

/* late_initcall(aml_tc101_init); */
module_init(aml_tc101_init);
module_exit(aml_tc101_exit);

MODULE_AUTHOR("AMLOGIC");
MODULE_DESCRIPTION("LCD Extern driver for TC101");
MODULE_LICENSE("GPL");
