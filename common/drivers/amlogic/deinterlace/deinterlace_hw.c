#include <linux/string.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/platform_device.h>
#include <linux/module.h>

#include <linux/amlogic/vpu.h>
#include <linux/amlogic/cpu_version.h>
#include <linux/amlogic/canvas/canvas.h>
#include <linux/amlogic/amports/vframe.h>
#include <linux/amlogic/amports/vframe_provider.h>
/* #include <linux/iw7023.h> */
#include "deinterlace.h"
#include "register.h"
#ifdef DET3D
#include "detect3d.h"
#endif

#ifndef DI_CHAN2_CANVAS
#define DI_CHAN2_CANVAS DI_CHAN2_CANVAS0
#endif
#ifndef DI_CHAN2_LUMA_X
#define DI_CHAN2_LUMA_X DI_CHAN2_LUMA_X0
#endif
#ifndef DI_CHAN2_LUMA_Y
#define DI_CHAN2_LUMA_Y  DI_CHAN2_LUMA_Y0
#endif
#ifndef DI_CHAN2_LUMA_RPT_PAT
#define DI_CHAN2_LUMA_RPT_PAT DI_CHAN2_LUMA0_RPT_PAT
#endif

uint di_mtn_1_ctrl1;
uint mtn_ctrl1;

static bool cue_enable;

pd_detect_threshold_t win_pd_th[MAX_WIN_NUM];
pd_win_prop_t pd_win_prop[MAX_WIN_NUM];
extern int mpeg2vdin_flag;

static bool frame_dynamic;
MODULE_PARM_DESC(frame_dynamic, "\n frame_dynamic\n");
module_param(frame_dynamic, bool, 0664);

static bool frame_dynamic_dbg;
MODULE_PARM_DESC(frame_dynamic_dbg, "\n frame_dynamic_dbg\n");
module_param(frame_dynamic_dbg, bool, 0664);

static int frame_dynamic_level;
MODULE_PARM_DESC(frame_dynamic_level, "\n frame_dynamic_level\n");
module_param(frame_dynamic_level, int, 0664);

MODULE_PARM_DESC(cue_enable, "\n cue_enable\n");
module_param(cue_enable, bool, 0664);
#ifdef MCDI_SUPPORT
static unsigned short mcen_mode = 1;
MODULE_PARM_DESC(mcen_mode, "\n blend mc enable\n");
module_param(mcen_mode, ushort, 0664);
#endif

#ifdef DET3D
static unsigned int det3d_cfg;
#endif

static int vdin_en;

static void set_di_inp_fmt_more(
		int hfmt_en,
		int hz_yc_ratio,	/* 2bit */
		int hz_ini_phase,	/* 4bit */
		int vfmt_en,
		int vt_yc_ratio,	/* 2bit */
		int vt_ini_phase,	/* 4bit */
		int y_length,
		int c_length,
		int hz_rpt		/* 1bit */
	);

static void set_di_inp_mif(DI_MIF_t  *mif, int urgent, int hold_line);

static void set_di_mem_fmt_more(
		int hfmt_en,
		int hz_yc_ratio,	/* 2bit */
		int hz_ini_phase,	/* 4bit */
		int vfmt_en,
		int vt_yc_ratio,	/* 2bit */
		int vt_ini_phase,	/* 4bit */
		int y_length,
		int c_length,
		int hz_rpt	/* 1bit */
	);

static void set_di_mem_mif(DI_MIF_t *mif, int urgent, int hold_line);

static void set_di_if1_fmt_more(
		int hfmt_en,
		int hz_yc_ratio,	/* 2bit */
		int hz_ini_phase,	/* 4bit */
		int vfmt_en,
		int vt_yc_ratio,	/* 2bit */
		int vt_ini_phase,	/* 4bit */
		int y_length,
		int c_length,
		int hz_rpt		/* 1bit */
	);

static void set_di_if1_mif(DI_MIF_t *mif, int urgent, int hold_line);

static void set_di_chan2_mif(DI_MIF_t *mif, int urgent, int hold_line);

static void set_di_if0_mif(DI_MIF_t *mif, int urgent, int hold_line);

static void di_nr_init(void);
#if (defined NEW_DI_V2 && !defined NEW_DI_TV)
static void ma_di_init(void)
{
	/* 420->422 chrome difference is large motion is large,flick */
	Wr(DI_MTN_1_CTRL4, 0x01800880);
	Wr(DI_MTN_1_CTRL7, 0x0a800480);
	/* ei setting */
	Wr(DI_EI_CTRL0, 0x00ff0100);
	Wr(DI_EI_CTRL1, 0x5a0a0f2d);
	Wr(DI_EI_CTRL2, 0x050a0a5d);
	Wr(DI_EI_CTRL3, 0x80000013);
	/* mtn setting */
	Wr(DI_MTN_1_CTRL1, 0xa0202015);
	#if 0
	/* no use from g9tv */
	Wr(DI_MTN_CTRL, 0xe228c440);
	Wr(DI_BLEND_CTRL1, 0xc4402840);
	Wr(DI_BLEND_CTRL2, 0x430);
	#endif
}
#endif
#ifdef MCDI_SUPPORT
static void mc_di_param_init(void)
{
	Wr(MCDI_CHK_EDGE_GAIN_OFFST, 0x4f6124);
	Wr(MCDI_LMV_RT, 0x7455);
	Wr(MCDI_LMV_GAINTHD, 0x6014d409);
	Wr(MCDI_REL_DET_LPF_MSK_22_30, 0x0a010001);
	Wr(MCDI_REL_DET_LPF_MSK_31_34, 0x01010101);
}
#endif
void di_hw_init(void)
{
#ifdef NEW_DI_V1
	unsigned short fifo_size = 0xc0;
#endif

#ifdef NEW_DI_V1
	/* enable old DI mode for m6tv */
	Wr(DI_CLKG_CTRL, 0x1); /* di no clock gate */

	/* fifo size setting from 0x1be60 to 0x1bf20 */
	Wr(VD1_IF0_LUMA_FIFO_SIZE,	fifo_size);
	/* 1a63 is vd1_if0_luma_fifo_size */
	Wr(VD2_IF0_LUMA_FIFO_SIZE,	fifo_size);
	/* 1a83 is vd2_if0_luma_fifo_size */
	Wr(DI_INP_LUMA_FIFO_SIZE,	fifo_size);
	/* 17d8 is DI_INP_luma_fifo_size */
	Wr(DI_MEM_LUMA_FIFO_SIZE,	fifo_size);
	/* 17e5 is DI_MEM_luma_fifo_size */
	Wr(DI_IF1_LUMA_FIFO_SIZE,	fifo_size);
	/* 17f2 is  DI_IF1_luma_fifo_size */
	Wr(DI_CHAN2_LUMA_FIFO_SIZE, fifo_size);
	/* 17b3 is DI_chan2_luma_fifo_size */
#endif
	Wr(DI_PRE_HOLD, (0 << 31) | (31 << 16) | 31);

#if 0
	/* need not set DI_CLKG_CTRL, hardware default
	value of this register is already 0 */
	/* Wr_reg_bits(DI_CLKG_CTRL, 0x0, 0, 2);
	// bit 0: 1, no clock; bit 1: 0, auto clock gate */
#endif
	/* nr default setting */
	di_nr_init();
#if (defined NEW_DI_V2 && !defined NEW_DI_TV)
	ma_di_init();
#endif
#ifdef MCDI_SUPPORT
	mc_di_param_init();
#endif
	Wr(DI_CLKG_CTRL, 0x2); /* di clock gate all */
}

void di_hw_uninit(void)
{
}

unsigned int nr2_en = 0x1;
module_param(nr2_en, uint, 0644);
MODULE_PARM_DESC(nr2_en, "\n nr2_en\n");

void enable_di_pre_aml(
	DI_MIF_t		   *di_inp_mif,
	DI_MIF_t		   *di_mem_mif,
	DI_MIF_t		   *di_chan2_mif,
	DI_SIM_MIF_t    *di_nrwr_mif,
	DI_SIM_MIF_t    *di_mtnwr_mif,
#ifdef NEW_DI_V1
	DI_SIM_MIF_t    *di_contp2rd_mif,
	DI_SIM_MIF_t    *di_contprd_mif,
	DI_SIM_MIF_t    *di_contwr_mif,
#endif
	int nr_en, int mtn_en, int pd32_check_en, int pd22_check_en,
	int hist_check_en, int pre_field_num, int pre_vdin_link,
	int hold_line, int urgent)
{
	int hist_check_only = 0;
#ifdef NEW_DI_V1
	int nr_w = 0, nr_h = 0;
#endif
	pd32_check_en = 1; /* for progressive luma detection */

	hist_check_only = hist_check_en && !nr_en && !mtn_en &&
			!pd22_check_en && !pd32_check_en;

	if (nr_en | mtn_en | pd22_check_en || pd32_check_en) {
		set_di_mem_mif(di_mem_mif, urgent, hold_line);
		if (!vdin_en)
			set_di_inp_mif(di_inp_mif, urgent, hold_line);
	}

	if (pd22_check_en || hist_check_only) {
		set_di_chan2_mif(di_chan2_mif, urgent, hold_line);
		#ifdef NEW_DI_V1
		RDMA_WR_BITS(DI_NR_CTRL0, cue_enable, 26, 1);
		#endif
	} else {
		RDMA_WR_BITS(DI_NR_CTRL0, 0, 26, 1);
	}

	/* set nr wr mif interface. */
	if (nr_en) {
		RDMA_WR(DI_NRWR_X, (di_nrwr_mif->start_x << 16)|
			(di_nrwr_mif->end_x));
		RDMA_WR(DI_NRWR_Y, (di_nrwr_mif->start_y << 16)|
						3 << 30|/*nrwr_words_lim*/
					(di_nrwr_mif->end_y));
		RDMA_WR(DI_NRWR_CTRL, di_nrwr_mif->canvas_num|
			(urgent<<16)|
			2<<26 |/*burst_lim 1->2 2->4*/
			1<<30); /* urgent bit 16 */

	}

	/* motion wr mif. */
	if (mtn_en)	{
#ifdef NEW_DI_V1
		RDMA_WR(DI_CONTWR_X, (di_contwr_mif->start_x << 16)|
			(di_contwr_mif->end_x));
		RDMA_WR(DI_CONTWR_Y, (di_contwr_mif->start_y << 16)|
			(di_contwr_mif->end_y));
		RDMA_WR(DI_CONTWR_CTRL, di_contwr_mif->canvas_num|
			(urgent << 8));/* urgent. */
		RDMA_WR(DI_CONTPRD_X, (di_contprd_mif->start_x << 16)|
			(di_contprd_mif->end_x));
		RDMA_WR(DI_CONTPRD_Y, (di_contprd_mif->start_y << 16)|
			(di_contprd_mif->end_y));
		RDMA_WR(DI_CONTP2RD_X, (di_contp2rd_mif->start_x << 16)|
			(di_contp2rd_mif->end_x));
		RDMA_WR(DI_CONTP2RD_Y, (di_contp2rd_mif->start_y << 16)|
			(di_contp2rd_mif->end_y));
		RDMA_WR(DI_CONTRD_CTRL, (di_contprd_mif->canvas_num << 8)|
						  (urgent << 16)|/* urgent */
						   di_contp2rd_mif->canvas_num);
		/* current field mtn canvas index. */
#endif
		RDMA_WR(DI_MTNWR_X, (di_mtnwr_mif->start_x << 16)|
			(di_mtnwr_mif->end_x));
		RDMA_WR(DI_MTNWR_Y, (di_mtnwr_mif->start_y << 16)|
			(di_mtnwr_mif->end_y));
		RDMA_WR(DI_MTNWR_CTRL, di_mtnwr_mif->canvas_num|
						(urgent << 8));	/* urgent. */
		RDMA_WR(DI_MTN_CTRL1, (0 << 8) | 2);
	}

#ifdef NEW_DI_V1
	nr_w = (di_nrwr_mif->end_x - di_nrwr_mif->start_x + 1);
	nr_h = (di_nrwr_mif->end_y - di_nrwr_mif->start_y + 1);
	RDMA_WR(NR2_FRM_SIZE, (nr_h<<16)|nr_w);
	/*gate for nr*/
	#ifdef NEW_DI_TV
	RDMA_WR_BITS(NR2_SW_EN, nr2_en, 4, 1);
	#else
	/*only process sd,avoid affecting sharp*/
	if ((nr_h<<1) >= 720 || nr_w >= 1280)
		RDMA_WR_BITS(NR2_SW_EN, 0, 4, 1);
	else
		RDMA_WR_BITS(NR2_SW_EN, nr2_en, 4, 1);
	#endif
	/*enable noise meter*/
	RDMA_WR_BITS(NR2_SW_EN, 1, 17, 1);
#endif
	/* reset pre */
	RDMA_WR(DI_PRE_CTRL, Rd(DI_PRE_CTRL) | 1 << 31);
	/* frame reset for the pre modules. */

	RDMA_WR(DI_PRE_CTRL, nr_en |			/* NR enable */
					(mtn_en << 1) |	/* MTN_EN */
			(pd32_check_en << 2)|/* check 3:2 pulldown */
			(pd22_check_en << 3)|/* check 2:2 pulldown */
					(1 << 4) |
/* 2:2 check mid pixel come from next field after MTN. */
			(hist_check_en << 5)|/* hist check enable */
			(1 << 6)|/* hist check  use chan2. */
					((!nr_en) << 7)|
/* hist check use data before noise reduction. */
	((pd22_check_en || hist_check_only) << 8)|
	/* chan 2 enable for 2:2 pull down check.*/
		(pd22_check_en << 9) |/* line buffer 2 enable */
					(0 << 10) |	/* pre drop first. */
					(0 << 11) | /* di pre repeat */
					(0 << 12) | /* pre viu link */
			(pre_vdin_link << 13) |
			(pre_vdin_link << 14) |/* pre go line link */
			(hold_line << 16)|/* pre hold line number */
					(1 << 22)|/* MTN after NR. */
			(pre_field_num << 29)|/* pre field number.*/
			(0x1 << 30) /* pre soft rst, pre frame rst */
				   );

#ifdef SUPPORT_MPEG_TO_VDIN
	if (mpeg2vdin_flag)
		RDMA_WR_BITS(DI_PRE_CTRL, 1, 13, 1);
#endif
#ifdef DET3D
	if (det3d_en && (!det3d_cfg)) {
		det3d_enable(1);
		det3d_cfg = 1;
	} else if ((!det3d_en) && det3d_cfg) {
		det3d_enable(0);
	det3d_cfg = 0;
	}
#endif
}
void enable_afbc_input(struct vframe_s *vf)
{
	unsigned int r, u, v;
	if (vf->type & VIDTYPE_COMPRESS) {
		r = (3 << 24) |
		    (17 << 16) |
		    (1 << 14) | /*burst1 1*/
		    vf->bitdepth;

		if ((vf->type & VIDTYPE_TYPEMASK) == VIDTYPE_INTERLACE_TOP)
			r |= 0x44;
		else if ((vf->type & VIDTYPE_TYPEMASK) ==
					VIDTYPE_INTERLACE_BOTTOM)
			r |= 0x88;

		RDMA_WR(AFBC_MODE, r);
		RDMA_WR(AFBC_ENABLE, 0x1700);
		RDMA_WR(AFBC_CONV_CTRL, 0x100);
		u = (vf->bitdepth >> (BITDEPTH_U_SHIFT)) & 0x3;
		v = (vf->bitdepth >> (BITDEPTH_V_SHIFT)) & 0x3;
		RDMA_WR(AFBC_DEC_DEF_COLOR,
			0x3FF00000 | /*Y,bit20+*/
			0x80 << (u + 10) |
			0x80 << v);
		/* chroma formatter */
		RDMA_WR(AFBC_VD_CFMT_CTRL,
			(1<<21)|/* HFORMATTER_YC_RATIO_2_1 */
			(1<<20)|/* HFORMATTER_EN */
			(1<<16)|/* VFORMATTER_RPTLINE0_EN */
			(0x8 << 1)|/* VFORMATTER_PHASE_BIT */
			1);/* VFORMATTER_EN */

		RDMA_WR(AFBC_VD_CFMT_W,
			(vf->width << 16) | (vf->width/2));

		RDMA_WR(AFBC_MIF_HOR_SCOPE,
			(0 << 16) | ((vf->width - 1)>>5));

		RDMA_WR(AFBC_PIXEL_HOR_SCOPE,
			(0 << 16) | (vf->width - 1));
		RDMA_WR(AFBC_VD_CFMT_H, vf->height);

		RDMA_WR(AFBC_MIF_VER_SCOPE,
		    (0 << 16) | ((vf->height-1)>>2));

		RDMA_WR(AFBC_PIXEL_VER_SCOPE,
			0 << 16 | (vf->height-1));
		RDMA_WR(AFBC_SIZE_IN, vf->height | vf->width << 16);
		RDMA_WR(AFBC_HEAD_BADDR, vf->canvas0Addr>>4);
		RDMA_WR(AFBC_BODY_BADDR, vf->canvas1Addr>>4);
		/* disable inp memory */
		RDMA_WR_BITS(DI_INP_GEN_REG, 0, 0, 1);
		/* afbc to di enable */
		RDMA_WR_BITS(VIU_MISC_CTRL0, 1, 19, 1);
		/* DI inp(current data) switch to AFBC */
		RDMA_WR_BITS(VIUB_MISC_CTRL0, 1, 16, 1);
	} else {
		RDMA_WR(AFBC_ENABLE, 0);
		/* afbc to vpp(replace vd1) enable */
		RDMA_WR_BITS(VIU_MISC_CTRL0, 0, 19, 1);
		/* DI inp(current data) switch to memory */
		RDMA_WR_BITS(VIUB_MISC_CTRL0, 0, 16, 1);
	}
}
#ifdef MCDI_SUPPORT
void enable_mc_di_pre(DI_MC_MIF_t *di_mcinford_mif,
DI_MC_MIF_t *di_mcinfowr_mif, DI_MC_MIF_t *di_mcvecwr_mif, int urgent)
{
	RDMA_WR(MCDI_MCVECWR_X, di_mcvecwr_mif->size_x);
	RDMA_WR(MCDI_MCVECWR_Y, di_mcvecwr_mif->size_y);
	RDMA_WR(MCDI_MCINFOWR_X, di_mcinfowr_mif->size_x);
	RDMA_WR(MCDI_MCINFOWR_Y, di_mcinfowr_mif->size_y);

	RDMA_WR(MCDI_MCINFORD_X, di_mcinford_mif->size_x);
	RDMA_WR(MCDI_MCINFORD_Y, di_mcinford_mif->size_y);
	RDMA_WR(MCDI_MCVECWR_CANVAS_SIZE,
		(di_mcvecwr_mif->size_x<<16)+di_mcvecwr_mif->size_y);
	RDMA_WR(MCDI_MCINFOWR_CANVAS_SIZE,
		(di_mcinfowr_mif->size_x<<16)+di_mcinfowr_mif->size_y);
	RDMA_WR(MCDI_MCINFORD_CANVAS_SIZE,
		(di_mcinford_mif->size_x<<16)+di_mcinford_mif->size_y);

	/* Wr(MCDI_MOTINEN,1<<1);	//enable motin refinement */

	RDMA_WR(MCDI_MCVECWR_CTRL, di_mcvecwr_mif->canvas_num |
			(0<<14) |	 /* sync latch en */
			(urgent<<8) |   /* urgent */
			(1<<12) |	 /* enable reset by frame rst */
			(0x4031<<16));
	RDMA_WR(MCDI_MCINFOWR_CTRL, di_mcinfowr_mif->canvas_num |
			(0<<14) |	 /* sync latch en */
			(urgent<<8) |	 /* urgent */
			(1<<12) |	 /* enable reset by frame rst */
			(0x4042<<16));
	RDMA_WR(MCDI_MCINFORD_CTRL, di_mcinford_mif->canvas_num |
			(0<<10) |	 /* sync latch en */
			(urgent<<8) |   /* urgent */
			(1<<9)  |		/* enable reset by frame rst */
			(0x42<<16));
}

void enable_mc_di_post(DI_MC_MIF_t *di_mcvecrd_mif, int urgent, bool reverse)
{
	VSYNC_WR_MPEG_REG(MCDI_MCVECRD_X, (reverse?1:0)<<30 |
			di_mcvecrd_mif->start_x<<16 |
			(di_mcvecrd_mif->size_x+di_mcvecrd_mif->start_x));
	VSYNC_WR_MPEG_REG(MCDI_MCVECRD_Y, (reverse?1:0)<<30 |
						di_mcvecrd_mif->start_y<<16 |
			(di_mcvecrd_mif->size_y+di_mcvecrd_mif->start_y));
	VSYNC_WR_MPEG_REG(MCDI_MCVECRD_CANVAS_SIZE,
			(di_mcvecrd_mif->size_x<<16)+di_mcvecrd_mif->size_y);
	VSYNC_WR_MPEG_REG(MCDI_MCVECRD_CTRL, di_mcvecrd_mif->canvas_num |
						(urgent<<8)|/* urgent */
						(1<<9)|/* canvas enable */
						(0 << 10) |
						(0x31<<16));
	if (di_mcvecrd_mif->blend_mode == 3)
		VSYNC_WR_MPEG_REG_BITS(MCDI_MC_CRTL, mcen_mode, 0, 2);
	else
		VSYNC_WR_MPEG_REG_BITS(MCDI_MC_CRTL, 0, 0, 2);
}

#endif

static void set_di_inp_fmt_more(int hfmt_en,
				int hz_yc_ratio,		/* 2bit */
				int hz_ini_phase,		/* 4bit */
				int vfmt_en,
				int vt_yc_ratio,		/* 2bit */
				int vt_ini_phase,		/* 4bit */
				int y_length,
				int c_length,
				int hz_rpt	/* 1bit */
		)
{
	int repeat_l0_en = 1, nrpt_phase0_en = 0;
	int vt_phase_step = (16 >> vt_yc_ratio);

	RDMA_WR(DI_INP_FMT_CTRL, (hz_rpt << 28)	|/* hz rpt pixel */
				  (hz_ini_phase << 24)	|/* hz ini phase */
				  (0 << 23)	|/* repeat p0 enable */
				  (hz_yc_ratio << 21)	|/* hz yc ratio */
				  (hfmt_en << 20)		|/* hz enable */
				 (nrpt_phase0_en << 17)|/* nrpt_phase0 enable */
				  (repeat_l0_en << 16)|/* repeat l0 enable */
				  (0 << 12)		|/* skip line num */
				  (vt_ini_phase << 8)	|/* vt ini phase */
				  (vt_phase_step << 1)|/* vt phase step (3.4) */
				  (vfmt_en << 0)		/* vt enable */
					);

	RDMA_WR(DI_INP_FMT_W, (y_length << 16) |	/* hz format width */
					 (c_length << 0)/* vt format width */
					);
}

static void set_di_inp_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
	unsigned int bytes_per_pixel;
	unsigned int demux_mode;
	unsigned int chro_rpt_lastl_ctrl;
	unsigned int luma0_rpt_loop_start;
	unsigned int luma0_rpt_loop_end;
	unsigned int luma0_rpt_loop_pat;
	unsigned int chroma0_rpt_loop_start;
	unsigned int chroma0_rpt_loop_end;
	unsigned int chroma0_rpt_loop_pat;
	unsigned int vt_ini_phase = 0;
	unsigned int reset_on_gofield;
	if (mif->set_separate_en != 0 && mif->src_field_mode == 1) {
		chro_rpt_lastl_ctrl = 1;
		luma0_rpt_loop_start = 1;
		luma0_rpt_loop_end = 1;
		chroma0_rpt_loop_start = 1;
		chroma0_rpt_loop_end = 1;
		luma0_rpt_loop_pat = 0x80;
		chroma0_rpt_loop_pat = 0x80;

		if (mif->output_field_num == 0)
			vt_ini_phase = 0xe;
		else
			vt_ini_phase = 0xa;
	} else if (mif->set_separate_en != 0 && mif->src_field_mode == 0) {
		chro_rpt_lastl_ctrl = 1;
		luma0_rpt_loop_start = 0;
		luma0_rpt_loop_end = 0;
		chroma0_rpt_loop_start = 0;
		chroma0_rpt_loop_end = 0;
		luma0_rpt_loop_pat = 0x0;
		chroma0_rpt_loop_pat = 0x0;
	} else if (mif->set_separate_en == 0 && mif->src_field_mode == 1) {
		chro_rpt_lastl_ctrl = 1;
		luma0_rpt_loop_start = 1;
		luma0_rpt_loop_end = 1;
		chroma0_rpt_loop_start = 1;
		chroma0_rpt_loop_end = 1;
		luma0_rpt_loop_pat = 0x80;
		chroma0_rpt_loop_pat = 0x80;
	} else {
		chro_rpt_lastl_ctrl = 0;
		luma0_rpt_loop_start = 0;
		luma0_rpt_loop_end = 0;
		chroma0_rpt_loop_start = 0;
		chroma0_rpt_loop_end = 0;
		luma0_rpt_loop_pat = 0x00;
		chroma0_rpt_loop_pat = 0x00;
	}


	bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
	demux_mode = mif->video_mode;


	/* ---------------------- */
	/* General register */
	/* ---------------------- */
	reset_on_gofield = (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXBB)?0:1;
	RDMA_WR(DI_INP_GEN_REG, (reset_on_gofield << 29) |
				(urgent << 28)	|/* chroma urgent bit */
				(urgent << 27)	|/* luma urgent bit. */
				(1 << 25)		|/* no dummy data. */
				(hold_line << 19)	|/* hold lines */
				(1 << 18)	|/* push dummy pixel */
				(demux_mode << 16)	|/* demux_mode */
				(bytes_per_pixel << 14)		|
				(1 << 12)	|/*burst_size_cr*/
				(1 << 10)	|/*burst_size_cb*/
				(3 << 8)	|/*burst_size_y*/
				(chro_rpt_lastl_ctrl << 6)	|
				((mif->set_separate_en != 0) << 1) |
				(1 << 0)/* cntl_enable */
	  );
	if (mif->set_separate_en == 2) {
		/* Enable NV12 Display */
		RDMA_WR_BITS(DI_INP_GEN_REG2, 1, 0, 1);
	} else {
		RDMA_WR_BITS(DI_INP_GEN_REG2, 0, 0, 1);
	}

	RDMA_WR(DI_INP_CANVAS0, (mif->canvas0_addr2 << 16) |
				/* cntl_canvas0_addr2 */
				(mif->canvas0_addr1 << 8) |
				/* cntl_canvas0_addr1 */
				(mif->canvas0_addr0 << 0)
				/* cntl_canvas0_addr0 */
	);

	/* ---------------------- */
	/* Picture 0 X/Y start,end */
	/* ---------------------- */
	RDMA_WR(DI_INP_LUMA_X0, (mif->luma_x_end0 << 16) |
			/* cntl_luma_x_end0 */
			(mif->luma_x_start0 << 0)/* cntl_luma_x_start0 */
		);
	RDMA_WR(DI_INP_LUMA_Y0, (mif->luma_y_end0 << 16) |
			/* cntl_luma_y_end0 */
			(mif->luma_y_start0 << 0) /* cntl_luma_y_start0 */
		);
	RDMA_WR(DI_INP_CHROMA_X0, (mif->chroma_x_end0 << 16) |
			(mif->chroma_x_start0 << 0)
		);
	RDMA_WR(DI_INP_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
		   (mif->chroma_y_start0 << 0)
		);

	/* ---------------------- */
	/* Repeat or skip */
	/* ---------------------- */
	RDMA_WR(DI_INP_RPT_LOOP, (0 << 28) |
					(0 << 24) |
					(0 << 20) |
					(0 << 16) |
					(chroma0_rpt_loop_start << 12) |
					(chroma0_rpt_loop_end << 8)	|
					(luma0_rpt_loop_start << 4)	|
					(luma0_rpt_loop_end << 0)
		);

	RDMA_WR(DI_INP_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
	RDMA_WR(DI_INP_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

	/* Dummy pixel value */
	RDMA_WR(DI_INP_DUMMY_PIXEL, 0x00808000);
	if ((mif->set_separate_en != 0)) {/* 4:2:0 block mode.*/
		set_di_inp_fmt_more(
						1,/* hfmt_en */
						1,/* hz_yc_ratio */
						0,/* hz_ini_phase */
						1,/* vfmt_en */
						1,/* vt_yc_ratio */
						vt_ini_phase,/* vt_ini_phase */
		mif->luma_x_end0 - mif->luma_x_start0 + 1,
						/* y_length */
		mif->chroma_x_end0 - mif->chroma_x_start0 + 1,
						/* c length */
						0);	/* hz repeat. */
	} else {
		set_di_inp_fmt_more(
						1,	/* hfmt_en */
						1,	/* hz_yc_ratio */
						0,	/* hz_ini_phase */
						0,	/* vfmt_en */
						0,	/* vt_yc_ratio */
						0,	/* vt_ini_phase */
	mif->luma_x_end0 - mif->luma_x_start0 + 1,
	((mif->luma_x_end0 >> 1) - (mif->luma_x_start0>>1) + 1),
						0); /* hz repeat. */
	}
}

static void set_di_mem_fmt_more(int hfmt_en,
				int hz_yc_ratio,	/* 2bit */
				int hz_ini_phase,	/* 4bit */
				int vfmt_en,
				int vt_yc_ratio,	/* 2bit */
				int vt_ini_phase,	/* 4bit */
				int y_length,
				int c_length,
				int hz_rpt	/* 1bit */
		)
{
	int vt_phase_step = (16 >> vt_yc_ratio);

	RDMA_WR(DI_MEM_FMT_CTRL,
				(hz_rpt << 28)	| /* hz rpt pixel */
			(hz_ini_phase << 24) | /* hz ini phase */
				(0 << 23)	| /* repeat p0 enable */
			(hz_yc_ratio << 21)	| /* hz yc ratio */
				(hfmt_en << 20)	| /* hz enable */
				(1 << 17)	| /* nrpt_phase0 enable */
				(0 << 16)	| /* repeat l0 enable */
				(0 << 12)	| /* skip line num */
			(vt_ini_phase << 8)	| /* vt ini phase */
			(vt_phase_step << 1) | /* vt phase step (3.4) */
			(vfmt_en << 0)		/* vt enable */
				);

	RDMA_WR(DI_MEM_FMT_W, (y_length << 16) |	/* hz format width */
					(c_length << 0)	/* vt format width */
				);
}

#ifdef NEW_DI_V1
static void set_di_chan2_fmt_more(int hfmt_en,
				int hz_yc_ratio,/* 2bit */
				int hz_ini_phase,/* 4bit */
				int vfmt_en,
				int vt_yc_ratio,/* 2bit */
				int vt_ini_phase,/* 4bit */
				int y_length,
				int c_length,
				int hz_rpt	/* 1bit */
		)
{
	int vt_phase_step = (16 >> vt_yc_ratio);

	RDMA_WR(DI_CHAN2_FMT_CTRL, (hz_rpt << 28) | /* hz rpt pixel */
				(hz_ini_phase << 24) | /* hz ini phase */
				(0 << 23) | /* repeat p0 enable */
				(hz_yc_ratio << 21) | /* hz yc ratio */
				(hfmt_en << 20) | /* hz enable */
				(1 << 17) | /* nrpt_phase0 enable */
				(0 << 16) | /* repeat l0 enable */
				(0 << 12) | /* skip line num */
				(vt_ini_phase << 8)	 | /* vt ini phase */
				(vt_phase_step << 1) | /* vt phase step (3.4) */
				(vfmt_en << 0)		/* vt enable */
				);

	RDMA_WR(DI_CHAN2_FMT_W, (y_length << 16) | /* hz format width */
					(c_length << 0)	/* vt format width */
				);
}
#endif

static void set_di_mem_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
	unsigned int bytes_per_pixel;
	unsigned int demux_mode;
	unsigned int chro_rpt_lastl_ctrl;
	unsigned int luma0_rpt_loop_start;
	unsigned int luma0_rpt_loop_end;
	unsigned int luma0_rpt_loop_pat;
	unsigned int chroma0_rpt_loop_start;
	unsigned int chroma0_rpt_loop_end;
	unsigned int chroma0_rpt_loop_pat;
	unsigned int reset_on_gofield;
	if (mif->set_separate_en != 0 && mif->src_field_mode == 1) {
		chro_rpt_lastl_ctrl = 1;
		luma0_rpt_loop_start = 1;
		luma0_rpt_loop_end = 1;
		chroma0_rpt_loop_start = 1;
		chroma0_rpt_loop_end = 1;
		luma0_rpt_loop_pat = 0x80;
		chroma0_rpt_loop_pat = 0x80;
	} else if (mif->set_separate_en != 0 && mif->src_field_mode == 0) {
		chro_rpt_lastl_ctrl = 1;
		luma0_rpt_loop_start = 0;
		luma0_rpt_loop_end = 0;
		chroma0_rpt_loop_start = 0;
		chroma0_rpt_loop_end = 0;
		luma0_rpt_loop_pat = 0x0;
		chroma0_rpt_loop_pat = 0x0;
	} else if (mif->set_separate_en == 0 && mif->src_field_mode == 1) {
		chro_rpt_lastl_ctrl = 1;
		luma0_rpt_loop_start = 1;
		luma0_rpt_loop_end = 1;
		chroma0_rpt_loop_start = 0;
		chroma0_rpt_loop_end = 0;
		luma0_rpt_loop_pat = 0x80;
		chroma0_rpt_loop_pat = 0x00;
	} else {
		chro_rpt_lastl_ctrl = 0;
		luma0_rpt_loop_start = 0;
		luma0_rpt_loop_end = 0;
		chroma0_rpt_loop_start = 0;
		chroma0_rpt_loop_end = 0;
		luma0_rpt_loop_pat = 0x00;
		chroma0_rpt_loop_pat = 0x00;
	}

	bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
	demux_mode = mif->video_mode;


	/* ---------------------- */
	/* General register */
	/* ---------------------- */
	reset_on_gofield = (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXBB)?0:1;
	RDMA_WR(DI_MEM_GEN_REG, (reset_on_gofield << 29) |
						/* reset on go field */
				(urgent << 28)	| /* urgent bit. */
				(urgent << 27)	| /* urgent bit. */
				(1 << 25)		| /* no dummy data. */
				(hold_line << 19) |	/* hold lines */
				(1 << 18) |	/* push dummy pixel */
				(demux_mode << 16) | /* demux_mode */
				(bytes_per_pixel << 14)	|
				(1 << 12)	|/*burst_size_cr*/
				(1 << 10)	|/*burst_size_cb*/
				(3 << 8)	|/*burst_size_y*/
				(chro_rpt_lastl_ctrl << 6)	|
				((mif->set_separate_en != 0) << 1)|
				(1 << 0)	/* cntl_enable */
	  );

	/* ---------------------- */
	/* Canvas */
	/* ---------------------- */
	RDMA_WR(DI_MEM_CANVAS0, (mif->canvas0_addr2 << 16) |
		/* cntl_canvas0_addr2 */
(mif->canvas0_addr1 << 8) | (mif->canvas0_addr0 << 0));

	/* ---------------------- */
	/* Picture 0 X/Y start,end */
	/* ---------------------- */
	RDMA_WR(DI_MEM_LUMA_X0, (mif->luma_x_end0 << 16) |
(mif->luma_x_start0 << 0) /* cntl_luma_x_start0 */
		);
	RDMA_WR(DI_MEM_LUMA_Y0, (mif->luma_y_end0 << 16) |
(mif->luma_y_start0 << 0) /* cntl_luma_y_start0 */
		);
	RDMA_WR(DI_MEM_CHROMA_X0, (mif->chroma_x_end0 << 16) |
				(mif->chroma_x_start0 << 0)
		);
	RDMA_WR(DI_MEM_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
				(mif->chroma_y_start0 << 0)
		);

	/* ---------------------- */
	/* Repeat or skip */
	/* ---------------------- */
	RDMA_WR(DI_MEM_RPT_LOOP, (0 << 28) |
					(0	<< 24) |
					(0	<< 20) |
					(0	  << 16) |
					(chroma0_rpt_loop_start << 12) |
					(chroma0_rpt_loop_end << 8) |
					(luma0_rpt_loop_start << 4) |
					(luma0_rpt_loop_end << 0)
		);

	RDMA_WR(DI_MEM_LUMA0_RPT_PAT, luma0_rpt_loop_pat);
	RDMA_WR(DI_MEM_CHROMA0_RPT_PAT, chroma0_rpt_loop_pat);

	/* Dummy pixel value */
	RDMA_WR(DI_MEM_DUMMY_PIXEL, 0x00808000);
	if ((mif->set_separate_en != 0)) {/* 4:2:0 block mode.*/
		set_di_mem_fmt_more(
						1,	/* hfmt_en */
						1,	/* hz_yc_ratio */
						0,  /* hz_ini_phase */
						1,	/* vfmt_en */
						1,	/* vt_yc_ratio */
						0,	/* vt_ini_phase */
			mif->luma_x_end0 - mif->luma_x_start0 + 1,
						/* y_length */
			mif->chroma_x_end0 - mif->chroma_x_start0 + 1,
						/* c length */
						0);	/* hz repeat. */
	} else {
		set_di_mem_fmt_more(1,	/* hfmt_en */
		1,	/* hz_yc_ratio */
		0,  /* hz_ini_phase */
		0,	/* vfmt_en */
		0,	/* vt_yc_ratio */
		0,	/* vt_ini_phase */
		mif->luma_x_end0 - mif->luma_x_start0 + 1,
		((mif->luma_x_end0 >> 1) - (mif->luma_x_start0>>1) + 1),
		0);	/* hz repeat. */
	}
}

static void set_di_if1_fmt_more(int hfmt_en,
				int hz_yc_ratio,/* 2bit */
				int hz_ini_phase,/* 4bit */
				int vfmt_en,
				int vt_yc_ratio,/* 2bit */
				int vt_ini_phase,/* 4bit */
				int y_length,
				int c_length,
				int hz_rpt	/* 1bit */
				)
{
	int vt_phase_step = (16 >> vt_yc_ratio);

	VSYNC_WR_MPEG_REG(DI_IF1_FMT_CTRL,
	(hz_rpt << 28)|/* hz rpt pixel */
	(hz_ini_phase << 24)|/* hz ini phase */
	(0 << 23)|/* repeat p0 enable */
	(hz_yc_ratio << 21)|/* hz yc ratio */
	(hfmt_en << 20)|/* hz enable */
	(1 << 17)|/* nrpt_phase0 enable */
	(0 << 16)|/* repeat l0 enable */
	(0 << 12)|/* skip line num */
	(vt_ini_phase << 8)|/* vt ini phase */
	(vt_phase_step << 1)|/* vt phase step (3.4) */
	(vfmt_en << 0) /* vt enable */
					);

	VSYNC_WR_MPEG_REG(DI_IF1_FMT_W, (y_length << 16) |
							 (c_length << 0));
}

extern int di_vscale_skip_count_real;
static const u32 vpat[] = {0, 0x8, 0x9, 0xa, 0xb, 0xc, 0xd, 0xe, 0xf};

static void set_di_if1_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
	unsigned int bytes_per_pixel, demux_mode;
	unsigned int pat, loop = 0, chro_rpt_lastl_ctrl = 0;

	if (mif->set_separate_en == 1) {
		pat = vpat[(di_vscale_skip_count_real<<1)+1];
		/*top*/
		if (mif->src_field_mode == 0) {
			chro_rpt_lastl_ctrl = 1;
			loop = 0x11;
			pat <<= 4;
		}
	} else {
		loop = 0;
		pat = vpat[di_vscale_skip_count_real];
	}

	bytes_per_pixel = mif->set_separate_en ? 0 : (mif->video_mode ? 2 : 1);
	demux_mode = mif->video_mode;


	/* ---------------------- */
	/* General register */
	/* ---------------------- */

	VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG, (0 << 29) | /* reset on go field */
			(urgent << 28)	|/* urgent */
			(urgent << 27)	|/* luma urgent */
			(1 << 25)|/* no dummy data. */
			(hold_line << 19)|/* hold lines */
			(1 << 18)|/* push dummy pixel */
			(demux_mode << 16)|/* demux_mode */
			(bytes_per_pixel << 14)|
			(1 << 12)|/*burst_size_cr*/
			(1 << 10)|/*burst_size_cb*/
			(3 << 8)|/*burst_size_y*/
			(chro_rpt_lastl_ctrl << 6)|
			((mif->set_separate_en != 0) << 1)|
			(1 << 0)/* cntl_enable */
		);

	/* ---------------------- */
	/* Canvas */
	/* ---------------------- */
	VSYNC_WR_MPEG_REG(DI_IF1_CANVAS0, (mif->canvas0_addr2 << 16) |
(mif->canvas0_addr1 << 8) | (mif->canvas0_addr0 << 0));

	/* ---------------------- */
	/* Picture 0 X/Y start,end */
	/* ---------------------- */
	VSYNC_WR_MPEG_REG(DI_IF1_LUMA_X0, (mif->luma_x_end0 << 16) |
(mif->luma_x_start0 << 0));
	VSYNC_WR_MPEG_REG(DI_IF1_LUMA_Y0, (mif->luma_y_end0 << 16) |
(mif->luma_y_start0 << 0));
	VSYNC_WR_MPEG_REG(DI_IF1_CHROMA_X0, (mif->chroma_x_end0 << 16) |
(mif->chroma_x_start0 << 0));
	VSYNC_WR_MPEG_REG(DI_IF1_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
(mif->chroma_y_start0 << 0));

	/* ---------------------- */
	/* Repeat or skip */
	/* ---------------------- */
	VSYNC_WR_MPEG_REG(DI_IF1_RPT_LOOP, (loop << 24) |
							   (loop << 16) |
							   (loop << 8) |
							   (loop << 0)
					 );

	VSYNC_WR_MPEG_REG(DI_IF1_LUMA0_RPT_PAT, pat);
	VSYNC_WR_MPEG_REG(DI_IF1_CHROMA0_RPT_PAT, pat);

	/* Dummy pixel value */
	VSYNC_WR_MPEG_REG(DI_IF1_DUMMY_PIXEL, 0x00808000);
	if (mif->set_separate_en != 0) { /* 4:2:0 block mode. */
		set_di_if1_fmt_more(1, /* hfmt_en */
		1,/* hz_yc_ratio */
		0,/* hz_ini_phase */
		1,	/* vfmt_en */
		1, /* vt_yc_ratio */
		0, /* vt_ini_phase */
		mif->luma_x_end0 - mif->luma_x_start0 + 1,
		mif->chroma_x_end0 - mif->chroma_x_start0 + 1,
							 0); /* hz repeat. */
	} else {
		set_di_if1_fmt_more(1,	/* hfmt_en */
		1, /* hz_yc_ratio */
		0, /* hz_ini_phase */
		0,	/* vfmt_en */
		0,	/* vt_yc_ratio */
		0, /* vt_ini_phase */
			mif->luma_x_end0 - mif->luma_x_start0 + 1,
			((mif->luma_x_end0 >> 1) - (mif->luma_x_start0>>1) + 1),
							 0); /* hz repeat */
	}
}

static void set_di_chan2_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
	unsigned int bytes_per_pixel;
	unsigned int demux_mode;
	unsigned int luma0_rpt_loop_start;
	unsigned int luma0_rpt_loop_end;
	unsigned int luma0_rpt_loop_pat;
	unsigned int reset_on_gofield;

	bytes_per_pixel = mif->set_separate_en ? 0 :
((mif->video_mode == 1) ? 2 : 1);
	demux_mode =  mif->video_mode & 1;

	if (mif->src_field_mode == 1) {
		luma0_rpt_loop_start = 1;
		luma0_rpt_loop_end = 1;
		luma0_rpt_loop_pat = 0x80;
	} else {
		luma0_rpt_loop_start = 0;
		luma0_rpt_loop_end = 0;
		luma0_rpt_loop_pat = 0;
	}
	/* ---------------------- */
	/* General register */
	/* ---------------------- */
	reset_on_gofield = (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXBB)?0:1;
	RDMA_WR(DI_CHAN2_GEN_REG, (reset_on_gofield << 29) |
				(urgent << 28) | /* urgent */
				(urgent << 27) | /* luma urgent */
				(1 << 25) |	/* no dummy data. */
				(hold_line << 19) |	/* hold lines */
				(1 << 18) |/* push dummy pixel */
				(demux_mode << 16) |
				(bytes_per_pixel << 14)	|
				(1 << 12)	|/*burst_size_cr*/
				(1 << 10)	|/*burst_size_cb*/
				(3 << 8) |/*burst_size_y*/
((hold_line == 0 ? 1 : 0) << 7) |
(0 << 6)|(0 << 1)|(1 << 0));


	/* ---------------------- */
	/* Canvas */
	/* ---------------------- */
	RDMA_WR(DI_CHAN2_CANVAS, (0 << 16) | /* cntl_canvas0_addr2 */
(0 << 8)|(mif->canvas0_addr0 << 0));

	/* ---------------------- */
	/* Picture 0 X/Y start,end */
	/* ---------------------- */
	RDMA_WR(DI_CHAN2_LUMA_X, (mif->luma_x_end0 << 16) |
	/* cntl_luma_x_end0 */
(mif->luma_x_start0 << 0));
	RDMA_WR(DI_CHAN2_LUMA_Y, (mif->luma_y_end0 << 16) |
	/* cntl_luma_y_end0 */
(mif->luma_y_start0 << 0));

	/* ---------------------- */
	/* Repeat or skip */
	/* ---------------------- */
	RDMA_WR(DI_CHAN2_RPT_LOOP, (0 << 28) |
							(0	 << 24) |
							(0	 << 20) |
							(0	 << 16) |
							(0	 << 12) |
							(0	 << 8)	|
						(luma0_rpt_loop_start << 4) |
						(luma0_rpt_loop_end << 0)
	);

	RDMA_WR(DI_CHAN2_LUMA_RPT_PAT, luma0_rpt_loop_pat);

	/* Dummy pixel value */
	RDMA_WR(DI_CHAN2_DUMMY_PIXEL, 0x00808000);

#ifdef NEW_DI_V1
	if ((mif->set_separate_en != 0)) {	/* 4:2:0 block mode. */
		set_di_chan2_fmt_more(
						1,	/* hfmt_en */
						1,	/* hz_yc_ratio */
						0,	/* hz_ini_phase */
						1,	/* vfmt_en */
						1,	/* vt_yc_ratio */
						0,	/* vt_ini_phase */
		mif->luma_x_end0 - mif->luma_x_start0 + 1,	/* y_length */
		mif->chroma_x_end0 - mif->chroma_x_start0 + 1,/* c length */
						0);	/* hz repeat. */
	} else {
		set_di_chan2_fmt_more(
						1,	/* hfmt_en */
						1,	/* hz_yc_ratio */
						0,	/* hz_ini_phase */
						0,	/* vfmt_en */
						0,	/* vt_yc_ratio */
						0,	/* vt_ini_phase */
		mif->luma_x_end0 - mif->luma_x_start0 + 1,	/* y_length */
		((mif->luma_x_end0 >> 1) - (mif->luma_x_start0>>1) + 1),
						0);	/* hz repeat. */
	}
#endif

}

static void set_di_if0_mif(DI_MIF_t *mif, int urgent, int hold_line)
{
	unsigned int pat, loop = 0;

	if (mif->set_separate_en == 1) {
		pat = vpat[(di_vscale_skip_count_real<<1)+1];
		if (mif->src_field_mode == 0) {/* top */
			loop = 0x11;
			pat <<= 4;
		}
	} else {
		loop = 0;
	pat = vpat[di_vscale_skip_count_real];
	/* ---------------------- */
	/* Canvas */
	/* ---------------------- */
	VSYNC_WR_MPEG_REG(VD1_IF0_CANVAS0, (mif->canvas0_addr2 << 16)|
(mif->canvas0_addr1 << 8)|(mif->canvas0_addr0 << 0));

	/* ---------------------- */
	/* Picture 0 X/Y start,end */
	/* ---------------------- */
	VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_X0, (mif->luma_x_end0 << 16) |
(mif->luma_x_start0 << 0));
	VSYNC_WR_MPEG_REG(VD1_IF0_LUMA_Y0, (mif->luma_y_end0 << 16) |
(mif->luma_y_start0 << 0));
	VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_X0, (mif->chroma_x_end0 << 16) |
(mif->chroma_x_start0 << 0));
	VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA_Y0, (mif->chroma_y_end0 << 16) |
(mif->chroma_y_start0 << 0));

	}
	/* ---------------------- */
	/* Repeat or skip */
	/* ---------------------- */
	VSYNC_WR_MPEG_REG(VD1_IF0_RPT_LOOP,
				   (loop << 24) |
				   (loop << 16)   |
				   (loop << 8) |
				   (loop << 0));
	VSYNC_WR_MPEG_REG(VD1_IF0_LUMA0_RPT_PAT,   pat);
	VSYNC_WR_MPEG_REG(VD1_IF0_CHROMA0_RPT_PAT, pat);
}

void initial_di_pre_aml(int hsize_pre, int vsize_pre, int hold_line)
{
	Wr(DI_PRE_SIZE, (hsize_pre - 1) | ((vsize_pre - 1) << 16));
	Wr(DI_PRE_CTRL, 0 |	/* NR enable */
(0 << 1) |	/* MTN_EN */
(1 << 2) |	/* check 3:2 pulldown */
(0 << 3) |	/* check 2:2 pulldown */
(0 << 4) | /* 2:2 check mid pixel come from next field after MTN. */
(0 << 5) |	/* hist check enable */
(0 << 6) |	/* hist check not use chan2. */
(0 << 7) |	/* hist check use data before noise reduction. */
(0 << 8) |	/* chan 2 enable for 2:2 pull down check. */
(0 << 9) |	/* line buffer 2 enable */
(0 << 10) |	/* pre drop first. */
(0 << 11) |	/* pre repeat. */
(0 << 12) |	/* pre viu link */
(hold_line << 16) |	/* pre hold line number */
(0 << 29) |	/* pre field number. */
(0x3 << 30)	/* pre soft rst, pre frame rst. */
			);
#ifdef SUPPORT_MPEG_TO_VDIN
	if (mpeg2vdin_flag)
		Wr_reg_bits(DI_PRE_CTRL, 1, 13, 1);
#endif
	Wr(DI_MC_22LVL0, (Rd(DI_MC_22LVL0) & 0xffff0000) | 256);
	Wr(DI_MC_32LVL0, (Rd(DI_MC_32LVL0) & 0xffffff00) | 16);
}

void initial_di_post_2(int hsize_post, int vsize_post, int hold_line)
{
	VSYNC_WR_MPEG_REG(DI_POST_SIZE,
(hsize_post - 1) | ((vsize_post - 1) << 16));
	/* di demo */
	VSYNC_WR_MPEG_REG(DI_BLEND_REG0_X, ((hsize_post-1)>>1));
	VSYNC_WR_MPEG_REG(DI_BLEND_REG0_Y, (vsize_post-1));
	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, Rd(DI_BLEND_CTRL)|
(0x2 << 20) |	/* top mode. EI only */
25); /* KDEINT */
	VSYNC_WR_MPEG_REG(DI_POST_CTRL, (0 << 0) |
					  (0 << 1)	|
					  (0 << 2)	|
					  (0 << 3)	|
					  (0 << 4)	|
					  (0 << 5)	|
					  (0 << 6)	|
					  (0 << 7)	|
					  (1 << 8)	|
					  (0 << 9)	|
					  (0 << 10) |
					  (0 << 11) |
					  (0 << 12) |
					  (hold_line << 16) |
					  (0 << 29) |
					  (0x3 << 30)
		);
	if (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXBB)
		/* enable ma,disable if0 to vpp */
		VSYNC_WR_MPEG_REG_BITS(VIU_MISC_CTRL0, 5, 16, 3);
}

void di_post_switch_buffer(
	DI_MIF_t		   *di_buf0_mif,
	DI_MIF_t		   *di_buf1_mif,
	DI_SIM_MIF_t    *di_diwr_mif,
	DI_SIM_MIF_t    *di_mtnprd_mif,
#ifdef MCDI_SUPPORT
	DI_MC_MIF_t	   *di_mcvecrd_mif,
#endif
	int ei_en, int blend_en, int blend_mtn_en, int blend_mode,
	int di_vpp_en, int di_ddr_en,
	int post_field_num, int hold_line, int urgent
)
{
	int ei_only, buf1_en;

	ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en);
	buf1_en =  (!ei_only && (di_ddr_en || di_vpp_en));

	if (ei_en || di_vpp_en || di_ddr_en)
		set_di_if0_mif(di_buf0_mif, urgent, hold_line);

	if (!ei_only && (di_ddr_en || di_vpp_en)) {
		VSYNC_WR_MPEG_REG(DI_IF1_CANVAS0,
(di_buf1_mif->canvas0_addr2 << 16) |
(di_buf1_mif->canvas0_addr1 << 8) | (di_buf1_mif->canvas0_addr0 << 0));
	}

	/* motion for current display field. */
	if (blend_mtn_en) {
		VSYNC_WR_MPEG_REG(DI_MTNRD_CTRL,
(di_mtnprd_mif->canvas_num << 8) | (urgent << 16)
	 ); /* current field mtn canvas index. */

	}

	if (di_ddr_en) {
		VSYNC_WR_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num |
			(urgent << 16)); /* urgent. */
	}

	VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, Rd(DI_BLEND_CTRL)|
			(blend_en<<31) | (blend_mode<<20) | 0x1c0001f);
#ifdef MCDI_SUPPORT
	VSYNC_WR_MPEG_REG(MCDI_MCVECRD_CTRL,
(Rd(MCDI_MCVECRD_CTRL) & 0xffffff00) |
(1<<9) |	  /* canvas enable */
di_mcvecrd_mif->canvas_num |  /* canvas index. */
(urgent << 8));
		if (di_mcvecrd_mif->blend_mode == 3)
			VSYNC_WR_MPEG_REG_BITS(MCDI_MC_CRTL, mcen_mode, 0, 2);
		else
			VSYNC_WR_MPEG_REG_BITS(MCDI_MC_CRTL, 0, 0, 2);
#endif
	VSYNC_WR_MPEG_REG(DI_POST_CTRL,
((ei_en|blend_en) << 0) |	/* line buffer 0 enable */
((blend_mode == 1?1:0) << 1)  |
(ei_en << 2) |			/* ei  enable */
(blend_mtn_en << 3) |	/* mtn line buffer enable */
(blend_mtn_en  << 4) |/* mtnp read mif enable */
(blend_en << 5) |
(1 << 6) |		/* di mux output enable */
(di_ddr_en << 7) |/* di write to SDRAM enable.*/
(di_vpp_en << 8) |/* di to VPP enable. */
(0 << 9) |		/* mif0 to VPP enable. */
(0 << 10) |		/* post drop first. */
(0 << 11) |
(di_vpp_en << 12) | /* post viu link */
(hold_line << 16) | /* post hold line number */
(post_field_num << 29) |	/* post field number. */
(0x1 << 30)	/* post soft rst  post frame rst. */
		);
}

void enable_di_post_2(
	DI_MIF_t		   *di_buf0_mif,
	DI_MIF_t		   *di_buf1_mif,
	DI_SIM_MIF_t    *di_diwr_mif,
	DI_SIM_MIF_t    *di_mtnprd_mif,
	int ei_en, int blend_en, int blend_mtn_en, int blend_mode,
	int di_vpp_en, int di_ddr_en, int post_field_num,
	int hold_line, int urgent
)
{
	int ei_only;
	int buf1_en;

	ei_only = ei_en && !blend_en && (di_vpp_en || di_ddr_en);
	buf1_en =  (!ei_only && (di_ddr_en || di_vpp_en));

	if (ei_en || di_vpp_en || di_ddr_en)
		set_di_if0_mif(di_buf0_mif, di_vpp_en, hold_line);

	if (!ei_only && (di_ddr_en || di_vpp_en))
		set_di_if1_mif(di_buf1_mif, di_vpp_en, hold_line);

	/* printk("%s: ei_only %d,buf1_en %d,ei_en %d,di_vpp_en %d,
di_ddr_en %d,blend_mtn_en %d,blend_mode %d.\n",
__func__,ei_only,buf1_en,ei_en,di_vpp_en,di_ddr_en,
blend_mtn_en,blend_mode); */
	/* motion for current display field. */
		VSYNC_WR_MPEG_REG(DI_MTNPRD_X,
(di_mtnprd_mif->start_x << 16) | (di_mtnprd_mif->end_x));
		VSYNC_WR_MPEG_REG(DI_MTNPRD_Y,
(di_mtnprd_mif->start_y << 16) | (di_mtnprd_mif->end_y));
	if (blend_mtn_en) {
		VSYNC_WR_MPEG_REG(DI_MTNRD_CTRL,
(di_mtnprd_mif->canvas_num << 8) | (urgent << 16)
	 ); /* current field mtn canvas index */
	}

	if (di_ddr_en) {
		VSYNC_WR_MPEG_REG(DI_DIWR_X,
(di_diwr_mif->start_x << 16) | (di_diwr_mif->end_x));
	   VSYNC_WR_MPEG_REG(DI_DIWR_Y,
(di_diwr_mif->start_y << 16) | (di_diwr_mif->end_y * 2 + 1));
	   VSYNC_WR_MPEG_REG(DI_DIWR_CTRL, di_diwr_mif->canvas_num|
(urgent << 16));
	}

		VSYNC_WR_MPEG_REG(DI_BLEND_CTRL, Rd(DI_BLEND_CTRL)|
			(blend_en<<31) | (blend_mode<<20) | 0x1c0001f);

	VSYNC_WR_MPEG_REG(DI_POST_CTRL,
((ei_en | blend_en) << 0) |	/* line buffer 0 enable */
((blend_mode == 1?1:0) << 1)  |
(ei_en << 2) | /* ei  enable */
(blend_mtn_en << 3) |	/* mtn line buffer enable */
(blend_mtn_en  << 4) |/* mtnp read mif enable */
(blend_en << 5) |
(1 << 6) |/* di mux output enable */
(di_ddr_en << 7) |	/* di write to SDRAM enable. */
(di_vpp_en << 8) |	/* di to VPP enable. */
(0 << 9) |	/* mif0 to VPP enable. */
(0 << 10) |	/* post drop first. */
(0 << 11) |
(di_vpp_en << 12) |	/* post viu link */
(hold_line << 16) |	/* post hold line number */
(post_field_num << 29) |	/* post field number. */
(0x1 << 30)	/* post soft rst  post frame rst. */
		);
}

void disable_post_deinterlace_2(void)
{
	VSYNC_WR_MPEG_REG(DI_POST_CTRL, 0x3 << 30);
	VSYNC_WR_MPEG_REG(DI_POST_SIZE, (32-1) | ((128-1) << 16));
	VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG, 0x3 << 30);
	if (get_cpu_type() >= MESON_CPU_MAJOR_ID_GXBB) {
		/* disable ma,enable if0 to vpp,enable afbc to vpp */
		VSYNC_WR_MPEG_REG_BITS(VIU_MISC_CTRL0, 0, 16, 4);
		/* DI inp(current data) switch to memory */
		VSYNC_WR_MPEG_REG_BITS(VIUB_MISC_CTRL0, 0, 16, 1);
	}
	/* VSYNC_WR_MPEG_REG(DI_IF1_GEN_REG,
Rd(DI_IF1_GEN_REG) & 0xfffffffe); */
}

void enable_film_mode_check(unsigned int width, unsigned int height,
		enum vframe_source_type_e source_type)
{
	unsigned int win0_start_x, win0_end_x, win0_start_y, win0_end_y;
	unsigned int win1_start_x, win1_end_x, win1_start_y, win1_end_y;
	unsigned int win2_start_x, win2_end_x, win2_start_y, win2_end_y;
	unsigned int win3_start_x, win3_end_x, win3_start_y, win3_end_y;
	unsigned int win4_start_x, win4_end_x, win4_start_y, win4_end_y;

	win0_start_x = win1_start_x = win2_start_x = 0;
	win3_start_x = win4_start_x = 0;
	win0_end_x = win1_end_x = win2_end_x = width-1;
	win3_end_x = win4_end_x = width-1;
	win0_start_y = 0;
	win1_start_y = (height>>3); /* 1/8 */
	win0_end_y = win1_start_y - 1;
	win2_start_y = win1_start_y + (height>>2); /* 1/4 */
	win1_end_y = win2_start_y - 1;
	win3_start_y = win2_start_y + (height>>2); /* 1/4 */
	win2_end_y = win3_start_y - 1;
	win4_start_y = win3_start_y + (height>>2); /* 1/4 */
	win3_end_y = win4_start_y - 1;
	win4_end_y = win4_start_y + (height>>3) - 1; /* 1/8 */

	pd_win_prop[0].pixels_num =
(win0_end_x-win0_start_x)*(win0_end_y-win0_start_y);
	pd_win_prop[1].pixels_num =
(win1_end_x-win1_start_x)*(win1_end_y-win1_start_y);
	pd_win_prop[2].pixels_num =
(win2_end_x-win2_start_x)*(win2_end_y-win2_start_y);
	pd_win_prop[3].pixels_num =
(win3_end_x-win3_start_x)*(win3_end_y-win3_start_y);
	pd_win_prop[4].pixels_num =
(win4_end_x-win4_start_x)*(win4_end_y-win4_start_y);

	RDMA_WR(DI_MC_REG0_X, (win0_start_x << 16) | win0_end_x);
	RDMA_WR(DI_MC_REG0_Y, (win0_start_y << 16) | win0_end_y);
	RDMA_WR(DI_MC_REG1_X, (win1_start_x << 16) | win1_end_x);
	RDMA_WR(DI_MC_REG1_Y, (win1_start_y << 16) | win1_end_y);
	RDMA_WR(DI_MC_REG2_X, (win2_start_x << 16) | win2_end_x);
	RDMA_WR(DI_MC_REG2_Y, (win2_start_y << 16) | win2_end_y);
	RDMA_WR(DI_MC_REG3_X, (win3_start_x << 16) | win3_end_x);
	RDMA_WR(DI_MC_REG3_Y, (win3_start_y << 16) | win3_end_y);
	RDMA_WR(DI_MC_REG4_X, (win4_start_x << 16) | win4_end_x);
	RDMA_WR(DI_MC_REG4_Y, (win4_start_y << 16) | win4_end_y);

}

static int fdn[5] = {0};
bool read_pulldown_info(pulldown_detect_info_t *field_pd_info,
		pulldown_detect_info_t *win_pd_info)
{
	int i;
	unsigned long pd_info[6];
	unsigned long tmp;
	Wr(DI_INFO_ADDR, 0);
	for (i  = 0; i < 6; i++)
		pd_info[i] = Rd(DI_INFO_DATA);

	memset(field_pd_info, 0, sizeof(pulldown_detect_info_t));
	field_pd_info->field_diff		= pd_info[2];
	field_pd_info->field_diff_num	= pd_info[4]&0xffffff;
	field_pd_info->frame_diff		= pd_info[0];
	field_pd_info->frame_diff_num	= pd_info[1]&0xffffff;

	fdn[0] = fdn[1];
	fdn[1] = fdn[2];
	fdn[2] = fdn[3];
	fdn[3] = fdn[4];
	fdn[4] = field_pd_info->frame_diff_num;
	/* if (fdn[0] || fdn[1] || fdn[2] || fdn[3] || fdn[4]) */
	if (frame_dynamic_dbg)
		pr_info("\n fdn[4]= %x", fdn[4]);
	if (frame_dynamic_level == 0)
		fdn[4] = fdn[4]&0xffff00;
	else if (frame_dynamic_level == 1)
		fdn[4] = fdn[4]&0xfffe00;
	else if (frame_dynamic_level == 2)
		fdn[4] = fdn[4]&0xfffc00;
	else if (frame_dynamic_level == 3)
		fdn[4] = fdn[4]&0xfff800;
	else if (frame_dynamic_level == 4)
		fdn[4] = fdn[4]&0xfff000;
	else
		fdn[4] = fdn[4]&0xffff00;
	if ((fdn[0]&0xffff00) || fdn[1] || fdn[2] || fdn[3] || fdn[4])
		frame_dynamic = true;
	else
		frame_dynamic = false;

	for (i = 0; i < MAX_WIN_NUM; i++)
		memset(&(win_pd_info[i]), 0, sizeof(pulldown_detect_info_t));

	for (i = 0; i < MAX_WIN_NUM; i++)
		win_pd_info[i].frame_diff = Rd(DI_INFO_DATA);

	for (i = 0; i < MAX_WIN_NUM; i++)
		win_pd_info[i].field_diff = Rd(DI_INFO_DATA);

	for (i = 0; i < MAX_WIN_NUM; i++)
		tmp = Rd(DI_INFO_DATA); /* luma */

	for (i = 0; i < MAX_WIN_NUM; i++)
		win_pd_info[i].frame_diff_num = Rd(DI_INFO_DATA)&0xffffff;

	for (i = 0; i < MAX_WIN_NUM; i++)
		win_pd_info[i].field_diff_num =
(Rd(DI_INFO_DATA)&0xfffff)<<4;

	return frame_dynamic;
}

void di_post_read_reverse(bool reverse)
{
#ifdef NEW_DI_TV
	if (reverse) {
		Wr_reg_bits(DI_IF1_GEN_REG2,	3, 2, 2);
		Wr_reg_bits(VD1_IF0_GEN_REG2, 0xf, 2, 4);
		Wr_reg_bits(VD2_IF0_GEN_REG2, 0xf, 2, 4);
		#ifdef MCDI_SUPPORT
		/* motion vector read reverse*/
		Wr_reg_bits(MCDI_MCVECRD_X, 1, 30, 1);
		Wr_reg_bits(MCDI_MCVECRD_Y, 1, 30, 1);
		Wr_reg_bits(MCDI_MC_CRTL, 0, 8, 1);
		#endif
	} else {
		Wr_reg_bits(DI_IF1_GEN_REG2,  0, 2, 2);
		Wr_reg_bits(VD1_IF0_GEN_REG2, 0, 2, 4);
		Wr_reg_bits(VD2_IF0_GEN_REG2, 0, 2, 4);
		#ifdef MCDI_SUPPORT
		Wr_reg_bits(MCDI_MCVECRD_X, 0, 30, 1);
		Wr_reg_bits(MCDI_MCVECRD_Y, 0, 30, 1);
		Wr_reg_bits(MCDI_MC_CRTL, 1, 8, 1);
		#endif
	}
#endif
}
void di_post_read_reverse_irq(bool reverse)
{
#ifdef NEW_DI_TV
	if (reverse) {
		VSYNC_WR_MPEG_REG_BITS(DI_IF1_GEN_REG2,    3, 2, 2);
		VSYNC_WR_MPEG_REG_BITS(VD1_IF0_GEN_REG2, 0xf, 2, 4);
		VSYNC_WR_MPEG_REG_BITS(VD2_IF0_GEN_REG2, 0xf, 2, 4);
		VSYNC_WR_MPEG_REG_BITS(DI_MTNRD_CTRL, 0xf, 17, 4);
		#ifdef MCDI_SUPPORT
		/* motion vector read reverse*/
		VSYNC_WR_MPEG_REG_BITS(MCDI_MCVECRD_X, 1, 30, 1);
		VSYNC_WR_MPEG_REG_BITS(MCDI_MCVECRD_Y, 1, 30, 1);
		VSYNC_WR_MPEG_REG_BITS(MCDI_MC_CRTL, 0, 8, 1);
		#endif
	} else {
		VSYNC_WR_MPEG_REG_BITS(DI_IF1_GEN_REG2,  0, 2, 2);
		VSYNC_WR_MPEG_REG_BITS(VD1_IF0_GEN_REG2, 0, 2, 4);
		VSYNC_WR_MPEG_REG_BITS(VD2_IF0_GEN_REG2, 0, 2, 4);
		VSYNC_WR_MPEG_REG_BITS(DI_MTNRD_CTRL, 0, 17, 4);
		#ifdef MCDI_SUPPORT
		VSYNC_WR_MPEG_REG_BITS(MCDI_MCVECRD_X, 0, 30, 1);
		VSYNC_WR_MPEG_REG_BITS(MCDI_MCVECRD_Y, 0, 30, 1);
		VSYNC_WR_MPEG_REG_BITS(MCDI_MC_CRTL, 1, 8, 1);
		#endif
	}
#endif
}

static unsigned char pre_power_on;
static unsigned char post_power_on;
void di_set_power_control(unsigned char type, unsigned char enable)
{
	if (di_debug_flag&0x20)
		return;
	if (type == 0) {
		/* WRITE_MPEG_REG_BITS(HHI_VPU_MEM_PD_REG0,
enable?0:3, 26, 2); //di pre */
		switch_vpu_mem_pd_vmod(VPU_DI_PRE,
enable?VPU_MEM_POWER_ON:VPU_MEM_POWER_DOWN);
		pre_power_on = enable;
	}
	if (type == 1)
		post_power_on = enable;
}

unsigned char di_get_power_control(unsigned char type)
{
	if (type == 0) {
		return pre_power_on;
	} else {
#if 0
/* let video.c handle it */
		return 1;
#else
		return post_power_on;
#endif
	}

}

static void di_nr_init(void)
{
	Wr(DI_NR_CTRL0, 0xc60c0804);
	Wr(DI_NR_CTRL1, 0x403e3c3a);
	Wr(DI_NR_CTRL2, 0x08010a01);
	Wr(NR2_MET_NM_CCTRL, 0x45056410);
	Wr(NR2_MATNR_SNR_NRM_GAIN, 0x4);
	Wr(NR2_MATNR_SNR_LPF_CFG, 0xc2b64);
	Wr(NR2_MATNR_SNR_EDGE2B, 0xcff08);
	Wr(NR2_MATNR_YBETA_SCL, 0x00ff2000);
	Wr(NR2_MATNR_MTN_CRTL2, 0x32020);
	Wr(NR2_MATNR_MTN_COR, 0x3333);
	Wr(NR2_MATNR_DEGHOST, 0x133);
	Wr(NR2_MATNR_ALPHALP_LUT1, 0x80805040);
	Wr(NR2_MATNR_ALPHALP_LUT2, 0x90808080);
	Wr(NR2_MATNR_ALPHALP_LUT3, 0xffe0c0a4);
	Wr(NR2_MATNR_ALPHAHP_LUT1, 0x80805040);
	Wr(NR2_MATNR_ALPHAHP_LUT2, 0x90808080);
	Wr(NR2_MATNR_ALPHAHP_LUT3, 0xffe0c0a4);
	Wr(NR3_MODE, 0x3);
	Wr(NR3_COOP_PARA, 0x28ff00);
	Wr(NR3_CNOOP_GAIN, 0x881900);
	Wr(NR3_YMOT_PARA, 0x0c0a1e);
	Wr(NR3_CMOT_PARA, 0x08140f);
	Wr(NR3_SUREMOT_YGAIN, 0x100c4014);
	Wr(NR3_SUREMOT_CGAIN, 0x22264014);
}
