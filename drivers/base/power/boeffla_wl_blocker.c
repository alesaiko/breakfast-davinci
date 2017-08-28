/*
 * Author: andip71, 01.09.2017
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

#define pr_fmt(fmt)	"wakelock-blocker: " fmt

#include <linux/module.h>
#include <linux/miscdevice.h>

#include "boeffla_wl_blocker.h"

static struct wakelock_blocker wl_blocker = {
	.reserved_wakelocks = RESERVED_WAKELOCKS,
};

int __wakelock_blocked(struct wakeup_source *ws)
{
	char name[MAX_LEN_WAKELOCK];

	if (!wl_blocker.enabled)
		return 0;

	scnprintf(name, MAX_LEN_WAKELOCK, ";%s;", ws->name);

	return !!strnstr(wl_blocker.total_wakelocks, name, MAX_LEN_TOTAL);
}

static inline void update_total_wakelocks(void)
{
	int len;

	/* Start the list with a semicolon */
	len = scnprintf(wl_blocker.total_wakelocks, MAX_LEN_TOTAL, ";");

	if (strlen(wl_blocker.reserved_wakelocks) != 0)
		len += scnprintf(wl_blocker.total_wakelocks + len,
				 MAX_LEN_TOTAL - len, "%s;",
				 wl_blocker.reserved_wakelocks);

	if (strlen(wl_blocker.blocked_wakelocks) != 0)
		len += scnprintf(wl_blocker.total_wakelocks + len,
				 MAX_LEN_TOTAL - len, "%s;",
				 wl_blocker.blocked_wakelocks);
}

static ssize_t enabled_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	return scnprintf(buf, 12, "%u\n", wl_blocker.enabled);
}

static ssize_t enabled_store(struct device *dev,
			     struct device_attribute *attr,
			     const char *buf, size_t count)
{
	int ret;
	unsigned int val;

	ret = kstrtouint(buf, 10, &val);
	if (ret != 0)
		return -EINVAL;

	wl_blocker.enabled = !!val;

	return count;
}

static ssize_t blocked_wakelocks_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return scnprintf(buf, MAX_LEN_USER,
			"%s\n", wl_blocker.blocked_wakelocks);
}

static ssize_t blocked_wakelocks_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	int ret;

	if (count > MAX_LEN_USER)
		return -EINVAL;

	ret = sscanf(buf, "%s", wl_blocker.blocked_wakelocks);
	if (ret != 1)
		return -EINVAL;

	update_total_wakelocks();

	return count;
}

static ssize_t reserved_wakelocks_show(struct device *dev,
				       struct device_attribute *attr,
			    	       char *buf)
{
	return scnprintf(buf, MAX_LEN_RESERVED,
			"%s\n", wl_blocker.reserved_wakelocks);
}

static ssize_t reserved_wakelocks_store(struct device *dev,
					struct device_attribute *attr,
					const char *buf, size_t count)
{
	int ret;

	if (count > MAX_LEN_RESERVED)
		return -EINVAL;

	ret = sscanf(buf, "%s", wl_blocker.reserved_wakelocks);
	if (ret != 1)
		return -EINVAL;

	update_total_wakelocks();

	return count;
}

static ssize_t version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	return scnprintf(buf, PAGE_SIZE, "%s\n", WAKELOCK_BLOCKER_VERSION);
}

static DEVICE_ATTR(enabled, 0644,
	enabled_show, enabled_store);
static DEVICE_ATTR(wakelock_blocker, 0644,
	blocked_wakelocks_show, blocked_wakelocks_store);
static DEVICE_ATTR(wakelock_blocker_default, 0644,
	reserved_wakelocks_show, reserved_wakelocks_store);
static DEVICE_ATTR(version, 0444,
	version_show, NULL);

static struct attribute *blocker_attributes[] = {
	&dev_attr_enabled.attr,
	&dev_attr_wakelock_blocker.attr,
	&dev_attr_wakelock_blocker_default.attr,
	&dev_attr_version.attr,
	NULL
};

static struct attribute_group blocker_group = {
	.attrs = blocker_attributes,
};

static struct miscdevice blocker_device = {
	.name = "boeffla_wakelock_blocker",
	.minor = MISC_DYNAMIC_MINOR,
};

static int blocker_init(void)
{
	int ret;

	ret = misc_register(&blocker_device);
	if (ret < 0) {
		pr_err("Unable to register misc device.\n");
		return ret;
	}

	ret = sysfs_create_group(&blocker_device.this_device->kobj,
				 &blocker_group);
	if (ret < 0) {
		pr_err("Unable to create blocker sysfs group.\n");
		misc_deregister(&blocker_device);
		return ret;
	}

	update_total_wakelocks();

	return 0;
}
module_init(blocker_init);