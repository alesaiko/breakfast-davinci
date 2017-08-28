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

#include <linux/device.h>

#define WAKELOCK_BLOCKER_VERSION	"2.0.0"
#define RESERVED_WAKELOCKS	        "qcom_rx_wakelock;wlan;wlan_wow_wl;wlan_extscan_wl;netmgr_wl;NETLINK"

#define MAX_LEN_WAKELOCK                (50)
#define MAX_LEN_USER                    (255)
#define MAX_LEN_RESERVED                (127)

/* 3 more characters for delimiters */
#define MAX_LEN_TOTAL		        (MAX_LEN_USER + MAX_LEN_RESERVED + 3)

struct wakelock_blocker {
        unsigned int enabled;

        char blocked_wakelocks[MAX_LEN_USER];
        char reserved_wakelocks[MAX_LEN_RESERVED];
        char total_wakelocks[MAX_LEN_TOTAL];
};

int __wakelock_blocked(struct wakeup_source *ws);