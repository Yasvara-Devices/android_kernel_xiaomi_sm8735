/* SPDX-License-Identifier: GPL-2.0 */
/*
 * mca_policy.h
 *
 * The one charging-policy envelope the kernel keeps.
 *
 * Everything else about charging on this device - JEITA bands, input and
 * charge current caps, the thermal ladders, the CP ladders - is policy, and
 * policy belongs in userspace where a profile can change it without a
 * reflash. The DT values remain the power-on defaults; the sysfs override
 * paths that consume this header replace them at runtime.
 *
 * What stays compiled in is only the *absolute* silicon/chemistry ceiling
 * (SC8581 OCP/OVP, Li-ion plating). The vendor *policy* defaults below are
 * just power-on values: userspace (GameSpace/mi_thermal_engine) may widen
 * them up to ABS via the policy_envelope sysfs node without a reflash.
 *
 * Values mirror HW_*_ABS in the mi_thermal_engine daemon
 * (hardware/xiaomi/thermal/src/config.rs).
 */

#ifndef _MCA_COMMON_MCA_POLICY_H_
#define _MCA_COMMON_MCA_POLICY_H_

#include <linux/kernel.h>

/*
 * Float voltage ceiling.
 *
 * This was 4550 and that was wrong: the FFC rows this device actually ships
 * (mca_buckchg_jeita_gbl_para, the 20-48 C bands) carry vterm 4580, and the
 * quick-charge volt_para ladders go to 4570. An envelope under the vendor's own
 * table does not protect the cell - it just makes the stock policy the one
 * thing a profile cannot express, so "restore defaults" would come back 30 mV
 * low and no one would know why.
 *
 * 4600 clears the highest shipped row by 20 mV and nothing more. It is
 * deliberately NOT the CP's bat-ovp-threshold (4750 mV): that is the hardware
 * cutting the pack off after something already went wrong. This is the software
 * limit that should mean it never does.
 */
#define MCA_POLICY_FV_MAX_MV 4600
#define MCA_POLICY_FV_ABS_MAX_MV 4750
#define MCA_POLICY_FV_MIN_MV 3000

/*
 * Absolute cold/hot bounds for any band boundary a profile can define.
 * Charging below 0 C plates directly - that is what the stock cold bands with
 * their sub-1 A currents exist to manage - and above 60 C the electrolyte
 * starts to break down. A profile can pick any band layout it likes inside
 * this range, including a single band spanning all of it.
 */
#define MCA_POLICY_TEMP_MIN_C (-20)
#define MCA_POLICY_TEMP_MAX_C 60
/* Absolute chemistry bounds. Userspace may widen the *policy* window out to
 * these via policy_envelope, but never past them. */
#define MCA_POLICY_TEMP_ABS_MIN_C (-20)
#define MCA_POLICY_TEMP_ABS_MAX_C 60

/*
 * Runtime-writable envelope. Defaults are the safe vendor policy above;
 * GameSpace/mi_thermal_engine may raise them up to the ABS silicon limits
 * through the policy_envelope sysfs node. Single definition lives in
 * mca_common/mca_policy.c so every driver (jeita/buckchg/thermal) clamps
 * against the same values.
 */
extern int mca_policy_fcc_max_ma;
extern int mca_policy_iin_max_ma;
extern int mca_policy_fv_max_mv;
extern int mca_policy_temp_min_c;
extern int mca_policy_temp_max_c;
extern int mca_policy_iterm_min_ma;
extern int mca_policy_iterm_max_ma;

/* Parses "<key> <value>" (or "reset"). Returns 0 on success, -EINVAL. */
int mca_policy_envelope_write(const char *buf);
/* Prints current envelope into buf (PAGE_SIZE). Returns strlen. */
int mca_policy_envelope_read(char *buf);

/* Charge current: power-on default, runtime-writable via policy_envelope.
 * 20000 matches the div_max_curr ceiling already used by the CP ladders
 * (div_max_curr in onyx-charger-common.dtsi). It is NOT silicon: the SC8581
 * BUS_OCP register goes to 6375 mA (x4 = 25500 mA pack), so the absolute
 * ceiling below leaves headroom for a 120 W (20640 mA @172k) overclock. */
#define MCA_POLICY_FCC_DEFAULT_MA 20000
#define MCA_POLICY_FCC_ABS_MAX_MA 25000

/* Input current, per adapter path. 6 A at 9 V is 54 W, comfortably past what
 * the buck alone can carry; the PDO negotiation clamps below this anyway.
 * Absolute 8000 allows 20 V/6 A-class adapters through the buck path. */
#define MCA_POLICY_IIN_DEFAULT_MA 6000
#define MCA_POLICY_IIN_ABS_MAX_MA 8000

/* Termination current. Zero would mean "never terminate". */
#define MCA_POLICY_ITERM_MIN_MA 50
#define MCA_POLICY_ITERM_MAX_MA 6000

/* Source compat: old code used MAX as the policy value. It is now the
 * power-on default; the live ceiling is mca_policy_*_ma. */
#define MCA_POLICY_FCC_MAX_MA MCA_POLICY_FCC_DEFAULT_MA
#define MCA_POLICY_IIN_MAX_MA MCA_POLICY_IIN_DEFAULT_MA

/**
 * mca_policy_clamp_fv - clamp a float voltage into the safe envelope
 *
 * A 0 mV request is passed through: JEITA rows legitimately use vterm 0 to
 * mean "this band does not charge", and clamping that up to a minimum would
 * turn a cutoff row into a charging row.
 */
static inline int mca_policy_clamp_fv(int mv)
{
	if (mv <= 0)
		return 0;

	return clamp(mv, MCA_POLICY_FV_MIN_MV, mca_policy_fv_max_mv);
}

static inline int mca_policy_clamp_fcc(int ma)
{
	return clamp(ma, 0, mca_policy_fcc_max_ma);
}

static inline int mca_policy_clamp_iin(int ma)
{
	return clamp(ma, 0, mca_policy_iin_max_ma);
}

static inline int mca_policy_clamp_iterm(int ma)
{
	if (ma <= 0)
		return mca_policy_iterm_min_ma;

	return clamp(ma, mca_policy_iterm_min_ma, mca_policy_iterm_max_ma);
}

/**
 * mca_policy_clamp_temp - clamp a band boundary, in whole degrees C
 *
 * The sentinel rows (-65535 / 65535) that DT uses to open-end the first and
 * last band are preserved rather than clamped: they are structural, not
 * temperatures, and squashing them to -20/60 would leave a gap that
 * mca_buckchg_jeita_update() reports as "can not find jeita para" and then
 * bails out of - leaving the previous vote latched.
 */
static inline int mca_policy_clamp_temp(int c)
{
	if (c <= -1000 || c >= 1000)
		return c;

	return clamp(c, mca_policy_temp_min_c, mca_policy_temp_max_c);
}

#endif /* _MCA_COMMON_MCA_POLICY_H_ */
