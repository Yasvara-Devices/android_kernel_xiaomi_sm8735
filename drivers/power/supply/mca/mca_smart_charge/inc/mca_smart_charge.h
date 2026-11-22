/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_smart_charge.h
 *
 * smart charge driver
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

#ifndef __SMART_CHARGE_H__
#define __SMART_CHARGE_H__

enum SMART_CHG_MODE {
	SMART_CHG_NIGHT, // Warning: smart night not a smart charge feature
	SMART_CHG_NAVIGATION = 1,
	SMART_CHG_OUTDOOR = 2,
	SMART_CHG_LOWFAST = 3,
	SMART_CHG_ENDURANCE_PRO = 4,
	SMART_CHG_WLS_SUPER = 5,
	SMART_CHG_SENSE_CHG = 6,
	SMART_CHG_WLS_QUIET = 7,
	SMART_CHG_EXTREME_COLD = 8,
	SMART_CHG_TRAVELWAIT = 9,
	SMART_CHG_MAX = 15,
};

union SMART_CHG_HEADER {
	unsigned long AsUINT32;
	struct {
		unsigned long enable : 1; /* BIT0: en_ret*/
		unsigned long func_type : 15; /* BIT1 - BIT15: func_typ */
		unsigned long func_value : 16; /* BIT16 - BIT31: func_val */
	};
	struct {
		/* BIT0: en_ret*/
		unsigned long : 1; /*bit [0:0]*/
		/* BIT1 - BIT15: func_typ */
		unsigned long navigation : 1; /*bit [1:1]*/
		unsigned long outdoor : 1; /*bit [2:2]*/
		unsigned long lowfast : 1; /*bit [3:3]*/
		unsigned long endurance_pro : 1; /*bit [4:4]*/
		unsigned long wls_super_chg : 1; /*bit [5:5]*/
		unsigned long sense_chg : 1; /*bit [6:6]*/
		unsigned long wls_quiet : 1; /*bit [7:7]*/
		unsigned long extreme_cold : 1; /*bit [8:8]*/
		unsigned long travel_wait : 1; /*bit [9:9]*/
		unsigned long smart_bypass : 1; /*bit [10:10]*/
		unsigned long : 5; /*bit [15:11]*/
		/* BIT16 - BIT31: func_val */
		unsigned long soc_limit : 8; /*bit [23:16]*/
		unsigned long : 8; /*bit [31:24]*/
	};
};

union SMART_CHG_MIEVENT {
	unsigned long AsUINT32;
	struct {
		unsigned long : 1; /*bit [0:0]*/
		/* BIT1 - BIT15: func_typ */
		unsigned long navigation : 1; /*bit [1:1]*/
		unsigned long outdoor : 1; /*bit [2:2]*/
		unsigned long lowfast : 1; /*bit [3:3]*/
		unsigned long endurance_pro : 1; /*bit [4:4]*/
		unsigned long wls_super_chg : 1; /*bit [5:5]*/
		unsigned long sense_chg : 1; /*bit [6:6]*/
		unsigned long wls_quiet : 1; /*bit [7:7]*/
		unsigned long extreme_cold : 1; /*bit [8:8]*/
		unsigned long travel_wait : 1; /*bit [9:9]*/
	};
};

enum smart_chg_posture_stat {
	SMART_CHG_POSTURE_UNKNOW,
	SMART_CHG_POSTURE_DESKTOP = 1,
	SMART_CHG_POSTURE_HOLDER = 2,
	SMART_CHG_POSTURE_ONEHAND = 3,
	SMART_CHG_POSTURE_TWOHAND_H = 4,
	SMART_CHG_POSTURE_TWOHAND_V = 5,
	SMART_CHG_POSTURE_ANS_CALL = 6,
	SMART_CHG_POSTURE_INDEX_MAX,
	SMART_CHG_POSTURE_FAKE_CLEAR = 100,
	SMART_CHG_POSTURE_FAKE_DESKTOP = 101,
	SMART_CHG_POSTURE_FAKE_HOLDER = 102,
	SMART_CHG_POSTURE_FAKE_ONEHAND = 103,
	SMART_CHG_POSTURE_FAKE_TWOHAND_H = 104,
	SMART_CHG_POSTURE_FAKE_TWOHAND_V = 105,
	SMART_CHG_POSTURE_FAKE_ANS_CALL = 106,
	SMART_CHG_POSTURE_FAKE_INDEX_MAX,
};

enum smart_chg_scene_type {
	SMART_CHG_SCENE_NORMAL = 0,
	SMART_CHG_SCENE_HUANJI = 1,
	SMART_CHG_SCENE_PHONE = 5,
	SMART_CHG_SCENE_NOLIMIT = 6,
	SMART_CHG_SCENE_CLASS0 = 7,
	SMART_CHG_SCENE_YOUTUBE = 8,
	SMART_CHG_SCENE_NAVIGATION = 10,
	SMART_CHG_SCENE_VIDEO = 11,
	SMART_CHG_SCENE_VIDEOCHAT = 14,
	SMART_CHG_SCENE_CAMERA = 15,
	SMART_CHG_SCENE_TGAME = 18,
	SMART_CHG_SCENE_MGAME = 19,
	SMART_CHG_SCENE_YUANSHEN = 20,
	SMART_CHG_SCENE_XINGTIE = 25,
	SMART_CHG_SCENE_PER_NORMAL = 50,
	SMART_CHG_SCENE_PER_CLASS0 = 57,
	SMART_CHG_SCENE_PER_YOUTUBE = 58,
	SMART_CHG_SCENE_PER_VIDEO = 61,
	SMART_CHG_SCENE_INDEX_MAX,
	SMART_CHG_SCENE_REDIR_START = 1000,
	SMART_CHG_SCENE_REDIR_INDEX_MAX =
		SMART_CHG_SCENE_REDIR_START + SMART_CHG_SCENE_INDEX_MAX,
};

enum smart_chg_sic_mode {
	SMART_CHG_SIC_MODE_BALANCED = 0,
	SMART_CHG_SIC_MODE_SLIGHTCHG = 2,
	SMART_CHG_SIC_MODE_MIDDLE = 4,
	SMART_CHG_SIC_MODE_SUPERCHG = 8,
	SMART_CHG_SIC_MODE_MODE_MAX_INDEX,
};

enum smart_chg_pwr_boost_type {
	SMART_CHG_OUTDOOR_PWR_BOOST = 0,
	SMART_CHG_TRAVELWAIT_PWR_BOOST = 1,
	SMART_CHG_PWR_BOOST_MAX_INDEX,
};

struct SMART_CHG_DATA {
	uint16_t enable;
	uint16_t func_value;
	bool use_fake_value;
	int fake_value;
};

struct SMART_CHG_INFO {
	uint8_t ret_code;
	struct SMART_CHG_DATA smart_chgcfg[SMART_CHG_MAX];
};

struct ICHG_CC_CFG {
	int cc_max;
	int delta_ichg;
};

#define CC_ICHG_MAX_GROUP 2

#define SMART_BYPASS_TEMP_SECTION_MAX 10
#define SMART_BYPASS_DEBOUNCE_NS (10 * NSEC_PER_SEC)
#define SMART_CHG_BOARD_TEMP_SCALE 100

struct smart_bypass_temp_section {
	int temp_low;
	int temp_high;
	int hyst_low;
	int hyst_high;
	int fcc;
};

/*
 * Zero-Deficit Float Bypass (ZDFB)
 *
 * Not to be confused with SMART_CHG_HEADER.smart_bypass above: that bit drives
 * a scene-keyed fcc limiter (see smart_charge_handle_bypass_chg) and never
 * takes the pack out of the charge loop.
 */
enum mca_bypass_chg_mode {
	BYPASS_CHG_MODE_OFF = 0,
	BYPASS_CHG_MODE_FORCE = 1, /* engage whenever the cable allows it */
	BYPASS_CHG_MODE_AUTO = 2, /* engage on the game scenes only */
	BYPASS_CHG_MODE_MAX,
};

#define BYPASS_CHG_VOTER "bypass_chg"

/* FCC ceilings voted on "buck_charge_curr" (MCA_VOTE_MIN) - units: mA */
#define BYPASS_FLOAT_FCC_MA_DEFAULT 50  /* 0.22 W micro-float @4.35V */
#define BYPASS_FLOAT_FCC_MA_MIN 30
#define BYPASS_FLOAT_FCC_MA_MAX 150
#define BYPASS_ASSIST_FCC_MA 600 /* transient deficit assist */
#define BYPASS_FULL_FCC_MA 0   /* 100% SOC termination */

/* IBAT trip points - charge-positive mA convention after normalisation */
#define BYPASS_DEFICIT_IBAT_MA (-50) /* shallow deficit: debounce */
#define BYPASS_DEEP_DEFICIT_IBAT_MA (-200) /* deep deficit: instant */
#define BYPASS_RECOVER_IBAT_MA (-10) /* asymmetric recovery band */

/*
 * ZDFB v2 - closed-loop float regulation
 * --------------------------------------
 * v1 held a fixed FCC ceiling (50 mA float / 600 mA assist). A MIN-vote
 * ceiling can stop the pack overcharging but it can never stop it draining:
 * a deficit means input power < system power, and no ceiling creates input
 * headroom. Measured under Genshin: 3.6-5.9 A out of the pack while engaged.
 *
 * Two things had to change:
 *
 * 1. HEADROOM. Every engaged state welded the charge pumps off, leaving the
 *    buck as the only input path at buck_9v_in = 1600 mA => 9 V x 1.6 A =
 *    14.4 W. A 60 fps title draws well past that. While engaged we now vote
 *    the buck input ceiling up so the path can actually carry VSYS.
 *
 * 2. REGULATION. The discrete ladder cannot land on zero; it lands wherever
 *    the load leaves it under the ceiling. Replaced with an integral
 *    controller on measured ibat, so FCC converges to whatever value holds
 *    ibat at the target and tracks the load instead of guessing at it.
 *
 * Everything here is a default: DT overrides at probe, and the bypass_params
 * sysfs node retunes live so policy stays in userspace.
 */
/*
 * Gains below are the measured-stable set, not first guesses, arrived at over
 * three live tunings on bypass_params:
 *
 *   kp=60 db=25 step=250 - held the average but hunted badly: fcc wandering
 *                          0-404 mA, ibat swinging -237..+140.
 *   kp=25 db=40 step=80  - stopped hunting, but a 179 s audit showed the pack
 *                          still dipping to -25 mA about 3% of the time, and
 *                          fcc pinned at 0 the entire run. The cause was the
 *                          deadband itself: every drain seen was 7-25 mA, i.e.
 *                          entirely INSIDE +/-40 mA, so the loop was by
 *                          construction forbidden from correcting any of them.
 *   kp=15 db=5  step=30  - deadband now sits under the drains it has to catch.
 *                          Measured under an 8-core load: 240/240 samples
 *                          within 50 mA, worst 7 mA (0.03 W), fcc actively
 *                          regulating at 23 mA instead of sitting dead at 0.
 *
 * The lesson worth keeping: a deadband wider than the error it is meant to
 * reject does not damp the loop, it disables it.
 */
#define BYPASS_TARGET_IBAT_MA_DEFAULT 0   /* hold the pack at 0 A */
#define BYPASS_FCC_MAX_MA_DEFAULT 3000    /* PI output ceiling */
#define BYPASS_PI_KP_DEFAULT 15           /* percent of error per step */
#define BYPASS_PI_DEADBAND_MA_DEFAULT 30  /* no action inside +/- this */
#define BYPASS_PI_STEP_MAX_MA_DEFAULT 50  /* slew per 250 ms tick */
#define BYPASS_INPUT_9V_MA_DEFAULT 3000   /* 9 V x 3 A = 27 W of headroom */
#define BYPASS_INPUT_5V_MA_DEFAULT 3000   /* 5 V x 3 A = 15 W */

/* Dual-Regime Hybrid SC8581 DIV2 CP-Bypass defaults (on PPS adapters) */
#define BYPASS_CP_ENABLE_DEFAULT 1        /* 1 = enable SC8581 CP-Bypass >12W */
#define BYPASS_CP_ENTER_MW_DEFAULT 12000  /* enter CP-Bypass above 12 W load */
#define BYPASS_CP_VBAT_MAX_MV_DEFAULT 4450
#define BYPASS_CP_HOLD_TICKS 12           /* 12 x 250 ms = 3 s sustained load */
#define BYPASS_CP_START_GRACE_MS 4000     /* allow 4 s for CP path open */
#define BYPASS_CP_COOLDOWN_MS 10000       /* 10 s anti-chatter cooldown */

/* Bounds the DT/sysfs values are clamped into. */
#define BYPASS_FCC_MAX_MA_LIMIT 6000
#define BYPASS_PI_KP_LIMIT 300
#define BYPASS_INPUT_MA_LIMIT 4000

/* timing / hysteresis */
#define BYPASS_EVAL_PERIOD_MS 250 /* eval cadence */
#define BYPASS_DEBOUNCE_CNT 3   /* 3 x 250 ms = 750 ms */
#define BYPASS_ASSIST_HOLD_MS 3000 /* min ASSIST dwell */

/* SOC guards */
#define BYPASS_FLOOR_SOC 20  /* legacy safety exit */
#define BYPASS_FLOAT_SOC 100 /* FULL entry threshold */

enum mca_bypass_chg_state {
	BYPASS_CHG_ST_IDLE = 0,
	BYPASS_CHG_ST_ACTIVE = 1,    /* buck ON @ regulated FCC, CP locked OFF */
	BYPASS_CHG_ST_DEFICIT = 2,   /* backward compat */
	BYPASS_CHG_ST_ASSIST = 3,    /* buck saturated at fcc_max + hold timer */
	BYPASS_CHG_ST_FULL = 4,      /* SOC 100%: FCC 0, buck still feeds */
	BYPASS_CHG_ST_CP_BYPASS = 5, /* SC8581 DIV2 direct-drive bypass (PPS) */
	BYPASS_CHG_ST_MAX,
};

struct bypass_chg_data {
	int state;
	int fcc_ma;             /* current FCC ceiling vote */
	int ibat_ma;            /* +charge / -discharge */
	int soc;
	int deficit_cnt;        /* shallow-deficit debounce counter */
	int assist_cnt;         /* total ASSIST engagements (telemetry) */
	int transition_cnt;     /* total transitions (chatter monitor) */
	unsigned long assist_hold_jif;
	bool applied;           /* votes currently latched */
	int sat_cnt;            /* consecutive ticks the PI sat at its ceiling */
	int cp_load_cnt;        /* sustained heavy-load ticks toward CP handoff */
	unsigned long cp_start_jif;
	unsigned long cp_cooldown_jif;
	struct mca_votable *en_voter;        /* "chg_enable"        AND */
	struct mca_votable *fcc_voter;       /* "buck_charge_curr"  MIN */
	struct mca_votable *cp_voter;        /* "quick_chg_disable" OR  */
	struct mca_votable *in9v_voter;      /* "buck_9v_in"        MIN */
	struct mca_votable *in5v_voter;      /* "buck_5v_in"        MIN */
	struct mca_votable *bypass_in_voter; /* "buck_bypass_in"    MAX */
	struct mca_votable *cp_bypass_voter; /* "cp_bypass_en"      OR  */
};

/* ZDFB v3 + Hybrid CP-Bypass tunables - DT defaults, live-retunable over sysfs. */
struct bypass_chg_tune {
	int target_ibat_ma;
	int fcc_max_ma;
	int pi_kp;
	int pi_deadband_ma;
	int pi_step_max_ma;
	int input_9v_ma;
	int input_5v_ma;
	int cp_enable;
	int cp_enter_mw;
	int cp_vbat_max_mv;
};

struct smart_charge_info {
	struct class *smart_charge_class;
	struct cdev pri_dev;
	struct device *sys_dev;
	struct device *dev;
	dev_t dev_num;
	struct delayed_work smart_charge_work;
	struct delayed_work smart_sense_chg_work;
	struct delayed_work smart_soc_limit_work;
	struct mca_votable *smartchg_delta_fv_voter;
	struct mca_votable *smartchg_delta_ichg_voter;
	struct notifier_block panel_nb;

	int delta_fv;
	int delta_ichg;
	int soc_limit;
	int online;
	int cycle_count;
	int cell_type;
	int smart_sic_mode;
	int screen_state;
	int scene;
	int fake_scene;
	int board_temp;
	int enable_fv_dec_by_cc;

	int smart_night_val;
	int smart_batt_val;
	int smart_delta_ichg;

	bool support_cc_ichg;
	bool support_sensechg_v2;
	bool soc_limit_enable;
	char reserve[2];
	struct ICHG_CC_CFG ichg_cc_table[CC_ICHG_MAX_GROUP];
	/* Smart Charging Engine */
	union SMART_CHG_HEADER smart_chg_control;
	struct SMART_CHG_INFO smart_chg_data;
	char *mmap_addr;
	int map_flag;
	int pulse_mode;
	int support_csd;
	int plugin_rsoc;
	int night_enable_rsoc;
	union SMART_CHG_MIEVENT ignore_upload;
	size_t mmap_size;
	struct mca_votable *smartchg_set_fcc_voter;
	struct notifier_block thermal_nb;
	int wls_online;
	int support_bypass;
	int bypass_entry_soc;
	int bypass_exit_soc;
	uint16_t bypass_enable;
	uint16_t bypass_reserved;
	uint16_t last_bypass_state;
	int bypass_active;
	int bypass_temp_index;
	int last_bypass_fcc;
	ktime_t bypass_start_time;
	bool bypass_exit_flag;
	int bypass_high_num;
	int bypass_med_num;
	int bypass_low_num;
	struct smart_bypass_temp_section
		bypass_high_lmt[SMART_BYPASS_TEMP_SECTION_MAX];
	struct smart_bypass_temp_section
		bypass_med_lmt[SMART_BYPASS_TEMP_SECTION_MAX];
	struct smart_bypass_temp_section
		bypass_low_lmt[SMART_BYPASS_TEMP_SECTION_MAX];
	int mishow_config;

	/* real bypass charging (ZDFB) */
	int support_bypass_chg;
	int bypass_chg_mode;
	int bypass_float_fcc_ma;
	struct bypass_chg_tune bypass_tune;
	struct bypass_chg_data bypass;
	struct delayed_work bypass_chg_work;
};

enum smartchg_attr_list {
	MCA_PROP_SMARTCHG,
	MCA_PROP_SMARTCHG_FV,
	MCA_PROP_SMARTCHG_ICHG,
	MCA_PROP_SMARTBATT,
	MCA_PROP_SMARTNIGHT,
	MCA_PROP_POSTURE,
	MCA_PROP_SCENE,
	MCA_PROP_BOARD_TEMP,
	MCA_PROP_SMART_SIC_MODE,
	MCA_PROP_BYPASS_CHG,
	MCA_PROP_BYPASS_CHG_STATE,
	MCA_PROP_BYPASS_PARAMS,
};

enum smartchg_cell_type {
	MCA_CELL_TYPE_1S = 1,
	MCA_CELL_TYPE_2S,
	MCA_CELL_TYPE_MAX,
};

enum smartchg_cc_ichg_cfg_mode_ele {
	SMARTCHG_MODE_CYCLECOUNT,
	SMARTCHG_MODE_DELTA_ICHG,
	SMARTCHG_MODE_PARA_MAX,
};

#endif /* __SMART_CHARGE_H__ */
