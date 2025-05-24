/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>

struct channel_data {
	int ch;
	const char* name;
	struct sensor_value value;
	bool unusable;
};

int main(void)
{
	const struct device *dev;
	struct channel_data ch[] = {
		{ SENSOR_CHAN_PHOTOCURRENT_GREEN, "GREEN" },
		{ SENSOR_CHAN_PHOTOCURRENT_RED, "RED" },
		{ SENSOR_CHAN_PHOTOCURRENT_IR, "IR" },
	};

	int err;

       	dev = DEVICE_DT_GET_ANY(maxim_max30101);
	if (dev == NULL) {
		dev = DEVICE_DT_GET_ANY(maxim_max30102);
	}
	if (dev == NULL) {
		printf("Could not get max30101/max30102 device\n");
		return 0;
	}

	if (!device_is_ready(dev)) {
		printf("%s is not ready\n", dev->name);
		return 0;
	}

	while (1) {
		k_sleep(K_MSEC(100));

		err = sensor_sample_fetch(dev);
		if (err) {
			printf("sensor_sample_fetch failed %d\n", err);
			continue;
		}

		for (int i=0; i<ARRAY_SIZE(ch); i++) {
			if (ch[i].unusable) {
				continue;
			}

			err = sensor_channel_get(dev, ch[i].ch, &ch[i].value);
			if (err == -ENOTSUP) {
				ch[i].unusable = true;
				printf("%s channel not available %s\n", ch[i].name, dev->name);
				continue;
			} else if (err) {
				printf("sensor_channel_get failed %d\n", err);
			}
			printf("%s=%d.%06d ", ch[i].name, ch[i].value.val1, ch[i].value.val2);
		}

		/* Print green LED data*/
		printf("\n");
	}

	return 0;
}
