/*
 * drivers/amlogic/vpu/vpu_reg.h
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

#ifndef __VPU_REG_H__
#define __VPU_REG_H__
#include <linux/amlogic/iomap.h>
#include "vpu.h"

extern void __iomem *reg_base_aobus;
extern void __iomem *reg_base_cbus;

/* ********************************
 * register define
 * ********************************* */
/* base & offset */
#define REG_BASE_AOBUS                  (0xc8100000L)
#define REG_BASE_CBUS                   (0xc1100000L)
#define REG_BASE_HIU                    (0xc883c000L)
#define REG_BASE_RST                    (0xc1104400L)
#define REG_OFFSET_AOBUS(reg)           ((reg))
#define REG_OFFSET_CBUS(reg)            ((reg << 2))
#define REG_OFFSET_HIU(reg)             ((reg << 2))
#define REG_OFFSET_RST(reg)             ((reg << 2))

/* offset address */
#define AO_RTI_GEN_PWR_SLEEP0           ((0x00 << 10) | (0x3a << 2))

/* M8M2 register */
#define HHI_GP_PLL_CNTL                 0x1010
/* G9TV register */
#define HHI_GP1_PLL_CNTL                0x1016
#define HHI_GP1_PLL_CNTL2               0x1017
#define HHI_GP1_PLL_CNTL3               0x1018
#define HHI_GP1_PLL_CNTL4               0x1019

#define HHI_MEM_PD_REG0                 0x1040
#define HHI_VPU_MEM_PD_REG0             0x1041
#define HHI_VPU_MEM_PD_REG1             0x1042
/* GX register */
#define HHI_MEM_PD_REG0_GX              0x40
#define HHI_VPU_MEM_PD_REG0_GX          0x41
#define HHI_VPU_MEM_PD_REG1_GX          0x42

#define HHI_VPU_CLK_CNTL                0x106f
/* GX register */
#define HHI_VPU_CLK_CNTL_GX             0x6f
#define HHI_VPU_CLKB_CNTL_GX            0x83
#define HHI_VAPBCLK_CNTL_GX             0x7d

#define RESET0_REGISTER                 0x1101
#define RESET1_REGISTER                 0x1102
#define RESET2_REGISTER                 0x1103
#define RESET3_REGISTER                 0x1104
#define RESET4_REGISTER                 0x1105
#define RESET0_MASK                     0x1110
#define RESET1_MASK                     0x1111
#define RESET2_MASK                     0x1112
#define RESET3_MASK                     0x1113
#define RESET4_MASK                     0x1114
/* GX register */
#define RESET0_REGISTER_GX              0x01
#define RESET1_REGISTER_GX              0x02
#define RESET2_REGISTER_GX              0x03
#define RESET3_REGISTER_GX              0x04
#define RESET4_REGISTER_GX              0x05
#define RESET0_MASK_GX                  0x10
#define RESET1_MASK_GX                  0x11
#define RESET2_MASK_GX                  0x12
#define RESET3_MASK_GX                  0x13
#define RESET4_MASK_GX                  0x14

/* memory mapping */
#define REG_ADDR_AOBUS(reg)             (REG_BASE_AOBUS + REG_OFFSET_AOBUS(reg))
#define REG_ADDR_CBUS(reg)              (REG_BASE_CBUS + REG_OFFSET_CBUS(reg))
#define REG_ADDR_HIU(reg)               (REG_BASE_HIU + REG_OFFSET_HIU(reg))
#define REG_ADDR_RST(reg)               (REG_BASE_RST + REG_OFFSET_RST(reg))

/* ********************************
 * register access api
 * ********************************* */
enum VPU_Chip_e vpu_chip_type;

struct reg_map_s {
	unsigned int base_addr;
	unsigned int size;
	void __iomem *p;
	int flag;
};

static struct reg_map_s vpu_reg_maps[] = {
	{ /* CBUS */
		.base_addr = 0xc1100000,
		.size = 0x10000,
	},
	{ /* HIU */
		.base_addr = 0xc883c000,
		.size = 0x400,
	},
	{ /* AO */
		.base_addr = 0xc8100000,
		.size = 0x100,
	},
};

static inline int vpu_ioremap(void)
{
	int i;
	int ret = 0;

	for (i = 0; i < ARRAY_SIZE(vpu_reg_maps); i++) {
		vpu_reg_maps[i].p = ioremap(vpu_reg_maps[i].base_addr,
					vpu_reg_maps[i].size);
		if (vpu_reg_maps[i].p == NULL) {
			vpu_reg_maps[i].flag = 0;
			pr_info("VPU reg map failed: 0x%x\n",
				vpu_reg_maps[i].base_addr);
			ret = -1;
		} else {
			vpu_reg_maps[i].flag = 1;
			/* pr_info("VPU reg mapped: 0x%x -> %p\n",
				vpu_reg_maps[i].base_addr,
				vpu_reg_maps[i].p); */
		}
	}
	return ret;
}

static inline unsigned int vpu_hiu_read(unsigned int _reg)
{
	void __iomem *p;

	if (vpu_chip_type == VPU_CHIP_GXBB)
		p = vpu_reg_maps[1].p + REG_OFFSET_HIU(_reg);
	else
		p = vpu_reg_maps[0].p + REG_OFFSET_CBUS(_reg);

	return readl(p);
};

static inline void vpu_hiu_write(unsigned int _reg, unsigned int _value)
{
	void __iomem *p;

	if (vpu_chip_type == VPU_CHIP_GXBB)
		p = vpu_reg_maps[1].p + REG_OFFSET_HIU(_reg);
	else
		p = vpu_reg_maps[0].p + REG_OFFSET_CBUS(_reg);

	writel(_value, p);
};

static inline void vpu_hiu_setb(unsigned int _reg,
		unsigned int _value,
		unsigned int _start,
		unsigned int _len)
{
	vpu_hiu_write(_reg, ((vpu_hiu_read(_reg) &
			~(((1L << (_len))-1) << (_start))) |
			(((_value)&((1L<<(_len))-1)) << (_start))));
}

static inline unsigned int vpu_hiu_getb(unsigned int _reg,
		unsigned int _start, unsigned int _len)
{
	return (vpu_hiu_read(_reg) >> (_start)) & ((1L << (_len)) - 1);
}

static inline void vpu_hiu_set_mask(unsigned int _reg, unsigned int _mask)
{
	vpu_hiu_write(_reg, (vpu_hiu_read(_reg) | (_mask)));
}

static inline void vpu_hiu_clr_mask(unsigned int _reg, unsigned int _mask)
{
	vpu_hiu_write(_reg, (vpu_hiu_read(_reg) & (~(_mask))));
}

static inline unsigned int vpu_cbus_read(unsigned int _reg)
{
	void __iomem *p;

	p = vpu_reg_maps[0].p + REG_OFFSET_CBUS(_reg);
	return readl(p);
};

static inline void vpu_cbus_write(unsigned int _reg, unsigned int _value)
{
	void __iomem *p;

	p = vpu_reg_maps[0].p + REG_OFFSET_CBUS(_reg);
	writel(_value, p);
};

static inline void vpu_cbus_setb(unsigned int _reg,
		unsigned int _value,
		unsigned int _start,
		unsigned int _len)
{
	vpu_cbus_write(_reg, ((vpu_cbus_read(_reg) &
			~(((1L << (_len))-1) << (_start))) |
			(((_value)&((1L<<(_len))-1)) << (_start))));
}

static inline unsigned int vpu_cbus_getb(unsigned int _reg,
		unsigned int _start, unsigned int _len)
{
	return (vpu_cbus_read(_reg) >> (_start)) & ((1L << (_len)) - 1);
}

static inline void vpu_cbus_set_mask(unsigned int _reg, unsigned int _mask)
{
	vpu_cbus_write(_reg, (vpu_cbus_read(_reg) | (_mask)));
}

static inline void vpu_cbus_clr_mask(unsigned int _reg, unsigned int _mask)
{
	vpu_cbus_write(_reg, (vpu_cbus_read(_reg) & (~(_mask))));
}

static inline unsigned int vpu_aobus_read(unsigned int _reg)
{
	void __iomem *p;

	p = vpu_reg_maps[2].p + REG_OFFSET_AOBUS(_reg);
	return readl(p);
};

static inline void vpu_aobus_write(unsigned int _reg, unsigned int _value)
{
	void __iomem *p;

	p = vpu_reg_maps[2].p + REG_OFFSET_AOBUS(_reg);
	writel(_value, p);
};

static inline void vpu_aobus_setb(unsigned int _reg,
		unsigned int _value,
		unsigned int _start,
		unsigned int _len)
{
	vpu_aobus_write(_reg, ((vpu_aobus_read(_reg) &
			~(((1L << (_len))-1) << (_start))) |
			(((_value)&((1L<<(_len))-1)) << (_start))));
}

static inline unsigned int vpu_aobus_getb(unsigned int _reg,
		unsigned int _start, unsigned int _len)
{
	return (vpu_aobus_read(_reg) >> (_start)) & ((1L << (_len)) - 1);
}

static inline void vpu_aobus_set_mask(unsigned int _reg, unsigned int _mask)
{
	vpu_aobus_write(_reg, (vpu_aobus_read(_reg) | (_mask)));
}

static inline void vpu_aobus_clr_mask(unsigned int _reg, unsigned int _mask)
{
	vpu_aobus_write(_reg, (vpu_aobus_read(_reg) & (~(_mask))));
}

#endif
