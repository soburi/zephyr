/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/sensor.h>
#include <stdio.h>

int main(void)
{
	const struct device *dev;
	int channels[] = { SENSOR_CHAN_PHOTOCURRENT_GREEN,
	       		   SENSOR_CHAN_PHOTOCURRENT_RED,
			   SENSOR_CHAN_PHOTOCURRENT_IR };
	const char* ch_name[] = { "GREEN", "RED", "IR" };
	struct sensor_value values[] = { {0, 0}, {0, 0}, {0, 0} };
	bool ch_available[] = { true, true, true };
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
		err = sensor_sample_fetch(dev);
		if (err) {
			printf("sensor_sample_fetch failed %d\n", err);
		}

		for (int i=0; i<ARRAY_SIZE(values); i++) {
			if (!ch_available[i]) {
				continue;
			}

			err = sensor_channel_get(dev, channels[i], &values[i]);
			if (err == -ENOTSUP) {
				ch_available[i] = false;
				printf("%s channel not available %s\n", ch_name[i], dev->name);
				continue;
			} else if (err) {
				printf("sensor_channel_get failed %d\n", err);
			}
			printf("%s=%d.%06d ", ch_name[i], values[i].val1, values[i].val2);
		}

		/* Print green LED data*/
		printf("\n");

		k_sleep(K_MSEC(20));
	}

	return 0;
}
