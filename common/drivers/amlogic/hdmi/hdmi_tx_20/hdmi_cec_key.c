/*
 * drivers/amlogic/hdmi/hdmi_tx_20/hdmi_cec_key.c
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

#include <linux/input.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_cec.h>
#include <linux/amlogic/hdmi_tx/hdmi_tx_module.h>
#include <linux/delay.h>
#include <linux/timer.h>

struct hdmitx_dev *hdmitx_device = NULL;

#define HR_DELAY(n)		(ktime_set(0, n * 1000 * 1000))
__u16 cec_key_map[128] = {
	KEY_SELECT, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, 0 , 0 , 0 ,/* 0x00 */
	0 , KEY_HOMEPAGE , KEY_MENU, 0, 0, KEY_BACK, 0, 0,
	0 , 0, 0, 0, 0, 0, 0, 0, /* 0x10 */
	0 , 0, 0, 0, 0, 0, 0, 0,
	KEY_0 , KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7,
	KEY_8 , KEY_9, KEY_DOT, 0, 0, 0, 0, 0,
	0 , 0, 0, 0, 0, 0, 0, 0,/* 0x30 */
	0 , 0, 0, 0, 0, 0, 0, 0,

	KEY_POWER , KEY_VOLUMEUP, KEY_VOLUMEDOWN, KEY_MUTE,
	KEY_PLAYPAUSE, KEY_STOP, KEY_PLAYPAUSE, KEY_RECORD, /* 0x40 */
	KEY_REWIND, KEY_FASTFORWARD, KEY_EJECTCD, KEY_NEXTSONG,
	KEY_PREVIOUSSONG, 0, 0, 0,
	0 , 0, 0, 0, 0, 0, 0, 0, /* 0x50 */
	0 , 0, 0, 0, 0, 0, 0, 0,
	KEY_PLAYCD, KEY_PLAYPAUSE, KEY_RECORD, KEY_PAUSECD,
	KEY_STOPCD, KEY_MUTE, 0, KEY_TUNER,/* 0x60 */
	0 , KEY_MEDIA, 0, 0, KEY_POWER, KEY_POWER, 0, 0,
	0 , KEY_BLUE, KEY_RED, KEY_GREEN, KEY_YELLOW, 0, 0, 0,/* 0x70 */
	0 , 0, 0, 0, 0, 0, 0, 0x2fd,
};

struct hrtimer cec_key_timer;
static int last_key_irq = -1;
enum hrtimer_restart cec_key_up(struct hrtimer *timer)
{
	hdmi_print(INF, CEC"last:%d up\n", cec_key_map[last_key_irq]);
	input_event(cec_global_info.remote_cec_dev,
		    EV_KEY, cec_key_map[last_key_irq], 0);
	input_sync(cec_global_info.remote_cec_dev);

	return HRTIMER_NORESTART;
}

void cec_send_event(struct cec_rx_message_t *pcec_message)
{
	int i;
	unsigned char brdcst, opcode;
	unsigned char initiator, follower;
	unsigned char operand_num;
	unsigned char msg_length;
	unsigned char operands[14];

	/* parse message */
	if ((!pcec_message) || !check_cec_msg_valid(pcec_message))
		return;

	initiator   = pcec_message->content.msg.header >> 4;
	follower	= pcec_message->content.msg.header & 0x0f;
	opcode	  = pcec_message->content.msg.opcode;
	operand_num = pcec_message->operand_num;
	brdcst	  = (follower == 0x0f);
	msg_length  = pcec_message->msg_length;

	for (i = 0; i < operand_num; i++) {
		operands[i] = pcec_message->content.msg.operands[i];
		hdmi_print(INF, CEC  ":operands[%d]:%u\n", i, operands[i]);
	}
	if (cec_global_info.cec_flag.cec_key_flag) {
		input_event(cec_global_info.remote_cec_dev,
			EV_KEY, cec_key_map[operands[0]], 1);
		input_sync(cec_global_info.remote_cec_dev);
		hdmi_print(INF, CEC  ":key map:%d\n", cec_key_map[operands[0]]);
	} else {
		input_event(cec_global_info.remote_cec_dev, EV_KEY,
		    cec_key_map[operands[0]], 0);
		input_sync(cec_global_info.remote_cec_dev);
		hdmi_print(INF, CEC  ":key map:%d\n", cec_key_map[operands[0]]);
	}
}


void cec_send_event_irq(void)
{
	int i;
	unsigned char   operand_num_irq;
	unsigned char operands_irq[14];
	unsigned int mask;
	static unsigned int last_key = -1;
	static s64 last_key_report;
	s64 cur_time;
	ktime_t now = ktime_get();

	operand_num_irq = cec_global_info.cec_rx_msg_buf.cec_rx_message
	    [cec_global_info.cec_rx_msg_buf.rx_write_pos].operand_num;
	for (i = 0; i < operand_num_irq; i++) {
		operands_irq[i] = cec_global_info.cec_rx_msg_buf.cec_rx_message
		    [cec_global_info.cec_rx_msg_buf.rx_write_pos]
		    .content.msg.operands[i];
		hdmi_print(INF, CEC  ":operands_irq[%d]:0x%x\n", i,
			operands_irq[i]);
	}

	switch (cec_global_info.cec_rx_msg_buf.cec_rx_message
		[cec_global_info.cec_rx_msg_buf.rx_write_pos].
		content.msg.operands[0]) {
	case 0x33:
		/* cec_system_audio_mode_request(); */
		/* cec_set_system_audio_mode(); */
		break;
	case 0x35:
		break;
	default:
		break;
	}
	mask = (1 << CEC_FUNC_MSAK) | (1 << ONE_TOUCH_STANDBY_MASK);
	if (((hdmitx_device->cec_func_config & mask) != mask) &&
	    (cec_key_map[operands_irq[0]] == KEY_POWER)) {
		hdmi_print(INF, CEC "ignore power key when standby disabled\n");
		return;
	}

	cur_time = ktime_to_us(now);
	if (last_key == -1)
		last_key = operands_irq[0];
	else {
		/*
		 * filter for 2 key power function nearby
		 */
		if ((cec_key_map[last_key] == cec_key_map[operands_irq[0]]) &&
		    (cec_key_map[operands_irq[0]] == KEY_POWER)) {
			if (cur_time - last_key_report < (s64)(600 * 1000)) {
				pr_info("%s:%x, cur:%x, diff:%lld\n",
					"same key function too quick, last",
					last_key, operands_irq[0],
					cur_time - last_key_report);
				pr_info("ignore this key\n");
				return;
			}
		}
	}

	input_event(cec_global_info.remote_cec_dev, EV_KEY,
		    cec_key_map[operands_irq[0]], 1);
	input_sync(cec_global_info.remote_cec_dev);
	last_key_irq = operands_irq[0];
	hrtimer_start(&cec_key_timer, HR_DELAY(200), HRTIMER_MODE_REL);
	hdmi_print(INF, CEC  ":key map:%d\n", cec_key_map[operands_irq[0]]);
	last_key_report = cur_time;
}

void cec_user_control_pressed_irq(void)
{
	hdmi_print(INF, CEC  ": Key pressed\n");
	cec_send_event_irq();
}

void cec_user_control_released_irq(void)
{
	hdmi_print(INF, CEC  ": Key released\n");
	/*
	 * key must be valid
	 */
	if (last_key_irq != -1) {
		hrtimer_cancel(&cec_key_timer);
		input_event(cec_global_info.remote_cec_dev,
		    EV_KEY, cec_key_map[last_key_irq], 0);
		input_sync(cec_global_info.remote_cec_dev);
	}
}

void cec_user_control_pressed(struct cec_rx_message_t *pcec_message)
{
	hdmi_print(INF, CEC  ": Key pressed\n");
	cec_global_info.cec_flag.cec_key_flag = 1;
	cec_send_event(pcec_message);
}

void cec_user_control_released(struct cec_rx_message_t *pcec_message)
{
	hdmi_print(INF, CEC  ": Key released\n");
	cec_global_info.cec_flag.cec_key_flag = 1;
	cec_send_event(pcec_message);
}


/*
 * STANDBY: get STANDBY command from TV
 */
void cec_standby(struct cec_rx_message_t *pcec_message)
{
	unsigned int mask;
	static s64 last_standby;
	s64 cur_time;

	ktime_t now = ktime_get();
	cur_time = ktime_to_us(now);
	pr_info("cur time:%lld, last standby:%lld, diff:%lld\n",
		cur_time, last_standby, cur_time - last_standby);
	if (cur_time - last_standby < (s64)(600 * 1000)) {
		/* small than 600ms */
		pr_info("standby recieved too much\n");
		return;
	}
	mask = (1 << CEC_FUNC_MSAK) | (1 << ONE_TOUCH_STANDBY_MASK);
	if ((hdmitx_device->cec_func_config & mask) == mask) {
		hdmi_print(INF, CEC  ": System will be in standby mode\n");
		input_event(cec_global_info.remote_cec_dev,
			EV_KEY, KEY_POWER, 1);
		input_sync(cec_global_info.remote_cec_dev);
		input_event(cec_global_info.remote_cec_dev,
			EV_KEY, KEY_POWER, 0);
		input_sync(cec_global_info.remote_cec_dev);
	}
	last_standby = cur_time;
}

void cec_key_init(void)
{
	hdmitx_device = get_hdmitx_device();
}

