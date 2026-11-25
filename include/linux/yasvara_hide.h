/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_YASVARA_HIDE_H
#define _LINUX_YASVARA_HIDE_H

#ifdef __KERNEL__
#include <linux/cred.h>
#include <linux/dcache.h>
#include <linux/limits.h>
#include <linux/statfs.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uidgid.h>
#include <uapi/linux/magic.h>
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
		YASVARA_LIT("libyasvara_zygisk"),
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
static inline bool yasvara_is_app_uid(void)
{
	return (from_kuid(&init_user_ns, current_uid()) % 100000) >= 10000;
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

#endif /* _LINUX_YASVARA_HIDE_H */
