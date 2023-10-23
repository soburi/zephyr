/*
 * Copyright (c) 2023 Chen Xingyu <hi@xingrz.me>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_CLOCK_CONTROL_CH56X_
#define ZEPHYR_DRIVERS_CLOCK_CONTROL_CH56X_

#include <zephyr/devicetree.h>
#include <zephyr/devicetree/clocks.h>
#include <zephyr/dt-bindings/clock/ch56x-clocks.h>

#define CH56X_CLOCK_CONTROLLER DEVICE_DT_GET(DT_NODELABEL(hclk))

struct ch56x_clock_dev_config {
	uint8_t offset;
	uint8_t bit;
};

#define Z_CH56X_CLOCK_DEV_CONFIG_NAME(node_id)                                                     \
	_CONCAT(__ch56x_clock_dev_config, DEVICE_DT_NAME_GET(node_id))

#define Z_CH56X_CLOCK_DEV_CONFIG_INIT(node_id)                                                     \
	{                                                                                          \
		.offset = DT_CLOCKS_CELL(node_id, offset), .bit = DT_CLOCKS_CELL(node_id, bit),    \
	}

#define CH56X_CLOCK_DT_DEFINE(node_id)                                                             \
	struct ch56x_clock_dev_config Z_CH56X_CLOCK_DEV_CONFIG_NAME(node_id) =                     \
		Z_CH56X_CLOCK_DEV_CONFIG_INIT(node_id);

#define CH56X_CLOCK_DT_INST_DEFINE(inst) CH56X_CLOCK_DT_DEFINE(DT_DRV_INST(inst))

#define CH56X_CLOCK_DT_DEV_CONFIG_GET(node_id) &Z_CH56X_CLOCK_DEV_CONFIG_NAME(node_id)

#define CH56X_CLOCK_DT_INST_DEV_CONFIG_GET(inst) CH56X_CLOCK_DT_DEV_CONFIG_GET(DT_DRV_INST(inst))

#endif /* ZEPHYR_DRIVERS_CLOCK_CONTROL_CH56X_ */
