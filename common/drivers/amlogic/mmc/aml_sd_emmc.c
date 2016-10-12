/*
 * drivers/amlogic/mmc/aml_sdhc.c
 *
 * SDHC Driver
 *
 * Copyright (C) 2010 Amlogic, Inc.
*/
#include <linux/module.h>
#include <linux/debugfs.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/platform_device.h>
#include <linux/timer.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/io.h>
#include <linux/of_irq.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/highmem.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/irq.h>
#include <linux/amlogic/sd.h>
#include <linux/amlogic/iomap.h>
#include <linux/amlogic/cpu_version.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/mmc/emmc_partitions.h>
#include <../drivers/mmc/core/mmc_ops.h>
#include "amlsd.h"

#ifdef SD_EMMC_ENABLE_TIMEOUT
static unsigned int sd_emmc_error_flag;
#endif
/* static unsigned int sd_emmc_debug_flag;*/
/* static unsigned int sd_emmc_err_bak; */
#ifdef SD_EMMC_DATA_TASKLET
struct tasklet_struct sd_emmc_finish_tasklet;
#endif
#ifdef CALIBRATION
u32 sd_emmc_version[2] = {0x2015, 0x1001};
#endif
static void aml_sd_emmc_set_clk_rate(struct mmc_host *mmc,
					unsigned int clk_ios);
/*for multi host claim host*/
static struct mmc_claim aml_sd_emmc_claim;

/* void aml_sd_emmc_send_stop(struct amlsd_host *host); */

/* static void aml_sd_emmc_clk_switch( */
/* struct amlsd_platform *pdata, int clk_div, int clk_src_sel); */
/* static void aml_sd_emmc_set_clk_rate(*/
/*struct mmc_host *mmc, unsigned int clk_ios); */
struct amlsd_host *host_emmc = NULL;
struct class *emmc_class = NULL;
static unsigned int log2i(unsigned int val)
{
	unsigned int ret = -1;
	while (val != 0) {
		val >>= 1;
		ret++;
	}
	return ret;
}

/*
static void  sd_emmc_debug_status(struct amlsd_host *host)
{
	switch (sd_emmc_debug_flag) {
	case 1:
		host->status = HOST_RSP_CRC_ERR;
		sd_emmc_err("Force host->status:%d here\n", host->status);
		break;
	default:
		break;
	}
	//only enable once for debug
	sd_emmc_debug_flag = 0;
}*/

struct aml_tuning_data {
	const u8 *blk_pattern;
	unsigned int blksz;
};


#ifdef CALIBRATION
u32 checksum_cali(u32 *buffer, u32 length)
{
	u32 sum = 0, *i = buffer;
	buffer += length;
	for (; i < buffer; sum += *(i++))
		;
	return sum;
}
#endif

#ifdef CALIBRATION
u8 line_x, cal_time;
u8 dly_tmp;
u8 max_index;


#define MAX_CALI_RETRY	(3)
#define MAX_DELAY_CNT	(16)
#define CALI_BLK_CNT	(10)

static int is_larger(u8 value, u8 base, u8 wrap)
{
	int ret = 0;
	if ((value > base) || ((value < base) && (base == wrap)))
		ret = 1;
	return ret;
}

static void find_base(struct amlsd_platform *pdata, u8 *is_base_index,
		u8 fir_base[2][2], u8 *first_base_temp_num, u32 base_index_val,
			u8 *calout_cmp_num)
{
	u8 first_base_temp;
	if (pdata->calout[dly_tmp][cal_time] != 0xFF) {
		/* calculate base index! */
		if (*is_base_index == 1) {
			first_base_temp = pdata->calout[dly_tmp][cal_time];
			*first_base_temp_num = *first_base_temp_num + 1;
			if (*first_base_temp_num == 1) {
				fir_base[0][0] = first_base_temp;
				fir_base[0][1] = fir_base[0][1] + 1;
			} else {
					if (first_base_temp == fir_base[0][0])
						fir_base[0][1]++;
					else {
						fir_base[1][0] =
							first_base_temp;
						fir_base[1][1]++;
					}
			}
			/* get a higher index, add the counter! */
		} else if (is_larger(pdata->calout[dly_tmp][cal_time],
				base_index_val, max_index))
					*calout_cmp_num = *calout_cmp_num + 1;
	} else {
		/*todo, if we do not capture a valid value,
			HIGHLIGHT(cal_time = 0) may cause error!!! */
			pr_err("!!!Do not capture a valid index");
			pr_err("@ line %d on capture %d\n",
						line_x, cal_time);
	}
}
static int aml_sd_emmc_execute_tuning_index(struct mmc_host *mmc, u32 opcode,
		struct aml_tuning_data *tuning_data, u32 *adj_win_start)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	struct mmc_request mrq = {NULL};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};
	struct scatterlist sg;
	u32 line_delay;
	struct sd_emmc_delay *line_dly = (struct sd_emmc_delay *)&line_delay;
	u32 adjust = sd_emmc_regs->gadjust;
	struct sd_emmc_adjust *gadjust = (struct sd_emmc_adjust *)&adjust;
	u32 base_index[10] = {0};
	u32 base_index_val = 0;
	u32 base_index_min = max_index + 1;
	u32 base_index_max = 0;
	u32 blksz = 512;
	u8 *blk_test;
	u32 max_cal_result = 0;
	u32 min_cal_result = 10000;	/*2 circles*/
	u32 cal_result[8];
	u8 done;
	u8 is_base_index;
	u8 cal_per_line_num = 8;
	u8 calout_cmp_num = 0;
	u32 cali_retry = 0;
	u8 bus_width = 8;
	u8 fir_base[2][2] = { {0} };
	/*u8 first_base_temp;*/
	u8 first_base_temp_num = 0;
	u8 ln_delay[8] = {0};
	u8 delay_step;
#if 0
	int i;
#endif
	if (get_cpu_type() == MESON_CPU_MAJOR_ID_GXBB)
		delay_step = 125;
	else
		delay_step = 200;
	sd_emmc_regs->gdelay = 0;
	line_delay = sd_emmc_regs->gdelay;
	blk_test = kmalloc(blksz * CALI_BLK_CNT, GFP_KERNEL);
	if (!blk_test)
		return -ENOMEM;

	if (mmc->ios.bus_width == 0)
		bus_width = 1;
	else if (mmc->ios.bus_width == 2)
		bus_width = 4;
	else
		bus_width = 8;
	max_index = (sd_emmc_regs->gclock & 0x3f) - 1;

_cali_retry:
	max_cal_result = 0;
	min_cal_result = 10000;
	base_index_min = max_index + 1;
	base_index_max = 0;
	pr_info("%s: trying cali %d-th time(s)\n",
				mmc_hostname(mmc), cali_retry);
	host->is_tunning = 1;
	/* for each line */
	for (line_x = 0; line_x < bus_width; line_x++) {
		done = 0;
		base_index_val = 0;
		is_base_index = 1;
		memset(pdata->calout, 0xFF, 20 * 20);
		first_base_temp_num = 0;
		fir_base[0][0] = 0;
		fir_base[0][1] = 0;
		fir_base[1][0] = 0;
		fir_base[1][1] = 0;
		/* for each delay index! */
		for (dly_tmp = 0; dly_tmp < MAX_DELAY_CNT; dly_tmp++) {
			sd_emmc_regs->gdelay = 0;
			switch (line_x) {
			case 0:
				line_dly->dat0 = dly_tmp;
				break;
			case 1:
				line_dly->dat1 = dly_tmp;
				break;
			case 2:
				line_dly->dat2 = dly_tmp;
				break;
			case 3:
				line_dly->dat3 = dly_tmp;
				break;
			case 4:
				line_dly->dat4 = dly_tmp;
				break;
			case 5:
				line_dly->dat5 = dly_tmp;
				break;
			case 6:
				line_dly->dat6 = dly_tmp;
				break;
			case 7:
				line_dly->dat7 = dly_tmp;
				break;
			default:
				break;
			}
			sd_emmc_regs->gdelay = line_delay;
			calout_cmp_num = 0;
			/* cal_time */
			for (cal_time = 0; cal_time < cal_per_line_num;
					cal_time++) {
				/* send read cmd. */
				pdata->caling = 1;
				cmd.opcode = opcode;
				cmd.arg = 0;
				cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

				stop.opcode = MMC_STOP_TRANSMISSION;
				stop.arg = 0;
				stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

				data.blksz = blksz;
				data.blocks = CALI_BLK_CNT;
				data.flags = MMC_DATA_READ;
				data.sg = &sg;
				data.sg_len = 1;

				memset(blk_test, 0, blksz*CALI_BLK_CNT);
				sg_init_one(&sg, blk_test, blksz*CALI_BLK_CNT);

				mrq.cmd = &cmd;
				mrq.stop = &stop;
				mrq.data = &data;
				host->mrq = &mrq;
				/*enable cali*/
				sd_emmc_regs->gadjust = adjust;
				mmc_wait_for_req(mmc, &mrq);
				pdata->calout[dly_tmp][cal_time] =
						sd_emmc_regs->gcalout & 0x3f;
				pdata->caling = 0;
				gadjust->cali_enable = 0;
				gadjust->cali_sel = 0;
				sd_emmc_regs->gadjust = adjust;
				/*get a valid index*/
				find_base(pdata, &is_base_index,
				fir_base, &first_base_temp_num,
				base_index_val, &calout_cmp_num);
			}	/* endof cal_time loop... */
			/* get base index value */
			/* if ((base_index_val > 0) && (is_base_index == 1)) {*/
			if (is_base_index == 1) {
				is_base_index = 0;
				if (fir_base[1][1] > fir_base[0][1])
					base_index_val = fir_base[1][0];
				else
					base_index_val = fir_base[0][0];
				/*base_index_val = valid_base_index; */
				if (base_index_val < base_index_min)
					base_index_min = base_index_val;
				if (base_index_val > base_index_max)
					base_index_max = base_index_val;
				base_index[line_x] = base_index_val;
			/*pr_err("get base index %d value @ line (%d)\n",
					base_index_val, line_x);*/
			} else if (calout_cmp_num == cal_per_line_num) {
				break;
			}
		}  /* endof dly_tmp loop... */
		/* get a valid index on current line! */
		if ((dly_tmp == MAX_DELAY_CNT)
				&& (calout_cmp_num != cal_per_line_num)) {
			/* Do not get a valid line delay index value! */
			if (cali_retry < MAX_CALI_RETRY) {
				pr_err("Do't get valid ln_delay @ line %d, try\n",
					line_x);
				cali_retry++;
				goto _cali_retry;
			} else {
				kfree(blk_test);
				blk_test = NULL;
				pr_info("%s: calibration failed, use default\n",
					mmc_hostname(host->mmc));
				return -1;
			}
		} else {
			ln_delay[line_x] = dly_tmp;
		}
		/*debug code...*/
#if 0

		for (i = 0; i < 16; i++) {
			pr_info("%02x %02x %02x %02x %02x %02x %02x %02x\n",
					pdata->calout[i][0],
					pdata->calout[i][1],
					pdata->calout[i][2],
					pdata->calout[i][3],
					pdata->calout[i][4],
					pdata->calout[i][5],
					pdata->calout[i][6],
					pdata->calout[i][7]);
		}
#endif
	}
	host->is_tunning = 0;

	/* if base index wrap, fix */
	for (line_x = 0; line_x < bus_width; line_x++) {
		/* 1000 means index is 1ns */
		/* make sure no neg-value  for ln_delay*/
		if (ln_delay[line_x]*delay_step > 1000)
			ln_delay[line_x] = 1000 / delay_step;

		if (base_index[line_x] == max_index) {
			cal_result[line_x] = ((max_index+1))*1000 -
				ln_delay[line_x]*delay_step;
		} else if ((base_index_max == max_index) &&
			(base_index[line_x] != (max_index - 1)) &&
			(base_index[line_x] != (max_index - 2))) {
			cal_result[line_x] = ((base_index[line_x]+1)%
			(max_index+1) + (max_index+1))*1000 -
				ln_delay[line_x]*delay_step;
		} else {
			cal_result[line_x] = (base_index[line_x]+1)%
				(max_index+1) * 1000 -
					ln_delay[line_x]*delay_step;
		}
		max_cal_result = (max_cal_result < cal_result[line_x])
				? cal_result[line_x] : max_cal_result;
		min_cal_result = (min_cal_result > cal_result[line_x])
				? cal_result[line_x] : min_cal_result;
		pr_err("%s: delay[%d]=%5d padding=%2d, bidx=%d\n",
				mmc_hostname(mmc), line_x, cal_result[line_x],
				ln_delay[line_x], base_index[line_x]);
	}
	pr_info("%s: calibration result @ %d: max(%d), min(%d)\n",
		mmc_hostname(mmc), cali_retry, max_cal_result, min_cal_result);
	/* retry cali here! */
	if ((max_cal_result - min_cal_result) >= 2000) {
		if (cali_retry < MAX_CALI_RETRY) {
			cali_retry++;
			goto _cali_retry;
		} else {
			kfree(blk_test);
			blk_test = NULL;
			pr_info("%s: calibration failed, use default\n",
				mmc_hostname(host->mmc));
			return -1;
		}
	}

	/* swap base_index_max */
	if ((base_index_max == max_index) && (base_index_min == 0))
		base_index_max = 0;
	if (max_cal_result < (base_index_max * 1000))
		max_cal_result = (base_index_max * 1000);
	/* calculate each line delay we should use! */
	line_dly->dat0 = (((max_cal_result - cal_result[0]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[0]) / delay_step);
	line_dly->dat1 = (((max_cal_result - cal_result[1]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[1]) / delay_step);
	line_dly->dat2 = (((max_cal_result - cal_result[2]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[2]) / delay_step);
	line_dly->dat3 = (((max_cal_result - cal_result[3]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[3]) / delay_step);
	line_dly->dat4 = (((max_cal_result - cal_result[4]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[4]) / delay_step);
	line_dly->dat5 = (((max_cal_result - cal_result[5]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[5]) / delay_step);
	line_dly->dat6 = (((max_cal_result - cal_result[6]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[6]) / delay_step);
	line_dly->dat7 = (((max_cal_result - cal_result[7]) / delay_step)
			> 15) ? 15 :
			((max_cal_result - cal_result[7]) / delay_step);
	/* set default cmd delay*/
	if (get_cpu_type() == MESON_CPU_MAJOR_ID_GXBB)
		gadjust->cmd_delay = 7;

	pr_err("%s: line_delay =0x%x, max_cal_result =%d\n",
		mmc_hostname(mmc), line_delay, max_cal_result);
	sd_emmc_regs->gadjust = adjust;
	/* set delay count into reg*/
	sd_emmc_regs->gdelay = line_delay;

	pr_err("%s: base_index_max %d, base_index_min %d\n",
		mmc_hostname(mmc), base_index_max, base_index_min);

	/* get adjust point! */
	*adj_win_start = base_index_max + 2;

	kfree(blk_test);
	blk_test = NULL;
	return 0;
}
#endif


/* TODO....., based on new tuning function */
static int aml_sd_emmc_execute_tuning_(struct mmc_host *mmc, u32 opcode,
					struct aml_tuning_data *tuning_data,
					u32 adj_win_start)
{
#if 1 /* need finish later */
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 vclk;
	struct sd_emmc_clock *clkc = (struct sd_emmc_clock *)&(vclk);
	u32 vctrl;
	struct sd_emmc_config *ctrl = (struct sd_emmc_config *)&vctrl;
	u32 adjust = sd_emmc_regs->gadjust;
	struct sd_emmc_adjust *gadjust = (struct sd_emmc_adjust *)&adjust;
	u32 clk_rate = 1000000000;
	const u8 *blk_pattern = tuning_data->blk_pattern;
	unsigned int blksz = tuning_data->blksz;
	unsigned long flags;
	int ret = 0;
	struct mmc_request mrq = {NULL};
	struct mmc_command cmd = {0};
	struct mmc_command stop = {0};
	struct mmc_data data = {0};
	struct scatterlist sg;
	int n, nmatch, ntries = 10;
	int rx_phase = 0;
	int adj_delay = 0;
	struct aml_emmc_adjust *emmc_adj = &host->emmc_adj;
	u8 *blk_test;
	u32 clock, clk_div;
	u32 adj_delay_find;
	u32 rx_tuning_result[20] = {0};
	int wrap_win_start = -1, wrap_win_size = 0;
	int best_win_start = -1, best_win_size = 0;
	int curr_win_start = -1, curr_win_size = 0;
	sd_emmc_regs->gadjust = 0;
tunning:
	spin_lock_irqsave(&host->mrq_lock, flags);
	pdata->need_retuning = false;
	spin_unlock_irqrestore(&host->mrq_lock, flags);
	vclk = sd_emmc_regs->gclock;
	vctrl = sd_emmc_regs->gcfg;
	clk_div = clkc->div;
	clock = clk_rate / clk_div;/*200mhz, bus_clk */
	pdata->mmc->actual_clock = ctrl->ddr ?
				(clock / 2) : clock;/*100mhz in ddr */

	if (ctrl->ddr == 1)
		blksz = 512;
	blk_test = kmalloc(blksz, GFP_KERNEL);
	if (!blk_test)
		return -ENOMEM;

	host->is_tunning = 1;
	pr_info("%s: clk %d %s tuning start\n",
		mmc_hostname(mmc), (ctrl->ddr ? (clock / 2) : clock),
			(ctrl->ddr ? "DDR mode" : "SDR mode"));
	for (adj_delay = 0; adj_delay < clk_div; adj_delay++) {
		gadjust->adj_delay = adj_delay;
		gadjust->adj_enable = 1;
		gadjust->cali_enable = 0;
		gadjust->cali_rise = 0;
		sd_emmc_regs->gadjust = adjust;
		/* try ntries */
		for (n = 0, nmatch = 0; n < ntries; n++) {
			if (ctrl->ddr == 1)
				cmd.opcode = 17;
			else
				cmd.opcode = opcode;
			cmd.arg = 0;
			cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

			stop.opcode = MMC_STOP_TRANSMISSION;
			stop.arg = 0;
			stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

			data.blksz = blksz;
			data.blocks = 1;
			data.flags = MMC_DATA_READ;
			data.sg = &sg;
			data.sg_len = 1;

			memset(blk_test, 0, blksz);
			sg_init_one(&sg, blk_test, blksz);

			mrq.cmd = &cmd;
			mrq.stop = &stop;
			mrq.data = &data;
			host->mrq = &mrq;
			mmc_wait_for_req(mmc, &mrq);
			if (!cmd.error && !data.error) {
				if (ctrl->ddr == 1)
					nmatch++;
				else if (!memcmp(blk_pattern,
						blk_test, blksz))
						nmatch++;
				else {
					sd_emmc_dbg(AMLSD_DBG_TUNING,
						"mismatch: rx_phase=%d ",
							rx_phase);
						sd_emmc_dbg(AMLSD_DBG_TUNING,
						"adj_delay=%d nmatch=%d\n",
							adj_delay, nmatch);
						break;
					}
			} else {
					sd_emmc_dbg(AMLSD_DBG_TUNING,
						"Tuning transfer error:");
					sd_emmc_dbg(AMLSD_DBG_TUNING,
						"rx_phase=%d adj_delay=%d\n",
							rx_phase, adj_delay);
					sd_emmc_dbg(AMLSD_DBG_TUNING,
				       "nmatch=%d cmd.error=%d data.error=%d\n",
						nmatch, cmd.error, data.error);
					break;
			}
		}

		rx_tuning_result[adj_delay] = nmatch;
		/*get a ok adjust point!*/
		if (nmatch == ntries) {
			if (adj_delay == 0)
				wrap_win_start = adj_delay;

			if (wrap_win_start >= 0)
				wrap_win_size++;

			if (curr_win_start < 0)
				curr_win_start = adj_delay;

			curr_win_size++;
			pr_info("%s: rx_tuning_result[%d] = %d\n",
				mmc_hostname(host->mmc), adj_delay, nmatch);
		} else {
			if (curr_win_start >= 0) {
				if (best_win_start < 0) {
					best_win_start = curr_win_start;
					best_win_size = curr_win_size;
				} else {
					if (best_win_size < curr_win_size) {
						best_win_start = curr_win_start;
						best_win_size = curr_win_size;
					}
				}

				wrap_win_start = -1;
				curr_win_start = -1;
				curr_win_size = 0;
			}
		}

	}
	/* last point is ok! */
	if (curr_win_start >= 0) {
		if (best_win_start < 0) {
			best_win_start = curr_win_start;
			best_win_size = curr_win_size;
		} else if (wrap_win_size > 0) {
			/* Wrap around case */
			if (curr_win_size + wrap_win_size > best_win_size) {
				best_win_start = curr_win_start;
				best_win_size = curr_win_size + wrap_win_size;
			}
		} else if (best_win_size < curr_win_size) {
			best_win_start = curr_win_start;
			best_win_size = curr_win_size;
		}

		curr_win_start = -1;
		curr_win_size = 0;
	}
	if (best_win_size <= 0) {
		clkc->div += 1;
		sd_emmc_regs->gclock = vclk;
		pdata->clkc = sd_emmc_regs->gclock;
		pr_info("%s: tuning failed, reduce freq and retuning\n",
			mmc_hostname(host->mmc));
		goto tunning;
	} else {
		pr_info("%s: best_win_start =%d, best_win_size =%d\n",
			mmc_hostname(host->mmc), best_win_start, best_win_size);
	}

	if (best_win_size == clk_div)
		adj_delay_find = 0;
	else {
		adj_delay_find = best_win_start + (best_win_size - 1) / 2;
		adj_delay_find = adj_delay_find % clk_div;
	}
	/* fixme, for retry debug. */
	if (aml_card_type_mmc(pdata)
		&& (clk_div <= 5) && (adj_win_start != 100)
		&& (get_cpu_type() == MESON_CPU_MAJOR_ID_GXBB)) {
		pr_info("%s: adj_win_start %d\n",
			mmc_hostname(host->mmc), adj_win_start);
		adj_delay_find = adj_win_start % clk_div;
	}
	gadjust->adj_delay = adj_delay_find;
	gadjust->adj_enable = 1;
	gadjust->cali_enable = 0;
	gadjust->cali_rise = 0;
	sd_emmc_regs->gadjust = adjust;
	host->is_tunning = 0;

	/* fixme, yyh for retry flow. */
	emmc_adj->adj_win_start = best_win_start;
	emmc_adj->adj_win_len = best_win_size;
	emmc_adj->adj_point = adj_delay_find;
	emmc_adj->clk_div = clk_div;
	pr_info("%s: sd_emmc_regs->gclock=0x%x,sd_emmc_regs->gadjust=0x%x\n",
			mmc_hostname(host->mmc), sd_emmc_regs->gclock,
			sd_emmc_regs->gadjust);
	kfree(blk_test);
	/* do not dynamical tuning for no emmc device */
	if ((pdata->is_in) && !aml_card_type_mmc(pdata))
		schedule_delayed_work(&pdata->retuning, 15*HZ);
	return ret;
#endif
	return 0;
}

static int aml_sd_emmc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	struct aml_tuning_data tuning_data;
	u32 vclk;
	struct sd_emmc_clock *clkc = (struct sd_emmc_clock *)&(vclk);
	int err = -ENOSYS;
	u32 adj_win_start = 100;
	int ret;
	if (opcode == MMC_SEND_TUNING_BLOCK_HS200) {
		if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
			tuning_data.blk_pattern = tuning_blk_pattern_8bit;
			tuning_data.blksz = sizeof(tuning_blk_pattern_8bit);
		} else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
			tuning_data.blk_pattern = tuning_blk_pattern_4bit;
			tuning_data.blksz = sizeof(tuning_blk_pattern_4bit);
		} else {
			return -EINVAL;
		}
	} else if (opcode == MMC_SEND_TUNING_BLOCK) {
		tuning_data.blk_pattern = tuning_blk_pattern_4bit;
		tuning_data.blksz = sizeof(tuning_blk_pattern_4bit);
	} else {
		sd_emmc_err("Undefined command(%d) for tuning\n", opcode);
		return -EINVAL;
	}
	vclk = sd_emmc_regs->gclock;

#ifdef CALIBRATION
	if ((aml_card_type_mmc(pdata))
		&& (pdata->need_cali == 1) && (clkc->div <= 5)) {
		pdata->need_cali = 1;
		ret = aml_sd_emmc_execute_tuning_index(mmc, 18,
						&tuning_data, &adj_win_start);
		/* if calibration failed, gdelay use default value */
		if (ret)
			sd_emmc_regs->gdelay = 0x85854055;
	}
#endif
	/* excute tuning... */
	err = aml_sd_emmc_execute_tuning_(mmc, opcode,
				&tuning_data, adj_win_start);

	pr_info("%s: gclock =0x%x, gdelay=0x%x, gadjust=0x%x\n",
		mmc_hostname(mmc), sd_emmc_regs->gclock, sd_emmc_regs->gdelay,
		sd_emmc_regs->gadjust);
	return err;
}

/*soft reset after errors*/
void aml_sd_emmc_host_reset(struct amlsd_host *host)
{

	return;

}

/*setup reg initial value*/
static void aml_sd_emmc_reg_init(struct amlsd_host *host)
{
	u32 vclkc = 0;
	struct sd_emmc_clock *pclkc = (struct sd_emmc_clock *)&vclkc;
	u32 vconf = 0;
	struct sd_emmc_config *pconf = (struct sd_emmc_config *)&vconf;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	pr_info("%s %d\n", __func__, __LINE__);
	/* open gate for hw controller */
	/* switch_mod_gate_by_type(MOD_SD_EMMC, 1); */

	/* Need do controller reset here??? */
	aml_sd_emmc_host_reset(host);

	/* need clear first? */
	vclkc = 0;

	pclkc->div = 60;	 /* 400KHz */
	pclkc->src = 0;	  /* 0: Crystal 24MHz */
	pclkc->core_phase = 2;	  /* 2: 180 phase */
	pclkc->always_on = 1;	  /* Keep clock always on */

	sd_emmc_regs->gclock = vclkc;
	/* need clear first?? */
	vconf = 0;
	/* 1bit mode */
	pconf->bus_width = 0;
	/* 512byte block length */
	pconf->bl_len = 9;
	/* 64 CLK cycle, here 2^8 = 256 clk cycles */
	pconf->resp_timeout = 8;
	/* 1024 CLK cycle, Max. 100mS. */
	pconf->rc_cc = 4;
	pconf->err_abort = 0;

	sd_emmc_regs->gcfg = vconf;
	/*Clear irq status first*/

#ifdef SD_EMMC_IRQ_EN_ALL_INIT
	/*Set Irq Control*/
	sd_emmc_regs->girq_en = SD_EMMC_IRQ_ALL;
	sd_emmc_regs->gstatus = 0xffff;


#endif

}

/*wait for controller desc done*/
int aml_sd_emmc_wait_ready(struct amlsd_host *host, u32 timeout)
{
#if 0
	u32 vstat = 0;
	struct sd_emmc_status *status = (struct sd_emmc_status *)&vstat;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 i, busy;

	/* check cmd busy here */
	for (i = 0; i < timeout; i++) {
		/* vstat = readl(host->base+SD_EMMC_STAT); */
		vstat = sd_emmc_regs->gstatus;
#if 1
		busy = status->desc_wr_rdy;
#else
		busy = status->cmd_i;
#endif
		if (busy)
			return 0;

		if (i)
			sd_emmc_err(
			"SD_EMMC_STAT=%#x, sd_emmc controller is busy. i:%d\n",
			vstat, i);
		udelay(1);
	}

	sd_emmc_err("SD_EMMC_STAT=%#x, sd_emmc controller is busy.\n", vstat);

	return -1;
#else
	return 0;
#endif
}

static void __attribute__((unused))aml_sd_emmc_mrq_print_info(
	struct mmc_request *mrq, unsigned desc_cnt)
{
	pr_info("*mmc_request desc_cnt:%d cmd:%d, arg:0x%x, flags:0x%x",
	desc_cnt, mrq->cmd->opcode, mrq->cmd->arg, mrq->cmd->flags);

	if (mrq->cmd->data)
		pr_info(", blksz:%d, blocks:0x%x",
			mrq->data->blksz, mrq->data->blocks);

	pr_info("\n");

}

static void __attribute__((unused))
	aml_sd_emmc_desc_print_info(struct sd_emmc_desc_info *desc_info)
{
	struct cmd_cfg *des_cmd_cur =
		(struct cmd_cfg *)&(desc_info->cmd_info);

	pr_info("#####desc_info check, desc_info:0x%p\n",
		desc_info);

	pr_info("\tlength:%d\n", des_cmd_cur->length);
	pr_info("\tblock_mode:%d\n", des_cmd_cur->block_mode);
	pr_info("\tr1b:%d\n", des_cmd_cur->r1b);
	pr_info("\tend_of_chain:%d\n", des_cmd_cur->end_of_chain);
	pr_info("\ttimeout:%d\n", des_cmd_cur->timeout);
	pr_info("\tno_resp:%d\n", des_cmd_cur->no_resp);
	pr_info("\tno_cmd:%d\n", des_cmd_cur->no_cmd);
	pr_info("\tdata_io:%d\n", des_cmd_cur->data_io);
	pr_info("\tdata_wr:%d\n", des_cmd_cur->data_wr);
	pr_info("\tresp_nocrc:%d\n", des_cmd_cur->resp_nocrc);
	pr_info("\tresp_128:%d\n", des_cmd_cur->resp_128);
	pr_info("\tresp_num:%d\n", des_cmd_cur->resp_num);
	pr_info("\tdata_num:%d\n", des_cmd_cur->data_num);
	pr_info("\tcmd_index:%d\n", des_cmd_cur->cmd_index);
	pr_info("\tcmd_arg:0x%x\n", desc_info->cmd_arg);
	pr_info("\tdata_addr:0x%x\n", desc_info->data_addr);
	pr_info("\tresp_addr:0x%x\n", desc_info->resp_addr);

}


/*
static int aml_sd_emmc_desc_check(struct amlsd_host* host)
{
	struct sd_emmc_desc_info* desc_info =
		(struct sd_emmc_desc_info*)host->desc_buf;
	struct cmd_cfg *des_cmd_cur = NULL;
	int i, ret = 0;

	for(i=0; i<(SD_EMMC_MAX_DESC_MUN>>2); i++){
	des_cmd_cur = (struct cmd_cfg *)&(desc_info->cmd_info);
	if(des_cmd_cur->owner == 1){
		pr_info("Desc %d cmd%d check error,
			des_cmd_cur->owner have not been cleared yet!!!\n",
			i, des_cmd_cur->cmd_index);
		ret = -1;
		break;
	}
	if(des_cmd_cur->error == 1){
		pr_info("Desc %d cmd%d check error,
		des_cmd_cur->error detected!!!\n",
			i, des_cmd_cur->cmd_index);
		ret = -2;
		break;
	}

	desc_info++;
	}

	return ret;
}
*/

/*
read response (136bit or 48bit)
136bit: SRAM [498~511]
48bit: DESC response addr
*/
static int aml_sd_emmc_read_response(
	struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	struct sd_emmc_desc_info *desc_info =
		(struct sd_emmc_desc_info *)host->desc_buf;
	struct cmd_cfg *des_cmd_cur = NULL;
	int i;

	u32 *presp = (u32 *)cmd->resp;

	/* pr_info("%s %d cmd:%d, arg:0x%x\n",
		__func__, __LINE__, cmd->opcode, cmd->arg); */

	for (i = 0; i < (SD_EMMC_MAX_DESC_MUN>>2); i++) {
		des_cmd_cur = (struct cmd_cfg *)&(desc_info->cmd_info);
		if (des_cmd_cur->cmd_index == cmd->opcode) {
			/* pr_info("no_resp:%d resp_num:%d
			r1b:%d resp_128:%d\n", */
			/* des_cmd_cur->no_resp, des_cmd_cur->resp_num,
			des_cmd_cur->r1b, des_cmd_cur->resp_128); */
			break;
		}
		desc_info++;
	}
	if (cmd->flags & MMC_RSP_136) /*136 bit*/{
		presp[0] = sd_emmc_regs->gcmd_rsp3;
		presp[1] = sd_emmc_regs->gcmd_rsp2;
		presp[2] = sd_emmc_regs->gcmd_rsp1;
		presp[3] = sd_emmc_regs->gcmd_rsp0;
		/* pr_info("presp[0] = 0x%x\n", presp[0]); */
		/* pr_info("presp[1] = 0x%x\n", presp[1]); */
		/* pr_info("presp[2] = 0x%x\n", presp[2]); */
		/* pr_info("presp[3] = 0x%x\n", presp[3]); */
	} else if (cmd->flags & MMC_RSP_PRESENT) {
		/*48 bit*/
		presp[0] = desc_info->resp_addr;
		/* pr_info("presp[0] = 0x%x\n", presp[0]); */
		/* pr_info("##Cmd %d ,Resp 0x%x\n", cmd->opcode, presp[0]); */
	}

	return 0;
}

/*enable irq bit in reg*/
inline void aml_sd_emmc_enable_imask(struct amlsd_host *host, u32 irq)
{
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	u32 ictl = sd_emmc_regs->girq_en;

	ictl |= irq;
	sd_emmc_regs->girq_en = ictl;
	/* pr_info("%s %d REG:0x%x, Value:0x%x\n",
		__func__, __LINE__, (host->base+SD_EMMC_IRQC), ictl); */
}

/*disable irq bit in reg */
inline void aml_sd_emmc_disable_imask(struct amlsd_host *host, u32 irq)
{
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 ictl = sd_emmc_regs->girq_en;

	ictl &= ~irq;
	sd_emmc_regs->girq_en = ictl;
	/* pr_info("%s %d REG:0x%x, Value:0x%x\n",
		__func__, __LINE__, (host->base+SD_EMMC_IRQC), ictl); */
}

#ifdef SD_EMMC_REQ_DMA_SGMAP
static char *aml_sd_emmc_kmap_atomic(
		struct scatterlist *sg, unsigned long *flags)
{
	local_irq_save(*flags);
	return kmap_atomic(sg_page(sg)) + sg->offset;
}

static void aml_sd_emmc_kunmap_atomic(
		void *buffer, unsigned long *flags)
{
	kunmap_atomic(buffer);
	local_irq_restore(*flags);
}

/**
 * aml_sg_copy_buffer - Copy data between
 *a linear buffer and an SG list  for amlogic,
 * We don't disable irq in this function
 **/

static unsigned aml_sd_emmc_pre_dma(struct amlsd_host *host,
	struct mmc_request *mrq, struct sd_emmc_desc_info *desc)
{
	struct mmc_data *data = NULL;
	struct scatterlist *sg;
	struct sd_emmc_desc_info *desc_cur = NULL;
	struct cmd_cfg *des_cmd_cur = NULL;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	dma_addr_t sg_addr = 0;
	char *buffer = NULL;
	unsigned desc_cnt = 0, i = 0, data_len = 0;
	unsigned data_size = 0, sg_blocks = 0;
	unsigned char direction = 0, data_rw = 0;
	unsigned char block_mode = 0, data_num = 0, bl_len = 0;
	unsigned long flags;

	data = mrq->cmd->data;
	if (data == NULL) {
		WARN_ON(1);
		goto err_exit;
	}

	if (data->flags & MMC_DATA_READ) {
		direction = DMA_FROM_DEVICE;
		data_rw = 0;
	} else{
		direction = DMA_TO_DEVICE;
		data_rw = 1;
	}

#if 1
	host->sg_cnt = dma_map_sg(mmc_dev(host->mmc),
		data->sg, data->sg_len, direction);
#endif
	/*
	 * This only happens when someone fed
	 * us an invalid request.
	 */
	if (host->sg_cnt == 0) {
		WARN_ON(1);
		goto err_exit;
	}
	/* pr_info("%s %d sg_cnt:%d, direction:%d\n",
	 __func__, __LINE__, host->sg_cnt, direction); */

	data_size = (mrq->data->blksz * mrq->data->blocks);
	block_mode = ((mrq->data->blocks > 1)
		|| (mrq->data->blksz >= 512)) ? 1 : 0;

	data_num = 0;/*(data_size > 4) ? 0 : 1;*/
	bl_len = block_mode ? log2i(mrq->data->blksz) : 0;
	host->dma_sts = 0;
	if ((data_size & 0x3) && (host->sg_cnt > 1)) {
		host->dma_sts = (1<<0); /*  */
		pr_info("data:%d and sg_cnt:%d\n", data_size, host->sg_cnt);
	}
#if 0
	pr_info("%s %d sg_cnt:%d, block_mode:%d,\n",
			__func__, __LINE__, host->sg_cnt, block_mode);
	pr_info("data_num:%d bl_len:%d, blocks:%d, blksz:%d\n",
		data_num, bl_len, mrq->data->blocks, mrq->data->blksz);
#endif

	/* prepare desc for data operation */
	desc_cur = desc;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		WARN_ON(sg->length & 0x3);

		des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);
		if (desc_cnt != 0) { /* for first desc, */
			des_cmd_cur->no_resp = 1;
			des_cmd_cur->no_cmd = 1;
		}
		des_cmd_cur->data_io = 1;

		des_cmd_cur->owner = 1;
		des_cmd_cur->timeout = 0xc;

		des_cmd_cur->data_wr = data_rw;
		des_cmd_cur->block_mode = block_mode;
		des_cmd_cur->data_num = data_num;

		data_len = block_mode ?
			(sg_dma_len(sg)>>bl_len) : sg_dma_len(sg);

		if ((data_len > 0x1ff) || (data_len == 0)) {
			pr_info("Error block_mode:%d, data_len:%d, bl_len:%d\n",
				block_mode, data_len, bl_len);
			pr_info("mrq->data->blocks:%d, mrq->data->blksz:%d\n",
				mrq->data->blocks, mrq->data->blksz);
			WARN_ON(1);
		}
		des_cmd_cur->length = data_len;

		sg_blocks += des_cmd_cur->length;
		sg_addr = sg_dma_address(sg);

		if (sg_addr & 0x7) { /* for 64 bit dma mode */
			WARN_ON(host->sg_cnt > 1);

			host->dma_sts |= (1<<1); /*  */
			if (0) { /* use SRAM buffer */
				host->dma_sts |= (1<<2); /*  */

				desc_cur->data_addr = host->dma_gdesc;
				desc_cur->data_addr |= (1<<0);	 /* SRAM */
			} else{
				host->dma_sts |= (1<<3); /*  */
				desc_cur->data_addr = host->bn_dma_buf;
			}

			if (data->flags & MMC_DATA_WRITE) {
				buffer = aml_sd_emmc_kmap_atomic(sg, &flags);
				memcpy((host->dma_sts & (1<<2)) ?
				(void *)(sd_emmc_regs->gdesc) : host->bn_buf,
					buffer, data_size);
				aml_sd_emmc_kunmap_atomic(buffer, &flags);
			}

		} else{
			desc_cur->data_addr = sg_addr;
			/* desc_cur->data_addr &= ~(1<<0);   //DDR */
		}

	/* aml_sd_emmc_desc_print_info(desc_cur); */
		/* pr_info("%s %d desc_cur->data_addr:0x%x,
			des_cmd_cur->length:%d, sg->length:%d,
			sg_dma_len(sg):%d, bl_len:%d\n", */
		/* __func__, __LINE__, desc_cur->data_addr,
		des_cmd_cur->length, sg->length,
			sg_dma_len(sg), bl_len); */

		desc_cur++;
		desc_cnt++;
		memset(desc_cur, 0, sizeof(struct sd_emmc_desc_info));
	}

	WARN_ON(desc_cnt != host->sg_cnt);


err_exit:
	return host->sg_cnt;
}

/**
 * aml_sg_copy_buffer - Copy data between
 *a linear buffer and an SG list  for amlogic,
 * We don't disable irq in this function
 **/
static int aml_sd_emmc_post_dma(struct amlsd_host *host,
		struct mmc_request *mrq)
{
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	struct mmc_data *data = NULL;
	struct scatterlist *sg;
	char *buffer = NULL;
	unsigned long flags;
	int i, ret = 0;


	data = mrq->cmd->data;
	if (data == NULL) {
		WARN_ON(1);
		ret = -1;
		goto err_exit;
	}

	if ((data->flags & MMC_DATA_READ) && (host->dma_sts & (1<<1))) {
		dma_sync_sg_for_cpu(mmc_dev(host->mmc), data->sg,
			data->sg_len, DMA_FROM_DEVICE);

		for_each_sg(data->sg, sg, host->sg_cnt, i) {
			if (sg_dma_address(sg) & 0x7) {
				WARN_ON(!(host->dma_sts & (0x3<<2)));

				buffer = aml_sd_emmc_kmap_atomic(sg, &flags);
				/* WARN_ON(((unsigned long)buffer & PAGE_MASK)
					> (PAGE_SIZE - 0x7)); */
				memcpy(buffer, (host->dma_sts & (1<<2)) ?
				(void *)(sd_emmc_regs->gdesc) : host->bn_buf,
				(mrq->data->blksz * mrq->data->blocks));
				aml_sd_emmc_kunmap_atomic(buffer, &flags);
			}
		}
	}

	dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
		(data->flags & MMC_DATA_READ) ?
			DMA_FROM_DEVICE : DMA_TO_DEVICE);

err_exit:
	return ret;
}
#endif

#ifndef SD_EMMC_REQ_DMA_SGMAP
/*copy buffer from data->sg to
*dma buffer, set dma addr to reg*/
void aml_sd_emmc_prepare_dma(struct amlsd_host *host,
		struct mmc_request *mrq)
{
	struct mmc_data *data = mrq->data;

/* for temp write test */
	if (data->flags & MMC_DATA_WRITE) {
		aml_sg_copy_buffer(data->sg, data->sg_len,
		host->bn_buf, data->blksz*data->blocks, 1);
		sd_emmc_dbg(AMLSD_DBG_WR_DATA, "W Cmd %d, %x-%x-%x-%x\n",
		mrq->cmd->opcode,
		host->bn_buf[0], host->bn_buf[1],
		host->bn_buf[2], host->bn_buf[3]);
	}
}
#endif

static void aml_sd_emmc_clk_switch_off(struct amlsd_host *host)
{
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 vcfg = sd_emmc_regs->gcfg;

	struct sd_emmc_config *conf = (struct sd_emmc_config *)&vcfg;

	if (host->is_gated)
		/* sd_emmc_err("direct return\n"); */
		return;

	/*Turn off Clock, here close whole clk for controller*/
	/* clkc->div = 0; */
	/* writel(vclkc, host->base+SD_EMMC_CLKC); */

	conf->stop_clk = 1;
	/* pr_info("%s %d REG:0x%x, Value:0x%x\n",
		__func__, __LINE__, (host->base+SD_EMMC_CONF), vcfg); */
	sd_emmc_regs->gcfg = vcfg;

	host->is_gated = true;
	/* sd_emmc_err("clock off\n"); */
}

static void aml_sd_emmc_clk_switch_on(
	struct amlsd_platform *pdata, int clk_div, int clk_src_sel)
{
	struct amlsd_host *host = (void *)pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	u32 vclkc = sd_emmc_regs->gclock;
	struct sd_emmc_clock *clkc = (struct sd_emmc_clock *)&vclkc;
	u32 vcfg = sd_emmc_regs->gcfg;
	struct sd_emmc_config *conf = (struct sd_emmc_config *)&vcfg;

	WARN_ON(!clk_div);

	/*Set clock divide*/
	clkc->div = clk_div;
	clkc->src = clk_src_sel;

	sd_emmc_regs->gclock = vclkc;
	/*Turn on Clock*/
	conf->stop_clk = 0;

	sd_emmc_regs->gcfg = vcfg;

	host->is_gated = false;
}

static void aml_sd_emmc_clk_switch(struct amlsd_platform *pdata,
	int clk_div, int clk_src_sel)
{
	struct amlsd_host *host = (void *)pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	u32 vclkc = sd_emmc_regs->gclock;

	struct sd_emmc_clock *clkc = (struct sd_emmc_clock *)&vclkc;

	if (!host->is_gated && (clkc->div == clk_div)
				&& (clkc->src == clk_src_sel)) {
		/* sd_emmc_err("direct return\n"); */
		return; /* if the same, return directly */
	}

	aml_sd_emmc_clk_switch_off(host);
	/* mdelay(1); */

	WARN_ON(!clk_div);

	aml_sd_emmc_clk_switch_on(pdata, clk_div, clk_src_sel);
}


/*
* set host->clkc_w for 8bit emmc write
*cmd as it would fail on TXFIFO EMPTY,
* we decrease the clock for write cmd,
*and set host->clkc for other cmd
*/
void aml_sd_emmc_set_clkc(struct amlsd_platform *pdata)
{
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	u32 vclkc = sd_emmc_regs->gclock;
	struct sd_emmc_clock *clkc = (struct sd_emmc_clock *)&(pdata->clkc);

	if (!host->is_gated && (pdata->clkc == vclkc))
		return;

	if (host->is_gated)
		aml_sd_emmc_clk_switch(pdata, clkc->div, clkc->src);
	else
		sd_emmc_regs->gclock = pdata->clkc;

}

static void aml_sd_emmc_check_sdio_irq(struct amlsd_host *host)
{
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 vstat = sd_emmc_regs->gstatus;
	struct sd_emmc_status *ista = (struct sd_emmc_status *)&vstat;
	if (host->sdio_irqen) {
		if ((ista->irq_sdio || !(ista->dat_i & 0x02)) &&
			(host->mmc->sdio_irq_thread) &&
			(!atomic_read(&host->mmc->sdio_irq_thread_abort))) {
			/* pr_info("signalling irq 0x%x\n", vstat); */
			mmc_signal_sdio_irq(host->mmc);
		}
	}
}

void aml_sd_emmc_start_cmd(struct amlsd_platform *pdata,
		struct mmc_request *mrq)
{
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	struct sd_emmc_desc_info *desc_cur = NULL;
	struct cmd_cfg *des_cmd_cur = NULL;
	u32 vconf = sd_emmc_regs->gcfg;
	struct sd_emmc_config *pconf = (struct sd_emmc_config *)&vconf;
	u32 vstart = 0;
	struct sd_emmc_start *desc_start = (struct sd_emmc_start *)&vstart;
	u32 conf_flag = 0;
#ifdef CALIBRATION
	u32 adjust = sd_emmc_regs->gadjust;
	struct sd_emmc_adjust *gadjust = (struct sd_emmc_adjust *)&adjust;
#endif
#ifdef SD_EMMC_REQ_DMA_SGMAP
	u32 sg_len = 0;
#endif
	u32 desc_cnt = 0;

	sd_emmc_regs->gstart &= (~(1 << 1));
	/* unsigned long time_start_cnt = aml_read_cbus(ISA_TIMERE); */

	/* pr_info("%s %d cmd:%d, flags:0x%x, args:0x%x\n",
		__func__, __LINE__, */
	/* mrq->cmd->opcode, mrq->cmd->flags, mrq->cmd->arg); */

	/*Set clock for each port, change clock before wait ready*/
	aml_sd_emmc_set_clkc(pdata);

	/*prepare descriptor list*/
	desc_cur = (struct sd_emmc_desc_info *)host->desc_buf;
	/* memset(desc_cur, 0,
		(SD_EMMC_MAX_DESC_MUN>>2)*sizeof(struct sd_emmc_desc_info)); */
	memset(desc_cur, 0, sizeof(struct sd_emmc_desc_info));
	desc_cnt++;

	/*check bus width*/
	if (pconf->bus_width != pdata->width) {
		conf_flag |= 1<<0;
		pconf->bus_width = pdata->width;
	}

	/*check package size*/
	if (mrq->cmd->data) {
		if (pconf->bl_len != log2i(mrq->data->blksz)) {
			conf_flag |= 1<<1;
			pconf->bl_len = log2i(mrq->data->blksz);
		}
	}

	if (conf_flag) {
#ifdef SD_EMMC_DESC_SET_REG
		des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);

		/*Prepare desc for config register*/
		des_cmd_cur->owner = 1;
		des_cmd_cur->end_of_chain = 0;

		des_cmd_cur->no_cmd = 1;
		des_cmd_cur->data_io = 0;
		des_cmd_cur->cmd_index = SD_EMMC_DESC_REG_CONF;
		desc_cur->cmd_arg = (
			(pconf->bl_len&0xf) << 4) | (pconf->bus_width&0x3);
		desc_cur->data_addr = (0xf<<4) | (0x3<<0);
		desc_cnt++;
		desc_cur++;
		memset(desc_cur, 0, sizeof(struct sd_emmc_desc_info));
#else

		/*Writel config register*/
		sd_emmc_regs->gcfg = vconf;
#endif
	}

	/*Set Irq Control*/
#ifndef SD_EMMC_IRQ_EN_ALL_INIT
	irq_en->desc_err = 1;
	irq_en->resp_err = 1;
	irq_en->resp_timeout = 1;
	irq_en->desc_timeout = 1;
	irq_en->end_of_chain = 1;
	irq_en->desc_irq = 1;

	if (mrq->cmd->data) {
		if (mrq->data->flags & MMC_DATA_WRITE)
			irq_en->txd_err = 1;
		else
			irq_en->rxd_err = 0xff;
	} else{
		irq_en->rxd_err = 0;
		irq_en->txd_err = 0;
	}

#ifdef SD_EMMC_DESC_SET_REG
	des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);
	/*Prepare desc for IRQC register*/
	des_cmd_cur->owner = 1;
	des_cmd_cur->end_of_chain = 0;
	des_cmd_cur->no_resp = 1;
	des_cmd_cur->no_cmd = 1;
	des_cmd_cur->data_io = 0;
	des_cmd_cur->cmd_index = SD_EMMC_DESC_REG_IRQC;
	desc_cur->cmd_arg = (virqc&SD_EMMC_IRQ_ALL);
	desc_cur->data_addr = SD_EMMC_IRQ_ALL;
	desc_cnt++;
	desc_cur++;
	memset(desc_cur, 0, sizeof(struct sd_emmc_desc_info));
#else
	/*Set irq config*/
	sd_emmc_regs->girq_en = virqc;
#endif
#endif

	/*Add external CMD23 for multi-block operation*/
#ifdef SD_EMMC_MANUAL_CMD23
	if (((mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK)
		|| (mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK))
		&& (mrq->cmd->data) && (mrq->sbc)) {
		des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);
		/*Command Index*/
		des_cmd_cur->cmd_index = MMC_SET_BLOCK_COUNT;
		des_cmd_cur->no_resp = 0;
		des_cmd_cur->r1b = 0;
		/* response save into resp_addr itself */
		des_cmd_cur->resp_num = 1;
		des_cmd_cur->data_io = 0;
		/* 10mS for only cmd timeout */
		des_cmd_cur->timeout = 0xc;
		des_cmd_cur->owner = 1;
		des_cmd_cur->end_of_chain = 0;

		desc_cur->cmd_arg = mrq->data->blocks;
		/* SD_EMMC_DESC_RESP_STAT;   //check */
		desc_cur->resp_addr = 0;

		desc_cnt++;
		desc_cur++;
		memset(desc_cur, 0, sizeof(struct sd_emmc_desc_info));

	}
#endif

	/*prepare cmd desc info*/
	des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);

	/*Command Index*/
	des_cmd_cur->cmd_index = mrq->cmd->opcode;
	des_cmd_cur->error = 0;
	des_cmd_cur->owner = 1;
	des_cmd_cur->end_of_chain = 0;

	/*Command response*/

	if (mrq->cmd->flags & MMC_RSP_PRESENT) {
		des_cmd_cur->no_resp = 0;

#if 1
		/* save Resp into Resp addr,
		and check response from register for RSP_136 */
		if (mrq->cmd->flags & MMC_RSP_136)
			des_cmd_cur->resp_128 = 1;
		/* response save into resp_addr itself,
		and enable response check irq */
		des_cmd_cur->resp_num = 1;

		desc_cur->resp_addr = 0;   /* no check here */

#if 0
		if ((mrq->cmd->flags & MMC_RSP_R1B)
				|| (mrq->cmd->flags & MMC_RSP_R1))
			/* SD_EMMC_DESC_RESP_STAT;
			//check status only for stop for test */
		   desc_cur->resp_addr = 0;
		else
			desc_cur->resp_addr = 0;   /* no check here */
#endif

#else
		if (mrq->cmd->flags & MMC_RSP_136) {
			des_cmd_cur->resp_128 = 1;
			/* response save into SRAM */
			des_cmd_cur->resp_num = 0;
			desc_cur->resp_addr = (unsigned)(&sd_emmc_regs->gdesc);
			desc_cur->resp_addr |= (1<<0);
		} else{
			/* response save into resp_addr itself,
			and enable response check irq */
			des_cmd_cur->resp_num = 1;
			/* SD_EMMC_DESC_RESP_STAT;
			//check status only for stop for test */
			if ((mrq->cmd->flags & MMC_RSP_R1B)
					|| (mrq->cmd->flags & MMC_RSP_R1))
				desc_cur->resp_addr = 1;
			else
				desc_cur->resp_addr = 0;   /* no check here */
		}
#endif
		/* check data0 busy after R1 reponse */
		if (mrq->cmd->flags & MMC_RSP_BUSY)
			des_cmd_cur->r1b = 1;

		if (!(mrq->cmd->flags & MMC_RSP_CRC))
			des_cmd_cur->resp_nocrc = 1;
	} else{
		des_cmd_cur->no_resp = 1;
	}

	/* if (mrq->cmd->opcode == MMC_STOP_TRANSMISSION){ */
	 /* for stop comand */
	/* } */

	/* pr_info("no_resp:%d resp_num:%d r1b:%d resp_128:%d\n", */
	 /* des_cmd_cur->no_resp, des_cmd_cur->resp_num,
		des_cmd_cur->r1b, des_cmd_cur->resp_128); */

	desc_cur->cmd_arg = mrq->cmd->arg;
	if (!mrq->cmd->data)
		des_cmd_cur->timeout = 0xa;
	else
		des_cmd_cur->timeout = 0xc;

	if (mrq->cmd->data) {
#ifdef SD_EMMC_REQ_DMA_SGMAP
		sg_len = aml_sd_emmc_pre_dma(host, mrq, desc_cur);
		WARN_ON(sg_len == 0);
		desc_cnt += (sg_len - 1);
		desc_cur += (sg_len - 1); /* last desc here */
#else
		/* 2^15 = 327.68mS for data timeout, 10uS time based */
		des_cmd_cur->timeout = 0xc;

		des_cmd_cur->data_io = 1;
		/* des_cmd_cur->length = mrq->data->blocks; */

		if (mrq->data->blocks > 1) {
			des_cmd_cur->block_mode = 1;
			des_cmd_cur->length = mrq->data->blocks;
		} else{
			des_cmd_cur->block_mode = 0;
			des_cmd_cur->length = mrq->data->blksz;
		}

		if ((mrq->data->blksz * mrq->data->blocks) > 0) {
			des_cmd_cur->data_num = 0;
			desc_cur->data_addr = host->bn_dma_buf;
			desc_cur->data_addr &= ~(1<<0);   /* DDR */
		} else	/* write data into desc_cur->data_addr */
			des_cmd_cur->data_num = 1;

		if (mrq->data->flags & MMC_DATA_WRITE)
			des_cmd_cur->data_wr = 1;
		else
			des_cmd_cur->data_wr = 0;

#endif

#ifdef SD_EMMC_MANUAL_CMD23
		if (((mrq->cmd->opcode == MMC_WRITE_MULTIPLE_BLOCK)
			|| (mrq->cmd->opcode == MMC_READ_MULTIPLE_BLOCK))
			&& (!host->cmd_is_stop) && (!mrq->sbc)
			&& !(mrq->cmd->flags & (1 << 30))) {

			/* pr_info("Send stop command here\n"); */

			/* for stop comand,
			add another descriptor for stop command */
			desc_cnt++;
			desc_cur++;
			memset(desc_cur, 0, sizeof(struct sd_emmc_desc_info));

			des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);

			/*Command Index*/
			des_cmd_cur->cmd_index = MMC_STOP_TRANSMISSION;
			des_cmd_cur->no_resp = 0;
			des_cmd_cur->r1b = 1;
			/* response save into resp_addr itself */
			des_cmd_cur->resp_num = 1;
			des_cmd_cur->data_io = 0;
			/* 10mS for only cmd timeout */
			des_cmd_cur->timeout = 0xc;
			des_cmd_cur->owner = 1;
			/* SD_EMMC_DESC_RESP_STAT;
				check status only for stop for test */
			desc_cur->resp_addr = 0;
		}
#endif
	} else {
		des_cmd_cur->data_io = 0;
		/* Current 10uS based. 2^10 = 10mS for only cmd timeout */
		des_cmd_cur->timeout = 0xa;
	}

	if (mrq->cmd->opcode == MMC_SEND_STATUS)
		des_cmd_cur->timeout = 0xb;
	if (mrq->cmd->opcode == MMC_ERASE)
		des_cmd_cur->timeout = 0xf;

	/* Set end_of_chain */
	des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);
	des_cmd_cur->end_of_chain = 1;
	sd_emmc_regs->gstatus = SD_EMMC_IRQ_ALL;

	/*Set irq status: write 1 clear, no need*/
	/* writel(SD_EMMC_IRQ_ALL, host->base+SD_EMMC_STAT); */

	/*Wait command busy, check cmd busy*/
	/* if(aml_sd_emmc_wait_ready(host, STAT_POLL_TIMEOUT)){ */
	/* sd_emmc_err("aml_sd_emmc_wait_ready error before start cmd\n"); */
	/* } */

	/*start desc*/
	desc_start->init = 0;
	desc_start->busy = 1;
	desc_start->addr = host->desc_dma_addr>>2;

#if 0  /* debug */
	desc_cur = (struct sd_emmc_desc_info *)host->desc_buf;
	des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);

	aml_sd_emmc_mrq_print_info(mrq, desc_cnt);

	while (desc_cnt) {
		aml_sd_emmc_desc_print_info(desc_cur);
		desc_cur++;
		des_cmd_cur = (struct cmd_cfg *)&(desc_cur->cmd_info);
		desc_cnt--;
	}
#endif

	/* time_start_cnt = (time_start_cnt - host->time_req_sta); */
#if 0
	pr_info("%s %d time_cost:%dus cmd:%d arg:0x%x ",
		__func__, __LINE__,
			time_start_cnt, mrq->cmd->opcode, mrq->cmd->arg);
	if (mrq->cmd->data)
		pr_info("blksz:%d, blocks:%d",
			mrq->data->blksz, mrq->data->blocks);
	pr_info("\n");
#endif

	smp_wmb(); /*  */
	smp_rmb(); /*  */
#ifdef CALIBRATION
	if ((mrq->cmd->opcode == 18) && (pdata->caling == 1)) {
		gadjust->cali_rise = 0;
		gadjust->cali_sel = line_x;
		gadjust->cali_enable = 1;
		sd_emmc_regs->gadjust = adjust;
		/*schedule_delayed_work(&pdata->calouting, 0);*/
	}
#endif
	sd_emmc_regs->gstart = vstart;

}


/*mmc_request_done & do nothing in xfer_post*/
void aml_sd_emmc_request_done(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	unsigned long flags;

	spin_lock_irqsave(&host->mrq_lock, flags);
	host->xfer_step = XFER_FINISHED;
	host->mrq = NULL;
	host->status = HOST_INVALID;
	spin_unlock_irqrestore(&host->mrq_lock, flags);

#ifdef CONFIG_MMC_AML_DEBUG
	host->req_cnt--;

	aml_dbg_verify_pinmux(pdata);
	aml_dbg_verify_pull_up(pdata);

	if (0) {
		sd_emmc_err("%s: cmd%d, start sd_emmc reg:\n",
			mmc_hostname(host->mmc), mrq->cmd->opcode);
		aml_sd_emmc_print_reg_(host->reg_buf);
		sd_emmc_err("done reg:\n");
		aml_sd_emmc_print_reg(host);
	}

#endif

	if (pdata->xfer_post)
		pdata->xfer_post(pdata);

	/*Disable irq and clear irq status: write 1 clear*/
	 /* writel(SD_EMMC_IRQ_ALL, host->base+SD_EMMC_STAT); */

	/* aml_sd_emmc_wait_ready(host, STAT_POLL_TIMEOUT); */
	/*Wait command busy*/
	if (aml_sd_emmc_wait_ready(host, STAT_POLL_TIMEOUT))
		sd_emmc_err("aml_sd_emmc_wait_ready request done\n");

	aml_sd_emmc_check_sdio_irq(host);
	mmc_request_done(host->mmc, mrq);
}

static void aml_sd_emmc_print_err(struct amlsd_host *host)
{
	/* not print err msg for tuning cmd */
	if ((host->mrq->cmd->opcode == MMC_SEND_TUNING_BLOCK)
		|| (host->mrq->cmd->opcode == MMC_SEND_TUNING_BLOCK_HS200)) {
		return;
	}

	aml_sd_emmc_print_reg(host);
	aml_dbg_print_pinmux();
}

#ifdef SD_EMMC_ENABLE_TIMEOUT
/*error handler*/
static void aml_sd_emmc_timeout(struct work_struct *work)
{
	static int timeout_cnt;
	struct amlsd_host *host = container_of(
		work, struct amlsd_host, timeout.work);
	struct mmc_request *mrq;
	struct amlsd_platform *pdata = mmc_priv(host->mmc);
	unsigned long flags;
	/* unsigned long time_start_cnt = aml_read_cbus(ISA_TIMERE); */

	/* time_start_cnt = (time_start_cnt - host->time_req_sta) / 1000; */

	BUG_ON(!host->mrq || !host->mrq->cmd);

	spin_lock_irqsave(&host->mrq_lock, flags);
	if (host->xfer_step == XFER_FINISHED) {
		spin_unlock_irqrestore(&host->mrq_lock, flags);
		sd_emmc_err("%s :timeout after xfer finished\n",
			mmc_hostname(host->mmc));
		return;
	}

	if ((host->xfer_step == XFER_IRQ_TASKLET_DATA)
		|| (host->xfer_step == XFER_IRQ_TASKLET_CMD)) {
		schedule_delayed_work(&host->timeout, 50);
		host->time_req_sta = aml_read_cbus(ISA_TIMERE);

		timeout_cnt++;
		if (timeout_cnt > 30)
			goto timeout_handle;

		spin_unlock_irqrestore(&host->mrq_lock, flags);

		sd_emmc_err("%s: cmd%d, ISR have been run, xfer_step=%d;\n",
			mmc_hostname(host->mmc), host->mrq->cmd->opcode,
				host->xfer_step);
		return;
	}

timeout_handle:
	timeout_cnt = 0;

	mrq = host->mrq;
	host->xfer_step_prev = host->xfer_step;
	host->xfer_step = XFER_TIMER_TIMEOUT;
	mrq->cmd->error = -ETIMEDOUT;

	/* do not retry for sdcard & sdio wifi */
	if (!aml_card_type_mmc(pdata)) {
		sd_emmc_error_flag = 0;
		mrq->cmd->retries = 0;
	} else if (((sd_emmc_error_flag & (1<<3)) == 0)
		&& (mrq->data != NULL)
		&& pdata->is_in){  /* set cmd retry cnt when first error. */
		sd_emmc_error_flag |= (1<<3);
		mrq->cmd->retries = AML_TIMEOUT_RETRY_COUNTER;
	}

	if (sd_emmc_error_flag && (mrq->cmd->retries == 0)) {
		sd_emmc_error_flag |= (1<<30);
		sd_emmc_err("Command retried failed\n");
	}

	/* spin_unlock_irqrestore(&host->mrq_lock, flags); */

	/* aml_sd_emmc_status(host); */

	spin_unlock_irqrestore(&host->mrq_lock, flags);
	aml_sd_emmc_read_response(host->mmc, mrq->cmd);
	/* sd_emmc_err("time_start_cnt:%ld\n", time_start_cnt); */

	aml_sd_emmc_print_err(host);

	/* Need reset hw controller here?? */
	aml_sd_emmc_host_reset(host);


	/* do not send stop for sdio wifi case */
	if (host->mrq->stop && aml_card_type_mmc(pdata) && !host->cmd_is_stop
		&& (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK)
		&& (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200)) {
		aml_sd_emmc_send_stop(host);

	} else{
		spin_lock_irqsave(&host->mrq_lock, flags);
		if (host->cmd_is_stop)
			host->cmd_is_stop = 0;
		spin_unlock_irqrestore(&host->mrq_lock, flags);

		aml_sd_emmc_request_done(host->mmc, mrq);
	}

	return;
}
#endif

static void aml_sd_emmc_tuning_timer(struct work_struct *work)
{
	struct amlsd_platform *pdata = container_of(
		work, struct amlsd_platform, retuning.work);
	struct amlsd_host *host = (void *)pdata->host;
	unsigned long flags;

	spin_lock_irqsave(&host->mrq_lock, flags);
	pdata->need_retuning = true;
	spin_unlock_irqrestore(&host->mrq_lock, flags);
}

/*cmd request interface*/
void aml_sd_emmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct amlsd_platform *pdata;
	struct amlsd_host *host;
	/* u32 vista; */
	unsigned long flags;
	/* unsigned int timeout; */
	/* u32 tuning_opcode; */

	BUG_ON(!mmc);
	BUG_ON(!mrq);

	pdata = mmc_priv(mmc);
	host = (void *)pdata->host;

	if (aml_check_unsupport_cmd(mmc, mrq))
		return;

	/* pr_info("%s %d cmd:%d\n", __func__, __LINE__, mrq->cmd->opcode); */

	/* only for SDCARD */
	if (!pdata->is_in || (!host->init_flag && aml_card_type_sd(pdata))) {
		spin_lock_irqsave(&host->mrq_lock, flags);
		mrq->cmd->error = -ENOMEDIUM;
		mrq->cmd->retries = 0;
		spin_unlock_irqrestore(&host->mrq_lock, flags);
		mmc_request_done(mmc, mrq);
		return;
	}

#if 0
	if (pdata->need_retuning && mmc->card) {
		/* eMMC uses cmd21 but sd and sdio use cmd19 */
		tuning_opcode = (mmc->card->type == MMC_TYPE_MMC) ?
		MMC_SEND_TUNING_BLOCK_HS200 : MMC_SEND_TUNING_BLOCK;
		aml_sd_emmc_execute_tuning(mmc, tuning_opcode);
	}
#endif

	/* need disable Irq??? */
	/* aml_sd_emmc_disable_imask(host, sd_emmc_ICTL_ALL); */

	sd_emmc_dbg(AMLSD_DBG_REQ , "%s: starting CMD%u arg %08x flags %08x\n",
	mmc_hostname(mmc), mrq->cmd->opcode,
	mrq->cmd->arg, mrq->cmd->flags);

#ifdef CONFIG_AML_MMC_DEBUG_FORCE_SINGLE_BLOCK_RW
	 /* ???? for debug */
	if ((mrq->cmd->opcode == 18) || (mrq->cmd->opcode == 25))
		sd_emmc_err("cmd%d\n", mrq->cmd->opcode);
#endif

	if (mrq->cmd->opcode == 0)
		host->init_flag = 1;

	/*clear error flag if last command retried failed */
	if (host->error_flag & (1 << 30))
		host->error_flag = 0;

#ifndef SD_EMMC_REQ_DMA_SGMAP
	/*setup reg  especially for cmd with transfering data*/
	if (mrq->data) {
		/*Copy data to dma buffer for write request*/
		aml_sd_emmc_prepare_dma(host, mrq);

		sd_emmc_dbg(AMLSD_DBG_REQ ,
		"%s: blksz %d blocks %d flags %08x\n",
		mmc_hostname(mmc), mrq->data->blksz,
		mrq->data->blocks, mrq->data->flags);

		sd_emmc_dbg(AMLSD_DBG_REQ , "%s:tsac %d ms nsac %d\n",
		mmc_hostname(mmc), mrq->data->timeout_ns / 1000000,
		mrq->data->timeout_clks);
	}
#endif
	/*clear pinmux & set pinmux*/
	if (pdata->xfer_pre)
		pdata->xfer_pre(pdata);

#ifdef CONFIG_MMC_AML_DEBUG
	aml_dbg_verify_pull_up(pdata);
	aml_dbg_verify_pinmux(pdata);
#endif

	spin_lock_irqsave(&host->mrq_lock, flags);
	if (host->xfer_step != XFER_FINISHED && host->xfer_step != XFER_INIT)
		sd_emmc_err("host->xfer_step %d\n", host->xfer_step);

	/*host->mrq, used in irq & tasklet*/
	host->mrq = mrq;
	host->mmc = mmc;
	host->xfer_step = XFER_START;
	host->opcode = mrq->cmd->opcode;
	host->arg = mrq->cmd->arg;
	/* host->time_req_sta = aml_read_cbus(ISA_TIMERE); */

	/*setup reg for all cmd*/
	aml_sd_emmc_start_cmd(pdata, mrq);
	host->xfer_step = XFER_AFTER_START;
	spin_unlock_irqrestore(&host->mrq_lock, flags);
}

/*sd_emmc controller irq*/
static irqreturn_t aml_sd_emmc_irq(int irq, void *dev_id)
{
	struct amlsd_host *host = dev_id;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	struct mmc_host *mmc;
	struct amlsd_platform *pdata;
	struct mmc_request *mrq;
	unsigned long flags;
	/* bool exception_flag = false; */
	u32 vstat = 0;
	u32 virqc = 0;
	struct sd_emmc_irq_en *irqc = (struct sd_emmc_irq_en *)&virqc;
	struct sd_emmc_status *ista = (struct sd_emmc_status *)&vstat;
	u32 vstart = 0;
	struct sd_emmc_start *desc_start = (struct sd_emmc_start *)&vstart;
	/* unsigned long time_start_cnt = aml_read_cbus(ISA_TIMERE); */

	/* if(sd_emmc_debug_flag == 9){ */
	/* sd_emmc_err("FORCE ignore IRQ here\n"); */
	/* sd_emmc_debug_flag = 0; */
	/* return IRQ_HANDLED; */
	/* } */
	virqc = sd_emmc_regs->girq_en & 0xffff;
	vstat = sd_emmc_regs->gstatus & 0xffff;
	/* pr_info("%s %d occurred, vstat:0x%x,
		sd_emmc_regs->gstatus:%x\n",
		__func__, __LINE__, vstat, sd_emmc_regs->gstatus); */

	host->ista = vstat;
	if (irqc->irq_sdio && ista->irq_sdio) {
		if ((host->mmc->sdio_irq_thread)
			&& (!atomic_read(&host->mmc->sdio_irq_thread_abort))) {
			mmc_signal_sdio_irq(host->mmc);
			if (!(vstat & 0x3fff))
				return IRQ_HANDLED;
			/*else
				pr_info("other irq also occurred 0x%x\n",
			vstat);*/
		}
	} else if (!(vstat & 0x3fff)) {
		return IRQ_HANDLED;
	}
	spin_lock_irqsave(&host->mrq_lock, flags);
	mrq = host->mrq;
	mmc = host->mmc;
	pdata = mmc_priv(mmc);
	vstart = sd_emmc_regs->gstart;
	if ((desc_start->busy == 1) && (aml_card_type_mmc(pdata))) {
		desc_start->busy = 0;
		sd_emmc_regs->gstart = vstart;
	}
	if (!mmc) {
		pr_info("sd_emmc_regs->girq_en = 0x%x at line %d\n",
			sd_emmc_regs->girq_en, __LINE__);
		pr_info("sd_emmc_regs->gstatus = 0x%x at line %d\n",
			sd_emmc_regs->gstatus, __LINE__);
		pr_info("sd_emmc_regs->gcfg = 0x%x at line %d\n",
			sd_emmc_regs->gcfg, __LINE__);
		pr_info("sd_emmc_regs->gclock = 0x%x at line %d\n",
			sd_emmc_regs->gclock, __LINE__);
	}
	/* time_start_cnt = (time_start_cnt - host->time_req_sta); */
#if 0
	pr_info("%s %d time_cost:%dus cmd:%d arg:0x%x ",
		__func__, __LINE__, time_start_cnt,
			mrq->cmd->opcode, mrq->cmd->arg);
	if (mrq->cmd->data)
		pr_info("blksz:%d, blocks:%d",
			mrq->data->blksz, mrq->data->blocks);
	pr_info("\n");
#endif

	if (!mrq && !irqc->irq_sdio) {
		if (!ista->irq_sdio) {
			sd_emmc_err("NULL mrq in aml_sd_emmc_irq step %d",
				host->xfer_step);
			sd_emmc_err("status:0x%x,irq_c:0x%0x\n",
				sd_emmc_regs->gstatus, sd_emmc_regs->girq_en);
		}
		if (host->xfer_step == XFER_FINISHED ||
			host->xfer_step == XFER_TIMER_TIMEOUT){
			spin_unlock_irqrestore(&host->mrq_lock, flags);
			return IRQ_HANDLED;
		}
	/* WARN_ON(!mrq); */
	/* aml_sd_emmc_print_reg(host); */
		spin_unlock_irqrestore(&host->mrq_lock, flags);
		return IRQ_HANDLED;
	}
#if 0
	if ((host->xfer_step != XFER_AFTER_START)
		&& (!host->cmd_is_stop) && !irqc->irq_sdio) {
		sd_emmc_err("%s: host->xfer_step=%d\n",
			mmc_hostname(mmc), host->xfer_step);
		pr_info("%%sd_emmc_regs->girq_en = 0x%x at line %d\n",
			sd_emmc_regs->girq_en, __LINE__);
		pr_info("%%sd_emmc_regs->gstatus = 0x%x at line %d\n",
			sd_emmc_regs->gstatus, __LINE__);
		pr_info("%%sd_emmc_regs->gcfg = 0x%x at line %d\n",
			sd_emmc_regs->gcfg, __LINE__);
		pr_info("%%sd_emmc_regs->gclock = 0x%x at line %d\n",
			sd_emmc_regs->gclock, __LINE__);
	}
#endif
	if (mrq) {
		if (host->cmd_is_stop)
			host->xfer_step = XFER_IRQ_TASKLET_BUSY;
		else
			host->xfer_step = XFER_IRQ_OCCUR;
	}
#ifdef CALIBRATION
	if ((mrq->cmd->opcode == 18) && (pdata->caling == 1))
		pdata->caling = 0;
#endif
	sd_emmc_regs->gstatus &= 0xffff;
	spin_unlock_irqrestore(&host->mrq_lock, flags);

	if ((ista->rxd_err) || (ista->txd_err)) {
		host->status = HOST_DAT_CRC_ERR;
		mrq->cmd->error = -EILSEQ;
		/* report error! */
		if (host->is_tunning == 0) {
			sd_emmc_err("%s: warning... data crc, vstat:0x%x, virqc:%x",
				mmc_hostname(host->mmc), vstat, virqc);
			sd_emmc_err("@ cmd %d with %p; stop %d, status %d\n",
				mrq->cmd->opcode, mrq->data,
				host->cmd_is_stop, host->status);
		}
	} else if (ista->resp_err) {
		if (host->is_tunning == 0)
			sd_emmc_err("%s: warning... response crc,vstat:0x%x,virqc:%x\n",
				mmc_hostname(host->mmc), vstat, virqc);
		host->status = HOST_RSP_CRC_ERR;
		mrq->cmd->error = -EILSEQ;
	} else if (ista->resp_timeout) {
		if (host->is_tunning == 0)
			sd_emmc_err("%s: resp_timeout,vstat:0x%x,virqc:%x\n",
				mmc_hostname(host->mmc), vstat, virqc);
		host->status = HOST_RSP_TIMEOUT_ERR;
		mrq->cmd->error = -ETIMEDOUT;
	} else if (ista->desc_timeout) {
		if (host->is_tunning == 0)
			sd_emmc_err("%s: desc_timeout,vstat:0x%x,virqc:%x\n",
				mmc_hostname(host->mmc), vstat, virqc);
		host->status = HOST_DAT_TIMEOUT_ERR;
		mrq->cmd->error = -ETIMEDOUT;
	} else if ((ista->end_of_chain) || (ista->desc_irq)) {
		if (mrq->data)
			host->status = HOST_TASKLET_DATA;
		else
			host->status = HOST_TASKLET_CMD;
		mrq->cmd->error = 0;
	} else{
	   host->xfer_step = XFER_IRQ_UNKNOWN_IRQ;
		sd_emmc_err("%s: %s Unknown Irq Ictl 0x%x, Ista 0x%x\n",
		mmc_hostname(host->mmc), pdata->pinname, virqc, vstat);
	}


	if (host->xfer_step != XFER_IRQ_UNKNOWN_IRQ) {
#ifdef SD_EMMC_DATA_TASKLET
		tasklet_schedule(&sd_emmc_finish_tasklet);
		return IRQ_HANDLED;
#else
		return IRQ_WAKE_THREAD;
#endif
	} else
		return IRQ_HANDLED;
}

struct mmc_command aml_sd_emmc_cmd = {
	.opcode = MMC_STOP_TRANSMISSION,
	.flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC,
};
struct mmc_request aml_sd_emmc_stop = {
	.cmd = &aml_sd_emmc_cmd,
};

void aml_sd_emmc_send_stop(struct amlsd_host *host)
{
	struct amlsd_platform *pdata = mmc_priv(host->mmc);
	unsigned long flags;

	/*Already in mrq_lock*/
	if (delayed_work_pending(&host->timeout))
		cancel_delayed_work(&host->timeout);
	spin_lock_irqsave(&host->mrq_lock, flags);
	/*sd_emmc_err_bak = host->mrq->cmd->error;*/
	host->error_bak = host->mrq->cmd->error;
	host->mrq->cmd->error = 0;
	host->cmd_is_stop = 1;
	aml_sd_emmc_start_cmd(pdata, &aml_sd_emmc_stop);
	spin_unlock_irqrestore(&host->mrq_lock, flags);
}

#if 0	/* debug code, just for npp function list bug.... */
#ifdef SD_EMMC_DATA_TASKLET
static void aml_sd_emmc_data_tasklet(unsigned long data)
#else
static irqreturn_t aml_sd_emmc_data_thread(int irq, void *data)
#endif
#endif /* 0 */
static irqreturn_t aml_sd_emmc_data_thread(int irq, void *data)
{
#ifdef SD_EMMC_DATA_TASKLET
	struct amlsd_host *host = (struct amlsd_host *)data;
#else
	struct amlsd_host *host = data;
#endif
	/*---------------------------------------------------------*/
	struct aml_emmc_adjust *emmc_adj = &host->emmc_adj;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 adjust = sd_emmc_regs->gadjust;
	struct sd_emmc_adjust *gadjust = (struct sd_emmc_adjust *)&adjust;
	/*---------------------------------------------------------*/
	u32 xfer_bytes = 0;
	struct mmc_request *mrq;
	enum aml_mmc_waitfor xfer_step;
	unsigned long flags;
	u32 status;
	struct amlsd_platform *pdata = mmc_priv(host->mmc);
	/* unsigned long time_start_cnt = aml_read_cbus(ISA_TIMERE); */
	/* time_start_cnt = (time_start_cnt - host->time_req_sta); */
	/* pr_info("%s %d cmd:%d time_cost:%dms\n",
		__func__, __LINE__, mrq->cmd->opcode, time_start_cnt); */

	spin_lock_irqsave(&host->mrq_lock, flags);
	mrq = host->mrq;
	xfer_step = host->xfer_step;
	status = host->status;
#if 0
	pr_info("%s %d time_cost:%dus cmd:%d arg:0x%x\n",
	__func__, __LINE__, time_start_cnt, mrq->cmd->opcode, mrq->cmd->arg);
#endif
	if ((xfer_step == XFER_FINISHED) || (xfer_step == XFER_TIMER_TIMEOUT)) {
		sd_emmc_err("Warning: %s xfer_step=%d, host->status=%d\n",
			mmc_hostname(host->mmc), xfer_step, status);
		spin_unlock_irqrestore(&host->mrq_lock, flags);
#ifdef SD_EMMC_DATA_TASKLET
		return;
#else
		return IRQ_HANDLED;
#endif
	}

	WARN_ON((host->xfer_step != XFER_IRQ_OCCUR)
		 && (host->xfer_step != XFER_IRQ_TASKLET_BUSY));

	if (!mrq) {
		sd_emmc_err("%s: !mrq xfer_step %d\n",
			mmc_hostname(host->mmc), xfer_step);
		if (xfer_step == XFER_FINISHED ||
			xfer_step == XFER_TIMER_TIMEOUT){
			spin_unlock_irqrestore(&host->mrq_lock, flags);
#ifdef SD_EMMC_DATA_TASKLET
			return;
#else
			return IRQ_HANDLED;
#endif
		}
		/* BUG(); */
		aml_sd_emmc_print_err(host);
	}

	if (host->cmd_is_stop) {
		/* --new irq enter, */
		host->cmd_is_stop = 0;
		mrq->cmd->error = host->error_bak;
		spin_unlock_irqrestore(&host->mrq_lock, flags);
		if (delayed_work_pending(&host->timeout))
			cancel_delayed_work(&host->timeout);
		aml_sd_emmc_request_done(host->mmc, host->mrq);
#ifdef SD_EMMC_DATA_TASKLET
		return;
#else
		if (host->is_tunning == 0)
			pr_info("%s : %d\n", __func__, __LINE__);
		return IRQ_HANDLED;
#endif
	}
	spin_unlock_irqrestore(&host->mrq_lock, flags);

	BUG_ON(!host->mrq->cmd);

	switch (status) {
	case HOST_TASKLET_DATA:
	case HOST_TASKLET_CMD:
		/* WARN_ON(aml_sd_emmc_desc_check(host)); */
		/* pr_info("%s %d cmd:%d\n", __func__, __LINE__,
			mrq->cmd->opcode); */
		host->error_flag = 0;
		if (mrq->cmd->data &&  mrq->cmd->opcode) {
			xfer_bytes = mrq->data->blksz*mrq->data->blocks;
			/* copy buffer from dma to data->sg in read cmd*/
#ifdef SD_EMMC_REQ_DMA_SGMAP
			WARN_ON(aml_sd_emmc_post_dma(host, mrq));
#else
			if (host->mrq->data->flags & MMC_DATA_READ) {
				aml_sg_copy_buffer(mrq->data->sg,
				mrq->data->sg_len, host->bn_buf,
				 xfer_bytes, 0);
			}
#endif
			mrq->data->bytes_xfered = xfer_bytes;
			host->xfer_step = XFER_TASKLET_DATA;
		} else {
			host->xfer_step = XFER_TASKLET_CMD;
		}
		spin_lock_irqsave(&host->mrq_lock, flags);
		mrq->cmd->error = 0;
		spin_unlock_irqrestore(&host->mrq_lock, flags);

		/* check ready?? */
		/*Wait command busy*/
/* if(aml_sd_emmc_wait_ready(host, (STAT_POLL_TIMEOUT<<2))){ */
/* sd_emmc_err( */
/* "aml_sd_emmc_wait_ready error after data thread\n" */
/* ); */
/* } */

		aml_sd_emmc_read_response(host->mmc, mrq->cmd);
		aml_sd_emmc_request_done(host->mmc, mrq);

		break;

	case HOST_RSP_TIMEOUT_ERR:
	case HOST_DAT_TIMEOUT_ERR:
	case HOST_RSP_CRC_ERR:
	case HOST_DAT_CRC_ERR:
		if (host->is_tunning == 0)
			pr_info("%s %d %s: cmd:%d\n", __func__, __LINE__,
				mmc_hostname(host->mmc), mrq->cmd->opcode);
		if (mrq->cmd->data) {
			dma_unmap_sg(mmc_dev(host->mmc), mrq->cmd->data->sg,
				mrq->cmd->data->sg_len,
				(mrq->cmd->data->flags & MMC_DATA_READ) ?
					DMA_FROM_DEVICE : DMA_TO_DEVICE);
		}
		aml_sd_emmc_read_response(host->mmc, host->mrq->cmd);

		/* set retry @ 1st error happens! */
		if ((host->error_flag == 0) && aml_card_type_mmc(pdata)
			&& (mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK_HS200)
			&& (mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK)
			&& (host->is_tunning == 0)) {

			sd_emmc_err("%s() %d: set 1st retry!\n",
				__func__, __LINE__);
			host->error_flag |= (1<<0);
			spin_lock_irqsave(&host->mrq_lock, flags);
			mrq->cmd->retries = AML_ERROR_RETRY_COUNTER;
			spin_unlock_irqrestore(&host->mrq_lock, flags);
		}

		if (aml_card_type_mmc(pdata) &&
			(host->error_flag & (1<<0)) && mrq->cmd->retries) {
			sd_emmc_err("retry cmd %d the %d-th time(s)\n",
				mrq->cmd->opcode, mrq->cmd->retries);
			/* chage configs on current host */
			emmc_adj->adj_point++;
			emmc_adj->adj_point %= emmc_adj->clk_div;
			pr_err("emmc, %d retry, adj %d -> %d\n",
				mrq->cmd->retries, gadjust->adj_delay,
				emmc_adj->adj_point);

			/*set new adjust!*/
			gadjust->adj_delay = emmc_adj->adj_point;
			gadjust->adj_enable = 1;
			sd_emmc_regs->gadjust = adjust;
		}
		/* last retry effort! */
		if (aml_card_type_mmc(pdata) &&
			host->error_flag && (mrq->cmd->retries == 0)) {
			host->error_flag |= (1<<30);
			sd_emmc_err("Command retried failed line:%d, cmd:%d\n",
				__LINE__, mrq->cmd->opcode);
		}
		/* retry need send a stop 2 emmc... */
		/* do not send stop for sdio wifi case */
		if (host->mrq->stop && aml_card_type_mmc(pdata) && pdata->is_in
		&& (host->mrq->cmd->opcode != MMC_SEND_TUNING_BLOCK)
			&& (host->mrq->cmd->opcode
				!= MMC_SEND_TUNING_BLOCK_HS200))
			aml_sd_emmc_send_stop(host);
		else
			aml_sd_emmc_request_done(host->mmc, mrq);
		break;

	default:
		sd_emmc_err("BUG %s: xfer_step=%d, host->status=%d\n",
			mmc_hostname(host->mmc),  xfer_step, status);
		aml_sd_emmc_print_err(host);
		/* BUG(); */
	}

	/* pr_info("%s %d cmd:%d\n", __func__, __LINE__, mrq->cmd->opcode); */

#ifdef SD_EMMC_DATA_TASKLET
		return;
#else
		/* pr_info("%s : %d\n", __func__, __LINE__); */
		return IRQ_HANDLED;
#endif
}



/*
1. clock valid range
2. clk config enable
3. select clock source
4. set clock divide
*/
static void aml_sd_emmc_set_clk_rate(struct mmc_host *mmc, unsigned int clk_ios)
{
	u32 clk_rate, clk_div, clk_src_sel, clk_src_div;
	unsigned long flags;
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = (void *)pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	if (clk_ios == 0) {
		aml_sd_emmc_clk_switch_off(host);
		return;
	}

	clk_src_div = -1;
	clk_src_sel = SD_EMMC_CLOCK_SRC_OSC;
	if (clk_ios < 20000000)
		clk_src_sel = SD_EMMC_CLOCK_SRC_OSC;
	else
		clk_src_sel = SD_EMMC_CLOCK_SRC_FCLK_DIV2;

	if (clk_ios > pdata->f_max)
		clk_ios = pdata->f_max;
	if (clk_ios < pdata->f_min)
		clk_ios = pdata->f_min;

	WARN_ON(clk_src_sel > SD_EMMC_CLOCK_SRC_FCLK_DIV2);

	switch (clk_src_sel) {
	case SD_EMMC_CLOCK_SRC_OSC:
		clk_rate = 24000000;
		break;
	case SD_EMMC_CLOCK_SRC_FCLK_DIV2:
		clk_rate = 1000000000;
		break;
	default:
		sdhc_err("%s: clock source error: %d\n",
			mmc_hostname(host->mmc), clk_src_sel);
		return; /* clk_src_div = -1; */
	}

	spin_lock_irqsave(&host->mrq_lock, flags);
	clk_div = (clk_rate / clk_ios) + (!!(clk_rate % clk_ios));

	aml_sd_emmc_clk_switch(pdata, clk_div, clk_src_sel);

	/* pr_info("sd_emmc_regs->gclock = 0x%x\n", sd_emmc_regs->gclock); */
	pdata->clkc = sd_emmc_regs->gclock;

	pdata->mmc->actual_clock = clk_rate / clk_div;

	/*Disable All Irq*/

	/*Wait for a while after clock setting*/
	/* udelay(100); */

	spin_unlock_irqrestore(&host->mrq_lock, flags);

	return;
}


static void aml_sd_emmc_set_timing(
		struct amlsd_platform *pdata, u32 timing)
{
	struct amlsd_host *host = (void *)pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 vctrl = sd_emmc_regs->gcfg;
	struct sd_emmc_config *ctrl = (struct sd_emmc_config *)&vctrl;
	u32 vclkc = sd_emmc_regs->gclock;
	struct sd_emmc_clock *clkc = (struct sd_emmc_clock *)&vclkc;
	u8 clk_div;
	u32 clk_rate = 1000000000;
	if ((timing == MMC_TIMING_MMC_HS400) ||
			 (timing == MMC_TIMING_MMC_DDR52) ||
				 (timing == MMC_TIMING_UHS_DDR50)) {
		if (timing == MMC_TIMING_MMC_HS400)
			ctrl->chk_ds = 1;
		ctrl->ddr = 1;
		clk_div = clkc->div;
		if (clk_div & 0x01)
			clk_div++;
			clkc->div = clk_div / 2;
		sd_emmc_regs->gclock = vclkc;
		pdata->clkc = sd_emmc_regs->gclock;
		pdata->mmc->actual_clock = clk_rate / clk_div;
		pr_info("%s: try set sd/emmc to DDR mode\n",
			mmc_hostname(host->mmc));

	} else
		ctrl->ddr = 0;

	sd_emmc_regs->gcfg = vctrl;
	sd_emmc_dbg(AMLSD_DBG_IOS, "sd emmc is %s\n",
			ctrl->ddr?"DDR mode":"SDR mode");
	return;
}

/*setup bus width, 1bit, 4bits, 8bits*/
static void aml_sd_emmc_set_bus_width(
		struct amlsd_platform *pdata, u32 busw_ios)
{
	struct amlsd_host *host = (void *)pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	u32 vctrl = sd_emmc_regs->gcfg;
	struct sd_emmc_config *ctrl = (struct sd_emmc_config *)&vctrl;
	u32 width = 0;

	switch (busw_ios) {
	case MMC_BUS_WIDTH_1:
		width = 0;
		break;
	case MMC_BUS_WIDTH_4:
		width = 1;
		break;
	case MMC_BUS_WIDTH_8:
		width = 2;
		break;
	default:
		sd_emmc_err("%s: error Data Bus\n", mmc_hostname(host->mmc));
		break;
	}

	ctrl->bus_width = width;
	pdata->width = width;

	sd_emmc_regs->gcfg = vctrl;
	sd_emmc_dbg(AMLSD_DBG_IOS, "Bus Width Ios %d\n", busw_ios);
}

/*call by mmc, power on, power off ...*/
static void aml_sd_emmc_set_power(struct amlsd_platform *pdata, u32 power_mode)
{
	switch (power_mode) {
	case MMC_POWER_ON:
		if (pdata->pwr_pre)
			pdata->pwr_pre(pdata);
		if (pdata->pwr_on)
			pdata->pwr_on(pdata);
		break;
	case MMC_POWER_UP:
		break;
	case MMC_POWER_OFF:
	default:
		if (pdata->pwr_pre)
			pdata->pwr_pre(pdata);
		if (pdata->pwr_off)
			pdata->pwr_off(pdata);
		break;
	}
}

/*call by mmc, set ios: power, clk, bus width*/
static void aml_sd_emmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = (void *)pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;

	if (!pdata->is_in)
		return;
	/*Set Power*/
	aml_sd_emmc_set_power(pdata, ios->power_mode);

	/*Set Clock*/
	aml_sd_emmc_set_clk_rate(mmc, ios->clock);

	/*Set Bus Width*/
	aml_sd_emmc_set_bus_width(pdata, ios->bus_width);

	/* Set Date Mode */
	aml_sd_emmc_set_timing(pdata, ios->timing);

	if (ios->power_mode == MMC_POWER_OFF) {
		sd_emmc_regs->gdelay = 0;
		sd_emmc_regs->gadjust = 0;
	}

	if (ios->chip_select == MMC_CS_HIGH) {
		aml_cs_high(pdata);
	} else if (ios->chip_select == MMC_CS_DONTCARE) {
		aml_cs_dont_care(pdata);
	} else { /* MMC_CS_LOW */
	/* Nothing to do */
	}
}

static void aml_sd_emmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	unsigned long flags;
	/* bool exception_flag = false; */
	/* u32 vstat = 0; */
	u32 vclock = 0;
	struct sd_emmc_clock *pclock = (struct sd_emmc_clock *)&vclock;
	u32 vconf = 0;
	struct sd_emmc_config *pconf = (struct sd_emmc_config *)&vconf;
	u32 virqc = 0;
	struct sd_emmc_irq_en *irqc = (struct sd_emmc_irq_en *)&virqc;

	host->sdio_irqen = enable;
	spin_lock_irqsave(&host->mrq_lock, flags);
	vclock = sd_emmc_regs->gclock;
	vconf = sd_emmc_regs->gcfg;
	virqc = sd_emmc_regs->girq_en;

	pclock->irq_sdio_sleep = 1;
	pclock->irq_sdio_sleep_ds = 0;
	pconf->irq_ds = 0;

	/* vstat = sd_emmc_regs->gstatus&SD_EMMC_IRQ_ALL; */
	if (enable)
		irqc->irq_sdio = 1;
	else
		irqc->irq_sdio = 0;

	sd_emmc_regs->girq_en = virqc;
	sd_emmc_regs->gclock = vclock;

	spin_unlock_irqrestore(&host->mrq_lock, flags);

	/* check if irq already occurred */
	aml_sd_emmc_check_sdio_irq(host);
}

/*get readonly: 0 for rw, 1 for ro*/
static int aml_sd_emmc_get_ro(struct mmc_host *mmc)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	u32 ro = 0;

	if (pdata->ro)
		ro = pdata->ro(pdata);
	return ro;
}

/*get card detect: 1 for insert, 0 for removed*/
int aml_sd_emmc_get_cd(struct mmc_host *mmc)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	return pdata->is_in; /* 0: no inserted  1: inserted */
}

/* Check if the card is pulling dat[0:3] low */
static int aml_sd_emmc_card_busy(struct mmc_host *mmc)
{
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct amlsd_host *host = pdata->host;
	struct sd_emmc_regs *sd_emmc_regs = host->sd_emmc_regs;
	unsigned status = 0;

	/* only check data3_0 gpio level?? */
	u32 vstat = sd_emmc_regs->gstatus;
	struct sd_emmc_status *ista = (struct sd_emmc_status *)&vstat;

	status = ista->dat_i & 0xf;
	/* sd_emmc_dbg(AMLSD_DBG_COMMON, "dat[0:3]=%#x\n", stat->dat3_0); */

	return !status;
}

#if 0/* def CONFIG_PM */

static int aml_sd_emmc_suspend(struct platform_device *pdev, pm_message_t state)
{
	int ret = 0;
	int i;
	struct amlsd_host *host = platform_get_drvdata(pdev);
	struct mmc_host *mmc;
	struct amlsd_platform *pdata;

	pr_info("***Entered %s:%s\n", __FILE__, __func__);
	i = 0;
	list_for_each_entry(pdata, &host->sibling, sibling) {
		cancel_delayed_work(&pdata->retuning);
		pdata->need_retuning = false;

		mmc = pdata->mmc;
		/* mmc_power_save_host(mmc); */
		ret = mmc_suspend_host(mmc);
		if (ret)
			break;

		i++;
	}

	if (ret) {
		list_for_each_entry(pdata, &host->sibling, sibling) {
			i--;
			if (i < 0)
				break;
			mmc = pdata->mmc;
			mmc_resume_host(mmc);
		}
	}
	pr_info("***Exited %s:%s\n", __FILE__, __func__);

	return ret;
}

static int aml_sd_emmc_resume(struct platform_device *pdev)
{
	int ret = 0;
	struct amlsd_host *host = platform_get_drvdata(pdev);
	struct mmc_host *mmc;
	struct amlsd_platform *pdata;

	pr_info("***Entered %s:%s\n", __FILE__, __func__);
	list_for_each_entry(pdata, &host->sibling, sibling) {
	/* detect if a card is exist or not if it is removable */
	if (!(pdata->caps & MMC_CAP_NONREMOVABLE))
		aml_sd_uart_detect(pdata);
	mmc = pdata->mmc;
	/* mmc_power_restore_host(mmc); */
	ret = mmc_resume_host(mmc);
	if (ret)
		break;
	}
	pr_info("***Exited %s:%s\n", __FILE__, __func__);
	return ret;
}

#else

#define aml_sd_emmc_suspend	NULL
#define aml_sd_emmc_resume		NULL

#endif

#ifdef CONFIG_HIBERNATION
static int aml_sd_emmc_restore(struct device *dev)
{
	struct platform_device *pdev;
	struct amlsd_host *host;
	struct amlsd_platform *pdata;
	pdev = to_platform_device(dev);
	host = platform_get_drvdata(pdev);
	/* for storage self-adaptive */
	if (NULL == host)
		return 0;
	list_for_each_entry(pdata, &host->sibling, sibling)
		if (!(pdata->caps & MMC_CAP_NONREMOVABLE))
			aml_sd_uart_detect(pdata);
	return 0;
}
const struct dev_pm_ops aml_sd_emmc_pm = {
	.restore	= aml_sd_emmc_restore,
};
#endif



static const struct mmc_host_ops aml_sd_emmc_ops = {
	.request = aml_sd_emmc_request,
	.set_ios = aml_sd_emmc_set_ios,
	.enable_sdio_irq = aml_sd_emmc_enable_sdio_irq,
	.get_cd = aml_sd_emmc_get_cd,
	.get_ro = aml_sd_emmc_get_ro,
	.start_signal_voltage_switch = aml_signal_voltage_switch,
	.card_busy = aml_sd_emmc_card_busy,
	.execute_tuning = aml_sd_emmc_execute_tuning,
	.hw_reset = aml_emmc_hw_reset,
};



/* static ssize_t sd_emmc_debug_func(struct class *class,
struct class_attribute *attr, const char *buf, size_t count)
{
int ret;

ret = sscanf(buf, "%x", &sd_emmc_debug_flag);

pr_info("sd_emmc_debug_flag: %d\n", sd_emmc_debug_flag);

return count;
}

static ssize_t show_sd_emmc_debug(struct class *class,
struct class_attribute *attr,	char *buf)
{

pr_info("sd_emmc_debug_flag: %d\n", sd_emmc_debug_flag);

pr_info("9 : Force sd_emmc irq timeout error\n");

return 0;
}

static struct class_attribute sd_emmc_class_attrs[] = {
__ATTR(debug,  S_IRUGO | S_IWUSR , show_sd_emmc_debug, sd_emmc_debug_func),
__ATTR_NULL
};*/

static struct amlsd_host *aml_sd_emmc_init_host(struct amlsd_host *host)
{
	/* struct amlsd_host *host; */

	spin_lock_init(&aml_sd_emmc_claim.lock);
	init_waitqueue_head(&aml_sd_emmc_claim.wq);

	/* host = kzalloc(sizeof(struct amlsd_host), GFP_KERNEL); */

#ifdef SD_EMMC_DATA_TASKLET
	tasklet_init(&sd_emmc_finish_tasklet,
		aml_sd_emmc_data_tasklet, (unsigned long)host);

	if (request_irq(host->irq, aml_sd_emmc_irq,
		IRQF_DISABLED, "sd_emmc", (void *)host)) {
		sd_emmc_err("Request sd_emmc Irq Error!\n");
		return NULL;
	}
#else
	if (request_threaded_irq(host->irq, aml_sd_emmc_irq,
		aml_sd_emmc_data_thread, IRQF_DISABLED, "sd_emmc",
		(void *)host)) {
		sd_emmc_err("Request sd_emmc Irq Error!\n");
		return NULL;
	}
#endif

	/* for descriptor info */
	host->desc_buf = dma_alloc_coherent(host->dev,
		SD_EMMC_MAX_DESC_MUN*(sizeof(struct sd_emmc_desc_info)),
				&host->desc_dma_addr, GFP_KERNEL);
	if (NULL == host->desc_buf) {
		sd_emmc_err(" desc_buf Dma alloc Fail!\n");
		return NULL;
	}


	/* do not need malloc one dma buffer later */
	host->bn_buf = dma_alloc_coherent(host->dev, SD_EMMC_BOUNCE_REQ_SIZE,
				&host->bn_dma_buf, GFP_KERNEL);
	if (NULL == host->bn_buf) {
		sd_emmc_err("Dma alloc Fail!\n");
		return NULL;
	}


#ifdef SD_EMMC_ENABLE_TIMEOUT
	INIT_DELAYED_WORK(&host->timeout, aml_sd_emmc_timeout);
#endif

	spin_lock_init(&host->mrq_lock);
	mutex_init(&host->pinmux_lock);
	host->xfer_step = XFER_INIT;

	INIT_LIST_HEAD(&host->sibling);

	host->init_flag = 1;

	host->version = AML_MMC_VERSION;
	host->storage_flag = storage_flag;
	host->pinctrl = NULL;
	host->is_gated = false;
	host->status = HOST_INVALID;
	host->msg_buf = kmalloc(MESSAGE_BUF_SIZE, GFP_KERNEL);
	if (!host->msg_buf)
		pr_info("malloc message buffer fail\n");

#ifdef CONFIG_MMC_AML_DEBUG
	host->req_cnt = 0;
	sd_emmc_err("CONFIG_MMC_AML_DEBUG is on!\n");
#endif

#ifdef CONFIG_AML_MMC_DEBUG_FORCE_SINGLE_BLOCK_RW
	sd_emmc_err("CONFIG_AML_MMC_DEBUG_FORCE_SINGLE_BLOCK_RW is on!\n");
#endif

/* host->debug.name = kzalloc( */
/* strlen((const char *)AML_SD_EMMC_MAGIC)+1, GFP_KERNEL); */
/* strcpy((char *)(host->debug.name), (const char *)AML_SD_EMMC_MAGIC); */
/* host->debug.class_attrs = sd_emmc_class_attrs; */
/* if (class_register(&host->debug)) */
/* pr_info(" class register nand_class fail!\n"); */

	return host;
}

static const char *emmc_common_usage_str = {
"Usage:\n"
"echo print >debug\n"
"echo status >debug\n"
};

static const char *emmc_read_usage_str = {
"Usage:\n"
"echo clock >read\n"
"echo reg >read\n"
"echo rx_phase >read\n"
"echo tx_phase >read\n"
"echo line_delay >read\n"
"echo co_phase >read\n"
};

static ssize_t emmc_debug_common_help(struct class *class,
				struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", emmc_common_usage_str);
}
static ssize_t emmc_read_help(struct class *class,
				struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", emmc_read_usage_str);
}

static const char *emmc_usage_str = {
"Usage:\n"
"echo clock value >debug\n"
"echo line_dly 0-15 >debug\n"
"echo rx_phase 0-3 >debug\n"
"echo tx_phase 0-3 >debug\n"
"echo co_phase 0-3 >debug\n"
};
static ssize_t emmc_debug_help(struct class *class,
		struct class_attribute *attr, char *buf)
{
	return sprintf(buf, "%s\n", emmc_usage_str);
}
static ssize_t emmc_debug(struct class *class, struct class_attribute *attr,
			const char *buf, size_t count)
{
	unsigned int ret;
	unsigned int t[6];
	struct mmc_host *mmc = host_emmc->mmc;
	struct amlsd_platform *pdata = mmc_priv(mmc);
	struct sd_emmc_regs *sd_emmc_regs = host_emmc->sd_emmc_regs;
	u32 vclkc = sd_emmc_regs->gclock;
	struct sd_emmc_clock *pclkc = (struct sd_emmc_clock *)&vclkc;
	u32 line_delay = sd_emmc_regs->gdelay;
	struct sd_emmc_delay *line_dly = (struct sd_emmc_delay *)&line_delay;
	u32 adjust = sd_emmc_regs->gadjust;
	struct sd_emmc_adjust *gadjust = (struct sd_emmc_adjust *)&adjust;
	u32 clk_rate = 1000000000;
	u32 vctrl = sd_emmc_regs->gcfg;
	struct sd_emmc_config *ctrl = (struct sd_emmc_config *)&vctrl;
	u32 clock;
	switch (buf[0]) {
	case 'c':
		if (buf[1] == 'l') {
			ret = sscanf(buf, "clock %d", &t[0]);
			if (t[0] < 50)
				pclkc->div = t[0];
			else
				pclkc->div = (clk_rate / t[0]) +
							(!!(clk_rate % t[0]));
		} else if (buf[1] == 'o') {
			ret = sscanf(buf, "co_phase %d", &t[0]);
			pclkc->core_phase = t[0];
		}
		break;
	case 'l':
		ret = sscanf(buf, "line_dly %d", &t[0]);
		line_dly->dat0 = t[0];
		line_dly->dat1 = t[0];
		line_dly->dat2 = t[0];
		line_dly->dat3 = t[0];
		line_dly->dat4 = t[0];
		line_dly->dat5 = t[0];
		line_dly->dat6 = t[0];
		line_dly->dat7 = t[0];
		gadjust->cmd_delay = t[0];
		gadjust->ds_delay = t[0];
		break;
	case 'r':
		ret = sscanf(buf, "rx_phase %d", &t[0]);
		pclkc->rx_phase = t[0];
		break;
	case 't':
		ret = sscanf(buf, "tx_phase %d", &t[0]);
		pclkc->tx_phase = t[0];
		break;
	default:
		break;
	}
	if (vclkc != sd_emmc_regs->gclock) {
		sd_emmc_regs->gclock = vclkc;
		pdata->clkc = sd_emmc_regs->gclock;
		clock = clk_rate / pclkc->div;
		if (sd_emmc_regs->gcfg & (1 << 2))
			pdata->mmc->actual_clock = clock / 2;
		else
			pdata->mmc->actual_clock = clock;
		pr_info("emmc: sd_emmc_regs->gclock = 0x%x\n",
						sd_emmc_regs->gclock);
		pr_info("clock %s mode = %d\n", (ctrl->ddr ? "DDR" : "SDR"),
			(ctrl->ddr ? (clock / 2) : clock));
	}
	if (line_delay != sd_emmc_regs->gdelay) {
		sd_emmc_regs->gdelay = line_delay;
		sd_emmc_regs->gadjust = adjust;
		pr_info("emmc: sd_emmc_regs->gdelay = 0x%x\n",
					sd_emmc_regs->gdelay);
		pr_info("emmc: sd_emmc_regs->gadjust = 0x%x\n",
					sd_emmc_regs->gadjust);
	}

	if (ret != 1 || ret != 2)
		return -EINVAL;
	return count;
}
static ssize_t emmc_read_debug(struct class *class,
					struct class_attribute *attr,
					const char *buf, size_t count)
{
	unsigned int ret;
	struct sd_emmc_regs *sd_emmc_regs = host_emmc->sd_emmc_regs;
	u32 vclkc = sd_emmc_regs->gclock;
	struct sd_emmc_clock *pclkc = (struct sd_emmc_clock *)&vclkc;
	u32 line_delay = sd_emmc_regs->gdelay;
	struct sd_emmc_delay *line_dly = (struct sd_emmc_delay *)&line_delay;
	u32 clk_rate = 1000000000;
	u32 vctrl = sd_emmc_regs->gcfg;
	struct sd_emmc_config *ctrl = (struct sd_emmc_config *)&vctrl;
	u32 clock;

	switch (buf[0]) {
	case 'c':
		if (buf[1] == 'l') {
			clock = clk_rate / pclkc->div;
			pr_info("emmc: sd_emmc_regs->gclock = 0x%x\n",
					sd_emmc_regs->gclock);
			pr_info("%s mode clock = %d\n",
					(ctrl->ddr ? "DDR" : "SDR"),
					(ctrl->ddr ? (clock / 2) : clock));
		} else if (buf[1] == 'o') {
			pr_info("core_phase = 0x%x\n", pclkc->core_phase);
		} else
			;
		break;
	case 'l':
		pr_info("line_deley = 0x%x\n", line_dly->dat0);
			break;
	case 'r':
		if (buf[1] == 'x') {
				pr_info("rx_phase = 0x%x\n", pclkc->rx_phase);
		} else if (buf[1] == 'e') {
			pr_info("registe:\n");
			pr_info("gclock =0x%x\n", sd_emmc_regs->gclock);
			pr_info("gdelay =0x%x\n", sd_emmc_regs->gdelay);
			pr_info("gadjust =0x%x\n", sd_emmc_regs->gadjust);
			pr_info("gcalout =0x%x\n", sd_emmc_regs->gcalout);
			pr_info("gstart =0x%x\n", sd_emmc_regs->gstart);
			pr_info("gcfg =0x%x\n", sd_emmc_regs->gcfg);
			pr_info("gstatus =0x%x\n", sd_emmc_regs->gstatus);
			pr_info("girq_en =0x%x\n", sd_emmc_regs->girq_en);
		}
		break;
	case 't':
		pr_info("tx_phase = 0x%x\n", pclkc->tx_phase);
		break;
	default:
		break;
	}
	if (ret != 1 || ret != 2)
		return -EINVAL;
	return count;
}
static struct class_attribute emmc_debug_class_attrs[] = {
	__ATTR(debug,  S_IRUGO | S_IWUSR, emmc_debug_help, emmc_debug),
	__ATTR(help,  S_IRUGO | S_IWUSR, emmc_debug_common_help, NULL),
	__ATTR(read,  S_IRUGO | S_IWUSR, emmc_read_help, emmc_read_debug),
};

static int creat_emmc_class(void)
{
	emmc_class = class_create(THIS_MODULE, "emmc");
	if (IS_ERR(emmc_class)) {
		pr_err("create emmc_class debug class fail\n");
		return -1;
	}
	return 0;
}

static int creat_emmc_attr(void)
{
	int i;

	if (emmc_class == NULL) {
		pr_info("no emmc debug class exist\n");
		return -1;
	}

	for (i = 0; i < ARRAY_SIZE(emmc_debug_class_attrs); i++) {
		if (class_create_file(emmc_class, &emmc_debug_class_attrs[i]))
			pr_err("create emmc debug attribute %s fail\n",
				emmc_debug_class_attrs[i].attr.name);
	}

	return 0;
}

/* return storage device */
static u32 _get_storage_dev_by_gp(void)
{
	u32 storage_dev, boot_dev;
	u32 ret = 0;
	void __iomem *gp_cfg0;
	void __iomem *gp_cfg2;

	gp_cfg0 = ioremap_nocache(A0_GP_CFG0, sizeof(u32));
	gp_cfg2 = ioremap_nocache(A0_GP_CFG2, sizeof(u32));

	storage_dev = (readl(gp_cfg2) >> 28) & 0x7;
	boot_dev = readl(gp_cfg0) & 0xF;

	pr_info("storage %d, boot %d\n", storage_dev, boot_dev);

	switch (storage_dev) {
	case STORAGE_DEV_NOSET:
	/* old uboot, storage_dev was not set by bl3, check boot dev */
		switch (boot_dev) {
		case STORAGE_DEV_NAND:
		case STORAGE_DEV_EMMC:
			ret = boot_dev;
		break;
		case STORAGE_DEV_SDCARD:
		/* fixme...*/
			pr_err("warning you may need update your uboot!");
			BUG();
		break;
		default:
		break;
		}
	break;
	/* new uboot, already set by bl3 */
	case STORAGE_DEV_NAND:
	case STORAGE_DEV_EMMC:
		ret = storage_dev;
	break;
	default:
	/*do nothing.*/
	break;
	}
	/*pr_info("%s return %d\n", __func__, ret);*/
	return ret;
}

/* fixme*/
static u32 get_storage_dev_by_clk(void)
{
	u32 ret = 0;
	BUG();
	return ret;
}

/*
 return  1: emmc
		 2: nand
 */
static u32 get_storage_dev(void)
{
	u32 ret;

	ret = _get_storage_dev_by_gp();
	if (ret == 0) {
		/*get storage media by clock reg */
		ret = get_storage_dev_by_clk();
	}
	pr_err("%s return %d\n", __func__, ret);
	return ret;
}

int is_storage_emmc(void)
{
	int ret = 0;
	if (get_storage_dev() == STORAGE_DEV_EMMC)
		ret = 1;
	return ret;
}
static int aml_sd_emmc_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc = NULL;
	struct amlsd_host *host = NULL;
	struct amlsd_platform *pdata;
	struct resource *res_mem, *res_irq;
	int ret = 0, i;
	int size;
	/* pre_probe_host_ops(); // for tmp debug */

	/* enable debug */
	/* sd_emmc_debug = 0xffff; */
	pr_info("%s: line %d\n", __func__, __LINE__);
	aml_mmc_ver_msg_show();

	host = kzalloc(sizeof(struct amlsd_host), GFP_KERNEL);
	if (!host)
		return -ENODEV;

	res_mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res_mem) {
		pr_info("error to get IORESOURCE\n");
		goto fail_init_host;
	}
	size = resource_size(res_mem);
	res_irq = platform_get_resource(pdev, IORESOURCE_IRQ, 0);
	if (!res_irq) {
		kfree(host);
		pr_info("error to get irq resource\n");
		return -ENODEV;
	}
	host->irq = irq_of_parse_and_map(pdev->dev.of_node, 0);
	host->base = ioremap(0xc8834400, 0x200);
	host->sd_emmc_regs = (struct sd_emmc_regs *)
			devm_ioremap_nocache(&pdev->dev, res_mem->start, size);
	host->dma_gdesc = res_mem->start+0x200;
	host->dma_gping = res_mem->start+0x400;
	host->dma_gpong = res_mem->start+0x600;
	host->pdev = pdev;
	host->dev = &pdev->dev;
	aml_sd_emmc_init_host(host);
	if (!host)
		goto fail_init_host;

/* if (amlsd_get_reg_base(pdev, host)) */
/* goto fail_init_host; */


	aml_sd_emmc_reg_init(host);

	for (i = 0; i < MMC_MAX_DEVICE; i++) {
		/*malloc extra amlsd_platform*/
		mmc = mmc_alloc_host(sizeof(struct amlsd_platform), &pdev->dev);
		if (!mmc) {
			ret = -ENOMEM;
			goto probe_free_host;
		}

		pdata = mmc_priv(mmc);
		memset(pdata, 0, sizeof(struct amlsd_platform));
		if (amlsd_get_platform_data(pdev, pdata, mmc, i)) {
			mmc_free_host(mmc);
			break;
		}

		if (aml_card_type_mmc(pdata)) {
			if (!is_storage_emmc()) {
				mmc_free_host(mmc);
				goto fail_init_host;
			}
		}
		dev_set_name(&mmc->class_dev, "%s", pdata->pinname);

		INIT_DELAYED_WORK(&pdata->retuning, aml_sd_emmc_tuning_timer);

		if (pdata->caps & MMC_CAP_NONREMOVABLE)
			pdata->is_in = true;

		if (pdata->caps & MMC_PM_KEEP_POWER)
			mmc->pm_caps |= MMC_PM_KEEP_POWER;
		pdata->host = host;
		pdata->mmc = mmc;
		pdata->is_fir_init = true;
		pdata->is_tuned = false;
		pdata->need_retuning = false;
		pdata->signal_voltage = 0xff; /* init as an invalid value */
		host->is_tunning = 0;
		mmc->index = i;
		mmc->ops = &aml_sd_emmc_ops;
		mmc->alldev_claim = &aml_sd_emmc_claim;
		mmc->ios.clock = 400000;
		mmc->ios.bus_width = MMC_BUS_WIDTH_1;
		mmc->max_blk_count = 4095; /*  */
		mmc->max_blk_size = 4095; /*  */
		mmc->max_req_size = pdata->max_req_size;
		mmc->max_seg_size = mmc->max_req_size;
		mmc->max_segs = 1024;
		mmc->ocr_avail = pdata->ocr_avail;
		mmc->ocr = pdata->ocr_avail;
		mmc->caps = pdata->caps;
		mmc->caps2 = pdata->caps2;
		mmc->f_min = pdata->f_min;
		mmc->f_max = pdata->f_max;
		mmc->max_current_180 = 300; /* 300 mA in 1.8V */
		mmc->max_current_330 = 300; /* 300 mA in 3.3V */

		if (aml_card_type_sdio(pdata)) { /* if sdio_wifi */
			mmc->host_rescan_disable = true;
				/* do NOT run mmc_rescan for the first time */
				mmc->rescan_entered = 1;
		} else {
			mmc->host_rescan_disable = false;
			mmc->rescan_entered = 0;
		}
		if (aml_card_type_mmc(pdata)) {
			/* Poll down BOOT_15 in case hardward not pull down */
			u32 boot_poll_en, boot_poll_down;
			boot_poll_down = readl(host->base + BOOT_POLL_UP_DOWN);
			boot_poll_down &= (~(1 << 15));
			boot_poll_en = readl(host->base + BOOT_POLL_UP_DOWN_EN);
			boot_poll_en |= (1 << 15);
			writel(boot_poll_down, host->base + BOOT_POLL_UP_DOWN);
			writel(boot_poll_en, host->base + BOOT_POLL_UP_DOWN_EN);
			host_emmc = host;
			creat_emmc_class();
			creat_emmc_attr();
#ifdef CALIBRATION
			pdata->need_cali = 1;
#endif
		}
		if (pdata->port_init)
			pdata->port_init(pdata);

		aml_sduart_pre(pdata);

		ret = mmc_add_host(mmc);
		if (ret) { /* error */
			sd_emmc_err("Failed to add mmc host.\n");
			goto probe_free_host;
		} else { /* ok */
			if (aml_card_type_sdio(pdata)) { /* if sdio_wifi */
			sdio_host = mmc;
			/* do NOT run mmc_rescan for the first time */
			/* mmc->rescan_entered = 1;*/
			}
		}

		/* aml_sd_emmc_init_debugfs(mmc); */
		/*Add each mmc host pdata to this controller host list*/
		INIT_LIST_HEAD(&pdata->sibling);
		list_add_tail(&pdata->sibling, &host->sibling);

		/*Register card detect irq : plug in & unplug*/
		if (pdata->irq_in && pdata->irq_out) {
			host->irq_in = irq_of_parse_and_map(
				pdev->dev.of_node, 1);
			host->irq_out = irq_of_parse_and_map(
				pdev->dev.of_node, 2);
			pdata->irq_init(pdata);
			ret = request_threaded_irq(host->irq_in,
				(irq_handler_t)aml_sd_irq_cd, aml_irq_cd_thread,
				IRQF_DISABLED, "sd_emmc_mmc_in", (void *)pdata);
			if (ret) {
				sd_emmc_err("Failed to request mmc IN detect\n");
				goto probe_free_host;
			}
			ret |= request_threaded_irq(host->irq_out,
				(irq_handler_t)aml_sd_irq_cd,
				aml_irq_cd_thread,
				IRQF_DISABLED,
				"sd_emmc_mmc_out", (void *)pdata);
			if (ret) {
				sd_emmc_err("Failed to request mmc OUT detect\n");
				goto fail_cd_irq_in;
			}
		}
	}

	print_tmp("%s() success!\n", __func__);
	platform_set_drvdata(pdev, host);
	return 0;

fail_cd_irq_in:
	if (pdata->irq_in)
		free_irq(pdata->irq_in, pdata);
probe_free_host:
	list_for_each_entry(pdata, &host->sibling, sibling) {
	mmc = pdata->mmc;
	mmc_remove_host(mmc);
	mmc_free_host(mmc);
	}
fail_init_host:
	free_irq(host->irq, host);
#ifndef SD_EMMC_REQ_DMA_SGMAP
	dma_free_coherent(host->dev, SD_EMMC_BOUNCE_REQ_SIZE, host->bn_buf,
	(dma_addr_t)host->bn_dma_buf);
#endif
	kfree(host);
	print_tmp("aml_sd_emmc_probe() fail!\n");
	return ret;
}

int aml_sd_emmc_remove(struct platform_device *pdev)
{
	struct amlsd_host *host  = platform_get_drvdata(pdev);
	struct mmc_host *mmc;
	struct amlsd_platform *pdata;

#ifndef SD_EMMC_REQ_DMA_SGMAP
	dma_free_coherent(NULL, SD_EMMC_BOUNCE_REQ_SIZE, host->bn_buf,
	(dma_addr_t)host->bn_dma_buf);
#endif

	free_irq(host->irq, host);
	iounmap(host->base);

	list_for_each_entry(pdata, &host->sibling, sibling) {
	mmc = pdata->mmc;
	mmc_remove_host(mmc);
	mmc_free_host(mmc);
	}

	aml_devm_pinctrl_put(host);

	kfree(host->msg_buf);
	kfree(host);

	/* switch_mod_gate_by_type(MOD_SD_EMMC, 0); // gate clock off sd_emmc */

	return 0;
}

static const struct of_device_id aml_sd_emmc_dt_match[] = {
	{
	.compatible = "amlogic, aml_sd_emmc",
	},
	{},
};

MODULE_DEVICE_TABLE(of, aml_sd_emmc_dt_match);

static struct platform_driver aml_sd_emmc_driver = {
	.probe		 = aml_sd_emmc_probe,
	.remove		= aml_sd_emmc_remove,
	.suspend	= aml_sd_emmc_suspend,
	.resume		= aml_sd_emmc_resume,
	.driver		= {
	.name = "aml_sd_emmc",
	.owner = THIS_MODULE,
		.of_match_table = aml_sd_emmc_dt_match,
#ifdef CONFIG_HIBERNATION
		.pm     = &aml_sd_emmc_pm,
#endif
	},
};

static int __init aml_sd_emmc_init(void)
{
	return platform_driver_register(&aml_sd_emmc_driver);
}

static void __exit aml_sd_emmc_cleanup(void)
{
	platform_driver_unregister(&aml_sd_emmc_driver);
}

module_init(aml_sd_emmc_init);
module_exit(aml_sd_emmc_cleanup);

MODULE_DESCRIPTION("Amlogic Multimedia Card driver");
MODULE_LICENSE("GPL");
