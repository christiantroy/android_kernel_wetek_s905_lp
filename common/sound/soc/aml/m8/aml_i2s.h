/*
 * sound/soc/aml/m8/aml_i2s.h
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

#ifndef __AML_I2S_H__
#define __AML_I2S_H__

/* #define debug_printk */
#ifdef debug_printk
#define dug_printk(fmt, args...)  printk(fmt, ## args)
#else
#define dug_printk(fmt, args...)
#endif

struct audio_stream {
	int stream_id;
	int active;
	unsigned int last_ptr;
	unsigned int size;
	unsigned int sample_rate;
	unsigned int I2S_addr;
	spinlock_t lock;
	struct snd_pcm_substream *stream;
	unsigned i2s_mode; /* 0:master, 1:slave, */
	unsigned device_type;
	unsigned int xrun_num;
};
struct aml_audio_buffer {
	void *buffer_start;
	unsigned int buffer_size;
};

struct aml_i2s_dma_params {
	char *name;			/* stream identifier */
	struct snd_pcm_substream *substream;
	void (*dma_intr_handler)(u32, struct snd_pcm_substream *);
};
struct aml_dai_info {
	unsigned i2s_mode; /* 0:master, 1:slave, */
};
enum {
	I2S_MASTER_MODE = 0,
	I2S_SLAVE_MODE,
};
/*--------------------------------------------------------------------------*\
 * Data types
\*--------------------------------------------------------------------------*/
struct aml_runtime_data {
	struct aml_i2s_dma_params *params;
	dma_addr_t dma_buffer;		/* physical address of dma buffer */
	dma_addr_t dma_buffer_end;	/* first address beyond DMA buffer */

	struct snd_pcm *pcm;
	struct snd_pcm_substream *substream;
	struct audio_stream s;
	struct timer_list timer;	/* timeer for playback and capture */
	struct hrtimer hrtimer;
	void *buf; /* tmp buffer for playback or capture */
};

extern struct snd_soc_platform_driver aml_soc_platform;
/* extern struct aml_audio_interface aml_i2s_interface; */

#endif
