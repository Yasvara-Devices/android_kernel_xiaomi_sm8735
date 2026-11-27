// SPDX-License-Identifier: GPL-2.0
/*
 * Switches for the app-view hide filters (mountinfo/mounts/statmount, maps/smaps).
 *
 * /sys/kernel/yasvara_hide/akar  bit 0 = module mounts, bit 1 = Zygisk (written by rootd)
 * /sys/kernel/yasvara_hide/kait  0/1 = Xposed modules enabled (written by system_server)
 *
 * A writable overlay latches the mounts filter on for the rest of the boot. Writers own
 * their source only, so one never clears what another still needs.
 */

#include <linux/init.h>
#include <linux/jump_label.h>
#include <linux/kobject.h>
#include <linux/kstrtox.h>
#include <linux/mutex.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/security.h>
#include <linux/sysfs.h>
#include <linux/workqueue.h>
#include <linux/yasvara_hide.h>

DEFINE_STATIC_KEY_FALSE(yasvara_hide_mounts_key);
DEFINE_STATIC_KEY_FALSE(yasvara_hide_maps_key);

#define AKAR_MOUNTS	0x1
#define AKAR_MAPS	0x2

static DEFINE_MUTEX(yasvara_hide_lock);
static unsigned int akar_bits;
static bool kait_on;
static bool overlay_latched;

static void yasvara_hide_set(struct static_key_false *key, bool on)
{
	if (on)
		static_branch_enable(key);
	else
		static_branch_disable(key);
}

static void yasvara_hide_apply_locked(void)
{
	yasvara_hide_set(&yasvara_hide_mounts_key,
			 overlay_latched || (akar_bits & AKAR_MOUNTS));
	yasvara_hide_set(&yasvara_hide_maps_key,
			 kait_on || (akar_bits & AKAR_MAPS));
}

static void yasvara_hide_latch_work(struct work_struct *w)
{
	mutex_lock(&yasvara_hide_lock);
	overlay_latched = true;
	yasvara_hide_apply_locked();
	mutex_unlock(&yasvara_hide_lock);
}

static DECLARE_WORK(yasvara_hide_latch, yasvara_hide_latch_work);

/* Called under mount locks: text patching takes cpus_read_lock, so defer it. */
void yasvara_hide_latch_mounts(void)
{
	if (!READ_ONCE(overlay_latched))
		schedule_work(&yasvara_hide_latch);
}

static ssize_t akar_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%u\n", READ_ONCE(akar_bits));
}

static ssize_t akar_store(struct kobject *k, struct kobj_attribute *a,
			  const char *buf, size_t len)
{
	unsigned int v;
	int err = kstrtouint(buf, 0, &v);

	if (err)
		return err;
	if (v & ~(AKAR_MOUNTS | AKAR_MAPS))
		return -EINVAL;
	mutex_lock(&yasvara_hide_lock);
	WRITE_ONCE(akar_bits, v);
	yasvara_hide_apply_locked();
	mutex_unlock(&yasvara_hide_lock);
	return len;
}

static ssize_t kait_show(struct kobject *k, struct kobj_attribute *a, char *buf)
{
	return sysfs_emit(buf, "%d\n", READ_ONCE(kait_on));
}

static ssize_t kait_store(struct kobject *k, struct kobj_attribute *a,
			  const char *buf, size_t len)
{
	bool v;
	int err = kstrtobool(buf, &v);

	if (err)
		return err;
	mutex_lock(&yasvara_hide_lock);
	WRITE_ONCE(kait_on, v);
	yasvara_hide_apply_locked();
	mutex_unlock(&yasvara_hide_lock);
	return len;
}

/* 0600: init hands them to their writers (root:root akar, system:system kait). */
static struct kobj_attribute akar_attr = __ATTR(akar, 0600, akar_show, akar_store);
static struct kobj_attribute kait_attr = __ATTR(kait, 0600, kait_show, kait_store);

static struct attribute *yasvara_hide_attrs[] = {
	&akar_attr.attr,
	&kait_attr.attr,
	NULL,
};

static const struct attribute_group yasvara_hide_group = {
	.attrs = yasvara_hide_attrs,
};

static int __init yasvara_hide_init(void)
{
	struct kobject *kobj = kobject_create_and_add("yasvara_hide", kernel_kobj);

	if (!kobj)
		return -ENOMEM;
	return sysfs_create_group(kobj, &yasvara_hide_group);
}
late_initcall(yasvara_hide_init);

bool yasvara_should_hide_path_slow(const struct path *path, int open_flag, bool is_open)
{
	char buf[256];
	char *resolved;
	u8 kind;

	resolved = d_path(path, buf, sizeof(buf));
	if (IS_ERR(resolved))
		return false;
	kind = yasvara_match_hidden_path(resolved);
	if (kind == YASVARA_HIDE_PATH_NONE)
		return false;
	if (kind == YASVARA_HIDE_PATH_KAIT_IDLE && yasvara_hide_maps_on())
		return false;
	if (kind == YASVARA_HIDE_PATH_ADB)
		return true;
	if (is_open) {
		/* Write/create/truncate opens fall through to the real file and real error. */
		if (open_flag & (O_WRONLY | O_RDWR | O_CREAT | O_TMPFILE | O_TRUNC | O_EXCL))
			return false;
	}
	return true;
}

u8 yasvara_classify_hide_dir_slow(const struct path *path)
{
	char buf[256];
	char *resolved;

	resolved = d_path(path, buf, sizeof(buf));
	if (IS_ERR(resolved))
		return YASVARA_DIR_NONE;
	return yasvara_classify_hide_dir_path(resolved);
}

#ifdef CONFIG_SECURITY_SELINUX
static bool zygote_sids_ready;
static u32 sid_zygote;
static u32 sid_webview_zygote;
static u32 sid_zygote_next;

/*
 * Before the policy loads, SELinux maps every context to SECINITSID_KERNEL and reports
 * success, so a SID only counts once it converts back to the exact context.
 */
static u32 yasvara_lookup_sid(const char *ctx, u32 len)
{
	u32 sid = 0;
	char *back = NULL;
	u32 back_len = 0;
	bool ok;

	if (security_secctx_to_secid(ctx, len, &sid) || !sid)
		return 0;
	if (security_secid_to_secctx(sid, &back, &back_len))
		return 0;
	ok = back_len >= len && !memcmp(back, ctx, len) &&
	     (back_len == len || back[len] == '\0');
	security_release_secctx(back, back_len);
	return ok ? sid : 0;
}

#define YASVARA_SID(ctx) yasvara_lookup_sid(ctx, sizeof(ctx) - 1)

static void yasvara_init_zygote_sids(void)
{
	u32 z, wz, zn;

	mutex_lock(&yasvara_hide_lock);
	if (READ_ONCE(zygote_sids_ready)) {
		mutex_unlock(&yasvara_hide_lock);
		return;
	}
	z = YASVARA_SID("u:r:zygote:s0");
	if (z) {
		wz = YASVARA_SID("u:r:webview_zygote:s0");
		zn = YASVARA_SID("u:r:zygote_next:s0");
		WRITE_ONCE(sid_zygote, z);
		WRITE_ONCE(sid_webview_zygote, wz);
		WRITE_ONCE(sid_zygote_next, zn);
		smp_store_release(&zygote_sids_ready, true);
	}
	mutex_unlock(&yasvara_hide_lock);
}

bool yasvara_is_zygote_or_child_slow(void)
{
	u32 z, wz, zn;
	u32 cur_sid = 0, parent_sid = 0;
	struct task_struct *parent;

	if (unlikely(!smp_load_acquire(&zygote_sids_ready))) {
		yasvara_init_zygote_sids();
		if (!smp_load_acquire(&zygote_sids_ready))
			return false;
	}

	z = READ_ONCE(sid_zygote);
	wz = READ_ONCE(sid_webview_zygote);
	zn = READ_ONCE(sid_zygote_next);

	security_current_getsecid_subj(&cur_sid);
	if (yasvara_sid_matches_zygote(cur_sid, z, wz, zn))
		return true;

	rcu_read_lock();
	parent = rcu_dereference(current->real_parent);
	if (parent)
		security_task_getsecid_obj(parent, &parent_sid);
	rcu_read_unlock();

	return yasvara_sid_matches_zygote(parent_sid, z, wz, zn);
}
#else
bool yasvara_is_zygote_or_child_slow(void)
{
	return false;
}
#endif
