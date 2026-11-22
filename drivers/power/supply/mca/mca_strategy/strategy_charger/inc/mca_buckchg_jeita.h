/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_buckchg_jeita.h
 *
 * mca buck charger jeita control driver
 *
 * Copyright (c) 2024-2024 Xiaomi Technologies Co., Ltd.
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
#ifndef __MCA_BUCKCHG_JEITA_H__
#define __MCA_BUCKCHG_JEITA_H__

#define MCA_BUCKCHG_JEITA_INIT_VOTER_INTERVAL 1000
#define MCA_BUCKCHG_JEITA_UPDATE_FAST_INTERVAL 5000
#define MCA_BUCKCHG_JEITA_UPDATE_NORMAL_INTERVAL 3000
#define MCA_BUCKCHG_JEITA_FLIP_CURRENT_DEFAULT_VALUE 2950

#define MCA_BUCKCHG_JEITA_VBAT_HIGH_HYST 50
#define MCA_BUCKCHG_JEITA_VBAT_LOW_HYST 50

#define JEITA_VOLT_DATA_MAX_GROUP 4
enum jeita_volt_para_ele {
	BUCKCHG_VOLTAGE_TH = 0,
	BUCKCHG_CURRENT_MAX,
	BUCKCHG_VOLTAGE_PARA_MAX,
};

struct mca_buckchg_jeita_volt_data {
	int voltage;
	int max_current;
};

struct mca_buckchg_jeita_volt_para {
	int size;
	struct mca_buckchg_jeita_volt_data *volt_data;
};

#define JEITA_DATA_MAX_GROUP 15
enum jeita_temp_para_ele {
	JEITA_TEMP_PARA_TEMP_LOW = 0,
	JEITA_TEMP_PARA_TEMP_HIGH,
	JEITA_TEMP_PARA_LOW_TEMP_HYS,
	JEITA_TEMP_PARA_HIGH_TEMP_HYS,
	JEITA_TEMP_PARA_MAX_CURRENT,
	JEITA_TEMP_PARA_VTERM,
	JEITA_TEMP_PARA_ITERM,
	JEITA_TEMP_PARA_VOLT_PARA_NAME,
	JEITA_TEMP_PARA_MAX,
};

struct mca_buckchg_jeita_data {
	int temp_low;
	int temp_high;
	int low_temp_hys;
	int high_temp_hys;
	int max_current;
	int vterm;
	int iterm;
	struct mca_buckchg_jeita_volt_para volt_para;
};

struct mca_buckchg_jeita_para {
	int size;
	int fcc_size;
	struct mca_buckchg_jeita_data *jeita_data;
	struct mca_buckchg_jeita_data *jeita_ffc_data;
};

struct mca_buckchg_jeita_proc_data {
	int cur_jeita_index;
	int temp_hys_en;
	int max_chg_curr;
};

/*
 * Runtime band-table override.
 *
 * DT remains the power-on default and is never modified; a write to the
 * jeita_table sysfs node fills one of these and publishes it, so "reset" is
 * always just dropping back to the parsed DT copy.
 *
 * Two slots, published by a single pointer store. The alternative - taking a
 * mutex in the readers - is not safe here: mca_strategy_buckchg_jeita_get_status()
 * is reachable from inside a voter callback chain that already holds voter
 * locks, while mca_buckchg_jeita_update() takes the table first and then votes.
 * That is a textbook inversion. Double-buffering sidesteps it entirely: a
 * reader takes one READ_ONCE snapshot of the pointer and uses that slot for the
 * whole pass, so it can never observe a half-written table, and nothing is ever
 * freed underneath it.
 *
 * Bands carry no volt_para sub-ladder. On the live global table only the
 * -9..0 C row has one, and a profile that overrides the table is asking for
 * its own current limits, not for the stock CV taper to be reapplied
 * underneath them.
 */
struct mca_buckchg_jeita_ovr {
	struct mca_buckchg_jeita_data norm[JEITA_DATA_MAX_GROUP];
	struct mca_buckchg_jeita_data ffc[JEITA_DATA_MAX_GROUP];
	int size;
	int fcc_size;
};

enum jeita_sysfs_attr_list {
	MCA_JEITA_PROP_TABLE = 0,
	MCA_JEITA_PROP_PARAMS,
	MCA_JEITA_PROP_ENVELOPE,
};

struct mca_buck_jeita_smartchg_data {
	int delta_fv;
	int delta_ichg;
};

struct mca_buckchg_jeita_dev {
	struct device *dev;
	int online;
	int voter_ok;
	int dtpt_status;
	int has_gbl_batt_para;
	int has_tmp_batt_para;
	int vbat_high_hyst;
	int vbat_low_hyst;
	int vbat_low_cold_hyst;
	int base_vbat_low_hyst;
	int flip_vbat_low_hyst;
	int support_base_flip;
	int base_flip_same;

	struct delayed_work monitor_work;
	struct mca_votable *flip_fcc_voter;
	struct mca_votable *en_voter;
	struct mca_votable *fcc_voter;
	struct mca_votable *vterm_voter;
	struct mca_votable *iterm_voter;
	struct mca_buckchg_jeita_proc_data proc_data;
	struct mca_buckchg_jeita_proc_data base_proc_data;
	struct mca_buckchg_jeita_proc_data flip_proc_data;
	/* dts config */
	struct mca_buckchg_jeita_para jeita_para;
	struct mca_buckchg_jeita_para base_jeita_para;
	struct mca_buckchg_jeita_para flip_jeita_para;
	struct mca_buck_jeita_smartchg_data smartchg_data;
	bool baacfg_update;
	int vterm;
	int jeita_hot_termination_hyst;
	int cold_zone;

	/*
	 * Runtime policy. ovr == NULL means the DT table is live.
	 * ovr_lock serialises writers only - readers are lock-free, see the
	 * comment on struct mca_buckchg_jeita_ovr.
	 */
	struct mca_buckchg_jeita_ovr ovr_slot[2];
	struct mca_buckchg_jeita_ovr *ovr;
	int ovr_next;
	struct mutex ovr_lock;
	/*
	 * Was ABNORMAL_BATT_FV_MAX / JEITA_HOT_RECHARGE_VBAT_HYS. Both select
	 * how a hot band behaves rather than describing the cell, so both are
	 * policy: hot_fv_gate is the vterm at or below which a band is treated
	 * as a cutoff band instead of a charging band, and hot_recharge_hys is
	 * how far the pack must fall below that vterm before charging is
	 * allowed to resume.
	 */
	int hot_fv_gate;
	int hot_recharge_hys;
};

#endif /* __MCA_BUCKCHG_JEITA_H__ */
