/*
 * drivers/amlogic/tvin/vdin/vdin_drv.h
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



#ifndef __TVIN_VDIN_DRV_H
#define __TVIN_VDIN_DRV_H

/* Standard Linux Headers */
#include <linux/cdev.h>
#include <linux/spinlock.h>
#include <linux/irqreturn.h>
#include <linux/timer.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/time.h>
#include <linux/device.h>
#include <linux/clk.h>

/* Amlogic Headers */
#include <linux/amlogic/cpu_version.h>
#include <linux/amlogic/canvas/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_receiver.h>
#include <linux/amlogic/amports/vframe_provider.h>
#include <linux/amlogic/tvin/tvin_v4l2.h>
#ifdef CONFIG_AML_RDMA
#include <linux/amlogic/rdma/rdma_mgr.h>
#endif

/* Local Headers */
#include "../tvin_global.h"
#include "../tvin_frontend.h"
#include "vdin_vf.h"
#include "vdin_regs.h"

#define VDIN_VER "Ref.2015/06/30a"

/*the counter of vdin*/
#define VDIN_MAX_DEVS			2
#define VDIN_CRYSTAL                    24000000
/* values of vdin_dev_t.flags */
#define VDIN_FLAG_NULL			0x00000000
#define VDIN_FLAG_DEC_INIT		0x00000001
#define VDIN_FLAG_DEC_STARTED		0x00000002
#define VDIN_FLAG_DEC_OPENED		0x00000004
#define VDIN_FLAG_DEC_REGED		0x00000008
#define VDIN_FLAG_DEC_STOP_ISR		0x00000010
#define VDIN_FLAG_FORCE_UNSTABLE	0x00000020
#define VDIN_FLAG_FS_OPENED		0x00000100
#define VDIN_FLAG_SKIP_ISR              0x00000200
/*flag for vdin0 output*/
#define VDIN_FLAG_OUTPUT_TO_NR		0x00000400
/*flag for force vdin buffer recycle*/
#define VDIN_FLAG_FORCE_RECYCLE         0x00000800
/*flag for vdin scale down,color fmt convertion*/
#define VDIN_FLAG_MANUAL_CONVERTION     0x00001000
/*flag for vdin rdma enable*/
#define VDIN_FLAG_RDMA_ENABLE           0x00002000
/*values of vdin isr bypass check flag */
#define VDIN_BYPASS_STOP_CHECK          0x00000001
#define VDIN_BYPASS_CYC_CHECK           0x00000002
#define VDIN_BYPASS_VSYNC_CHECK         0x00000004
#define VDIN_BYPASS_VGA_CHECK           0x00000008


/*flag for flush vdin buff*/
#define VDIN_FLAG_BLACK_SCREEN_ON	1
#define VDIN_FLAG_BLACK_SCREEN_OFF	0
/* size for rdma table */
#define RDMA_TABLE_SIZE (PAGE_SIZE>>3)
/* #define VDIN_DEBUG */

static inline const char *vdin_fmt_convert_str(
		enum vdin_format_convert_e fmt_cvt)
{
	switch (fmt_cvt) {
	case VDIN_FORMAT_CONVERT_YUV_YUV422:
		return "FMT_CONVERT_YUV_YUV422";
		break;
	case VDIN_FORMAT_CONVERT_YUV_YUV444:
		return "FMT_CONVERT_YUV_YUV444";
		break;
	case VDIN_FORMAT_CONVERT_YUV_RGB:
		return "FMT_CONVERT_YUV_RGB";
		break;
	case VDIN_FORMAT_CONVERT_RGB_YUV422:
		return "FMT_CONVERT_RGB_YUV422";
		break;
	case VDIN_FORMAT_CONVERT_RGB_YUV444:
		return "FMT_CONVERT_RGB_YUV444";
		break;
	case VDIN_FORMAT_CONVERT_RGB_RGB:
		return "FMT_CONVERT_RGB_RGB";
		break;
	case VDIN_FORMAT_CONVERT_YUV_NV12:
		return "VDIN_FORMAT_CONVERT_YUV_NV12";
		break;
	case VDIN_FORMAT_CONVERT_YUV_NV21:
		return "VDIN_FORMAT_CONVERT_YUV_NV21";
		break;
	case VDIN_FORMAT_CONVERT_RGB_NV12:
		return "VDIN_FORMAT_CONVERT_RGB_NV12";
		break;
	case VDIN_FORMAT_CONVERT_RGB_NV21:
		return "VDIN_FORMAT_CONVERT_RGB_NV21";
		break;
	default:
		return "FMT_CONVERT_NULL";
		break;
	}
}

/*******for debug **********/
struct vdin_debug_s {
	struct tvin_cutwin_s			cutwin;
	unsigned short                  scaler4h;/* for vscaler */
	unsigned short                  scaler4w;/* for hscaler */
	unsigned short                  dest_cfmt;/* for color fmt convertion */
};
struct vdin_dev_s {
	unsigned int			index;

	dev_t					devt;
	struct cdev				cdev;
	struct device			*dev;

	char					name[15];
	/* bit0 TVIN_PARM_FLAG_CAP bit31: TVIN_PARM_FLAG_WORK_ON */
	unsigned int			flags;


	unsigned int			mem_start;
	unsigned int			mem_size;

	/* start address of captured frame data [8 bits] in memory */
	/* for Component input, frame data [8 bits] order is
	 * Y0Cb0Y1Cr0��Y2nCb2nY2n+1Cr2n�� */
	/* for VGA       input, frame data [8 bits] order is
	 * R0G0B0��RnGnBn�� */
	unsigned int			cap_addr;
	unsigned int			cap_size;

	unsigned int			h_active;
	unsigned int			v_active;
	enum vdin_format_convert_e	format_convert;

	enum vframe_source_type_e	source_type;
	enum vframe_source_mode_e	source_mode;

	unsigned int			*canvas_ids;
	unsigned int			canvas_h;
	unsigned int			canvas_w;
	unsigned int			canvas_max_size;
	unsigned int			canvas_max_num;
	struct vf_entry			*curr_wr_vfe;
	struct vf_entry			*last_wr_vfe;
	unsigned int			curr_field_type;

	unsigned int			irq;
	unsigned int			rdma_irq;
	char					irq_name[12];
	/* address offset(vdin0/vdin1/...) */
	unsigned int			addr_offset;
	unsigned int			vga_clr_cnt;
	unsigned int			vs_cnt_valid;
	unsigned int			vs_cnt_ignore;
	struct tvin_parm_s		parm;
	struct tvin_format_s	*fmt_info_p;
	struct vf_pool			*vfp;

	struct tvin_frontend_s		*frontend;
	struct tvin_sig_property_s	pre_prop;
	struct tvin_sig_property_s	prop;
	struct vframe_provider_s	vprov;
	 /* 0:from gpio A,1:from csi2 , 2:gpio B*/
	enum bt_path_e              bt_path;



	struct timer_list		timer;
	spinlock_t				dec_lock;
	struct tasklet_struct	isr_tasklet;
	spinlock_t				isr_lock;
	struct mutex			mm_lock; /* lock for mmap */
	struct mutex			fe_lock;

	unsigned int			unstable_flag;
	unsigned int			dec_enable;
	unsigned int			abnormal_cnt;
	/* bool  stamp_valid; use vfe replace tell the first frame */
	unsigned int			stamp;
	unsigned int			hcnt64;
	unsigned int			cycle;
	unsigned int			hcnt64_tag;
	unsigned int			cycle_tag;
	unsigned int			start_time;/* ms vdin start time */
		 int			    rdma_handle;
	struct clk				*msr_clk;
	struct vdin_debug_s			debug;
};



#ifdef CONFIG_TVIN_VDIN_CTRL
int vdin_ctrl_open_fe(int no, int port);
int vdin_ctrl_close_fe(int no);
int vdin_ctrl_start_fe(int no, struct vdin_parm_s *para);
int vdin_ctrl_stop_fe(int no);
enum tvin_sig_fmt_e vdin_ctrl_get_fmt(int no);
#endif

extern int start_tvin_service(int no, struct vdin_parm_s *para);
extern int stop_tvin_service(int no);
extern int vdin_reg_v4l2(struct vdin_v4l2_ops_s *v4l2_ops);
extern void vdin_unreg_v4l2(void);

extern int vdin_create_class_files(struct class *vdin_clsp);
extern void vdin_remove_class_files(struct class *vdin_clsp);
extern int vdin_create_device_files(struct device *dev);
extern void vdin_remove_device_files(struct device *dev);
extern int vdin_open_fe(enum tvin_port_e port, int index,
		struct vdin_dev_s *devp);
extern void vdin_close_fe(struct vdin_dev_s *devp);
extern void vdin_start_dec(struct vdin_dev_s *devp);
extern void vdin_stop_dec(struct vdin_dev_s *devp);
extern irqreturn_t vdin_isr_simple(int irq, void *dev_id);
extern irqreturn_t vdin_isr(int irq, void *dev_id);
extern irqreturn_t vdin_v4l2_isr(int irq, void *dev_id);

#endif /* __TVIN_VDIN_DRV_H */

