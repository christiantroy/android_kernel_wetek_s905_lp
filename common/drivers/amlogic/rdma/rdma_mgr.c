/*
 * drivers/amlogic/rdma/rdma_mgr.c
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
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>
#include <linux/fs.h>
#include <linux/dma-mapping.h>

#include <linux/string.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/ctype.h>
#include <linux/amlogic/vout/vout_notify.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <linux/clk.h>
#include <linux/kthread.h>
#include <linux/slab.h>

#include <linux/amlogic/rdma/rdma_mgr.h>
#include "../amports/vdec_reg.h"

#define DRIVER_NAME "amlogic-rdma"
#define MODULE_NAME "amlogic-rdma"
#define DEVICE_NAME "rdma"
#define CLASS_NAME  "rdma-class"

#define pr_dbg(fmt, args...) pr_info(KERN_DEBUG "RDMA: " fmt, ## args)
#define pr_error(fmt, args...) pr_info(KERN_ERR "RDMA: " fmt, ## args)

#define rdma_io_read(addr) readl(addr)
#define rdma_io_write(addr, val) writel((val), addr);

static int debug_flag;
/* burst size 0=16; 1=24; 2=32; 3=48.*/
static int ctrl_ahb_rd_burst_size = 3;
static int ctrl_ahb_wr_burst_size = 3;
static int rdma_watchdog = 4;
static int reset_count;
static int rdma_watchdog_count;
static int rdma_monitor_reg;
static int rdma_force_reset = -1;
#define RDMA_NUM 8
struct rdma_regadr_s {
	u32 rdma_ahb_start_addr;
	u32 rdma_ahb_end_addr;
	u32 trigger_mask_reg;
	u32 trigger_mask_reg_bitpos;
	u32 addr_inc_reg;
	u32 addr_inc_reg_bitpos;
	u32 rw_flag_reg;
	u32 rw_flag_reg_bitpos;
	u32 clear_irq_bitpos;
	u32 irq_status_bitpos;
};

struct rdma_instance_s {
	int not_process;
	struct rdma_regadr_s *rdma_regadr;
	struct rdma_op_s *op;
	void *op_arg;
	int rdma_table_size;
	u32 *reg_buf;
	dma_addr_t dma_handle;
	u32 *rdma_table_addr;
	u32 rdma_table_phy_addr;
	int rdma_item_count;
	unsigned char keep_buf;
	unsigned char used;
	int prev_trigger_type;
};

struct rdma_device_info {
	const char *device_name;
	struct platform_device *rdma_dev;
	struct rdma_instance_s rdma_ins[RDMA_NUM];
};

static DEFINE_SPINLOCK(rdma_lock);

static struct rdma_device_info rdma_info;

static struct rdma_regadr_s rdma_regadr[RDMA_NUM] = {
	{RDMA_AHB_START_ADDR_MAN,
		RDMA_AHB_END_ADDR_MAN,
		0, 0,
		RDMA_ACCESS_MAN, 1,
		RDMA_ACCESS_MAN, 2,
		24, 24
	},
	{RDMA_AHB_START_ADDR_1,
		RDMA_AHB_END_ADDR_1,
		RDMA_ACCESS_AUTO,  8,
		RDMA_ACCESS_AUTO,  1,
		RDMA_ACCESS_AUTO,  5,
		25, 25
	},
	{RDMA_AHB_START_ADDR_2,
			RDMA_AHB_END_ADDR_2,
			RDMA_ACCESS_AUTO,  16,
			RDMA_ACCESS_AUTO,  2,
			RDMA_ACCESS_AUTO,  6,
			26, 26
	},
	{RDMA_AHB_START_ADDR_3,
		RDMA_AHB_END_ADDR_3,
		RDMA_ACCESS_AUTO,  24,
		RDMA_ACCESS_AUTO,  3,
		RDMA_ACCESS_AUTO,  7,
		27, 27
	},
	{RDMA_AHB_START_ADDR_4,
			RDMA_AHB_END_ADDR_4,
			RDMA_ACCESS_AUTO3, 0,
			RDMA_ACCESS_AUTO2, 0,
			RDMA_ACCESS_AUTO2, 4,
			28, 28
	},
	{RDMA_AHB_START_ADDR_5,
		RDMA_AHB_END_ADDR_5,
		RDMA_ACCESS_AUTO3, 8,
		RDMA_ACCESS_AUTO2, 1,
		RDMA_ACCESS_AUTO2, 5,
		29, 29
	},
	{RDMA_AHB_START_ADDR_6,
		RDMA_AHB_END_ADDR_6,
		RDMA_ACCESS_AUTO3, 16,
		RDMA_ACCESS_AUTO2, 2,
		RDMA_ACCESS_AUTO2, 6,
		30, 30
	},
	{RDMA_AHB_START_ADDR_7,
		RDMA_AHB_END_ADDR_7,
		RDMA_ACCESS_AUTO3, 24,
		RDMA_ACCESS_AUTO2, 3,
		RDMA_ACCESS_AUTO2, 7,
		31, 31
	}
};

int rdma_register(struct rdma_op_s *rdma_op, void *op_arg, int table_size)
{
	int i;
	struct rdma_device_info *info = &rdma_info;
	dma_addr_t dma_handle;
	for (i = 1; i < RDMA_NUM; i++) {
		/* 0 is reserved for RDMA MANUAL */
		if (info->rdma_ins[i].op == NULL &&
				info->rdma_ins[i].used == 0) {
			info->rdma_ins[i].not_process = 0;
			info->rdma_ins[i].op_arg = op_arg;

			if (info->rdma_ins[i].rdma_table_size == 0) {
				info->rdma_ins[i].rdma_table_addr =
					dma_alloc_coherent(
					&info->rdma_dev->dev, table_size,
					&dma_handle, GFP_KERNEL);
				info->rdma_ins[i].rdma_table_phy_addr
					= (u32)(dma_handle);

				info->rdma_ins[i].reg_buf =
					kmalloc(table_size, GFP_KERNEL);
				pr_info("%s, rdma_table_addr %p rdma_table_addr_phy %x reg_buf %p\n",
					__func__,
					info->rdma_ins[i].rdma_table_addr,
					info->rdma_ins[i].rdma_table_phy_addr,
					info->rdma_ins[i].reg_buf);
				info->rdma_ins[i].rdma_table_size = table_size;
			}
			info->rdma_ins[i].op = rdma_op;
			break;
		}
	}
	if (i < RDMA_NUM) {
		if (info->rdma_ins[i].rdma_table_addr == NULL ||
			info->rdma_ins[i].reg_buf == NULL) {
			info->rdma_ins[i].op = NULL;
			i = -1;
			pr_info("%s: memory allocate fail\n",
				__func__);
		}	else
			pr_info("%s success, handle %d table_size %d\n",
				__func__, i, table_size);
		return i;
	}
	return -1;
}
EXPORT_SYMBOL(rdma_register);


void rdma_unregister(int i)
{
	unsigned long flags;
	struct rdma_device_info *info = &rdma_info;
	pr_info("%s(%d)\r\n", __func__, i);
	if (i > 0 && i < RDMA_NUM && info->rdma_ins[i].op) {
		int table_size;

		/*rdma_clear(i);*/
		spin_lock_irqsave(&rdma_lock, flags);
		table_size = info->rdma_ins[i].rdma_table_size;
		info->rdma_ins[i].op = NULL;
		if (!info->rdma_ins[i].keep_buf)
			info->rdma_ins[i].rdma_table_size = 0;
		spin_unlock_irqrestore(&rdma_lock, flags);

		info->rdma_ins[i].op_arg = NULL;
		if (!info->rdma_ins[i].keep_buf) {
			kfree(info->rdma_ins[i].reg_buf);
			info->rdma_ins[i].reg_buf = NULL;
			if (info->rdma_ins[i].rdma_table_addr) {
				dma_free_coherent(&info->rdma_dev->dev,
				table_size,
				info->rdma_ins[i].rdma_table_addr,
				(dma_addr_t)
				info->rdma_ins[i].rdma_table_phy_addr);
			}
		}
	}
}
EXPORT_SYMBOL(rdma_unregister);
static void rdma_reset(unsigned char external_reset)
{
	if (debug_flag & 4)
		pr_info("%s(%d)\n",
			__func__, external_reset);

	if (external_reset)	{
		WRITE_MPEG_REG(RESET4_REGISTER,
				   (1 << 5));
	} else {
		WRITE_VCBUS_REG(RDMA_CTRL, (0x1 << 1));
		WRITE_VCBUS_REG(RDMA_CTRL, (0x1 << 1));
		WRITE_VCBUS_REG(RDMA_CTRL, (ctrl_ahb_wr_burst_size << 4) |
			(ctrl_ahb_rd_burst_size << 2) |
			 (0x0 << 1));
	}
	reset_count++;
}
irqreturn_t rdma_mgr_isr(int irq, void *dev_id)
{
	struct rdma_device_info *info = &rdma_info;
	u32 rdma_status = READ_VCBUS_REG(RDMA_STATUS);
	int i;
	if (debug_flag & 0x10)
		return IRQ_HANDLED;

	if (debug_flag & 2)
		pr_info("%s: %x\r\n", __func__, rdma_status);
		rdma_watchdog_count = 0;
	for (i = 0; i < RDMA_NUM; i++) {
		struct rdma_instance_s *ins = &info->rdma_ins[i];
		if (ins->not_process)
			continue;
		/*bypass osd rdma done case*/
		if (i == 3)
			continue;
		if (rdma_status & (1 << ins->rdma_regadr->irq_status_bitpos)) {
			if (debug_flag & 2)
				pr_info("%s: process %d\r\n", __func__, i);

			WRITE_VCBUS_REG(RDMA_CTRL,
				(1 << ins->rdma_regadr->clear_irq_bitpos));

			if (ins->op && ins->op->irq_cb)
				ins->op->irq_cb(ins->op->arg);
		}
	}

	return IRQ_HANDLED;
}

/*
	trigger_type:
		0, stop,
		0x1~0xff, interrupt input trigger mode
		0x100, RDMA_TRIGGER_MANUAL
		> 0x100, debug mode

	return:
		-1, fail
		0, rdma table is empty, will not have rdma irq
		1, success
*/

int rdma_config(int handle, int trigger_type)
{
	int ret = 0;
	unsigned long flags;
	struct rdma_device_info *info = &rdma_info;
	struct rdma_instance_s *ins = &info->rdma_ins[handle];

	if (handle == 0)
		pr_info("%s error, rdma_config(handle == 0) not allowed\n",
			__func__);

	spin_lock_irqsave(&rdma_lock, flags);
	if (ins->op == NULL) {
		spin_unlock_irqrestore(&rdma_lock, flags);

		pr_info("%s: handle (%d) not register\n",
			__func__, handle);
		return -1;
	}
	/*This is a memory barrier*/
	wmb();
	if (ins->rdma_item_count <= 0 || trigger_type == 0) {
		if (trigger_type == RDMA_TRIGGER_MANUAL)
			WRITE_VCBUS_REG(RDMA_ACCESS_MAN,
				READ_VCBUS_REG(RDMA_ACCESS_MAN) & (~1));
			if (debug_flag & 2) {
				pr_info("%s: trigger_type %d : %d\r\n",
				__func__, trigger_type , ins->rdma_item_count);
			}
		WRITE_VCBUS_REG_BITS(
			ins->rdma_regadr->trigger_mask_reg,
			0, ins->rdma_regadr->trigger_mask_reg_bitpos, 8);
		ret = 0;
	} else {
		memcpy(ins->rdma_table_addr, ins->reg_buf,
			ins->rdma_item_count * 2 * sizeof(u32));

		if (trigger_type > 0 && trigger_type <= RDMA_TRIGGER_MANUAL) {
			ins->prev_trigger_type = trigger_type;
			if (trigger_type == RDMA_TRIGGER_MANUAL) {
				/*manual RDMA*/
				struct rdma_instance_s *man_ins =
					&info->rdma_ins[0];
				WRITE_VCBUS_REG(RDMA_ACCESS_MAN,
				READ_VCBUS_REG(RDMA_ACCESS_MAN) & (~1));
				WRITE_VCBUS_REG(
				man_ins->rdma_regadr->rdma_ahb_start_addr,
				ins->rdma_table_phy_addr);
				WRITE_VCBUS_REG(
				man_ins->rdma_regadr->rdma_ahb_end_addr,
				ins->rdma_table_phy_addr
				+ ins->rdma_item_count * 8 - 1);

				WRITE_VCBUS_REG_BITS(
				man_ins->rdma_regadr->addr_inc_reg,
				0,
				man_ins->rdma_regadr->addr_inc_reg_bitpos,
				1);
				WRITE_VCBUS_REG_BITS(
				man_ins->rdma_regadr->rw_flag_reg,
				1,
				man_ins->rdma_regadr->rw_flag_reg_bitpos,
				1);
		/* Manual-start RDMA*/
				WRITE_VCBUS_REG(RDMA_ACCESS_MAN,
					READ_VCBUS_REG(RDMA_ACCESS_MAN) | 1);

				if (debug_flag & 2)
					pr_info("%s: manual config %d:\r\n",
					__func__, ins->rdma_item_count);
			} else { /* interrupt input trigger RDMA */
				if (debug_flag & 2)
					pr_info("%s: case 3 : %d:\r\n",
					__func__ , ins->rdma_item_count);
				WRITE_VCBUS_REG_BITS(
				ins->rdma_regadr->trigger_mask_reg,
				0,
				ins->rdma_regadr->trigger_mask_reg_bitpos,
				8);

				WRITE_VCBUS_REG(
					ins->rdma_regadr->rdma_ahb_start_addr,
					ins->rdma_table_phy_addr);
				WRITE_VCBUS_REG(
					ins->rdma_regadr->rdma_ahb_end_addr,
					ins->rdma_table_phy_addr
					+ ins->rdma_item_count * 8 - 1);

				WRITE_VCBUS_REG_BITS(
					ins->rdma_regadr->addr_inc_reg,
					0,
					ins->rdma_regadr->addr_inc_reg_bitpos,
					1);
				WRITE_VCBUS_REG_BITS(
					ins->rdma_regadr->rw_flag_reg,
					1,
					ins->rdma_regadr->rw_flag_reg_bitpos,
					1);
				WRITE_VCBUS_REG_BITS(
				ins->rdma_regadr->trigger_mask_reg,
				trigger_type,
				ins->rdma_regadr->trigger_mask_reg_bitpos,
				8);
			}
		}	else if (trigger_type == 0x101) { /* debug mode */
			int i;
			for (i = 0; i < ins->rdma_item_count; i++) {
				WRITE_VCBUS_REG(ins->rdma_table_addr[i << 1],
					ins->rdma_table_addr[(i << 1) + 1]);
				if (debug_flag & 1)
					pr_info("WR(%x)<=%x\n",
					ins->rdma_table_addr[i << 1],
					ins->rdma_table_addr[(i << 1) + 1]);
			}
		} else if (trigger_type == 0x102) { /* debug mode */
			int i;
			for (i = 0; i < ins->rdma_item_count; i++) {
				WRITE_VCBUS_REG(ins->reg_buf[i << 1],
					ins->reg_buf[(i << 1) + 1]);
				if (debug_flag & 1)
					pr_info("WR(%x)<=%x\n",
					ins->reg_buf[i << 1],
					ins->reg_buf[(i << 1) + 1]);
			}
		}
		ret = 1;
	}
	ins->rdma_item_count = 0;
	spin_unlock_irqrestore(&rdma_lock, flags);

	if (debug_flag & 2)
		pr_info("%s: (%d 0x%x) ret %d\r\n",
			__func__, handle, trigger_type, ret);

	return ret;
}
EXPORT_SYMBOL(rdma_config);

int rdma_clear(int handle)
{
	int ret = 0;
	struct rdma_device_info *info = &rdma_info;
	struct rdma_instance_s *ins = &info->rdma_ins[handle];
	if (handle == 0 || ins->op == NULL) {
		pr_info("%s error, handle (%d) not register\n",
			__func__, handle);
		return -1;
	}

	WRITE_VCBUS_REG_BITS(ins->rdma_regadr->trigger_mask_reg,
		0, ins->rdma_regadr->trigger_mask_reg_bitpos, 8);

	return ret;
}
EXPORT_SYMBOL(rdma_clear);

u32 rdma_read_reg(int handle, u32 adr)
{
	int i;
	struct rdma_device_info *info = &rdma_info;
	struct rdma_instance_s *ins = &info->rdma_ins[handle];
	u32 read_val = READ_VCBUS_REG(adr);

	for (i = (ins->rdma_item_count - 1) ; i >= 0; i--) {
		if (ins->reg_buf[i << 1] == adr) {
			read_val = ins->reg_buf[(i << 1) + 1];
			break;
		}
	}
	return read_val;
}
EXPORT_SYMBOL(rdma_read_reg);


int rdma_reset_tigger_flag = 0;
int rdma_watchdog_setting(int flag)
{
	if (flag == 0)
		rdma_watchdog_count = 0;
	else
		rdma_watchdog_count++;
	if (debug_flag & 8) {
		rdma_force_reset = 1;
		debug_flag = 0;
	}
	return 0;
}
EXPORT_SYMBOL(rdma_watchdog_setting);

int rdma_write_reg(int handle, u32 adr, u32 val)
{
	struct rdma_device_info *info = &rdma_info;
	struct rdma_instance_s *ins = &info->rdma_ins[handle];

	if (ins->rdma_table_size == 0)
		return -1;

	if (debug_flag & 1)
		pr_info("rdma_write(%d) %d(%x)<=%x\n",
		handle, ins->rdma_item_count, adr, val);
	if (((ins->rdma_item_count << 1) + 1) <
		(ins->rdma_table_size / sizeof(u32))) {
		ins->reg_buf[ins->rdma_item_count << 1] = adr;
		ins->reg_buf[(ins->rdma_item_count << 1) + 1] = val;
		ins->rdma_item_count++;
	}	else {
		int i;
		if (debug_flag & 4)
			pr_info("%s(%d, %x, %x) buf overflow\n",
				__func__, handle, adr, val);
		rdma_watchdog_count++;
		if (debug_flag & 4) {
			rdma_watchdog_count = 4;
			debug_flag = 0;
		}
		if (rdma_watchdog > 0 && rdma_watchdog_count > rdma_watchdog) {
			rdma_watchdog_count = 0;
			rdma_reset(1);
			rdma_config(handle, ins->prev_trigger_type);
			pr_info("%s rdma reset\n", __func__);
			rdma_reset_tigger_flag = 1;
		}	else {
			for (i = 0; i < ins->rdma_item_count; i++)
				WRITE_VCBUS_REG(ins->reg_buf[i << 1],
					ins->reg_buf[(i << 1) + 1]);
		}
		ins->rdma_item_count = 0;
		ins->reg_buf[ins->rdma_item_count << 1] = adr;
		ins->reg_buf[(ins->rdma_item_count << 1) + 1] = val;
		ins->rdma_item_count++;
	}
	if (rdma_force_reset == 1) {
		rdma_force_reset = 0;
		rdma_reset_tigger_flag = 1;
		rdma_watchdog_count = 0;
		rdma_reset(1);
		rdma_config(handle, ins->prev_trigger_type);
		pr_info("%s rdma force reset\n", __func__);
	}
	return 0;
}
EXPORT_SYMBOL(rdma_write_reg);

int rdma_write_reg_bits(int handle, u32 adr, u32 val, u32 start, u32 len)
{
	int i;
	struct rdma_device_info *info = &rdma_info;
	struct rdma_instance_s *ins = &info->rdma_ins[handle];
	u32 read_val = READ_VCBUS_REG(adr);
	u32 write_val;

	if (ins->rdma_table_size == 0)
		return -1;

	for (i = (ins->rdma_item_count - 1) ; i >= 0; i--) {
		if (ins->reg_buf[i<<1] == adr) {
			read_val = ins->reg_buf[(i<<1)+1];
			break;
		}
	}
	write_val = (read_val & ~(((1L<<(len))-1)<<(start)))
		|((unsigned int)(val) << (start));
	if (debug_flag & 1)
		pr_info("rdma_write(%d) %d(%x)<=%x\n",
			handle, ins->rdma_item_count, adr, write_val);

	rdma_write_reg(handle, adr, write_val);
	return 0;
}
EXPORT_SYMBOL(rdma_write_reg_bits);

MODULE_PARM_DESC(debug_flag, "\n debug_flag\n");
module_param(debug_flag, uint, 0664);

MODULE_PARM_DESC(rdma_watchdog, "\n rdma_watchdog\n");
module_param(rdma_watchdog, uint, 0664);

MODULE_PARM_DESC(reset_count, "\n reset_count\n");
module_param(reset_count, uint, 0664);

MODULE_PARM_DESC(rdma_monitor_reg, "\n rdma_monitor_reg\n");
module_param(rdma_monitor_reg, uint, 0664);

MODULE_PARM_DESC(ctrl_ahb_rd_burst_size, "\n ctrl_ahb_rd_burst_size\n");
module_param(ctrl_ahb_rd_burst_size, uint, 0664);

MODULE_PARM_DESC(ctrl_ahb_wr_burst_size, "\n ctrl_ahb_wr_burst_size\n");
module_param(ctrl_ahb_wr_burst_size, uint, 0664);

/* static int __devinit rdma_probe(struct platform_device *pdev) */
static int rdma_probe(struct platform_device *pdev)
{
	int i;
	u32 data32;
	int int_rdma;
	struct rdma_device_info *info = &rdma_info;
	int_rdma = platform_get_irq_byname(pdev, "rdma");

	pr_info("%s\n", __func__);

	for (i = 0; i < RDMA_NUM; i++) {
			info->rdma_ins[i].rdma_table_size = 0;
			info->rdma_ins[i].rdma_regadr = &rdma_regadr[i];
			info->rdma_ins[i].keep_buf = 1;
			/*do not change it in normal case*/
			info->rdma_ins[i].used = 0;
	}

#if 1
	info->rdma_ins[3].used = 1; /* OSD driver uses this channel */
#endif
	if (int_rdma  == -ENXIO) {
		dev_err(&pdev->dev, "cannot get rdma irq resource\n");
		return -ENODEV;
	}
	if (request_irq(int_rdma, &rdma_mgr_isr,
			IRQF_SHARED, "rdma", (void *)"rdma")) {
		dev_err(&pdev->dev, "can't request irq for rdma\n");
		return -ENODEV;
	}

	data32  = 0;
	data32 |= 0 << 6;
	data32 |= ctrl_ahb_wr_burst_size << 4;
	data32 |= ctrl_ahb_rd_burst_size << 2;
	data32 |= 0 << 1;
	data32 |= 0 << 0;
	WRITE_VCBUS_REG(RDMA_CTRL, data32);

	info->rdma_dev = pdev;
	return 0;

}

/* static int __devexit rdma_remove(struct platform_device *pdev) */
static int rdma_remove(struct platform_device *pdev)
{
	pr_error("RDMA driver removed.\n");

	return 0;
}

static const struct of_device_id rdma_dt_match[] = {
	{
		.compatible = "amlogic, meson, rdma",
	},
	{},
};

static struct platform_driver rdma_driver = {
	.probe = rdma_probe,
	.remove = rdma_remove,
	.driver = {
		.name = "amlogic-rdma",
		.of_match_table = rdma_dt_match,
	},
};

static int __init amrdma_init(void)
{
	int r;

	r = platform_driver_register(&rdma_driver);
	if (r) {
		pr_error("Unable to register rdma driver\n");
		return r;
	}
	pr_info("register rdma platform driver\n");

	return 0;
}

static void __exit amrdma_exit(void)
{

	platform_driver_unregister(&rdma_driver);
}

postcore_initcall(amrdma_init);
module_exit(amrdma_exit);

MODULE_DESCRIPTION("AMLOGIC RDMA management driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Rain Zhang <rain.zhang@amlogic.com>");
