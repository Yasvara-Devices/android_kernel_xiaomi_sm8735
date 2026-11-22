// SPDX-License-Identifier: GPL-2.0
/*
 * mca_smart_charge.c
 *
 * smart_charge driver
 *
 * Copyright (c) 2023-2023 Xiaomi Technologies Co., Ltd.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/workqueue.h>
#include <linux/jiffies.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/thermal.h>
#include <linux/kdev_t.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/mm.h>
#include <linux/version.h>
#include <mca/protocol/protocol_class.h>
#include <mca/strategy/strategy_class.h>
#include <mca/strategy/strategy_fg_class.h>
#include <mca/strategy/strategy_soc_limit.h>
#include <mca/common/mca_event.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_parse_dts.h>
#include <mca/common/mca_voter.h>
#include <mca/common/mca_sysfs.h>
#include <mca/common/mca_panel.h>
#include <mca/smartchg/smart_chg_class.h>
#include "inc/mca_smart_charge.h"
#include <mca/common/mca_charge_mievent.h>
#include <mca/common/mca_hwid.h>
#include <mca/shared_memory/charger_partition_class.h>
#include <linux/ktime.h>
#include "hwid.h"

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_smart_charge"
#endif
#ifndef BIT
#define BIT(x) (1 << (x))
#endif

#define SMART_CHG_SOCLMT_TRIG_DEFAULT 80
#define SMART_CHG_SOCLMT_CANCEL_DEFAULT 5
#define SMART_CHARGE_DELTA_FV_DEFAULT_VALUE 0
#define SMART_CHARGE_DELTA_ICHG_DEFAULT_VALUE 0
#define SMART_CHARGE_DTPT_DELTA_VALUE 15
#define SMART_CHARGE_FAKE_SCENE_DEFAULT -1
#define MAX_MMAP_SIZE 4096

enum mca_smart_charge_ioctrl_cmd {
	MCA_SMARTCHG_CMD_DATA = 1,
	MCA_SMARTCHG_CMD_INVALID,
};

#define MCA_SMARTCHG_MAGIC 0x10
#define SMARTCHG_DATA_CMD                                \
	_IOC(_IOC_READ | _IOC_WRITE, MCA_SMARTCHG_MAGIC, \
	     MCA_SMARTCHG_CMD_DATA, PAGE_SIZE)

static struct smart_charge_info *global_smartchg_info;
static struct mca_smartchg_if_ops
	*g_mca_smartchg_if_ops[MCA_SMARTCHG_IF_CHG_TYPE_END];
static void smart_charge_report_scene(struct smart_charge_info *info,
				      int scene);

static struct mca_smartchg_if_ops *mca_smartchg_if_get_ops(unsigned int type)
{
	return g_mca_smartchg_if_ops[type];
}

int mca_smartchg_if_ops_register(struct mca_smartchg_if_ops *ops)
{
	if (!ops) {
		mca_log_err("ops invalid %d\n");
		return -1;
	}

	if (ops->type < 0 || ops->type >= MCA_SMARTCHG_IF_CHG_TYPE_END) {
		mca_log_err("ops->type invalid %d\n", ops->type);
		return -1;
	}

	g_mca_smartchg_if_ops[ops->type] = ops;
	return 0;
}
EXPORT_SYMBOL(mca_smartchg_if_ops_register);

static ssize_t smart_charge_sysfs_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf);
static ssize_t smart_charge_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count);

struct mca_sysfs_attr_info smartchg_sysfs_field_tbl[] = {
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG,
			  smart_chg),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG_FV,
			  smart_fv),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTCHG_ICHG,
			  smart_ichg),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTBATT,
			  smart_batt),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMARTNIGHT,
			  smart_night),
	mca_sysfs_attr_ro(smart_charge_sysfs, 0440, MCA_PROP_POSTURE, posture),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SCENE, scene),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_BOARD_TEMP,
			  board_temp),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_SMART_SIC_MODE,
			  smart_sic_mode),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_BYPASS_CHG,
			  bypass_charge),
	mca_sysfs_attr_ro(smart_charge_sysfs, 0444, MCA_PROP_BYPASS_CHG_STATE,
			  bypass_charge_state),
	mca_sysfs_attr_rw(smart_charge_sysfs, 0664, MCA_PROP_BYPASS_PARAMS,
			  bypass_params),
};

#define SMARTCHG_SYSFS_ATTRS_SIZE ARRAY_SIZE(smartchg_sysfs_field_tbl)

static struct attribute *smartchg_sysfs_attrs[SMARTCHG_SYSFS_ATTRS_SIZE + 1];

static const struct attribute_group smartchg_sysfs_attr_group = {
	.attrs = smartchg_sysfs_attrs,
};

void mca_smartchg_set_scene(int scene)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info) {
		mca_log_err("info is null\n");
		return;
	}

	mca_log_info("set scene: %d:%d => %d\n", info->scene, info->fake_scene,
		     scene);
	if (scene >= SMART_CHG_SCENE_NORMAL &&
	    scene < SMART_CHG_SCENE_INDEX_MAX) {
		smart_charge_report_scene(info, scene);
		info->scene = scene;
	} else if (scene >= SMART_CHG_SCENE_REDIR_START &&
		   scene < SMART_CHG_SCENE_REDIR_INDEX_MAX) {
		info->fake_scene = scene - SMART_CHG_SCENE_REDIR_START;
	} else if (scene == SMART_CHARGE_FAKE_SCENE_DEFAULT) {
		info->fake_scene = SMART_CHARGE_FAKE_SCENE_DEFAULT;
	}

	cancel_delayed_work_sync(&info->smart_sense_chg_work);
	schedule_delayed_work(&info->smart_sense_chg_work, 0);
}
EXPORT_SYMBOL(mca_smartchg_set_scene);

int mca_smartchg_get_scene(void)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info) {
		mca_log_err("info is null\n");
		return 0;
	}

	if (info->fake_scene != SMART_CHARGE_FAKE_SCENE_DEFAULT)
		return info->fake_scene;

	return info->scene;
}
EXPORT_SYMBOL(mca_smartchg_get_scene);

void mca_smartchg_set_board_temp(int board_temp)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info) {
		mca_log_err("info is null\n");
		return;
	}

	mca_log_info("set board_temp: %d\n", board_temp);
	info->board_temp = board_temp;
}
EXPORT_SYMBOL(mca_smartchg_set_board_temp);

int mca_smartchg_get_board_temp(void)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info) {
		mca_log_err("info is null\n");
		return 0;
	}

	return info->board_temp;
}
EXPORT_SYMBOL(mca_smartchg_get_board_temp);

int mca_smartchg_is_extreme_cold_enabled(void)
{
	if (global_smartchg_info)
		return global_smartchg_info->smart_chg_data
			       .smart_chgcfg[SMART_CHG_EXTREME_COLD]
			       .enable == 1;
	return false;
}
EXPORT_SYMBOL(mca_smartchg_is_extreme_cold_enabled);

int mca_smartchg_get_limit_soc(void)
{
	struct smart_charge_info *info = global_smartchg_info;

	if (!info) {
		mca_log_err("info is null\n");
		return 0;
	}
	if (info->soc_limit_enable)
		return info->soc_limit;
	return 0;
}
EXPORT_SYMBOL(mca_smartchg_get_limit_soc);

static void smart_charge_report_scene(struct smart_charge_info *info, int scene)
{
	static int last_report_scene;
	int report_scene = 0;
	int len = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	if (scene != info->scene) {
		switch (scene) {
		case SMART_CHG_SCENE_PHONE:
		case SMART_CHG_SCENE_VIDEO:
		case SMART_CHG_SCENE_VIDEOCHAT:
			report_scene = scene;
			break;
		default:
			report_scene = 0;
			break;
		}

		if (report_scene != last_report_scene) {
			mca_log_info("report scene: %d => %d\n",
				     last_report_scene, report_scene);
			last_report_scene = report_scene;

			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				       "MCA_SMARTCHG_SCENE=%d", report_scene);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}
	}
}

static void smart_charge_handle_sense_chg(struct smart_charge_info *info)
{
	int posture_st = SMART_CHG_POSTURE_UNKNOW;
	int scene = SMART_CHG_SCENE_NORMAL;
	int sic_mode = SMART_CHG_SIC_MODE_BALANCED;
	bool active_stat = false;
	int len = 0;
	char event[MCA_EVENT_NOTIFY_SIZE] = { 0 };
	struct mca_event_notify_data event_data = { 0 };

	scene = info->fake_scene != SMART_CHARGE_FAKE_SCENE_DEFAULT ?
			info->fake_scene :
			info->scene;
	posture_st =
		info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.use_fake_value ?
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.fake_value :
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.func_value;

	if (!info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG].enable ||
	    posture_st <= SMART_CHG_POSTURE_UNKNOW ||
	    posture_st >= SMART_CHG_POSTURE_INDEX_MAX)
		return;

	switch (scene) {
	case SMART_CHG_SCENE_PHONE:
		active_stat = true;
		if (posture_st >= SMART_CHG_POSTURE_DESKTOP &&
		    posture_st <= SMART_CHG_POSTURE_HOLDER)
			sic_mode = SMART_CHG_SIC_MODE_SUPERCHG;
		else if (posture_st >= SMART_CHG_POSTURE_ONEHAND &&
			 posture_st <= SMART_CHG_POSTURE_TWOHAND_V)
			sic_mode = SMART_CHG_SIC_MODE_MIDDLE;
		else if (posture_st == SMART_CHG_POSTURE_ANS_CALL)
			sic_mode = SMART_CHG_SIC_MODE_BALANCED;
		break;
	case SMART_CHG_SCENE_VIDEO:
	case SMART_CHG_SCENE_VIDEOCHAT:
	case SMART_CHG_SCENE_PER_VIDEO:
		active_stat = true;
		if (posture_st >= SMART_CHG_POSTURE_DESKTOP &&
		    posture_st <= SMART_CHG_POSTURE_HOLDER)
			sic_mode = SMART_CHG_SIC_MODE_SUPERCHG;
		else if (posture_st >= SMART_CHG_POSTURE_ONEHAND &&
			 posture_st <= SMART_CHG_POSTURE_ANS_CALL)
			sic_mode = SMART_CHG_SIC_MODE_BALANCED;
		break;
	case SMART_CHG_SCENE_CLASS0:
	case SMART_CHG_SCENE_PER_CLASS0:
		if (info->support_sensechg_v2) {
			active_stat = true;
			if (posture_st >= SMART_CHG_POSTURE_DESKTOP &&
			    posture_st <= SMART_CHG_POSTURE_HOLDER)
				sic_mode = SMART_CHG_SIC_MODE_SUPERCHG;
			else if (posture_st >= SMART_CHG_POSTURE_ONEHAND &&
				 posture_st <= SMART_CHG_POSTURE_ANS_CALL)
				sic_mode = SMART_CHG_SIC_MODE_BALANCED;
		} else {
			mca_log_err("unsupport scene: %d\n", info->scene);
			return;
		}
		break;
	default:
		mca_log_err("unsupport scene: %d\n", info->scene);
		return;
	}

	if (info->screen_state == 0)
		sic_mode /= 2;

	mca_log_info(
		"active_stat: %d, online: %d, screen_state: %d, scene: %d, posture: %d, smart_sic_mode: %d -> %d\n",
		active_stat, info->online, info->screen_state, scene,
		posture_st, info->smart_sic_mode, sic_mode);
	if (sic_mode != info->smart_sic_mode) {
		info->smart_sic_mode = sic_mode;
		if (active_stat && info->online) {
			len = snprintf(event, MCA_EVENT_NOTIFY_SIZE,
				       "POWER_SUPPLY_SMART_SIC_MODE=%d",
				       info->smart_sic_mode);
			event_data.event = event;
			event_data.event_len = len;
			mca_event_report_uevent(&event_data);
		}
	}
}

//static void smartcharging_handle_soc_limit_process(struct smart_charge_info *info,
//	bool enable, int soc_limit_thre)
//{
//	int curr_soc = 0;
//	static struct mca_smartchg_if_ops *if_ops;

//	if (enable) {
//		curr_soc = strategy_class_fg_ops_get_soc();
//		if (curr_soc >= soc_limit_thre) {
//			mca_log_info("soc_limit_thre = %d\n", soc_limit_thre);
//			info->soc_limit_enable = true;
//			for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
//				if_ops = mca_smartchg_if_get_ops(i);
//				if (!if_ops || !if_ops->set_soc_limit_sts)
//					continue;
//				if_ops->set_soc_limit_sts(if_ops->data, info->soc_limit_enable);
//			}
//		} else{
//			cancel_delayed_work_sync(&info->smart_soc_limit_work);
//			schedule_delayed_work(&info->smart_soc_limit_work, msecs_to_jiffies(1000));
//		}
//	} else {
//		mca_log_info("disable soc limit\n");
//		cancel_delayed_work_sync(&info->smart_soc_limit_work);
//		info->soc_limit_enable = true;
//		for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
//			if_ops = mca_smartchg_if_get_ops(i);
//			if (!if_ops || !if_ops->set_soc_limit_sts)
//				continue;
//			if_ops->set_soc_limit_sts(if_ops->data, info->soc_limit_enable);
//		}
//	}
//}

static void smart_charge_set_soc_limit(bool enable)
{
	static struct mca_smartchg_if_ops *if_ops;

	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_soc_limit_sts)
			continue;
		if_ops->set_soc_limit_sts(if_ops->data, enable);
	}
}

#define SMART_CHG_SOC_HYS 2
static void smart_charge_soc_limit_workfunc(struct work_struct *work)
{
	struct smart_charge_info *info = container_of(
		work, struct smart_charge_info, smart_soc_limit_work.work);
	int curr_soc = 0;

	if (!info->online) {
		mca_log_err("adapter is plugout, don't handle soc limit\n");
		return;
	}

	if (info->soc_limit) {
		curr_soc = strategy_class_fg_ops_get_soc();
		mca_log_info("curr_soc = %d, soc_limit_thre = %d\n", curr_soc,
			     info->soc_limit);
		if (curr_soc >= info->soc_limit) {
			if (info->soc_limit_enable == false) {
				info->soc_limit_enable = true;
				smart_charge_set_soc_limit(
					info->soc_limit_enable);
			}

			if (curr_soc >= info->soc_limit + SMART_CHG_SOC_HYS) {
				if (info->smart_chg_data
					    .smart_chgcfg[SMART_CHG_ENDURANCE_PRO]
					    .enable &&
				    !info->ignore_upload.endurance_pro) {
					mca_charge_mievent_report(
						CHARGE_DFX_SMART_ENDURANCE_SOC_ERR,
						&curr_soc, 1);
					info->ignore_upload.endurance_pro =
						true;
				}
				if (info->smart_chg_data
					    .smart_chgcfg[SMART_CHG_NAVIGATION]
					    .enable &&
				    !info->ignore_upload.navigation) {
					mca_charge_mievent_report(
						CHARGE_DFX_SMART_NAVIGATION_SOC_ERR,
						&curr_soc, 1);
					info->ignore_upload.endurance_pro =
						true;
				}
			}

		} else if (curr_soc <=
			   info->soc_limit - SMART_CHG_SOCLMT_CANCEL_DEFAULT) {
			if (info->soc_limit_enable == true) {
				info->soc_limit_enable = false;
				smart_charge_set_soc_limit(
					info->soc_limit_enable);
			}
		}
		schedule_delayed_work(&info->smart_soc_limit_work,
				      msecs_to_jiffies(1000));
	} else {
		mca_log_info("disable soc limit\n");
		info->soc_limit_enable = false;
		smart_charge_set_soc_limit(info->soc_limit_enable);
	}
	mca_log_info("soc_limit_enable = %d\n", info->soc_limit_enable);
}

/*to handle navigation and smart night algorithm conflict*/
static void
smartcharging_handle_algorithm_conflict(struct smart_charge_info *info)
{
	bool smart_night = false;
	bool smart_navigation = false;
	bool smart_endurance_pro = false;
	uint32_t soc_upper_thre = SMART_CHG_SOCLMT_TRIG_DEFAULT;

	smart_night = info->smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT].enable;
	smart_navigation =
		info->smart_chg_data.smart_chgcfg[SMART_CHG_NAVIGATION].enable;
	smart_endurance_pro =
		info->smart_chg_data.smart_chgcfg[SMART_CHG_ENDURANCE_PRO]
			.enable;

	/*endurance protect is prior to all others, then navigation is prior to smart night charge */
	if (smart_endurance_pro) {
		soc_upper_thre = info->smart_chg_data
					 .smart_chgcfg[SMART_CHG_ENDURANCE_PRO]
					 .func_value;
		mca_log_info("smart_endurance %d, soc_upper_thre:%d",
			     smart_endurance_pro, soc_upper_thre);
	} else if (smart_navigation) {
		soc_upper_thre =
			info->smart_chg_data.smart_chgcfg[SMART_CHG_NAVIGATION]
				.func_value;
		mca_log_info("smart_navigation %d, soc_upper_thre:%d",
			     smart_navigation, soc_upper_thre);
	} else if (smart_night) {
		soc_upper_thre =
			info->smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT]
				.func_value;
		mca_log_info("smart_night %d, soc_upper_thre:%d", smart_night,
			     soc_upper_thre);
	} else {
		soc_upper_thre = 0;
	}
	info->soc_limit = soc_upper_thre;
	cancel_delayed_work_sync(&info->smart_soc_limit_work);
	schedule_delayed_work(&info->smart_soc_limit_work, 0);
	//smartcharging_handle_soc_limit_process(info, !!soc_upper_thre, info->soc_limit);
}

static void smartcharging_handle_wls_super(struct smart_charge_info *info)
{
	static struct mca_smartchg_if_ops *if_ops;

	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_wls_super_sts)
			continue;
		if_ops->set_wls_super_sts(
			if_ops->data,
			info->smart_chg_data.smart_chgcfg[SMART_CHG_WLS_SUPER]
				.enable);
	}
}

static void smartcharging_handle_power_boost(struct smart_charge_info *info,
					     int type)
{
	static struct mca_smartchg_if_ops *if_ops;

	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_pwr_boost_sts)
			continue;
		if (type == SMART_CHG_OUTDOOR_PWR_BOOST)
			if_ops->set_pwr_boost_sts(
				if_ops->data,
				info->smart_chg_data
					.smart_chgcfg[SMART_CHG_OUTDOOR]
					.enable);
		else if (type == SMART_CHG_TRAVELWAIT_PWR_BOOST)
			if_ops->set_pwr_boost_sts(
				if_ops->data,
				info->smart_chg_data
					.smart_chgcfg[SMART_CHG_TRAVELWAIT]
					.enable);
	}
}

static void smartcharging_handle_wls_quiet(struct smart_charge_info *info)
{
	static struct mca_smartchg_if_ops *if_ops;

	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_wls_quiet_sts)
			continue;
		if_ops->set_wls_quiet_sts(
			if_ops->data,
			info->smart_chg_data.smart_chgcfg[SMART_CHG_WLS_QUIET]
				.enable);
	}
}

static void smartcharging_handle_controlmessage(void)
{
	struct smart_charge_info *info = global_smartchg_info;
	uint32_t smart_header = info->smart_chg_control.AsUINT32;
	int soc = strategy_class_fg_ops_get_soc();

	if (!info)
		return;

	smart_header = (smart_header & 0xFFFE);
	smart_header = (smart_header - 1) & smart_header;
	mca_log_info("smart_header: %d", smart_header);
	if (!smart_header) {
		info->smart_chg_data.ret_code = false;
		mca_log_err("input correct\n");
	} else {
		info->smart_chg_data.ret_code = true;
		mca_log_err("error input invalid\n");
		return;
	}

	mca_log_info(
		"smart_chg_control: enable: %d, func_type: 0x%04x, func_value: %d\n",
		info->smart_chg_control.enable,
		info->smart_chg_control.func_type << 1,
		info->smart_chg_control.func_value);

	if (info->smart_chg_control.navigation) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_NAVIGATION].enable =
			info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_NAVIGATION]
			.func_value = info->smart_chg_control.soc_limit;
		smartcharging_handle_algorithm_conflict(info);
		if (info->smart_chg_control.enable)
			mca_charge_mievent_report(
				CHARGE_DFX_SMART_NAVIGATION_TRIGGERED, &soc, 1);
		else
			mca_charge_mievent_set_state(
				MIEVENT_STATE_END,
				CHARGE_DFX_SMART_NAVIGATION_TRIGGERED);

		info->ignore_upload.navigation =
			soc >= info->smart_chg_control.soc_limit ? true : false;

	} else if (info->smart_chg_control.endurance_pro) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_ENDURANCE_PRO]
			.enable = info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_ENDURANCE_PRO]
			.func_value = info->smart_chg_control.soc_limit;
		smartcharging_handle_algorithm_conflict(info);
		if (info->smart_chg_control.enable)
			mca_charge_mievent_report(
				CHARGE_DFX_SMART_ENDURANCE_TRIGGERED, &soc, 1);
		else
			mca_charge_mievent_set_state(
				MIEVENT_STATE_END,
				CHARGE_DFX_SMART_ENDURANCE_TRIGGERED);

		info->ignore_upload.endurance_pro =
			soc >= info->smart_chg_control.soc_limit ? true : false;

	} else if (info->smart_chg_control.outdoor) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_OUTDOOR].enable =
			info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_OUTDOOR].func_value =
			0;
		smartcharging_handle_power_boost(info,
						 SMART_CHG_OUTDOOR_PWR_BOOST);
	} else if (info->smart_chg_control.wls_super_chg) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_WLS_SUPER].enable =
			info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_WLS_SUPER]
			.func_value = 0;
		smartcharging_handle_wls_super(info);
	} else if (info->smart_chg_control.lowfast) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_LOWFAST].enable =
			info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_LOWFAST].func_value =
			0;
	} else if (info->smart_chg_control.wls_quiet) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_WLS_QUIET].enable =
			info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_WLS_QUIET]
			.func_value = 0;
		smartcharging_handle_wls_quiet(info);
	} else if (info->smart_chg_control.sense_chg) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG].enable =
			info->smart_chg_control.enable;
		if (info->smart_chg_control.func_value >
		    SMART_CHG_POSTURE_FAKE_CLEAR) {
			mca_log_info("debug sense charge, set value: %d\n",
				     info->smart_chg_control.func_value);
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.use_fake_value = true;
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.fake_value =
				info->smart_chg_control.func_value -
				SMART_CHG_POSTURE_FAKE_CLEAR;
		} else if (info->smart_chg_control.func_value ==
			   SMART_CHG_POSTURE_FAKE_CLEAR) {
			mca_log_info(
				"debug sense charge, cancel fake value: %d\n",
				info->smart_chg_control.func_value);
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.use_fake_value = false;
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.fake_value = 0;
		} else {
			info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				.func_value =
				info->smart_chg_control.func_value;
		}
		smart_charge_handle_sense_chg(info);
	} else if (info->smart_chg_control.extreme_cold) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_EXTREME_COLD]
			.enable = info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_EXTREME_COLD]
			.func_value = 0;
	} else if (info->smart_chg_control.travel_wait) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_TRAVELWAIT].enable =
			info->smart_chg_control.enable;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_TRAVELWAIT]
			.func_value = 0;
		smartcharging_handle_power_boost(
			info, SMART_CHG_TRAVELWAIT_PWR_BOOST);
	} else if (info->smart_chg_control.smart_bypass) {
		info->bypass_reserved = 0;
		info->bypass_enable = info->smart_chg_control.enable & 1;
	}
}

static int smartcharging_receive_control_message(int val)
{
	struct smart_charge_info *info = global_smartchg_info;

	info->smart_chg_control.AsUINT32 = val;
	mca_log_info("smart_chg value is %08x\n", val);
	smartcharging_handle_controlmessage();
	return 0;
}

static int get_smart_chg_message(void)
{
	struct smart_charge_info *info = global_smartchg_info;
	uint32_t control_header = 0;
	int index = 0;

	control_header |= (info->smart_chg_data.ret_code & 0x01);

	for (index = SMART_CHG_NAVIGATION; index < SMART_CHG_MAX; index++) {
		if (info->smart_chg_data.smart_chgcfg[index].enable)
			control_header |= BIT(index);
		else
			control_header &= ~BIT(index);
	}
	mca_log_info("control_header = %d\n", control_header);
	return control_header;
}

static int handle_smart_batt_message(int val)
{
	struct smart_charge_info *info = global_smartchg_info;
	int vote_value;

	info->smart_batt_val = val;
	vote_value = info->smart_batt_val * info->cell_type;

	mca_log_info("smart_batt_val is %d vote_value %d\n", val, vote_value);
	mca_vote(info->smartchg_delta_fv_voter, "smart_batt", true, vote_value);

	return 0;
}

static int get_smart_batt_message(void)
{
	struct smart_charge_info *info = global_smartchg_info;

	return info->smart_batt_val;
}

static int handle_smart_night_message(int val)
{
	struct smart_charge_info *info = global_smartchg_info;

	info->smart_night_val = val;
	mca_log_info("smart_night_val is %d\n", !!val);
	if (val)
		strategy_class_fg_ops_get_rsoc(&info->night_enable_rsoc);
	if (!!val == 1 || !!val == 0) {
		info->smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT].enable = val;
		info->smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT].func_value =
			SMART_CHG_SOCLMT_TRIG_DEFAULT;
		smartcharging_handle_algorithm_conflict(info);
	}

	return 0;
}

static int get_smart_night_message(void)
{
	struct smart_charge_info *info = global_smartchg_info;

	return info->smart_night_val;
}

static void smart_charge_plugin_or_plugout_reset(struct smart_charge_info *info)
{
	if (info->online)
		mca_vote(info->smartchg_delta_ichg_voter, "batt_health", true,
			 info->smart_delta_ichg);
	else
		mca_vote(info->smartchg_delta_ichg_voter, "batt_health", false,
			 info->smart_delta_ichg);
}

static void smart_charge_csd_send_pulse(struct smart_charge_info *info)
{
	int soc = 0, rsoc = 0;
	int batt_temp = 0;
	bool smart_night = false;

	soc = strategy_class_fg_ops_get_soc();
	strategy_class_fg_ops_get_rsoc(&rsoc);
	(void)strategy_class_fg_ops_get_temperature(&batt_temp);
	batt_temp /= 10;
	smart_night = info->smart_chg_data.smart_chgcfg[SMART_CHG_NIGHT].enable;

	if (!info->pulse_mode) {
		if ((soc >= 50 && soc <= 80) &&
		    (batt_temp >= 20 && batt_temp <= 45)) {
			if (info->plugin_rsoc <= 45 && !info->screen_state &&
			    info->cycle_count > 100 &&
			    (info->cycle_count % 25 == 0) && rsoc == 50) {
				info->pulse_mode = 1;
				mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
						       MCA_EVENT_CSD_SEND_PULSE,
						       &info->pulse_mode);
			} else if (smart_night == true) {
				if (info->night_enable_rsoc <= 45 &&
				    rsoc == 50) {
					info->pulse_mode = 2;
					mca_event_block_notify(
						MCA_EVENT_CHARGE_STATUS,
						MCA_EVENT_CSD_SEND_PULSE,
						&info->pulse_mode);
				} else if (info->night_enable_rsoc > 45 &&
					   rsoc == 76) {
					info->pulse_mode = 3;
					mca_event_block_notify(
						MCA_EVENT_CHARGE_STATUS,
						MCA_EVENT_CSD_SEND_PULSE,
						&info->pulse_mode);
				}
			}
		}
	} else if ((info->cycle_count % 25 != 0) && smart_night == false) {
		info->pulse_mode = 0;
		mca_event_block_notify(MCA_EVENT_CHARGE_STATUS,
				       MCA_EVENT_CSD_SEND_PULSE,
				       &info->pulse_mode);
	}
	mca_log_info(
		"soc: %d, rsoc: %d, plugin_rsoc: %d, night_enable_rsoc: %d, batt_temp: %d, cycle: %d, night: %d, pulse_mode: %d",
		soc, rsoc, info->plugin_rsoc, info->night_enable_rsoc,
		batt_temp, info->cycle_count, smart_night, info->pulse_mode);
}

static int smart_charge_thermal_notifier_cb(struct notifier_block *nb,
					    unsigned long event, void *val)
{
	struct smart_charge_info *info =
		container_of(nb, struct smart_charge_info, thermal_nb);

	switch (event) {
	case MCA_EVENT_THERMAL_BOARD_TEMP_CHANGE:
		info->board_temp = *(int *)val / SMART_CHG_BOARD_TEMP_SCALE;
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static void
update_smart_bypass_temp_section(struct smart_charge_info *info,
				 struct smart_bypass_temp_section *sections,
				 unsigned int count)
{
	int board_temp = info->board_temp;
	int cur = info->bypass_temp_index;
	int report_index;
	int i;

	for (i = 0; i < (int)count; i++) {
		if (board_temp >= sections[i].temp_high ||
		    sections[i].temp_low > board_temp)
			continue;

		report_index = cur;
		if (cur < i) {
			if (board_temp >= sections[cur].temp_high +
						  sections[cur].hyst_high) {
				report_index = i;
				info->bypass_temp_index = i;
			}
		} else if (cur > i &&
			   sections[cur].temp_low - sections[cur].hyst_low >
				   board_temp) {
			report_index = i;
			info->bypass_temp_index = i;
		}

		mca_log_err("for_bypass board_temp:%d, cur_temp_index:%d\n",
			    board_temp, report_index);
		return;
	}

	mca_log_err("temp over range invalid, current:%d\n", -1);
}

static void smart_charge_check_bypass_status(struct smart_charge_info *info)
{
	static bool once_flag;
	bool prev = once_flag;

	if (info->bypass_exit_flag) {
		once_flag = false;
		return;
	}

	if (!once_flag) {
		mca_log_err("for_bypass start time\n");
		info->bypass_start_time = ktime_get_boottime();
	} else {
		if (ktime_get_boottime() >
		    info->bypass_start_time + SMART_BYPASS_DEBOUNCE_NS) {
			mca_log_err("for_bypass exit bypass\n");
			mca_vote(info->smartchg_set_fcc_voter, "smart_bypass",
				 false, 0);
			info->bypass_exit_flag = true;
		} else {
			goto debounce;
		}
	}
	once_flag = prev ^ 1;
debounce:
	mca_log_err("for_bypass Debounce: %lld s\n",
		    (ktime_get_boottime() - info->bypass_start_time) /
			    NSEC_PER_SEC);
}

static const int smart_bypass_med_scenes[] = { 0x08, 0x0b, 0x1c,
					       0x3a, 0x3d, 0x4e };
static const int smart_bypass_high_scenes[] = { 0x12, 0x13, 0x14, 0x19,
						0x4b, 0x1f5, 0x2be, 0x2bc };

static bool smart_bypass_scene_in_set(int scene, const int *set, int n)
{
	for (int i = 0; i < n; i++)
		if (set[i] == scene)
			return true;
	return false;
}

static void smart_charge_handle_bypass_chg(struct smart_charge_info *info)
{
	int fcc = 0;
	uint16_t state;
	int soc;

	if (!info->support_bypass)
		return;

	state = info->bypass_enable;
	if (info->bypass_enable == 0)
		goto report;

	soc = strategy_class_fg_ops_get_soc();
	if (soc < info->bypass_entry_soc || soc > info->bypass_exit_soc) {
		mca_log_err("for_bypass soc not in range, quit bypass mode\n");
		state = 0;
		goto report;
	}
	if (info->wls_online) {
		mca_log_err("for_bypass wlschg online, quit bypass mode\n");
		state = 0;
		goto report;
	}

	if (smart_bypass_scene_in_set(info->scene, smart_bypass_med_scenes,
				      ARRAY_SIZE(smart_bypass_med_scenes))) {
		update_smart_bypass_temp_section(info, info->bypass_med_lmt,
						 info->bypass_med_num);
		info->bypass_active = 1;
		fcc = info->bypass_med_lmt[info->bypass_temp_index].fcc;
	} else if (smart_bypass_scene_in_set(
			   info->scene, smart_bypass_high_scenes,
			   ARRAY_SIZE(smart_bypass_high_scenes))) {
		update_smart_bypass_temp_section(info, info->bypass_high_lmt,
						 info->bypass_high_num);
		info->bypass_active = 1;
		fcc = info->bypass_high_lmt[info->bypass_temp_index].fcc;
	} else {
		info->bypass_active = 0;
		smart_charge_check_bypass_status(info);
	}

	if (fcc != 0 && fcc != info->last_bypass_fcc) {
		mca_vote(info->smartchg_set_fcc_voter, "smart_bypass", true,
			 fcc);
		info->bypass_exit_flag = false;
	}

report:
	mca_log_err("for_bypass [%d->%d] fcc[%d %d]\n", info->last_bypass_state,
		    state, fcc, info->last_bypass_fcc);
	if (info->last_bypass_state != state) {
		info->last_bypass_state = state;
		if (state == 0)
			mca_vote(info->smartchg_set_fcc_voter, "smart_bypass",
				 false, 0);
	}
	if (fcc != info->last_bypass_fcc)
		info->last_bypass_fcc = fcc;
}

/* ======================== Real bypass charging ============================
 *
 * Bypass takes the pack out of the charge loop while the adapter keeps running
 * the system, so it stays cool and stops cycling through a long session.
 *
 * Two paths can push current into the battery here and both have to be shut
 * down, or bypass silently does nothing the moment PPS lands:
 *
 *	chg_enable        (VOTE_AND) -> set_chg()           -> buck charge FET
 *	quick_chg_disable (VOTE_OR)  -> force_stop_charging -> charge pump
 *
 * Only ever vote "stop" here. chg_enable is a VOTE_AND, so jeita, vbat_ovp and
 * the thermal clients keep full authority to cut charging on their own; bypass
 * can never talk the charger past a safety vote.
 *
 * Two things are deliberately left alone. input_suspend/hiz cuts VBUS and runs
 * the phone off its own pack, which is the opposite of bypass. And unlike
 * strategy_buckchg_process_soc_limit_change() we do not vote input_voltage
 * down: that drops VBUS to 5V and caps the path at 5V x 2000mA = 10W, where
 * holding 9V leaves 9V x 1600mA = 14.4W. That gap decides whether a heavy title
 * breaks even or bleeds.
 *
 * Load can still outrun the path - a 60fps AAA title peaks past 14W. The pack
 * then covers the deficit only, a few watts instead of the whole load, so it
 * drains far slower than unplugged and dissipates far less heat. The state
 * machine bounds the rest:
 *
 *	ACTIVE  -> pack floating near 0 A
 *	DEFICIT -> sustained discharge; the input is already maxed, so ride it
 *	           out and let the soc floor be the backstop
 *	ASSIST  -> soc reached the floor; hand the buck back to refill gently,
 *	           CP stays off so the refill does not cook the pack mid-game
 *
 * ASSIST releases back to ACTIVE once soc climbs past the entry threshold, so a
 * long session oscillates inside a narrow band instead of draining flat or
 * sitting pinned at 100%.
 */
#define BYPASS_CHG_VOTER "bypass_chg"

/*
 * Scenes MCA reports for the game profiles. Distinct from
 * smart_bypass_high_scenes above, which feeds Xiaomi's fcc limiter.
 */
static const int bypass_chg_game_scenes[] = {
	SMART_CHG_SCENE_TGAME,
	SMART_CHG_SCENE_MGAME,
	SMART_CHG_SCENE_YUANSHEN,
	SMART_CHG_SCENE_XINGTIE,
};

static bool bypass_chg_scene_is_game(int scene)
{
	for (int i = 0; i < (int)ARRAY_SIZE(bypass_chg_game_scenes); i++)
		if (bypass_chg_game_scenes[i] == scene)
			return true;

	return false;
}

/*
 * Zero-Deficit Float Bypass (ZDFB)
 * --------------------------------
 * Legacy bypass voted 0 on "chg_enable", gating the buck power stage.
 * With the buck OFF the adapter cannot feed VSYS, so every gaming load
 * spike (measured: 334 mA / -1.46 W peak) is covered by the pack:
 *   -1.0 mAh per 2-minute audit, ~30 mAh/h sustained micro-drain.
 *
 * ZDFB keeps the buck permanently permitted and instead clamps FCC via
 * a MIN-vote on "buck_charge_curr":
 *   ACTIVE : float FCC ceiling (default 50 mA = 0.22 W micro-float)
 *   ASSIST : 600 mA ceiling while a transient deficit is absorbed
 *   FULL   : 0 mA at SOC 100% (no trickle overcharge, buck still feeds
 *            VSYS so the adapter carries 100% of the system load)
 * Charge pumps stay locked OFF in every engaged state => the thermal
 * envelope is identical to the legacy bypass (thermal invariance).
 */
static const char * const bypass_state_name[BYPASS_CHG_ST_MAX] = {
	[BYPASS_CHG_ST_IDLE]      = "IDLE",
	[BYPASS_CHG_ST_ACTIVE]    = "ACTIVE",
	[BYPASS_CHG_ST_DEFICIT]   = "DEFICIT",
	[BYPASS_CHG_ST_ASSIST]    = "ASSIST",
	[BYPASS_CHG_ST_FULL]      = "FULL",
	[BYPASS_CHG_ST_CP_BYPASS] = "CP_BYPASS",
};

/*
 * Clamp every tunable into a range the hardware can actually service. This is
 * the one policy decision the kernel keeps: userspace may retune freely inside
 * these bounds and can never write a value that asks the buck for more than it
 * can deliver.
 */
static void bypass_tune_sanitise(struct bypass_chg_tune *t)
{
	t->target_ibat_ma = clamp(t->target_ibat_ma, -500, 2000);
	t->fcc_max_ma = clamp(t->fcc_max_ma, 0, BYPASS_FCC_MAX_MA_LIMIT);
	t->pi_kp = clamp(t->pi_kp, 1, BYPASS_PI_KP_LIMIT);
	t->pi_deadband_ma = clamp(t->pi_deadband_ma, 1, 500);
	t->pi_step_max_ma = clamp(t->pi_step_max_ma, 10, 2000);
	t->input_9v_ma = clamp(t->input_9v_ma, 100, BYPASS_INPUT_MA_LIMIT);
	t->input_5v_ma = clamp(t->input_5v_ma, 100, BYPASS_INPUT_MA_LIMIT);
	t->cp_enable = clamp(t->cp_enable, 0, 1);
	t->cp_enter_mw = clamp(t->cp_enter_mw, 6000, 25000);
	t->cp_vbat_max_mv = clamp(t->cp_vbat_max_mv, 4200, 4480);
}

static int bypass_chg_latch_voters(struct smart_charge_info *info)
{
	struct bypass_chg_data *bp = &info->bypass;

	if (!bp->en_voter)
		bp->en_voter = mca_find_votable("chg_enable");
	if (!bp->cp_voter)
		bp->cp_voter = mca_find_votable("quick_chg_disable");
	if (!bp->fcc_voter)
		bp->fcc_voter = mca_find_votable("buck_charge_curr");
	if (!bp->in9v_voter)
		bp->in9v_voter = mca_find_votable("buck_9v_in");
	if (!bp->in5v_voter)
		bp->in5v_voter = mca_find_votable("buck_5v_in");
	if (!bp->bypass_in_voter)
		bp->bypass_in_voter = mca_find_votable("buck_bypass_in");
	if (!bp->cp_bypass_voter)
		bp->cp_bypass_voter = mca_find_votable("cp_bypass_en");

	return (bp->en_voter && bp->cp_voter && bp->fcc_voter) ?
		0 : -EPROBE_DEFER;
}

static bool bypass_chg_can_enter_cp(struct smart_charge_info *info)
{
	struct bypass_chg_data *bp = &info->bypass;
	unsigned int pps_max_w = 0;
	int vbat_mv = 0;

	if (!info->bypass_tune.cp_enable || !bp->cp_bypass_voter)
		return false;

	if (bp->cp_cooldown_jif && time_before(jiffies, bp->cp_cooldown_jif))
		return false;

	if (bp->soc <= BYPASS_FLOOR_SOC || bp->soc > 96)
		return false;

	if (strategy_class_fg_ops_get_voltage(&vbat_mv) ||
	    vbat_mv >= info->bypass_tune.cp_vbat_max_mv)
		return false;

	/* Universal check: succeeds on BOTH Xiaomi PPS and 3rd-party PD_VERIFY PPS */
	if (protocol_class_get_adapter_pwr_max_power(ADAPTER_PROTOCOL_PPS,
						     &pps_max_w) ||
	    pps_max_w < 25)
		return false;

	return true;
}

static int bypass_chg_buck_power_mw(void)
{
	int vbus = 0, ibus = 0;

	if (mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					 STRATEGY_STATUS_TYPE_VBUS, &vbus) ||
	    mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					 STRATEGY_STATUS_TYPE_IBUS, &ibus))
		return 0;

	/* Normalise uV/uA vs mV/mA defensively */
	if (vbus > 50000)
		vbus /= 1000;
	if (ibus > 50000)
		ibus /= 1000;
	if (vbus <= 0 || ibus <= 0)
		return 0;

	return (vbus * ibus) / 1000;
}

/*
 * bypass_chg_regulate - clamped integral controller holding ibat at target (ZDFB v3)
 *
 * Output clamping IS anti-windup (accumulator == output in [0, fcc_max_ma]).
 * Removed the broken v2 BYPASS_WINDUP_GUARD_MA (400 mA) undo which pinned
 * fcc_ma at ~400 mA during heavy deficits, caused hunting across -200 mA, and
 * prevented ASSIST from ever triggering cleanly.
 */
static void bypass_chg_regulate(struct smart_charge_info *info)
{
	struct bypass_chg_data *bp = &info->bypass;
	const struct bypass_chg_tune *t = &info->bypass_tune;
	int err_ma = t->target_ibat_ma - bp->ibat_ma;
	int step;

	if (err_ma > -t->pi_deadband_ma && err_ma < t->pi_deadband_ma) {
		bp->sat_cnt = 0;
		return;
	}

	step = (err_ma * t->pi_kp) / 100;
	step = clamp(step, -t->pi_step_max_ma, t->pi_step_max_ma);

	bp->fcc_ma = clamp(bp->fcc_ma + step, 0, t->fcc_max_ma);

	/*
	 * Saturated at fcc_max_ma while still draining -> the buck input path
	 * is genuinely at its ceiling; escalate cleanly to ASSIST (or CP-Bypass).
	 */
	if (bp->fcc_ma >= t->fcc_max_ma && err_ma > t->pi_deadband_ma)
		bp->sat_cnt++;
	else
		bp->sat_cnt = 0;
}

/*
 * bypass_chg_apply - commit the current ZDFB v3 / CP-Bypass state to voters
 */
static void bypass_chg_apply(struct smart_charge_info *info)
{
	struct bypass_chg_data *bp = &info->bypass;
	bool engaged = bp->state != BYPASS_CHG_ST_IDLE;
	bool cp_mode = bp->state == BYPASS_CHG_ST_CP_BYPASS;
	int bypass_in_req = max(info->bypass_tune.input_9v_ma,
				info->bypass_tune.input_5v_ma);

	/*
	 * Dual-regime CP gate:
	 *   CP_BYPASS -> enable SC8581 DIV2 direct-drive bypass (98.2% eff)
	 *   Buck ZDFB -> keep charge pumps locked OFF
	 */
	if (bp->cp_bypass_voter)
		mca_vote(bp->cp_bypass_voter, BYPASS_CHG_VOTER, cp_mode, 1);
	if (bp->cp_voter)
		mca_vote(bp->cp_voter, BYPASS_CHG_VOTER,
			 engaged && !cp_mode, 1);

	/*
	 * Lift buck wire_chg_type input cap via buck_bypass_in (MCA_VOTE_MAX)
	 * so buck_9v_in (MCA_VOTE_MIN) is no longer stuck at 1.6 A (~13 W).
	 * Adapter PDO/UCSI current rating and thermal/antiburn MIN votes still
	 * clamp below us -> safety remains strictly monotonic.
	 */
	if (bp->bypass_in_voter)
		mca_vote(bp->bypass_in_voter, BYPASS_CHG_VOTER, engaged,
			 bypass_in_req);
	if (bp->in9v_voter)
		mca_vote(bp->in9v_voter, BYPASS_CHG_VOTER, engaged,
			 info->bypass_tune.input_9v_ma);
	if (bp->in5v_voter)
		mca_vote(bp->in5v_voter, BYPASS_CHG_VOTER, engaged,
			 info->bypass_tune.input_5v_ma);

	/* Never inhibit buck while engaged (needed for VSYS + instant fallback) */
	if (bp->en_voter)
		mca_vote(bp->en_voter, BYPASS_CHG_VOTER, engaged, 1);

	/* Float / assist / full FCC ceiling via MIN-vote (units: mA) */
	if (bp->fcc_voter)
		mca_vote(bp->fcc_voter, BYPASS_CHG_VOTER, engaged,
			 engaged ? bp->fcc_ma : 0);

	if (!bp->applied && engaged)
		mca_log_info("ZDFB v3 engaged: state=%s fcc=%dmA in9v=%dmA cp=%d\n",
			     bypass_state_name[bp->state], bp->fcc_ma,
			     info->bypass_tune.input_9v_ma, cp_mode);
	bp->applied = engaged;
}

static void bypass_chg_exit(struct smart_charge_info *info)
{
	struct bypass_chg_data *bp = &info->bypass;

	if (bp->state == BYPASS_CHG_ST_IDLE && !bp->applied)
		return;

	bp->state = BYPASS_CHG_ST_IDLE;
	bp->fcc_ma = 0;
	bp->deficit_cnt = 0;
	bp->sat_cnt = 0;
	bp->cp_load_cnt = 0;

	if (bp->cp_bypass_voter)
		mca_vote(bp->cp_bypass_voter, BYPASS_CHG_VOTER, false, 0);
	if (bp->en_voter)
		mca_vote(bp->en_voter, BYPASS_CHG_VOTER, false, 0);
	if (bp->fcc_voter)
		mca_vote(bp->fcc_voter, BYPASS_CHG_VOTER, false, 0);
	if (bp->cp_voter)
		mca_vote(bp->cp_voter, BYPASS_CHG_VOTER, false, 0);
	if (bp->bypass_in_voter)
		mca_vote(bp->bypass_in_voter, BYPASS_CHG_VOTER, false, 0);
	if (bp->in9v_voter)
		mca_vote(bp->in9v_voter, BYPASS_CHG_VOTER, false, 0);
	if (bp->in5v_voter)
		mca_vote(bp->in5v_voter, BYPASS_CHG_VOTER, false, 0);

	bp->applied = false;
	mca_log_info("ZDFB v3 disengaged, all votes released\n");
}

static void bypass_chg_enter_state(struct smart_charge_info *info, int state)
{
	struct bypass_chg_data *bp = &info->bypass;
	int old;

	if (bp->state == state)
		return;

	bp->transition_cnt++;
	mca_log_info("ZDFB %s -> %s (ibat=%dmA fcc=%dmA soc=%d%%)\n",
		     bypass_state_name[bp->state], bypass_state_name[state],
		     bp->ibat_ma, bp->fcc_ma, bp->soc);
	old = bp->state;
	bp->state = state;

	switch (state) {
	case BYPASS_CHG_ST_ACTIVE:
		if (old == BYPASS_CHG_ST_IDLE || old == BYPASS_CHG_ST_FULL)
			bp->fcc_ma = info->bypass_float_fcc_ma ?
				     info->bypass_float_fcc_ma :
				     BYPASS_FLOAT_FCC_MA_DEFAULT;
		if (old == BYPASS_CHG_ST_CP_BYPASS)
			bp->cp_cooldown_jif =
				jiffies + msecs_to_jiffies(BYPASS_CP_COOLDOWN_MS);
		bp->deficit_cnt = 0;
		bp->cp_load_cnt = 0;
		break;
	case BYPASS_CHG_ST_ASSIST:
		bp->assist_cnt++;
		bp->deficit_cnt = 0;
		bp->assist_hold_jif =
			jiffies + msecs_to_jiffies(BYPASS_ASSIST_HOLD_MS);
		break;
	case BYPASS_CHG_ST_FULL:
		bp->fcc_ma = BYPASS_FULL_FCC_MA;
		bp->cp_load_cnt = 0;
		break;
	case BYPASS_CHG_ST_CP_BYPASS:
		bp->sat_cnt = 0;
		bp->deficit_cnt = 0;
		bp->cp_load_cnt = 0;
		bp->cp_start_jif =
			jiffies + msecs_to_jiffies(BYPASS_CP_START_GRACE_MS);
		break;
	default:
		break;
	}
}

static void bypass_chg_eval(struct smart_charge_info *info)
{
	struct bypass_chg_data *bp = &info->bypass;
	int raw_ua = 0;
	int qc_sts = MCA_QUICK_CHG_STS_NO_CHARGING;
	int buck_mw = 0;
	bool want;

	if (!info->support_bypass_chg)
		return;

	if (!info->online || info->wls_online) {
		bypass_chg_exit(info);
		return;
	}

	want = info->bypass_chg_mode == BYPASS_CHG_MODE_FORCE ||
	       (info->bypass_chg_mode == BYPASS_CHG_MODE_AUTO &&
		bypass_chg_scene_is_game(mca_smartchg_get_scene()));
	if (!want) {
		bypass_chg_exit(info);
		return;
	}

	if (bypass_chg_latch_voters(info))
		return;

	/* hold previous state on FG read errors - never flap blindly */
	if (strategy_class_fg_ops_get_current(&raw_ua))
		return;

	bp->ibat_ma = -raw_ua / 1000;	/* +charge / -discharge */
	bp->soc = strategy_class_fg_ops_get_soc();

	/* safety floor: pack too low for bypass */
	if (bp->soc <= BYPASS_FLOOR_SOC) {
		bypass_chg_exit(info);
		return;
	}

	switch (bp->state) {
	case BYPASS_CHG_ST_IDLE:
		bypass_chg_enter_state(info,
			bp->soc >= BYPASS_FLOAT_SOC ? BYPASS_CHG_ST_FULL :
						      BYPASS_CHG_ST_ACTIVE);
		break;

	case BYPASS_CHG_ST_ACTIVE:
		bypass_chg_regulate(info);
		buck_mw = bypass_chg_buck_power_mw();
		if (bypass_chg_can_enter_cp(info) &&
		    (buck_mw >= info->bypass_tune.cp_enter_mw ||
		     bp->ibat_ma < -250)) {
			if (++bp->cp_load_cnt >= BYPASS_CP_HOLD_TICKS) {
				bypass_chg_enter_state(info,
						       BYPASS_CHG_ST_CP_BYPASS);
				break;
			}
		} else {
			bp->cp_load_cnt = 0;
		}

		if (bp->sat_cnt >= BYPASS_DEBOUNCE_CNT)
			bypass_chg_enter_state(info, BYPASS_CHG_ST_ASSIST);
		else if (bp->soc >= BYPASS_FLOAT_SOC)
			bypass_chg_enter_state(info, BYPASS_CHG_ST_FULL);
		break;

	case BYPASS_CHG_ST_ASSIST:
		bypass_chg_regulate(info);
		if (bypass_chg_can_enter_cp(info)) {
			if (++bp->cp_load_cnt >= BYPASS_DEBOUNCE_CNT) {
				bypass_chg_enter_state(info,
						       BYPASS_CHG_ST_CP_BYPASS);
				break;
			}
		} else {
			bp->cp_load_cnt = 0;
		}

		if (bp->sat_cnt) {
			bp->assist_hold_jif = jiffies +
				msecs_to_jiffies(BYPASS_ASSIST_HOLD_MS);
		} else if (time_after(jiffies, bp->assist_hold_jif)) {
			bypass_chg_enter_state(info,
				bp->soc >= BYPASS_FLOAT_SOC ?
					BYPASS_CHG_ST_FULL :
					BYPASS_CHG_ST_ACTIVE);
		}
		break;

	case BYPASS_CHG_ST_CP_BYPASS:
		/*
		 * SC8581 DIV2 direct-drive bypass owns the PPS regulation loop.
		 * Hand back seamlessly to Buck ZDFB v3 if user disables cp_en,
		 * SOC hits 98%+, or quickchg exits (low load / vbat / tdie / fault).
		 */
		if (!info->bypass_tune.cp_enable || bp->soc >= 98) {
			bypass_chg_enter_state(info,
				bp->soc >= BYPASS_FLOAT_SOC ?
					BYPASS_CHG_ST_FULL :
					BYPASS_CHG_ST_ACTIVE);
			break;
		}
		(void)mca_strategy_func_get_status(
			STRATEGY_FUNC_TYPE_QUICK_CHARGE,
			STRATEGY_STATUS_TYPE_QC_CHARGE_STS, &qc_sts);
		if (qc_sts != MCA_QUICK_CHG_STS_CHARGING &&
		    time_after(jiffies, bp->cp_start_jif)) {
			mca_log_info("ZDFB CP_BYPASS exited (qc_sts=%d), fallback to buck\n",
				     qc_sts);
			bypass_chg_enter_state(info, BYPASS_CHG_ST_ACTIVE);
		}
		break;

	case BYPASS_CHG_ST_FULL:
		if (bp->ibat_ma < BYPASS_DEEP_DEFICIT_IBAT_MA)
			bypass_chg_enter_state(info, BYPASS_CHG_ST_ASSIST);
		break;

	default:
		bp->state = BYPASS_CHG_ST_IDLE;
		break;
	}

	bypass_chg_apply(info);
}

static void bypass_chg_workfunc(struct work_struct *work)
{
	struct smart_charge_info *info = container_of(
		work, struct smart_charge_info, bypass_chg_work.work);

	bypass_chg_eval(info);

	/*
	 * Keep polling with 250 ms cadence while the cable is in and bypass is active
	 */
	if (info->online && info->bypass_chg_mode != BYPASS_CHG_MODE_OFF)
		schedule_delayed_work(&info->bypass_chg_work,
				      msecs_to_jiffies(BYPASS_EVAL_PERIOD_MS));
}

static int bypass_chg_set_mode(struct smart_charge_info *info, int mode)
{
	if (!info->support_bypass_chg)
		return -EOPNOTSUPP;

	if (mode < 0 || mode >= BYPASS_CHG_MODE_MAX)
		return -EINVAL;

	info->bypass_chg_mode = mode;
	mca_log_info("bypass_chg: mode %d\n", mode);
	mod_delayed_work(system_wq, &info->bypass_chg_work, 0);

	return 0;
}

static void smart_charge_handle_mishow_config(struct smart_charge_info *info)
{
	const struct mca_hwid *hwid = mca_get_hwid_info();
	bool mishow = false;

	if (!hwid || mca_log_get_charge_boot_mode() == 0)
		return;
	if (hwid->platform_version != HARDWARE_PROJECT_O8 &&
	    hwid->platform_version != HARDWARE_PROJECT_O9)
		return;

	charger_partition_get_mishow(&mishow);
	info->mishow_config = mishow;
	mca_log_info("mishow config is %d\n", info->mishow_config);
	if (info->mishow_config)
		smartcharging_handle_algorithm_conflict(info);
}

static void smart_charge_workfunc(struct work_struct *work)
{
	struct smart_charge_info *info = container_of(
		work, struct smart_charge_info, smart_charge_work.work);
	int delta_ichg = 0;
	const struct mca_hwid *hwid = mca_get_hwid_info();

	//To-Do
	//Battery status monitoring work_func
	if (!info->online) {
		mca_log_err("adapter is plugout\n");
		return;
	}
	//battery health4.0 cyclecount decrease ichg
	strategy_class_fg_ops_get_cyclecount(&info->cycle_count);
	for (int i = 0; i < CC_ICHG_MAX_GROUP; i++) {
		if (info->cycle_count <= info->ichg_cc_table[i].cc_max) {
			delta_ichg = info->ichg_cc_table[i].delta_ichg;
			break;
		}
	}

	if (delta_ichg != info->smart_delta_ichg) {
		info->smart_delta_ichg = delta_ichg;
		mca_vote(info->smartchg_delta_ichg_voter, "batt_health", true,
			 delta_ichg);
		mca_log_info("cycle_count %d smart_delta_ichg %d\n",
			     info->cycle_count, info->smart_delta_ichg);
	}
	//csd1.0 feature:check battery swelling
	if (hwid && hwid->country_version == CountryCN && info->support_csd)
		smart_charge_csd_send_pulse(info);

	//smart bypass fcc limiter (mishow)
	smart_charge_handle_bypass_chg(info);
	smart_charge_handle_mishow_config(info);

	schedule_delayed_work(&info->smart_charge_work, msecs_to_jiffies(5000));
}

static void smart_charge_sense_chg_workfunc(struct work_struct *work)
{
	struct smart_charge_info *info = container_of(
		work, struct smart_charge_info, smart_sense_chg_work.work);

	if (!info)
		return;

	smart_charge_handle_sense_chg(info);
}

static int mca_smart_charge_process_event(int event, int value, void *data)
{
	struct smart_charge_info *info = data;

	if (!info)
		return -1;

	mca_log_info("receive event %d, value %d\n", event, value);
	switch (event) {
	case MCA_EVENT_WIRELESS_CONNECT:
		info->wls_online = 1;
		fallthrough;
	case MCA_EVENT_USB_CONNECT:
		info->online = 1;
		strategy_class_fg_ops_get_rsoc(&info->plugin_rsoc);
		smart_charge_plugin_or_plugout_reset(info);
		cancel_delayed_work_sync(&info->smart_charge_work);
		schedule_delayed_work(&info->smart_charge_work, 0);
		cancel_delayed_work_sync(&info->smart_soc_limit_work);
		schedule_delayed_work(&info->smart_soc_limit_work, 0);
		mod_delayed_work(system_wq, &info->bypass_chg_work, 0);
		break;
	case MCA_EVENT_WIRELESS_DISCONNECT:
		info->wls_online = 0;
		fallthrough;
	case MCA_EVENT_USB_DISCONNECT:
		info->online = 0;
		smart_charge_plugin_or_plugout_reset(info);
		cancel_delayed_work_sync(&info->smart_charge_work);
		cancel_delayed_work_sync(&info->smart_soc_limit_work);
		/* Cancel first: releasing the votes must be the last word. */
		cancel_delayed_work_sync(&info->bypass_chg_work);
		bypass_chg_exit(info);
		//memset(&info->smart_chg_data, 0, sizeof(info->smart_chg_data));
		memset(&info->smart_chg_control, 0,
		       sizeof(info->smart_chg_control));
		info->soc_limit_enable = false;
		info->ignore_upload.AsUINT32 = 0;
		smart_charge_set_soc_limit(info->soc_limit_enable);
		break;
	case MCA_EVENT_BATTERY_DTPT:
		if (value)
			mca_vote(info->smartchg_delta_fv_voter, "dtpt", true,
				 SMART_CHARGE_DTPT_DELTA_VALUE);
		else
			mca_vote(info->smartchg_delta_fv_voter, "dtpt", false,
				 0);
		break;
	default:
		break;
	}

	return 0;
}

static int smart_charge_fv_control(struct mca_votable *votable, void *data,
				   int effective_result,
				   const char *effective_client)
{
	struct smart_charge_info *info = data;
	static struct mca_smartchg_if_ops *if_ops;

	if (!data)
		return -1;

	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_delta_fv) {
			mca_log_info(" %d %p\n", i, if_ops);
			continue;
		}
		if_ops->set_delta_fv(if_ops->data, effective_result);
	}
	info->delta_fv = effective_result;
	mca_log_err("smartchg_delta_fv %d\n", effective_result);

	return 0;
}

static int smart_charge_ichg_control(struct mca_votable *votable, void *data,
				     int effective_result,
				     const char *effective_client)
{
	struct smart_charge_info *info = data;
	static struct mca_smartchg_if_ops *if_ops;

	if (!data)
		return -1;

	if (effective_result)
		info->delta_ichg = effective_result;
	else
		info->delta_ichg = 0;

	mca_log_err("smartchg_delta_ichg %d\n", effective_result);
	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_delta_ichg)
			continue;
		if_ops->set_delta_ichg(if_ops->data, effective_result);
	}

	return 0;
}

static int smart_charge_set_fcc_control(struct mca_votable *votable, void *data,
					int effective_result,
					const char *effective_client)
{
	static struct mca_smartchg_if_ops *if_ops;

	if (!data)
		return -1;

	mca_log_err("smartchg_set_fcc %d\n", effective_result);

	for (int i = 0; i < MCA_SMARTCHG_IF_CHG_TYPE_END; i++) {
		if_ops = mca_smartchg_if_get_ops(i);
		if (!if_ops || !if_ops->set_fcc)
			continue;
		if_ops->set_fcc(if_ops->data, effective_result);
	}

	return 0;
}

static int smart_charge_init_voter(struct smart_charge_info *info)
{
	info->smartchg_delta_fv_voter = mca_create_votable(
		"smartchg_delta_fv", MCA_VOTE_MAX, smart_charge_fv_control,
		SMART_CHARGE_DELTA_FV_DEFAULT_VALUE, info);
	if (IS_ERR(info->smartchg_delta_fv_voter)) {
		mca_log_err("smartchg_delta_fv voteable failed\n");
		return -1;
	}

	info->smartchg_delta_ichg_voter = mca_create_votable(
		"smartchg_delta_ichg", MCA_VOTE_MAX, smart_charge_ichg_control,
		SMART_CHARGE_DELTA_ICHG_DEFAULT_VALUE, info);
	if (IS_ERR(info->smartchg_delta_ichg_voter)) {
		mca_log_err("smartchg_delta_ichg voteable failed\n");
		return -1;
	}

	info->smartchg_set_fcc_voter = mca_create_votable(
		"smartchg_set_fcc", MCA_VOTE_MIN, smart_charge_set_fcc_control, 0,
		info);
	if (IS_ERR(info->smartchg_set_fcc_voter)) {
		mca_log_err("smartchg_fcc_voter voteable failed\n");
		return -1;
	}

	return 0;
}

/*
 * bypass_params_store - "<key> <value>", one knob per write.
 *
 * Keeps ZDFB policy in userspace: GameSpace can retune the regulator live
 * without a reflash, and everything lands through bypass_tune_sanitise() so a
 * bad write clamps instead of asking the buck for current it cannot source.
 */
static int bypass_params_store(struct smart_charge_info *info, const char *buf)
{
	struct bypass_chg_tune t = info->bypass_tune;
	char key[24];
	int val;

	if (sscanf(buf, "%23s %d", key, &val) != 2)
		return -EINVAL;

	if (!strcmp(key, "target"))
		t.target_ibat_ma = val;
	else if (!strcmp(key, "fcc_max"))
		t.fcc_max_ma = val;
	else if (!strcmp(key, "kp"))
		t.pi_kp = val;
	else if (!strcmp(key, "deadband"))
		t.pi_deadband_ma = val;
	else if (!strcmp(key, "step"))
		t.pi_step_max_ma = val;
	else if (!strcmp(key, "in9v"))
		t.input_9v_ma = val;
	else if (!strcmp(key, "in5v"))
		t.input_5v_ma = val;
	else if (!strcmp(key, "cp_en"))
		t.cp_enable = val;
	else if (!strcmp(key, "cp_mw"))
		t.cp_enter_mw = val;
	else if (!strcmp(key, "cp_vbat"))
		t.cp_vbat_max_mv = val;
	else
		return -EINVAL;

	bypass_tune_sanitise(&t);
	info->bypass_tune = t;
	mca_log_info("ZDFB v3 tune: %s=%d (target=%d fcc_max=%d kp=%d db=%d step=%d in9v=%d in5v=%d cp_en=%d cp_mw=%d cp_vbat=%d)\n",
		     key, val, t.target_ibat_ma, t.fcc_max_ma, t.pi_kp,
		     t.pi_deadband_ma, t.pi_step_max_ma, t.input_9v_ma,
		     t.input_5v_ma, t.cp_enable, t.cp_enter_mw,
		     t.cp_vbat_max_mv);
	return 0;
}

static ssize_t smart_charge_sysfs_show(struct device *dev,
				       struct device_attribute *attr, char *buf)
{
	struct mca_sysfs_attr_info *attr_info;
	ssize_t count = 0;
	struct smart_charge_info *info = dev_get_drvdata(dev);
	int val = 0;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  smartchg_sysfs_field_tbl,
					  SMARTCHG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_SMARTCHG:
		val = get_smart_chg_message();
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_SMARTBATT:
		val = get_smart_batt_message();
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_SMARTNIGHT:
		val = get_smart_night_message();
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_BYPASS_CHG:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->bypass_chg_mode);
		break;
	case MCA_PROP_BYPASS_CHG_STATE:
		/*
		 * state, the pack current that drove it (mA, +ve = drain), and
		 * the FCC ceiling the regulator has converged on (mA). The
		 * third field is what tells you whether the loop is holding or
		 * pinned - without it a deficit and a healthy float look alike.
		 */
		count = scnprintf(buf, PAGE_SIZE, "%d %d %d\n",
				  info->bypass.state, -info->bypass.ibat_ma,
				  info->bypass.fcc_ma);
		break;
	case MCA_PROP_BYPASS_PARAMS:
		count = scnprintf(buf, PAGE_SIZE,
				  "target=%d fcc_max=%d kp=%d deadband=%d step=%d in9v=%d in5v=%d cp_en=%d cp_mw=%d cp_vbat=%d\n",
				  info->bypass_tune.target_ibat_ma,
				  info->bypass_tune.fcc_max_ma,
				  info->bypass_tune.pi_kp,
				  info->bypass_tune.pi_deadband_ma,
				  info->bypass_tune.pi_step_max_ma,
				  info->bypass_tune.input_9v_ma,
				  info->bypass_tune.input_5v_ma,
				  info->bypass_tune.cp_enable,
				  info->bypass_tune.cp_enter_mw,
				  info->bypass_tune.cp_vbat_max_mv);
		break;
	case MCA_PROP_SMARTCHG_FV:
		val = mca_get_client_vote(info->smartchg_delta_fv_voter,
					  "smart_chg");
		val = val < 0 ? 0 : val;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_SMARTCHG_ICHG:
		val = mca_get_client_vote(info->smartchg_delta_ichg_voter,
					  "smart_chg");
		val = val < 0 ? 0 : val;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_POSTURE:
		val = info->smart_chg_data.smart_chgcfg[SMART_CHG_SENSE_CHG]
				      .use_fake_value ?
			      info->smart_chg_data
				      .smart_chgcfg[SMART_CHG_SENSE_CHG]
				      .fake_value :
			      info->smart_chg_data
				      .smart_chgcfg[SMART_CHG_SENSE_CHG]
				      .func_value;
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_SCENE:
		val = mca_smartchg_get_scene();
		count = scnprintf(buf, PAGE_SIZE, "%d\n", val);
		break;
	case MCA_PROP_BOARD_TEMP:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->board_temp);
		break;
	case MCA_PROP_SMART_SIC_MODE:
		count = scnprintf(buf, PAGE_SIZE, "%d\n", info->smart_sic_mode);
		break;
	default:
		break;
	}

	return count;
}

static ssize_t smart_charge_sysfs_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	struct mca_sysfs_attr_info *attr_info;
	struct smart_charge_info *info = dev_get_drvdata(dev);
	int val;
	int ret;

	attr_info = mca_sysfs_lookup_attr(attr->attr.name,
					  smartchg_sysfs_field_tbl,
					  SMARTCHG_SYSFS_ATTRS_SIZE);
	if (!attr_info)
		return -1;

	if (!info) {
		mca_log_err("%s dev_driverdata is null\n", __func__);
		return -1;
	}

	/*
	 * Handled before the scalar parse below: this node takes "<key> <val>"
	 * so a tuner can retune one knob without restating the whole set.
	 */
	if (attr_info->sysfs_attr_name == MCA_PROP_BYPASS_PARAMS) {
		if (bypass_params_store(info, buf))
			return -EINVAL;
		return count;
	}

	if (kstrtoint(buf, 0, &val))
		return -EINVAL;

	switch (attr_info->sysfs_attr_name) {
	case MCA_PROP_SMARTCHG:
		smartcharging_receive_control_message(val);
		break;
	case MCA_PROP_SMARTBATT:
		handle_smart_batt_message(val);
		break;
	case MCA_PROP_SMARTNIGHT:
		handle_smart_night_message(val);
		break;
	case MCA_PROP_BYPASS_CHG:
		/* Propagate verbatim: -EOPNOTSUPP tells the caller the board
		 * never opted in, which reads very differently from -EINVAL. */
		ret = bypass_chg_set_mode(info, val);
		if (ret)
			return ret;
		break;
	case MCA_PROP_SMARTCHG_FV:
		val = val * info->cell_type;
		mca_vote(info->smartchg_delta_fv_voter, "smart_chg", true, val);
		break;
	case MCA_PROP_SMARTCHG_ICHG:
		mca_vote(info->smartchg_delta_ichg_voter, "smart_chg", true,
			 val);
		break;
	case MCA_PROP_SCENE:
		mca_smartchg_set_scene(val);
		break;
	case MCA_PROP_BOARD_TEMP:
		mca_smartchg_set_board_temp(val);
		break;
	default:
		break;
	}

	mca_log_info("set the %d value = %d\n", attr_info->sysfs_attr_name,
		     val);

	return count;
}

static int smart_charge_sysfs_create_group(struct device *dev)
{
	mca_sysfs_init_attrs(smartchg_sysfs_attrs, smartchg_sysfs_field_tbl,
			     SMARTCHG_SYSFS_ATTRS_SIZE);
	return mca_sysfs_create_link_group("charger", "smart_charge", dev,
					   &smartchg_sysfs_attr_group);
}

static void smart_charge_sysfs_remove_group(struct device *dev)
{
	mca_sysfs_remove_link_group("charger", "smart_charge", dev,
				    &smartchg_sysfs_attr_group);
}

static int smart_charge_handle_baa_data(struct smart_charge_info *info)
{
	struct smart_basp_header *basp_header =
		(struct smart_basp_header *)info->mmap_addr;
	struct smart_batt_spec *pbatt_spec = NULL;
	static struct mca_smartchg_if_ops *if_ops;
	int offset = 0;

	if (!basp_header) {
		mca_log_err("mmap_addr is NULL\n");
		return -1;
	}

#if 0
	int *raw = (int *)(info->mmap_addr);
	for (int i = 0; i < basp_header->total_len / sizeof(int) - 10; i += 10) {
		mca_log_err("raw[%d]: %d, %d, %d, %d, %d, %d, %d, %d, %d, %d\n",
			i, raw[i], raw[i + 1], raw[i + 2], raw[i + 3], raw[i + 4],
			raw[i + 5], raw[i + 6], raw[i + 7], raw[i + 8], raw[i + 9]);
	}
#endif

	// update buck para
	offset = sizeof(struct smart_basp_header);
	if_ops = mca_smartchg_if_get_ops(MCA_SMARTCHG_IF_CHG_TYPE_JEITA);
	if (!if_ops || !if_ops->update_baa_para) {
		mca_log_err("jeita update_baa_para is null\n");
	} else {
		mca_log_err("jeita update_baa_para offset: %d, size: %d/%d\n",
			    offset, basp_header->jeita_ffc_term_size,
			    basp_header->jeita_normal_term_size);
		if_ops->update_baa_para(if_ops->data, info->mmap_addr + offset,
					basp_header->jeita_ffc_term_size,
					basp_header->jeita_normal_term_size);
	}

	// get wired para offset
	offset += basp_header->jeita_ffc_term_size *
		  sizeof(struct smart_batt_jeita_term_para);
	offset += basp_header->jeita_normal_term_size *
		  sizeof(struct smart_batt_jeita_term_para);

	// update wired para
	if_ops = mca_smartchg_if_get_ops(MCA_SMARTCHG_IF_CHG_TYPE_QC);
	if (!if_ops || !if_ops->update_baa_para) {
		mca_log_err("wired qc update_baa_para is null\n");
	} else {
		mca_log_err(
			"wired qc update_baa_para offset: %d, size: %d/%d\n",
			offset, basp_header->wired_ffc_size,
			basp_header->wired_normal_size);
		if_ops->update_baa_para(if_ops->data, info->mmap_addr + offset,
					basp_header->wired_ffc_size,
					basp_header->wired_normal_size);
	}

	// get wls para offset
	for (int i = 0; i < basp_header->wired_ffc_size; i++) {
		pbatt_spec =
			(struct smart_batt_spec *)(info->mmap_addr + offset);
		offset += offsetof(struct smart_batt_spec, steps) +
			  pbatt_spec->step_size *
				  sizeof(struct smart_batt_spec_curve);
	}
	for (int i = 0; i < basp_header->wired_normal_size; i++) {
		pbatt_spec =
			(struct smart_batt_spec *)(info->mmap_addr + offset);
		offset += offsetof(struct smart_batt_spec, steps) +
			  pbatt_spec->step_size *
				  sizeof(struct smart_batt_spec_curve);
	}

	// update wls para
	if_ops = mca_smartchg_if_get_ops(MCA_SMARTCHG_IF_CHG_TYPE_WL_QC);
	if (!if_ops || !if_ops->update_baa_para) {
		mca_log_err("wls qc update_baa_para is null\n");
	} else {
		mca_log_err("wls qc update_baa_para offset: %d, size: %d/%d\n",
			    offset, basp_header->wls_ffc_size,
			    basp_header->wls_normal_size);
		if_ops->update_baa_para(if_ops->data, info->mmap_addr + offset,
					basp_header->wls_ffc_size,
					basp_header->wls_normal_size);
	}

	return 0;
}

static int smart_charge_cal_checksum(struct smart_charge_info *info)
{
	struct smart_basp_header *basp_header = NULL;
	uint32_t checksum = 0;
	size_t i = 0;
	size_t total_len;
	char *temp_buff = info->mmap_addr;
	size_t buffer_size = info->mmap_size;

	if (!temp_buff) {
		mca_log_err("mmap_addr is NULL\n");
		return -1;
	}

	basp_header = kmalloc(sizeof(struct smart_basp_header), GFP_KERNEL);
	if (!basp_header) {
		mca_log_err("Alloc header failed\n");
		return -1;
	}
	memcpy(basp_header, temp_buff, sizeof(struct smart_basp_header));

	mca_log_err("header: %d, %d, %d; %d, %d; %d, %d; %d, %d\n",
		    basp_header->checksum, basp_header->type,
		    basp_header->total_len, basp_header->jeita_ffc_term_size,
		    basp_header->jeita_normal_term_size,
		    basp_header->wired_ffc_size, basp_header->wired_normal_size,
		    basp_header->wls_ffc_size, basp_header->wls_normal_size);

	total_len = basp_header->total_len;
	if ((total_len > buffer_size && info->map_flag) ||
	    total_len < sizeof(struct smart_basp_header)) {
		mca_log_err("Invalid total_len: %zu\n", total_len);
		kfree(basp_header);
		return -1;
	}

	for (i = sizeof(int); i < total_len; i++)
		checksum += (uint8_t)temp_buff[i];

	mca_log_err("cal checksum %d, origin checksum %d\n", checksum,
		    basp_header->checksum);
	if (checksum != basp_header->checksum) {
		kfree(basp_header);
		return -1;
	}

	kfree(basp_header);
	return 0;
}

static void smart_charge_handle_mmap_data(struct smart_charge_info *info)
{
	if (!info->mmap_addr || (!info->mmap_size && info->map_flag)) {
		mca_log_err("Invalid mmap buffer\n");
		goto out;
	}
	if (smart_charge_cal_checksum(info)) {
		mca_log_err("checksum failed\n");
		goto out;
	}

	if (info->enable_fv_dec_by_cc)
		smart_charge_handle_baa_data(info);

out:
	info->map_flag = 0;
	if (info->mmap_addr) {
		kfree(info->mmap_addr);
		info->mmap_addr = NULL;
		info->mmap_size = 0;
	}
}

static int smart_charge_file_open(struct inode *inode, struct file *filp)
{
	struct smart_charge_info *info = NULL;

	info = container_of(inode->i_cdev, struct smart_charge_info, pri_dev);
	if (info->mmap_addr)
		return 0;

	info->mmap_addr = kzalloc(PAGE_SIZE, GFP_KERNEL);
	if (!info->mmap_addr)
		return -1;

	mca_log_err("malloc memery succ\n");
	filp->private_data = info;

	return 0;
}

static int smart_charge_file_close(struct inode *inode, struct file *filp)
{
	struct smart_charge_info *info = NULL;

	info = container_of(inode->i_cdev, struct smart_charge_info, pri_dev);
	if (!info) {
		mca_log_err("Failed to get smart_charge_info from cdev\n");
		return -EINVAL;
	}

	if (info && info->mmap_addr)
		smart_charge_handle_mmap_data(info);
	mca_log_err("release memery succ\n");

	return 0;
}

static int smart_charge_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct smart_charge_info *info = filp->private_data;
	unsigned long size = vma->vm_end - vma->vm_start;
	int ret;

	if (size > MAX_MMAP_SIZE) {
		mca_log_err("mmap size %lu exceeds limit %d\n", size,
			    MAX_MMAP_SIZE);
		return -EINVAL;
	}

	if (!info || !info->mmap_addr || info->map_flag) {
		mca_log_err("map addr error\n");
		return -1;
	}
	if (vma->vm_end - vma->vm_start > PAGE_SIZE) {
		mca_log_err("map addr len error\n");
		return -1;
	}

	info->mmap_size = size;
	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	ret = remap_pfn_range(vma, vma->vm_start,
			      virt_to_phys(info->mmap_addr) >> PAGE_SHIFT,
			      vma->vm_end - vma->vm_start, vma->vm_page_prot);

	mca_log_err("vma start 0x%x, prot %d\n", vma->vm_start,
		    vma->vm_page_prot);
	info->map_flag = 1;

	if (ret)
		mca_log_err("remap pfn failed %d\n", ret);

	return ret;
}

static long smart_charge_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct smart_charge_info *info = file->private_data;
	int ret;
	int err = 0;

#if (KERNEL_VERSION(5, 0, 0) > LINUX_VERSION_CODE)
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok(VERIFY_WRITE, (void __user *)arg,
				 _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok(VERIFY_READ, (void __user *)arg,
				 _IOC_SIZE(cmd));
#else
	if (_IOC_DIR(cmd) & _IOC_READ)
		err = !access_ok((void *)arg, _IOC_SIZE(cmd));
	else if (_IOC_DIR(cmd) & _IOC_WRITE)
		err = !access_ok((void *)arg, _IOC_SIZE(cmd));
#endif
	if (err)
		return -EFAULT;

	switch (cmd) {
	case SMARTCHG_DATA_CMD:
		if (!info->mmap_addr) {
			mca_log_err("mmap addr is null\n");
			return -EINVAL;
		}
		ret = copy_from_user(info->mmap_addr, (void __user *)arg,
				     PAGE_SIZE);
		mca_log_info("data ret = %d\n", ret);
		if (ret)
			return -EINVAL;
		break;
	default:
		mca_log_err("invalid cmd\n");
		return -EINVAL;
	}
	return 0;
}

static int smart_charge_panel_notifier_cb(struct notifier_block *nb,
					  unsigned long event, void *val)
{
	struct smart_charge_info *info =
		container_of(nb, struct smart_charge_info, panel_nb);
	int tmp = 0;

	switch (event) {
	case MCA_EVENT_PANEL_SCREEN_STATE_CHANGE:
		tmp = *(int *)val;
		mca_log_info("update screen_state: %d => %d\n",
			     info->screen_state, tmp);
		info->screen_state = tmp;
		cancel_delayed_work_sync(&info->smart_sense_chg_work);
		schedule_delayed_work(&info->smart_sense_chg_work, 0);
		break;
	default:
		break;
	}
	return NOTIFY_DONE;
}

static const struct file_operations g_smt_chg_file_ops = {
	.owner = THIS_MODULE,
	.open = smart_charge_file_open,
	.release = smart_charge_file_close,
	.mmap = smart_charge_mmap,
#ifdef CONFIG_COMPAT
	.compat_ioctl = smart_charge_ioctl,
#endif /* CONFIG_COMPAT */
	.unlocked_ioctl = smart_charge_ioctl,
};

static int smart_charge_parse_dt(struct smart_charge_info *info)
{
	struct device_node *node = info->dev->of_node;
	int len = 0;
	int ret = 0;
	int batt_ichg_cfg[CC_ICHG_MAX_GROUP * SMARTCHG_MODE_PARA_MAX];

	/* real bypass charging (ZDFB) */
	(void)mca_parse_dts_u32(node, "support_bypass_chg",
				&info->support_bypass_chg, 0);
	(void)mca_parse_dts_u32(node, "bypass-float-fcc-ma",
				&info->bypass_float_fcc_ma,
				BYPASS_FLOAT_FCC_MA_DEFAULT);
	info->bypass_float_fcc_ma = clamp(info->bypass_float_fcc_ma,
					  BYPASS_FLOAT_FCC_MA_MIN,
					  BYPASS_FLOAT_FCC_MA_MAX);
	mca_log_info("ZDFB float FCC ceiling: %d mA\n",
		     info->bypass_float_fcc_ma);

	/* ZDFB v2 regulation tunables (DT defaults; sysfs retunes live) */
	(void)mca_parse_dts_u32(node, "bypass-target-ibat-ma",
				(u32 *)&info->bypass_tune.target_ibat_ma,
				BYPASS_TARGET_IBAT_MA_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-fcc-max-ma",
				(u32 *)&info->bypass_tune.fcc_max_ma,
				BYPASS_FCC_MAX_MA_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-pi-kp",
				(u32 *)&info->bypass_tune.pi_kp,
				BYPASS_PI_KP_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-pi-deadband-ma",
				(u32 *)&info->bypass_tune.pi_deadband_ma,
				BYPASS_PI_DEADBAND_MA_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-pi-step-max-ma",
				(u32 *)&info->bypass_tune.pi_step_max_ma,
				BYPASS_PI_STEP_MAX_MA_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-input-9v-ma",
				(u32 *)&info->bypass_tune.input_9v_ma,
				BYPASS_INPUT_9V_MA_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-input-5v-ma",
				(u32 *)&info->bypass_tune.input_5v_ma,
				BYPASS_INPUT_5V_MA_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-cp-enable",
				(u32 *)&info->bypass_tune.cp_enable,
				BYPASS_CP_ENABLE_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-cp-enter-mw",
				(u32 *)&info->bypass_tune.cp_enter_mw,
				BYPASS_CP_ENTER_MW_DEFAULT);
	(void)mca_parse_dts_u32(node, "bypass-cp-vbat-max-mv",
				(u32 *)&info->bypass_tune.cp_vbat_max_mv,
				BYPASS_CP_VBAT_MAX_MV_DEFAULT);
	bypass_tune_sanitise(&info->bypass_tune);
	mca_log_info("ZDFB v3: target=%dmA fcc_max=%dmA kp=%d db=%dmA step=%dmA in9v=%dmA in5v=%dmA cp_en=%d cp_mw=%d cp_vbat=%d\n",
		     info->bypass_tune.target_ibat_ma,
		     info->bypass_tune.fcc_max_ma, info->bypass_tune.pi_kp,
		     info->bypass_tune.pi_deadband_ma,
		     info->bypass_tune.pi_step_max_ma,
		     info->bypass_tune.input_9v_ma,
		     info->bypass_tune.input_5v_ma,
		     info->bypass_tune.cp_enable,
		     info->bypass_tune.cp_enter_mw,
		     info->bypass_tune.cp_vbat_max_mv);

	(void)mca_parse_dts_u32(node, "cell_type", &info->cell_type,
				MCA_CELL_TYPE_1S);
	mca_parse_dts_u32(node, "enable_fv_dec_by_cc",
			  &info->enable_fv_dec_by_cc, 0);
	mca_parse_dts_u32(node, "support_csd", &info->support_csd, 0);
	info->support_sensechg_v2 =
		of_property_read_bool(node, "support_sensechg_v2");
	info->support_cc_ichg = of_property_read_bool(node, "support_cc_ichg");
	if (info->support_cc_ichg) {
		len = mca_parse_dts_u32_count(node, "cc_ichg_cfg",
					      CC_ICHG_MAX_GROUP,
					      SMARTCHG_MODE_PARA_MAX);
		if (len < 0) {
			mca_log_err("parse cc_ichg_cfg failed\n");
			return -1;
		}

		ret = mca_parse_dts_u32_array(
			node, "cc_ichg_cfg", batt_ichg_cfg,
			CC_ICHG_MAX_GROUP * SMARTCHG_MODE_PARA_MAX);
		if (ret) {
			mca_log_err(" parse cc_ichg_cfg failed\n");
			return -1;
		}

		for (int row = 0; row < (len / SMARTCHG_MODE_PARA_MAX); row++) {
			int col = row * SMARTCHG_MODE_PARA_MAX;

			info->ichg_cc_table[row].cc_max =
				batt_ichg_cfg[col + SMARTCHG_MODE_CYCLECOUNT];
			info->ichg_cc_table[row].delta_ichg =
				batt_ichg_cfg[col + SMARTCHG_MODE_DELTA_ICHG];
			mca_log_err("%d, %d\n", info->ichg_cc_table[row].cc_max,
				    info->ichg_cc_table[row].delta_ichg);
		}
	}

	mca_parse_dts_u32(node, "smart_bypass_support", &info->support_bypass,
			  0);
	if (info->support_bypass) {
		const int elems =
			sizeof(struct smart_bypass_temp_section) / sizeof(u32);

		mca_parse_dts_u32(node, "smart_bypass_entry_soc",
				  &info->bypass_entry_soc, 0);
		mca_parse_dts_u32(node, "smart_bypass_exit_soc",
				  &info->bypass_exit_soc, 0);

		len = mca_parse_dts_u32_count(node, "smart_bypass_high_lmt_cfg",
					      SMART_BYPASS_TEMP_SECTION_MAX,
					      elems);
		if (len < 0) {
			mca_log_err("parse smart_bypass_high_lmt_cfg failed\n");
			return -1;
		}
		info->bypass_high_num = len / elems;
		ret = mca_parse_dts_u32_array(node, "smart_bypass_high_lmt_cfg",
					      (u32 *)info->bypass_high_lmt, len);
		if (ret < 0)
			return -1;

		len = mca_parse_dts_u32_count(node, "smart_bypass_med_lmt_cfg",
					      SMART_BYPASS_TEMP_SECTION_MAX,
					      elems);
		if (len < 0) {
			mca_log_err("parse smart_bypass_med_lmt_cfg failed\n");
			return -1;
		}
		info->bypass_med_num = len / elems;
		ret = mca_parse_dts_u32_array(node, "smart_bypass_med_lmt_cfg",
					      (u32 *)info->bypass_med_lmt, len);
		if (ret < 0)
			return -1;

		len = mca_parse_dts_u32_count(node, "smart_bypass_low_lmt_cfg",
					      SMART_BYPASS_TEMP_SECTION_MAX,
					      elems);
		if (len < 0) {
			mca_log_err("parse smart_bypass_low_lmt_cfg failed\n");
			return -1;
		}
		info->bypass_low_num = len / elems;
		ret = mca_parse_dts_u32_array(node, "smart_bypass_low_lmt_cfg",
					      (u32 *)info->bypass_low_lmt, len);
		if (ret < 0)
			return -1;
	}

	return 0;
}

static int smart_charge_probe(struct platform_device *pdev)
{
	struct smart_charge_info *info;
	static int probe_cnt;
	int ret = 0, online = 0, wireless_online = 0;

	mca_log_info("probe_cnt = %d\n", ++probe_cnt);
	info = devm_kzalloc(&pdev->dev, sizeof(*info), GFP_KERNEL);
	if (!info) {
		mca_log_err("out of memory\n");
		return -ENOMEM;
	}

	info->dev = &pdev->dev;
	platform_set_drvdata(pdev, info);
	global_smartchg_info = info;
	ret = smart_charge_parse_dt(info);
	if (ret) {
		mca_log_err("parse dt failed\n");
		return -1;
	}
	smart_charge_sysfs_create_group(info->dev);
	ret = smart_charge_init_voter(info);
	if (ret) {
		mca_log_err("init voter err\n");
		return -1;
	}

	(void)mca_strategy_ops_register(STRATEGY_FUNC_TYPE_SMARTCHG,
					mca_smart_charge_process_event, NULL,
					NULL, info);

	info->panel_nb.notifier_call = smart_charge_panel_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_PANEL, &info->panel_nb);

	info->thermal_nb.notifier_call = smart_charge_thermal_notifier_cb;
	mca_event_block_notify_register(MCA_EVENT_TYPE_THERMAL_TEMP,
					&info->thermal_nb);

	INIT_DELAYED_WORK(&info->smart_charge_work, smart_charge_workfunc);
	INIT_DELAYED_WORK(&info->smart_sense_chg_work,
			  smart_charge_sense_chg_workfunc);
	INIT_DELAYED_WORK(&info->smart_soc_limit_work,
			  smart_charge_soc_limit_workfunc);
	INIT_DELAYED_WORK(&info->bypass_chg_work, bypass_chg_workfunc);

	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BUCK_CHARGE,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &online);
	(void)mca_strategy_func_get_status(STRATEGY_FUNC_TYPE_BASIC_WIRELESS,
					   STRATEGY_STATUS_TYPE_ONLINE,
					   &wireless_online);
	ret = alloc_chrdev_region(&info->dev_num, 0, 1, "smart_charge");
	if (ret) {
		mca_log_err("alloc chrdev num failed\n");
		return ret;
	}

	cdev_init(&info->pri_dev, &g_smt_chg_file_ops);
	info->pri_dev.owner = THIS_MODULE;
	ret = cdev_add(&info->pri_dev, info->dev_num, 1);
	if (ret) {
		mca_log_err("cdev add failed\n");
		goto error1;
	}
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(6, 6, 9))
	info->smart_charge_class = class_create("smart_charge");
#else
	info->smart_charge_class = class_create(THIS_MODULE, "smart_charge");
#endif
	if (!info->smart_charge_class) {
		mca_log_err("create smart charge class failed\n");
		goto error2;
	}

	info->sys_dev = device_create(info->smart_charge_class, NULL,
				      info->dev_num, NULL, "smart_charge_cdev");
	if (!info->sys_dev) {
		mca_log_err("create smart charge device failed\n");
		goto error3;
	}

	if (online || wireless_online) {
		info->online = 1;
		schedule_delayed_work(&info->smart_charge_work, 0);
	}

	info->fake_scene = SMART_CHARGE_FAKE_SCENE_DEFAULT;

	mca_log_info("done\n");
	return 0;

error3:
	class_destroy(info->smart_charge_class);
error2:
	cdev_del(&info->pri_dev);
error1:
	unregister_chrdev_region(info->dev_num, 1);

	return -1;
}

static void smart_charge_remove(struct platform_device *pdev)
{
	struct smart_charge_info *info = platform_get_drvdata(pdev);

	unregister_chrdev_region(info->dev_num, 1);
	cdev_del(&info->pri_dev);
	device_destroy(info->smart_charge_class, info->dev_num);
	class_destroy(info->smart_charge_class);
	smart_charge_sysfs_remove_group(&pdev->dev);
	mca_event_block_notify_unregister(MCA_EVENT_TYPE_PANEL,
					  &info->panel_nb);

}

static const struct of_device_id match_table[] = {
	{ .compatible = "xiaomi,smart_charge" },
	{},
};

static struct platform_driver smart_charge_driver = {
	.driver = {
		.owner = THIS_MODULE,
		.name = "smart_charge",
		.of_match_table = match_table,
	},
	.probe = smart_charge_probe,
	.remove = smart_charge_remove,
};

static int __init smart_charge_init(void)
{
	return platform_driver_register(&smart_charge_driver);
}
module_init(smart_charge_init);

static void __exit smart_charge_exit(void)
{
	platform_driver_unregister(&smart_charge_driver);
}
module_exit(smart_charge_exit);

MODULE_DESCRIPTION("smart charge driver");
MODULE_AUTHOR("liuzhengqing@xiaomi.com");
MODULE_LICENSE("GPL v2");
