/*
 * Copyright (C) 2018, Alex Saiko <solcmdr@gmail.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/reboot.h>
#include <linux/syscalls.h>
#include <linux/workqueue.h>

#ifdef CONFIG_DRM
#include <drm/drm_notifier.h>
#endif

#include "internal.h"

#define DEF_DYNAMIC_FSYNC_ENABLED	(0)
#define DEF_DRM_NOTIFIER_IS_USED	(IS_ENABLED(CONFIG_DRM) ? 1 : 0)
#define DEF_SYNCHRONIZATION_DELAY	(3000)

static struct dynamic_sync {
	bool enabled;
	bool drm_notify_used;

	unsigned int delay;
	int screen_state;
} dyn_sync = {
	.enabled = DEF_DYNAMIC_FSYNC_ENABLED,
	.drm_notify_used = DEF_DRM_NOTIFIER_IS_USED,
	.delay = DEF_SYNCHRONIZATION_DELAY,
	.screen_state = -1,
};

static struct workqueue_struct *dfs_wq;
static struct delayed_work force_sync_work;

static inline void do_critical_sync(void)
{
	fsync_enabled = true;
	emergency_sync();
}

static void do_force_sync(struct work_struct *work)
{
	bool prev_state = fsync_enabled;

	fsync_enabled = true;
	sys_sync();
	fsync_enabled = prev_state;
}

static inline void dynamic_sync_suspend(void)
{
	dyn_sync.screen_state = 0;
	fsync_enabled = true;

	if (delayed_work_pending(&force_sync_work))
		return;

	queue_delayed_work(dfs_wq, &force_sync_work,
			msecs_to_jiffies(dyn_sync.delay));
}

static inline void dynamic_sync_resume(void)
{
	dyn_sync.screen_state = 1;

	if (delayed_work_pending(&force_sync_work))
		cancel_delayed_work(&force_sync_work);

	fsync_enabled = false;
}

#ifdef CONFIG_DRM
static int drm_notifier_callback(struct notifier_block *nb,
				 unsigned long event, void *data)
{
	struct drm_notify_data *evdata = data;
	int blank;

	if (!dyn_sync.enabled || !dyn_sync.drm_notify_used)
		return NOTIFY_OK;
	if (event != DRM_EVENT_BLANK || !evdata || !evdata->data)
		return NOTIFY_OK;

	blank = *(int *)evdata->data;
	switch (blank) {
	case DRM_BLANK_UNBLANK:
		dynamic_sync_resume();
		break;
	case DRM_BLANK_LP1:
	case DRM_BLANK_LP2:
	case DRM_BLANK_POWERDOWN:
		dynamic_sync_suspend();
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block drm_notifier_block = {
	.notifier_call = drm_notifier_callback,
};
#endif

static int panic_notifier_callback(struct notifier_block *nb,
				   unsigned long event, void *data)
{
	if (!dyn_sync.enabled)
		return NOTIFY_OK;

	do_critical_sync();

	return NOTIFY_DONE;
}

static struct notifier_block panic_notifier_block = {
	.notifier_call = panic_notifier_callback,
	.priority = INT_MAX,
};

static int reboot_notifier_callback(struct notifier_block *nb,
				    unsigned long event, void *data)
{
	if (!dyn_sync.enabled)
		return NOTIFY_OK;

	switch (event) {
	case SYS_HALT:
	case SYS_RESTART:
	case SYS_POWER_OFF:
		do_critical_sync();
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block reboot_notifier_block = {
	.notifier_call = reboot_notifier_callback,
	.priority = SHRT_MAX,
};

static ssize_t show_dynamic_sync_enabled(struct kobject *kobj,
					 struct kobj_attribute *attr,
					 char *buf)
{
	return scnprintf(buf, 12, "%u\n", dyn_sync.enabled);
}

static ssize_t store_dynamic_sync_enabled(struct kobject *kobj,
					  struct kobj_attribute *attr,
					  const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 2, &val);
	if (ret || val == dyn_sync.enabled)
		return -EINVAL;

	dyn_sync.enabled = !!val;

	fsync_enabled = true;
	sys_sync();

	if (dyn_sync.enabled && dyn_sync.drm_notify_used &&
	    dyn_sync.screen_state >= 0)
		fsync_enabled = !dyn_sync.screen_state;
	else
		fsync_enabled = !val;

	return count;
}

static struct kobj_attribute dynamic_sync_enabled =
	__ATTR(Dyn_fsync_active, 0644,
		show_dynamic_sync_enabled, store_dynamic_sync_enabled);

static ssize_t show_drm_notify_used(struct kobject *kobj,
				    struct kobj_attribute *attr,
				    char *buf)
{
	if (dyn_sync.screen_state < 0)
		return scnprintf(buf, 15, "<unsupported>\n");

	return scnprintf(buf, 12, "%u\n", dyn_sync.drm_notify_used);
}

static ssize_t store_drm_notify_used(struct kobject *kobj,
				     struct kobj_attribute *attr,
				     const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	if (dyn_sync.screen_state < 0)
		return -EINVAL;

	ret = kstrtouint(buf, 2, &val);
	if (ret || val == dyn_sync.drm_notify_used)
		return -EINVAL;

	dyn_sync.drm_notify_used = !!val;

	if (dyn_sync.enabled && !dyn_sync.screen_state)
		fsync_enabled = val;

	if (!val && delayed_work_pending(&force_sync_work))
		cancel_delayed_work_sync(&force_sync_work);

	return count;
}

static struct kobj_attribute dynamic_sync_drm_notify =
	__ATTR(Dyn_fsync_drm_notify, 0644,
		show_drm_notify_used, store_drm_notify_used);

static ssize_t show_dynamic_sync_delay(struct kobject *kobj,
				       struct kobj_attribute *attr,
				       char *buf)
{
	if (!dyn_sync.drm_notify_used)
		return scnprintf(buf, 15, "<unsupported>\n");

	return scnprintf(buf, 12, "%u\n", dyn_sync.delay);
}

static ssize_t store_dynamic_sync_delay(struct kobject *kobj,
					struct kobj_attribute *attr,
					const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	if (!dyn_sync.drm_notify_used)
		return -EINVAL;

	ret = kstrtouint(buf, 10, &val);
	if (ret || val == dyn_sync.delay)
		return -EINVAL;

	dyn_sync.delay = val;

	if (delayed_work_pending(&force_sync_work))
		mod_delayed_work(dfs_wq, &force_sync_work,
				 msecs_to_jiffies(dyn_sync.delay));

	return count;
}

static struct kobj_attribute dynamic_sync_delay =
	__ATTR(Dyn_fsync_delay, 0644,
		show_dynamic_sync_delay, store_dynamic_sync_delay);

static struct attribute *dynamic_sync_attrs[] = {
	&dynamic_sync_enabled.attr,
	&dynamic_sync_drm_notify.attr,
	&dynamic_sync_delay.attr,
	NULL
};

static struct attribute_group dynamic_sync_attr_group = {
	.name = "dyn_fsync",
	.attrs = dynamic_sync_attrs,
};

static int __init dynamic_sync_init(void)
{
	int ret;

	dfs_wq = alloc_workqueue("dynamic_sync_wq", WQ_UNBOUND | WQ_HIGHPRI, 0);
	if (!dfs_wq) {
		pr_err("Unable to allocate high-priority workqueue\n");
		return -EFAULT;
	}

	INIT_DELAYED_WORK(&force_sync_work, do_force_sync);

#ifdef CONFIG_DRM
	/*
	 * Do not fail if DRM notifier registration failed as it is
	 * expected to fail in case it is not used in kernel tree.
	 */
	ret = drm_register_client(&drm_notifier_block);
	if (ret < 0) {
		pr_err("Unable to register DRM notifier\n");
		dyn_sync.drm_notify_used = dyn_sync.delay = 0;
	}
#endif

	ret = atomic_notifier_chain_register(&panic_notifier_list,
					     &panic_notifier_block);
	if (ret < 0) {
		pr_err("Unable to register panic notifier\n");
		goto fail_panic;
	}

	ret = register_reboot_notifier(&reboot_notifier_block);
	if (ret < 0) {
		pr_err("Unable to register reboot notifier\n");
		goto fail_reboot;
	}

	ret = sysfs_create_group(kernel_kobj, &dynamic_sync_attr_group);
	if (ret < 0) {
		pr_err("Unable to create sysfs group\n");
		goto fail_sysfs;
	}

	return 0;

fail_sysfs:
	unregister_reboot_notifier(&reboot_notifier_block);
fail_reboot:
	atomic_notifier_chain_unregister(&panic_notifier_list,
					 &panic_notifier_block);
fail_panic:
#ifdef CONFIG_DRM
	drm_unregister_client(&drm_notifier_block);
#endif
	destroy_workqueue(dfs_wq);

	return ret;
}
late_initcall(dynamic_sync_init);
