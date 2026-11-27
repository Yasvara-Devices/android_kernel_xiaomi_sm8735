/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_YASVARA_HIDE_H
#define _LINUX_YASVARA_HIDE_H

#ifdef __KERNEL__
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/fcntl.h>
#include <linux/limits.h>
#include <linux/path.h>
#include <linux/rcupdate.h>
#include <linux/statfs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <uapi/linux/magic.h>
#include <uapi/linux/un.h>
#endif

struct yasvara_lit {
	const char *str;
	size_t len;
};

#define YASVARA_LIT(s) { (s), sizeof(s) - 1 }

static inline unsigned char yasvara_ascii_lower(unsigned char c)
{
	return (c >= 'A' && c <= 'Z') ? (unsigned char)(c + ('a' - 'A')) : c;
}

static inline bool yasvara_eq_ci_prefix_bounded(const char *s, size_t rem,
						const struct yasvara_lit *lit)
{
	size_t i;

	if (rem < lit->len)
		return false;
	for (i = 0; i < lit->len; i++) {
		unsigned char c = (unsigned char)s[i];

		if (!c || yasvara_ascii_lower(c) != (unsigned char)lit->str[i])
			return false;
	}
	return true;
}

/*
 * Fast single-pass case-insensitive substring matcher with a bitmask
 * (bits 0..25 for 'a'..'z', bit 26 for '/') so non-matching bytes skip
 * the marker table in a single bit test.
 */
#define YASVARA_SLASH_BIT (1U << 26)

static inline bool yasvara_has_ci_marker_n(const char *s, size_t max_len,
					   const struct yasvara_lit *markers,
					   size_t count, u32 first_mask)
{
	size_t i;

	if (!s)
		return false;

	for (i = 0; i < max_len && s[i]; i++) {
		unsigned char c = yasvara_ascii_lower((unsigned char)s[i]);
		u32 bit;
		size_t m;

		if (c >= 'a' && c <= 'z')
			bit = 1U << (c - 'a');
		else if (c == '/')
			bit = YASVARA_SLASH_BIT;
		else
			continue;

		if (!(first_mask & bit))
			continue;

		for (m = 0; m < count; m++) {
			if ((unsigned char)markers[m].str[0] == c &&
			    yasvara_eq_ci_prefix_bounded(s + i, max_len - i,
							 &markers[m]))
				return true;
		}
	}
	return false;
}

static inline bool yasvara_is_forbidden_vma_str_n(const char *s, size_t max_len)
{
	static const struct yasvara_lit vma_markers[] = {
		YASVARA_LIT("xposed"),
		YASVARA_LIT("lsposed"),
		YASVARA_LIT("lsplant"),
		YASVARA_LIT("lspatch"),
		YASVARA_LIT("chsbuffer"),
		YASVARA_LIT("libdobby"),
		YASVARA_LIT("sandhook"),
		YASVARA_LIT("libpine"),
		YASVARA_LIT("yasvara_zygisk"),
		YASVARA_LIT("yasvara_kait"),
		YASVARA_LIT("yasvara-kait"),
	};
	static const u32 vma_first_mask =
		(1U << ('c' - 'a')) | (1U << ('l' - 'a')) |
		(1U << ('s' - 'a')) | (1U << ('x' - 'a')) |
		(1U << ('y' - 'a'));

	return yasvara_has_ci_marker_n(s, max_len, vma_markers,
				       sizeof(vma_markers) / sizeof(vma_markers[0]),
				       vma_first_mask);
}

static inline bool yasvara_is_forbidden_mount_str_n(const char *s, size_t max_len)
{
	static const struct yasvara_lit mount_markers[] = {
		YASVARA_LIT("/mnt/scratch"),
		YASVARA_LIT("/dev/block/mapper/scratch"),
		YASVARA_LIT("overlayfs_loop"),
		YASVARA_LIT("modules.img"),
		YASVARA_LIT("/data/adb"),
		YASVARA_LIT("/data/misc/yasvara_root"),
		YASVARA_LIT("/misc/yasvara_root"),
		YASVARA_LIT("yasvara_mod"),
		YASVARA_LIT("magisk"),
		YASVARA_LIT("kernelsu"),
		YASVARA_LIT("apatch"),
		YASVARA_LIT("/debug_ramdisk"),
		YASVARA_LIT("/patch_hw"),
		YASVARA_LIT("/dev/meta_hybird_mnt"),
	};
	static const u32 mount_first_mask =
		YASVARA_SLASH_BIT | (1U << ('a' - 'a')) |
		(1U << ('k' - 'a')) | (1U << ('m' - 'a')) |
		(1U << ('o' - 'a')) | (1U << ('y' - 'a'));

	return yasvara_has_ci_marker_n(s, max_len, mount_markers,
				       sizeof(mount_markers) / sizeof(mount_markers[0]),
				       mount_first_mask);
}

static inline bool yasvara_path_is_or_under(const char *path, const char *prefix)
{
	size_t len;

	if (!path || !prefix)
		return false;
	len = strlen(prefix);
	if (strncmp(path, prefix, len) != 0)
		return false;
	return path[len] == '\0' || path[len] == '/';
}

static inline bool yasvara_is_overlay_fstype(const char *fstype)
{
	if (!fstype)
		return false;
	return strcmp(fstype, "overlay") == 0 || strcmp(fstype, "overlayfs") == 0;
}

static inline bool yasvara_should_hide_mount_parts(const char *fstype,
						   const char *mnt_root,
						   const char *target,
						   const char *devname)
{
	if (yasvara_is_overlay_fstype(fstype))
		return true;
	if (fstype && strcmp(fstype, "tmpfs") == 0 &&
	    yasvara_path_is_or_under(target, "/sbin"))
		return true;
	if (yasvara_is_forbidden_mount_str_n(target, 4096))
		return true;
	if (yasvara_is_forbidden_mount_str_n(devname, 4096))
		return true;
	if (yasvara_is_forbidden_mount_str_n(mnt_root, 4096))
		return true;
	/* Bind mounts from /data/adb/... on the /data superblock expose "/adb/..." as mnt_root. */
	if (yasvara_path_is_or_under(mnt_root, "/adb"))
		return true;
	return false;
}

static inline bool yasvara_is_mount_hidden_fast(const char *fstype,
						const char *devname,
						const char *root_name,
						const char *mp_name)
{
	if (yasvara_should_hide_mount_parts(fstype, NULL, NULL, devname))
		return true;
	if (root_name && yasvara_is_forbidden_mount_str_n(root_name, 256))
		return true;
	if (mp_name && yasvara_is_forbidden_mount_str_n(mp_name, 256))
		return true;
	return false;
}

static inline bool yasvara_is_system_mount_path(const char *target)
{
	static const char *const system_prefixes[] = {
		"/system",
		"/vendor",
		"/product",
		"/system_ext",
		"/odm",
		"/apex",
		"/system_dlkm",
		"/vendor_dlkm",
	};
	size_t i;

	if (!target)
		return false;
	if (target[0] == '/' && target[1] == '\0')
		return true;

	for (i = 0; i < sizeof(system_prefixes) / sizeof(system_prefixes[0]); i++) {
		if (yasvara_path_is_or_under(target, system_prefixes[i]))
			return true;
	}
	return false;
}

/* Fold dirty/swap counters into clean counters on executable VMAs to preserve Rss totals. */
static inline void yasvara_fold_exec_smap_fields(unsigned long *shared_clean,
						 unsigned long *shared_dirty,
						 unsigned long *private_clean,
						 unsigned long *private_dirty,
						 unsigned long *swap,
						 unsigned long *swap_shared,
						 u64 *pss_dirty,
						 u64 *swap_pss)
{
	*private_clean += *private_dirty;
	*private_dirty = 0;
	*shared_clean += *shared_dirty;
	*shared_dirty = 0;
	*swap = 0;
	*swap_shared = 0;
	*pss_dirty = 0;
	*swap_pss = 0;
}

#ifdef __KERNEL__
#include <linux/jump_label.h>

/*
 * Off until something exists to hide: one patched NOP per mount line / VMA.
 * Sources: /sys/kernel/yasvara_hide/{akar,kait} and a latch on writable overlays.
 */
DECLARE_STATIC_KEY_FALSE(yasvara_hide_mounts_key);
DECLARE_STATIC_KEY_FALSE(yasvara_hide_maps_key);

void yasvara_hide_latch_mounts(void);

static __always_inline bool yasvara_hide_mounts_on(void)
{
	return static_branch_unlikely(&yasvara_hide_mounts_key);
}

static __always_inline bool yasvara_hide_maps_on(void)
{
	return static_branch_unlikely(&yasvara_hide_maps_key);
}

static inline bool yasvara_is_app_uid(void)
{
	return (from_kuid(&init_user_ns, current_uid()) % 100000) >= 10000;
}

struct vfsmount;

struct yasvara_mnt_view {
	int mnt_id;
	u64 mnt_id_unique;
	dev_t s_dev;
	bool redirected;
	bool same_root;
};

void yasvara_resolve_visible_mount(struct vfsmount *vfsmnt,
				   struct yasvara_mnt_view *out);
void yasvara_resolve_visible_parent(struct vfsmount *vfsmnt,
				    int *parent_id,
				    u64 *parent_id_unique);

static __always_inline dev_t yasvara_visible_dev(struct vfsmount *vfsmnt,
						 dev_t fallback_dev)
{
	struct yasvara_mnt_view view;

	if (likely(!yasvara_hide_mounts_on() || !yasvara_is_app_uid()))
		return fallback_dev;
	yasvara_resolve_visible_mount(vfsmnt, &view);
	return view.redirected ? view.s_dev : fallback_dev;
}
#endif

static inline void yasvara_normalize_kstatfs(struct kstatfs *st, bool is_app)
{
	if (!is_app || !st)
		return;
	if ((unsigned long)st->f_type == (unsigned long)OVERLAYFS_SUPER_MAGIC) {
		st->f_type = (long)EROFS_SUPER_MAGIC_V1;
		st->f_flags |= ST_RDONLY;
		st->f_bfree = 0;
		st->f_bavail = 0;
	}
}

/*
 * Kernel VFS & AF_UNIX hiding (replaces legacy userspace Bionic policy/ADB hooks so inline
 * svc #0 syscalls, relative dirfd lookups, and symlinks are equally covered with zero AVCs).
 */
enum {
	YASVARA_HIDE_PATH_NONE = 0,
	YASVARA_HIDE_PATH_POLICY = 1,
	YASVARA_HIDE_PATH_ADB = 2,
	YASVARA_HIDE_PATH_KAIT_IDLE = 3,
};

enum {
	YASVARA_DIR_NONE = 0,
	YASVARA_DIR_DEV_SOCKET = 1,
	YASVARA_DIR_SYSTEM_ROOT = 2,
	YASVARA_DIR_FRAMEWORK = 3,
	YASVARA_DIR_PERMISSIONS = 4,
	YASVARA_DIR_OVERLAY = 5,
	YASVARA_DIR_SELINUX_ETC = 6,
	YASVARA_DIR_SELINUX_FS = 7,
	YASVARA_DIR_SYSTEM_EXT_BIN = 8,
	YASVARA_DIR_SYS_KERNEL = 9,
	YASVARA_DIR_SYSTEM_EXT_LIB64 = 10,
	YASVARA_DIR_SYSTEM_EXT_INIT = 11,
};

static inline bool yasvara_str_starts(const char *s, const char *prefix, size_t plen)
{
	return strncmp(s, prefix, plen) == 0;
}

#define YASVARA_STARTS(s, lit) yasvara_str_starts((s), (lit), sizeof(lit) - 1)

static inline bool yasvara_is_adbd_name(const char *name)
{
	return strncmp(name, "adbd", 4) == 0 &&
	       (name[4] == '\0' || strcmp(name + 4, ".secure") == 0);
}

static inline bool yasvara_is_selinux_policy_name_n(const char *name, size_t len)
{
	if (len < 6 || memcmp(name, "policy", 6) != 0)
		return false;
	return len == 6 || name[6] == '.' || name[6] == '-';
}

static inline u8 yasvara_match_hidden_path(const char *path)
{
	const char *rest;

	if (!path || path[0] != '/')
		return YASVARA_HIDE_PATH_NONE;

	switch (path[1]) {
	case 'd':
		if (YASVARA_STARTS(path, "/dev/socket/") &&
		    yasvara_is_adbd_name(path + 12))
			return YASVARA_HIDE_PATH_ADB;
		return YASVARA_HIDE_PATH_NONE;
	case 's':
		if (path[2] != 'y' || path[3] != 's')
			return YASVARA_HIDE_PATH_NONE;
		if (path[4] == '/') {
			if (YASVARA_STARTS(path + 5, "fs/selinux/policy")) {
				if (path[22] == '\0' || path[22] == '.' || path[22] == '-')
					return YASVARA_HIDE_PATH_POLICY;
				return YASVARA_HIDE_PATH_NONE;
			}
			if (YASVARA_STARTS(path + 5, "kernel/yasvara_hide") &&
			    (path[24] == '\0' || path[24] == '/'))
				return YASVARA_HIDE_PATH_POLICY;
			return YASVARA_HIDE_PATH_NONE;
		}
		if (path[4] != 't' || path[5] != 'e' || path[6] != 'm')
			return YASVARA_HIDE_PATH_NONE;
		rest = path + 7;
		if (*rest == '_') {
			if (!YASVARA_STARTS(rest, "_ext/"))
				return YASVARA_HIDE_PATH_NONE;
			rest += 5;
			if (YASVARA_STARTS(rest, "framework/org.lineageos.platform") ||
			    YASVARA_STARTS(rest, "overlay/Lineage") ||
			    strcmp(rest, "bin/adb_root") == 0 ||
			    strcmp(rest, "bin/yasvara_zygisk_companion") == 0 ||
			    strcmp(rest, "bin/yasvara_su_bench") == 0 ||
			    strcmp(rest, "lib64/libyasvara_zygisk.so") == 0 ||
			    strcmp(rest, "etc/init/init.yasvara-system_ext.rc") == 0)
				return YASVARA_HIDE_PATH_POLICY;
			if (strcmp(rest, "lib64/libyasvara_kait.so") == 0 ||
			    YASVARA_STARTS(rest, "framework/yasvara-kait.") ||
			    (YASVARA_STARTS(rest, "framework/oat/") &&
			     strstr(rest + 14, "yasvara-kait.") != NULL))
				return YASVARA_HIDE_PATH_KAIT_IDLE;
			return YASVARA_HIDE_PATH_NONE;
		}
		if (*rest != '/')
			return YASVARA_HIDE_PATH_NONE;
		rest++;
		switch (rest[0]) {
		case 'a':
			if (YASVARA_STARTS(rest, "addon.d") &&
			    (rest[7] == '\0' || rest[7] == '/'))
				return YASVARA_HIDE_PATH_POLICY;
			return YASVARA_HIDE_PATH_NONE;
		case 'f':
			if (!YASVARA_STARTS(rest, "framework/"))
				return YASVARA_HIDE_PATH_NONE;
			rest += 10;
			if (YASVARA_STARTS(rest, "org.lineageos.platform") ||
			    (YASVARA_STARTS(rest, "oat/") &&
			     strstr(rest + 4, "org.lineageos.platform") != NULL))
				return YASVARA_HIDE_PATH_POLICY;
			return YASVARA_HIDE_PATH_NONE;
		case 'e':
			if (YASVARA_STARTS(rest, "etc/permissions/org.lineageos."))
				return YASVARA_HIDE_PATH_POLICY;
			return YASVARA_HIDE_PATH_NONE;
		case 'p':
			if (YASVARA_STARTS(rest, "product/framework/org.lineageos.platform") ||
			    YASVARA_STARTS(rest, "product/overlay/Lineage"))
				return YASVARA_HIDE_PATH_POLICY;
			return YASVARA_HIDE_PATH_NONE;
		case 's':
			if (!YASVARA_STARTS(rest, "system_ext/"))
				return YASVARA_HIDE_PATH_NONE;
			rest += 11;
			if (YASVARA_STARTS(rest, "framework/org.lineageos.platform") ||
			    YASVARA_STARTS(rest, "overlay/Lineage") ||
			    strcmp(rest, "bin/adb_root") == 0 ||
			    strcmp(rest, "bin/yasvara_zygisk_companion") == 0 ||
			    strcmp(rest, "bin/yasvara_su_bench") == 0 ||
			    strcmp(rest, "lib64/libyasvara_zygisk.so") == 0 ||
			    strcmp(rest, "etc/init/init.yasvara-system_ext.rc") == 0)
				return YASVARA_HIDE_PATH_POLICY;
			if (strcmp(rest, "lib64/libyasvara_kait.so") == 0 ||
			    YASVARA_STARTS(rest, "framework/yasvara-kait.") ||
			    (YASVARA_STARTS(rest, "framework/oat/") &&
			     strstr(rest + 14, "yasvara-kait.") != NULL))
				return YASVARA_HIDE_PATH_KAIT_IDLE;
			return YASVARA_HIDE_PATH_NONE;
		case 'v':
			if (YASVARA_STARTS(rest, "vendor/etc/selinux/") &&
			    rest[19] != '\0')
				return YASVARA_HIDE_PATH_POLICY;
			return YASVARA_HIDE_PATH_NONE;
		default:
			return YASVARA_HIDE_PATH_NONE;
		}
	case 'v':
		if (YASVARA_STARTS(path, "/vendor/etc/selinux/") && path[20] != '\0')
			return YASVARA_HIDE_PATH_POLICY;
		if (YASVARA_STARTS(path, "/vendor/odm/etc/selinux/") && path[24] != '\0')
			return YASVARA_HIDE_PATH_POLICY;
		return YASVARA_HIDE_PATH_NONE;
	case 'o':
		if (YASVARA_STARTS(path, "/odm/etc/selinux/") && path[17] != '\0')
			return YASVARA_HIDE_PATH_POLICY;
		return YASVARA_HIDE_PATH_NONE;
	case 'p':
		if (!YASVARA_STARTS(path, "/product/"))
			return YASVARA_HIDE_PATH_NONE;
		if (YASVARA_STARTS(path + 9, "overlay/Lineage") ||
		    YASVARA_STARTS(path + 9, "framework/org.lineageos.platform"))
			return YASVARA_HIDE_PATH_POLICY;
		return YASVARA_HIDE_PATH_NONE;
	default:
		return YASVARA_HIDE_PATH_NONE;
	}
}

static inline bool yasvara_path_eq_n(const char *path, size_t len, const char *lit, size_t lit_len)
{
	return len == lit_len && memcmp(path, lit, len) == 0;
}

#define YASVARA_DIR_EQ(p, n, lit) yasvara_path_eq_n((p), (n), (lit), sizeof(lit) - 1)

static inline u8 yasvara_classify_hide_dir_path(const char *path)
{
	size_t len;

	if (!path || path[0] != '/')
		return YASVARA_DIR_NONE;
	len = strlen(path);
	while (len > 1 && path[len - 1] == '/')
		len--;

	if (YASVARA_DIR_EQ(path, len, "/dev/socket"))
		return YASVARA_DIR_DEV_SOCKET;
	if (YASVARA_DIR_EQ(path, len, "/system"))
		return YASVARA_DIR_SYSTEM_ROOT;
	if (YASVARA_DIR_EQ(path, len, "/system/framework") ||
	    YASVARA_DIR_EQ(path, len, "/system_ext/framework") ||
	    YASVARA_DIR_EQ(path, len, "/system/system_ext/framework") ||
	    YASVARA_DIR_EQ(path, len, "/product/framework") ||
	    YASVARA_DIR_EQ(path, len, "/system/product/framework"))
		return YASVARA_DIR_FRAMEWORK;
	if (YASVARA_DIR_EQ(path, len, "/system/etc/permissions"))
		return YASVARA_DIR_PERMISSIONS;
	if (YASVARA_DIR_EQ(path, len, "/product/overlay") ||
	    YASVARA_DIR_EQ(path, len, "/system/product/overlay") ||
	    YASVARA_DIR_EQ(path, len, "/system_ext/overlay") ||
	    YASVARA_DIR_EQ(path, len, "/system/system_ext/overlay"))
		return YASVARA_DIR_OVERLAY;
	if (YASVARA_DIR_EQ(path, len, "/vendor/etc/selinux") ||
	    YASVARA_DIR_EQ(path, len, "/system/vendor/etc/selinux") ||
	    YASVARA_DIR_EQ(path, len, "/odm/etc/selinux") ||
	    YASVARA_DIR_EQ(path, len, "/vendor/odm/etc/selinux"))
		return YASVARA_DIR_SELINUX_ETC;
	if (YASVARA_DIR_EQ(path, len, "/sys/fs/selinux"))
		return YASVARA_DIR_SELINUX_FS;
	if (YASVARA_DIR_EQ(path, len, "/sys/kernel"))
		return YASVARA_DIR_SYS_KERNEL;
	if (YASVARA_DIR_EQ(path, len, "/system_ext/bin") ||
	    YASVARA_DIR_EQ(path, len, "/system/system_ext/bin"))
		return YASVARA_DIR_SYSTEM_EXT_BIN;
	if (YASVARA_DIR_EQ(path, len, "/system_ext/lib64") ||
	    YASVARA_DIR_EQ(path, len, "/system/system_ext/lib64"))
		return YASVARA_DIR_SYSTEM_EXT_LIB64;
	if (YASVARA_DIR_EQ(path, len, "/system_ext/etc/init") ||
	    YASVARA_DIR_EQ(path, len, "/system/system_ext/etc/init"))
		return YASVARA_DIR_SYSTEM_EXT_INIT;
	return YASVARA_DIR_NONE;
}

#undef YASVARA_DIR_EQ
#undef YASVARA_STARTS

static inline bool yasvara_should_hide_dirent_n(u8 kind, const char *name, int namlen)
{
	if (!name || namlen <= 0)
		return false;
	if (name[0] == '.' && (namlen == 1 || (namlen == 2 && name[1] == '.')))
		return false;

	switch (kind) {
	case YASVARA_DIR_DEV_SOCKET:
		return (namlen == 4 && memcmp(name, "adbd", 4) == 0) ||
		       (namlen == 11 && memcmp(name, "adbd.secure", 11) == 0);
	case YASVARA_DIR_SYSTEM_ROOT:
		return namlen == 7 && memcmp(name, "addon.d", 7) == 0;
	case YASVARA_DIR_FRAMEWORK:
		return (namlen >= 22 && memcmp(name, "org.lineageos.platform", 22) == 0) ||
		       (namlen >= 13 && memcmp(name, "yasvara-kait.", 13) == 0);
	case YASVARA_DIR_PERMISSIONS:
		return namlen >= 14 && memcmp(name, "org.lineageos.", 14) == 0;
	case YASVARA_DIR_OVERLAY:
		return namlen >= 7 && memcmp(name, "Lineage", 7) == 0;
	case YASVARA_DIR_SELINUX_ETC:
		return true;
	case YASVARA_DIR_SELINUX_FS:
		return yasvara_is_selinux_policy_name_n(name, (size_t)namlen);
	case YASVARA_DIR_SYS_KERNEL:
		return namlen == 12 && memcmp(name, "yasvara_hide", 12) == 0;
	case YASVARA_DIR_SYSTEM_EXT_BIN:
		return (namlen == 8 && memcmp(name, "adb_root", 8) == 0) ||
		       (namlen == 24 && memcmp(name, "yasvara_zygisk_companion", 24) == 0) ||
		       (namlen == 16 && memcmp(name, "yasvara_su_bench", 16) == 0);
	case YASVARA_DIR_SYSTEM_EXT_LIB64:
		return namlen >= 11 && memcmp(name, "libyasvara_", 11) == 0;
	case YASVARA_DIR_SYSTEM_EXT_INIT:
		return namlen >= 13 && memcmp(name, "init.yasvara-", 13) == 0;
	default:
		return false;
	}
}

static inline bool yasvara_is_jdwp_control_bytes(unsigned short family,
						 const char *sun_path,
						 int addr_len,
						 size_t path_offset)
{
	if (family != 1 /* AF_UNIX */ || !sun_path)
		return false;
	if (addr_len != (int)(path_offset + 1 + 12))
		return false;
	return sun_path[0] == '\0' && memcmp(sun_path + 1, "jdwp-control", 12) == 0;
}

static inline bool yasvara_parent_name_maybe_hidden_n(const char *name, size_t len)
{
	if (!name)
		return false;
	if (len == 7) {
		return (name[0] == 's' && memcmp(name, "selinux", 7) == 0) ||
		       (name[0] == 'a' && memcmp(name, "addon.d", 7) == 0);
	}
	return len == 12 && name[0] == 'y' && memcmp(name, "yasvara_hide", 12) == 0;
}

static inline bool yasvara_leaf_name_maybe_hidden_n(const char *name, size_t len)
{
	if (len < 4 || !name)
		return false;
	switch (name[0]) {
	case 'a':
		return ((len == 4 || len == 11) && memcmp(name, "adbd", 4) == 0) ||
		       (len == 7 && memcmp(name, "addon.d", 7) == 0) ||
		       (len == 8 && memcmp(name, "adb_root", 8) == 0);
	case 'i':
		return len >= 13 && memcmp(name, "init.yasvara-", 13) == 0;
	case 'l':
		return len >= 11 && memcmp(name, "libyasvara_", 11) == 0;
	case 'p':
		return len >= 6 && yasvara_is_selinux_policy_name_n(name, len);
	case 'o':
		return len >= 14 && memcmp(name, "org.lineageos.", 14) == 0;
	case 'L':
		return len >= 7 && memcmp(name, "Lineage", 7) == 0;
	case 'y':
		return (len == 12 && memcmp(name, "yasvara_hide", 12) == 0) ||
		       (len >= 13 && memcmp(name, "yasvara-kait.", 13) == 0) ||
		       (len == 24 && memcmp(name, "yasvara_zygisk_companion", 24) == 0) ||
		       (len == 16 && memcmp(name, "yasvara_su_bench", 16) == 0);
	default:
		return false;
	}
}

static inline bool yasvara_dir_name_maybe_hidden_n(const char *name, size_t len)
{
	if (!name)
		return false;
	switch (len) {
	case 3:
		return name[0] == 'b' && memcmp(name, "bin", 3) == 0;
	case 4:
		return name[0] == 'i' && memcmp(name, "init", 4) == 0;
	case 5:
		return name[0] == 'l' && memcmp(name, "lib64", 5) == 0;
	case 6:
		return (name[0] == 's' && memcmp(name, "socket", 6) == 0) ||
		       (name[0] == 's' && memcmp(name, "system", 6) == 0) ||
		       (name[0] == 'k' && memcmp(name, "kernel", 6) == 0);
	case 7:
		return (name[0] == 'o' && memcmp(name, "overlay", 7) == 0) ||
		       (name[0] == 's' && memcmp(name, "selinux", 7) == 0);
	case 9:
		return name[0] == 'f' && memcmp(name, "framework", 9) == 0;
	case 11:
		return name[0] == 'p' && memcmp(name, "permissions", 11) == 0;
	default:
		return false;
	}
}

#ifdef __KERNEL__
bool yasvara_should_hide_path_slow(const struct path *path, int open_flag, bool is_open);
u8 yasvara_classify_hide_dir_slow(const struct path *path);

static inline bool yasvara_qstr_is_hidden_parent(const struct qstr *q)
{
	return yasvara_parent_name_maybe_hidden_n((const char *)q->name, q->len);
}

/* Sub-nanosecond RCU dentry prefilter: rejects >99.99% of paths before ever calling d_path(). */
static inline bool yasvara_dentry_maybe_hidden(const struct dentry *dentry)
{
	const struct dentry *parent, *gp;
	const unsigned char *name;
	u32 len;
	bool hit = false;

	if (unlikely(!dentry))
		return false;
	rcu_read_lock();
	len = READ_ONCE(dentry->d_name.len);
	name = READ_ONCE(dentry->d_name.name);
	hit = yasvara_leaf_name_maybe_hidden_n((const char *)name, len);
	if (!hit) {
		parent = READ_ONCE(dentry->d_parent);
		if (parent && parent != dentry) {
			if (yasvara_qstr_is_hidden_parent(&parent->d_name)) {
				hit = true;
			} else {
				gp = READ_ONCE(parent->d_parent);
				if (gp && gp != parent &&
				    yasvara_qstr_is_hidden_parent(&gp->d_name))
					hit = true;
			}
		}
	}
	rcu_read_unlock();
	return hit;
}

static inline bool yasvara_dentry_maybe_hide_dir(const struct dentry *dentry)
{
	const unsigned char *name;
	u32 len;
	bool hit;

	if (unlikely(!dentry))
		return false;
	rcu_read_lock();
	len = READ_ONCE(dentry->d_name.len);
	name = READ_ONCE(dentry->d_name.name);
	hit = yasvara_dir_name_maybe_hidden_n((const char *)name, len);
	rcu_read_unlock();
	return hit;
}

static __always_inline bool yasvara_should_hide_open(const struct path *path, int open_flag)
{
	if (likely(!yasvara_is_app_uid()))
		return false;
	if (likely(!yasvara_dentry_maybe_hidden(path->dentry)))
		return false;
	return yasvara_should_hide_path_slow(path, open_flag, true);
}

static __always_inline bool yasvara_should_hide_stat(const struct path *path)
{
	if (likely(!yasvara_is_app_uid()))
		return false;
	if (likely(!yasvara_dentry_maybe_hidden(path->dentry)))
		return false;
	return yasvara_should_hide_path_slow(path, 0, false);
}

static __always_inline u8 yasvara_classify_hide_dir(const struct path *path)
{
	if (likely(!yasvara_is_app_uid()))
		return YASVARA_DIR_NONE;
	if (likely(!yasvara_dentry_maybe_hide_dir(path->dentry)))
		return YASVARA_DIR_NONE;
	return yasvara_classify_hide_dir_slow(path);
}

static __always_inline bool yasvara_should_refuse_unix_connect(
	const struct sockaddr_un *sunaddr, int addr_len)
{
	if (likely(addr_len != (int)(offsetof(struct sockaddr_un, sun_path) + 1 + 12)))
		return false;
	if (likely(!yasvara_is_jdwp_control_bytes(sunaddr->sun_family, sunaddr->sun_path,
						  addr_len,
						  offsetof(struct sockaddr_un, sun_path))))
		return false;
	return yasvara_is_app_uid();
}
#endif

#endif /* _LINUX_YASVARA_HIDE_H */
