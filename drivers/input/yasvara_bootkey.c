// SPDX-License-Identifier: GPL-2.0
/*
 * Yasvara Bootguard early hardware key latch.
 *
 * Captures Safe Mode shortcuts (Volume Down x3 or Power + Volume Down hold)
 * from the moment the keypad driver probes until userspace post-fs-data reads
 * `/proc/yasvara_bootkey`. Upon read (or disarm timeout), this driver
 * permanently unregisters its input_handler and removes its procfs entry so
 * post-boot runtime overhead and procfs footprint are strictly zero.
 *
 * KABI-pure: touches zero exported kernel structs.
 */

#include <linux/atomic.h>
#include <linux/init.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/ktime.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/yasvara_hide.h>

#define YASVARA_VOLDOWN_TARGET      3
#define YASVARA_DEBOUNCE_NS         (80ULL * NSEC_PER_MSEC)
#define YASVARA_WINDOW_NS           (6000ULL * NSEC_PER_MSEC)
#define YASVARA_HOLD_NS             (1500ULL * NSEC_PER_MSEC)
#define YASVARA_AUTO_DISARM_SECS    45

static DEFINE_SPINLOCK(bootkey_lock);
static bool bootkey_triggered;
static const char *bootkey_reason = "none";
static u64 voldown_down_ns;
static u64 power_down_ns;
static u64 voldown_press_ns[YASVARA_VOLDOWN_TARGET];
static unsigned int voldown_press_count;
static atomic_t bootkey_disarmed = ATOMIC_INIT(0);

static struct input_handler bootkey_handler;
static struct proc_dir_entry *bootkey_proc;

static void bootkey_disarm_work_fn(struct work_struct *work);
static DECLARE_DELAYED_WORK(bootkey_disarm_work, bootkey_disarm_work_fn);

static void bootkey_check_hold_locked(u64 now_ns)
{
	if (bootkey_triggered)
		return;
	if (power_down_ns && voldown_down_ns) {
		u64 both_since = max(power_down_ns, voldown_down_ns);

		if (now_ns >= both_since && (now_ns - both_since) >= YASVARA_HOLD_NS) {
			bootkey_triggered = true;
			bootkey_reason = "power_voldown_hold";
			return;
		}
	}
	if (voldown_down_ns && now_ns >= voldown_down_ns &&
	    (now_ns - voldown_down_ns) >= YASVARA_HOLD_NS) {
		bootkey_triggered = true;
		bootkey_reason = "voldown_hold";
	}
}

static void bootkey_event(struct input_handle *handle, unsigned int type,
			  unsigned int code, int value)
{
	unsigned long flags;
	u64 now_ns;

	if (type != EV_KEY || (code != KEY_VOLUMEDOWN && code != KEY_POWER))
		return;
	if (atomic_read(&bootkey_disarmed))
		return;

	now_ns = ktime_get_boottime_ns();
	spin_lock_irqsave(&bootkey_lock, flags);

	if (code == KEY_VOLUMEDOWN) {
		if (value == 1) {
			u64 prev = voldown_press_count > 0 ?
				voldown_press_ns[voldown_press_count - 1] : 0;

			if (!voldown_down_ns)
				voldown_down_ns = now_ns;

			if (!prev || (now_ns - prev) >= YASVARA_DEBOUNCE_NS) {
				unsigned int i, keep = 0;

				for (i = 0; i < voldown_press_count; i++) {
					if (now_ns - voldown_press_ns[i] <= YASVARA_WINDOW_NS)
						voldown_press_ns[keep++] = voldown_press_ns[i];
				}
				voldown_press_count = keep;
				if (voldown_press_count < YASVARA_VOLDOWN_TARGET)
					voldown_press_ns[voldown_press_count++] = now_ns;
				if (voldown_press_count >= YASVARA_VOLDOWN_TARGET &&
				    !bootkey_triggered) {
					bootkey_triggered = true;
					bootkey_reason = "voldown_3x";
				}
			}
		} else if (value == 0) {
			bootkey_check_hold_locked(now_ns);
			voldown_down_ns = 0;
		}
	} else if (code == KEY_POWER) {
		if (value == 1) {
			if (!power_down_ns)
				power_down_ns = now_ns;
		} else if (value == 0) {
			bootkey_check_hold_locked(now_ns);
			power_down_ns = 0;
		}
	}

	bootkey_check_hold_locked(now_ns);
	spin_unlock_irqrestore(&bootkey_lock, flags);
}

static int bootkey_connect(struct input_handler *handler, struct input_dev *dev,
			   const struct input_device_id *id)
{
	struct input_handle *handle;
	int err;

	handle = kzalloc(sizeof(*handle), GFP_KERNEL);
	if (!handle)
		return -ENOMEM;

	handle->dev = dev;
	handle->handler = handler;
	handle->name = "yasvara_bootkey";

	err = input_register_handle(handle);
	if (err)
		goto err_free;

	err = input_open_device(handle);
	if (err)
		goto err_unregister;

	return 0;

err_unregister:
	input_unregister_handle(handle);
err_free:
	kfree(handle);
	return err;
}

static void bootkey_disconnect(struct input_handle *handle)
{
	input_close_device(handle);
	input_unregister_handle(handle);
	kfree(handle);
}

static const struct input_device_id bootkey_ids[] = {
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_VOLUMEDOWN)] = BIT_MASK(KEY_VOLUMEDOWN) },
	},
	{
		.flags = INPUT_DEVICE_ID_MATCH_EVBIT | INPUT_DEVICE_ID_MATCH_KEYBIT,
		.evbit = { BIT_MASK(EV_KEY) },
		.keybit = { [BIT_WORD(KEY_POWER)] = BIT_MASK(KEY_POWER) },
	},
	{ }
};

static struct input_handler bootkey_handler = {
	.event = bootkey_event,
	.connect = bootkey_connect,
	.disconnect = bootkey_disconnect,
	.name = "yasvara_bootkey",
	.id_table = bootkey_ids,
};

static void bootkey_disarm_work_fn(struct work_struct *work)
{
	if (atomic_xchg(&bootkey_disarmed, 1) == 0) {
		input_unregister_handler(&bootkey_handler);
		if (bootkey_proc) {
			proc_remove(bootkey_proc);
			bootkey_proc = NULL;
		}
	}
}

static int bootkey_proc_show(struct seq_file *m, void *v)
{
	unsigned long flags;
	bool triggered;
	const char *reason;
	unsigned int presses;

	if (yasvara_is_app_uid())
		return -EACCES;

	spin_lock_irqsave(&bootkey_lock, flags);
	bootkey_check_hold_locked(ktime_get_boottime_ns());
	triggered = bootkey_triggered;
	reason = bootkey_reason;
	presses = voldown_press_count;
	spin_unlock_irqrestore(&bootkey_lock, flags);

	seq_printf(m, "triggered=%d reason=%s voldown_presses=%u\n",
		   triggered ? 1 : 0, reason, presses);

	/* Consumed by post-fs-data: permanently unregister handler & remove proc node. */
	mod_delayed_work(system_wq, &bootkey_disarm_work, 0);
	return 0;
}

static int __init yasvara_bootkey_init(void)
{
	int err;

	err = input_register_handler(&bootkey_handler);
	if (err)
		return err;

	bootkey_proc = proc_create_single("yasvara_bootkey", 0400, NULL,
					  bootkey_proc_show);
	if (!bootkey_proc) {
		input_unregister_handler(&bootkey_handler);
		return -ENOMEM;
	}

	/* Auto-disarm even if userspace never consumes it (e.g., recovery/charger). */
	schedule_delayed_work(&bootkey_disarm_work,
			      secs_to_jiffies(YASVARA_AUTO_DISARM_SECS));
	return 0;
}
late_initcall(yasvara_bootkey_init);
