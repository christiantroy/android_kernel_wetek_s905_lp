/*
 * include/linux/amlogic/vout/vout_notify.h
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


#ifndef _VOUT_NOTIFY_H_
#define _VOUT_NOTIFY_H_

/* Linux Headers */
#include <linux/notifier.h>
#include <linux/list.h>
#include <linux/pm.h>

/* Local Headers */
#include "vinfo.h"

struct vout_op_s {
	const struct vinfo_s* (*get_vinfo)(void);
	int (*set_vmode)(enum vmode_e);
	enum vmode_e (*validate_vmode)(char *);
	int (*vmode_is_supported)(enum vmode_e);
	int (*disable)(enum vmode_e);
	int (*set_vframe_rate_hint)(int);
	int (*set_vframe_rate_end_hint)(void);
	int (*vout_suspend)(void);
	int (*vout_resume)(void);
};

struct vout_server_s {
	struct list_head list;
	char *name;
	struct vout_op_s op;
};

struct vout_module_s {
	struct list_head vout_server_list;
	struct vout_server_s *curr_vout_server;
};

extern int vout_register_client(struct notifier_block *);
extern int vout_unregister_client(struct notifier_block *);
extern int vout_register_server(struct vout_server_s *);
extern int vout_unregister_server(struct vout_server_s *);
extern int vout_notifier_call_chain(unsigned long, void *);

extern const struct vinfo_s *get_current_vinfo(void);
extern enum vmode_e get_current_vmode(void);
extern int set_current_vmode(enum vmode_e);
extern enum vmode_e validate_vmode(char *);
extern int set_vframe_rate_hint(int);
extern int set_vframe_rate_end_hint(void);

#ifdef CONFIG_AML_VOUT_FRAMERATE_AUTOMATION
extern void update_vmode_status(char *name);
extern void set_vout_mode_fr_auto(char *name);
#endif

extern int vout_suspend(void);
extern int vout_resume(void);

extern int get_power_level(void);

#define VOUT_EVENT_MODE_CHANGE         0x00010000
#define VOUT_EVENT_OSD_BLANK           0x00020000
#define VOUT_EVENT_OSD_DISP_AXIS       0x00030000
#define VOUT_EVENT_OSD_PREBLEND_ENABLE 0x00040000

/* vout2 */
extern int vout2_register_client(struct notifier_block *);
extern int vout2_unregister_client(struct notifier_block *);
extern int vout2_register_server(struct vout_server_s *);
extern int vout2_unregister_server(struct vout_server_s *);
extern int vout2_notifier_call_chain(unsigned long, void *);

extern const struct vinfo_s *get_current_vinfo2(void);
extern enum vmode_e get_current_vmode2(void);
extern int set_current_vmode2(enum vmode_e);
extern enum vmode_e validate_vmode2(char *);
extern void set_vout2_mode_internal(char *name);
extern enum vmode_e get_logo_vmode(void);
extern int set_logo_vmode(enum vmode_e);

extern int vout2_suspend(void);
extern int vout2_resume(void);

extern void update_vout_mode_attr(const struct vinfo_s *vinfo);

extern char *get_vout_mode_internal(void);

#endif /* _VOUT_NOTIFY_H_ */
