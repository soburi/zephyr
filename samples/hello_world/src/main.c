/*
 * Copyright (c) 2012-2014 Wind River Systems, Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdint.h>
#include <zephyr/drivers/regulator.h>

int regulator_check_range(const struct device *dev, uint32_t step)
{
        const struct regulator_common_config *config = dev->config;

	int ret = 0;

	printk("devname = %s min=%d max=%d\n", dev->name, config->min_uv, config->max_uv);

	for (uint32_t val = config->min_uv; val <= config->max_uv; val += step) {
		ret = regulator_set_voltage(dev, val, val);
		if (ret != 0) {
			//printk("err\n");
		}
	}

	return ret;
}


int main(void)
{
#if 0
                        vdd_3v3: DCDC1 {
                        vcc_3v3: DCDC3 {
                        axp_ps: DCDC5 {
                        vdd_1v8: ALDO1 {
                        vdda_3v3: ALDO2 {
#endif
	printf("Hello World! %s\n", CONFIG_BOARD_TARGET);

	const struct device *dev;
	dev = DEVICE_DT_GET(DT_NODELABEL(vdd_1v8));
	regulator_check_range(dev, 10000);
/*
	dev = DEVICE_DT_GET(DT_NODELABEL(vdda_3v3));
	regulator_check_range(dev, 100000);

	dev = DEVICE_DT_GET(DT_NODELABEL(vdda_3v3));
	regulator_check_range(dev, 100000);

	dev = DEVICE_DT_GET(DT_NODELABEL(vdd_3v3_sd));
	regulator_check_range(dev, 100000);
*/
	dev = DEVICE_DT_GET(DT_NODELABEL(vcc_bl));
	regulator_check_range(dev, 10000);

	dev = DEVICE_DT_GET(DT_NODELABEL(vdd_3v3));
	regulator_check_range(dev, 10000);

	dev = DEVICE_DT_GET(DT_NODELABEL(dcdc2));
	regulator_check_range(dev, 10000);

	dev = DEVICE_DT_GET(DT_NODELABEL(vcc_3v3));
	regulator_check_range(dev, 10000);

	dev = DEVICE_DT_GET(DT_NODELABEL(dcdc4));
	regulator_check_range(dev, 10000);

	dev = DEVICE_DT_GET(DT_NODELABEL(axp_ps));
	regulator_check_range(dev, 10000);

	return 0;
}


