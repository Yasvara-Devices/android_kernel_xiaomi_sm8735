// SPDX-License-Identifier: GPL-2.0
/*
 * mca_policy.c - runtime-writable charging policy envelope
 *
 * The vendor policy (20 A / 4600 mV / -20..60 C) used to be compile-time
 * macros that silently clamped every userspace override. GameSpace needs to
 * overclock (120 W = 20640 mA @172k), so the envelope is now variables with
 * safe defaults, writable up to absolute silicon limits via:
 *
 *   echo "fcc_max 22000" > .../jeita/policy_envelope
 *   cat .../jeita/policy_envelope
 *   echo "reset" > .../jeita/policy_envelope
 *
 * Keys: fcc_max, iin_max, fv_max, temp_min, temp_max, iterm_min, iterm_max.
 * Hardware OCP/OVP (SC8581, vbat_ovp, connector_antiburn) stays as the last
 * resort and is NOT widened here.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <mca/common/mca_log.h>
#include <mca/common/mca_policy.h>

#ifndef MCA_LOG_TAG
#define MCA_LOG_TAG "mca_policy"
#endif

int mca_policy_fcc_max_ma = MCA_POLICY_FCC_DEFAULT_MA;
EXPORT_SYMBOL(mca_policy_fcc_max_ma);

int mca_policy_iin_max_ma = MCA_POLICY_IIN_DEFAULT_MA;
EXPORT_SYMBOL(mca_policy_iin_max_ma);

int mca_policy_fv_max_mv = MCA_POLICY_FV_MAX_MV;
EXPORT_SYMBOL(mca_policy_fv_max_mv);

int mca_policy_temp_min_c = MCA_POLICY_TEMP_MIN_C;
EXPORT_SYMBOL(mca_policy_temp_min_c);

int mca_policy_temp_max_c = MCA_POLICY_TEMP_MAX_C;
EXPORT_SYMBOL(mca_policy_temp_max_c);

int mca_policy_iterm_min_ma = MCA_POLICY_ITERM_MIN_MA;
EXPORT_SYMBOL(mca_policy_iterm_min_ma);

int mca_policy_iterm_max_ma = MCA_POLICY_ITERM_MAX_MA;
EXPORT_SYMBOL(mca_policy_iterm_max_ma);

static void mca_policy_reset(void)
{
	mca_policy_fcc_max_ma = MCA_POLICY_FCC_DEFAULT_MA;
	mca_policy_iin_max_ma = MCA_POLICY_IIN_DEFAULT_MA;
	mca_policy_fv_max_mv = MCA_POLICY_FV_MAX_MV;
	mca_policy_temp_min_c = MCA_POLICY_TEMP_MIN_C;
	mca_policy_temp_max_c = MCA_POLICY_TEMP_MAX_C;
	mca_policy_iterm_min_ma = MCA_POLICY_ITERM_MIN_MA;
	mca_policy_iterm_max_ma = MCA_POLICY_ITERM_MAX_MA;
}

int mca_policy_envelope_write(const char *buf)
{
	char key[24];
	int val;

	if (!buf)
		return -EINVAL;

	if (sscanf(buf, "%23s", key) != 1)
		return -EINVAL;

	if (!strcmp(key, "reset")) {
		mca_policy_reset();
		mca_log_err("policy envelope reset to defaults (fcc %d iin %d fv %d temp %d..%d)\n",
			    mca_policy_fcc_max_ma, mca_policy_iin_max_ma,
			    mca_policy_fv_max_mv,
			    mca_policy_temp_min_c, mca_policy_temp_max_c);
		return 0;
	}

	if (sscanf(buf, "%23s %d", key, &val) != 2)
		return -EINVAL;

	if (!strcmp(key, "fcc_max")) {
		if (val < 0 || val > MCA_POLICY_FCC_ABS_MAX_MA)
			return -EINVAL;
		mca_policy_fcc_max_ma = val;
	} else if (!strcmp(key, "iin_max")) {
		if (val < 0 || val > MCA_POLICY_IIN_ABS_MAX_MA)
			return -EINVAL;
		mca_policy_iin_max_ma = val;
	} else if (!strcmp(key, "fv_max")) {
		if (val < MCA_POLICY_FV_MIN_MV || val > MCA_POLICY_FV_ABS_MAX_MV)
			return -EINVAL;
		mca_policy_fv_max_mv = val;
	} else if (!strcmp(key, "temp_min")) {
		if (val < MCA_POLICY_TEMP_ABS_MIN_C || val > mca_policy_temp_max_c)
			return -EINVAL;
		mca_policy_temp_min_c = val;
	} else if (!strcmp(key, "temp_max")) {
		if (val < mca_policy_temp_min_c || val > MCA_POLICY_TEMP_ABS_MAX_C)
			return -EINVAL;
		mca_policy_temp_max_c = val;
	} else if (!strcmp(key, "iterm_min")) {
		if (val < 0 || val > mca_policy_iterm_max_ma)
			return -EINVAL;
		mca_policy_iterm_min_ma = val;
	} else if (!strcmp(key, "iterm_max")) {
		if (val < mca_policy_iterm_min_ma || val > MCA_POLICY_IIN_ABS_MAX_MA)
			return -EINVAL;
		mca_policy_iterm_max_ma = val;
	} else {
		return -EINVAL;
	}

	mca_log_err("policy envelope: %s = %d (fcc %d iin %d fv %d temp %d..%d iterm %d..%d)\n",
		    key, val,
		    mca_policy_fcc_max_ma, mca_policy_iin_max_ma,
		    mca_policy_fv_max_mv,
		    mca_policy_temp_min_c, mca_policy_temp_max_c,
		    mca_policy_iterm_min_ma, mca_policy_iterm_max_ma);
	return 0;
}
EXPORT_SYMBOL(mca_policy_envelope_write);

int mca_policy_envelope_read(char *buf)
{
	if (!buf)
		return 0;

	return scnprintf(buf, PAGE_SIZE,
			 "fcc_max=%d iin_max=%d fv_max=%d temp_min=%d temp_max=%d iterm_min=%d iterm_max=%d src=%s\n",
			 mca_policy_fcc_max_ma, mca_policy_iin_max_ma,
			 mca_policy_fv_max_mv,
			 mca_policy_temp_min_c, mca_policy_temp_max_c,
			 mca_policy_iterm_min_ma, mca_policy_iterm_max_ma,
			 (mca_policy_fcc_max_ma != MCA_POLICY_FCC_DEFAULT_MA ||
			  mca_policy_fv_max_mv != MCA_POLICY_FV_MAX_MV) ?
			 "override" : "dt");
}
EXPORT_SYMBOL(mca_policy_envelope_read);
