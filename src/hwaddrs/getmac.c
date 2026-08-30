/*
 * Copyright (C) 2011-2012 The CyanogenMod Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <string.h>
#include <cutils/properties.h>

#define BT_ADDR_FILE "/data/misc/bd_addr"
#define WIFI_ADDR_FILE "/data/misc/wifi/config"

extern void oncrpc_init();
extern void oncrpc_task_start();
extern void nv_cmd_remote(int, int, void*);

static int is_macbuf_valid(const unsigned char *macbuf)
{
	return (macbuf[0] != 0 || macbuf[1] != 0 || macbuf[2] != 0);
}

/*
 * Get the Bluetooth MAC address.
 *
 * Prefer the runtime property used by some LG RILs, then the persistent
 * property used by the stock batman_lgu framework.  Fall back to the NV item
 * used by other contemporary LG Qualcomm devices.
 */
static int set_bt_mac(void)
{
	FILE *fd;
	char machex[PROPERTY_VALUE_MAX];
	unsigned char macbuf[8];

	property_get("service.brcm.bt.mac", machex, "");
	if (!machex[0]) {
		property_get("persist.service.brcm.bt.mac", machex, "");
	}
	if (!machex[0]) {
		memset(macbuf, 0, sizeof(macbuf));
		nv_cmd_remote(0, 447, &macbuf);
		if (is_macbuf_valid(macbuf)) {
			sprintf(machex, "%02x%02x%02x%02x%02x%02x",
				macbuf[5], macbuf[4], macbuf[3],
				macbuf[2], macbuf[1], macbuf[0]);
		}
	}
	if (!machex[0])
		return -1;

	fd = fopen(BT_ADDR_FILE, "w");
	if (!fd)
		return -1;

	fprintf(fd, "%c%c:%c%c:%c%c:%c%c:%c%c:%c%c\n",
		machex[0], machex[1], machex[2], machex[3],
		machex[4], machex[5], machex[6], machex[7],
		machex[8], machex[9], machex[10], machex[11]);
	fclose(fd);
	chmod(BT_ADDR_FILE, 0666);

	return 0;
}

/*
 * Get the wlan MAC from NV. This attempts to replicate the
 * wifi_read_mac_address function from the stock software.
 */
static int set_wifi_mac(void)
{
	FILE *fd;
	unsigned char macbuf[8];

	memset(macbuf, 0, sizeof(macbuf));
	nv_cmd_remote(0, 0x1246, &macbuf);

	if (!is_macbuf_valid(macbuf))
		return -1;

	fd = fopen(WIFI_ADDR_FILE, "w");
	if (!fd)
		return -1;

	fprintf(fd, "cur_etheraddr=%02x:%02x:%02x:%02x:%02x:%02x\n",
		macbuf[0], macbuf[1], macbuf[2],
		macbuf[3], macbuf[4], macbuf[5]);
	fclose(fd);
	chmod(WIFI_ADDR_FILE, 0666);

	return 0;
}

/* Read properties and set MAC addresses accordingly. */
int main(void)
{
	oncrpc_init();
	oncrpc_task_start();

	set_bt_mac();
	set_wifi_mac();

	return 0;
}
