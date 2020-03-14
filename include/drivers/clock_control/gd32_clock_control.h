/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2016 BayLibre, SAS
 * Copyright (c) 2017 Linaro Limited.
 * Copyright (c) 2017 RnDity Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_GD32_CLOCK_CONTROL_H_
#define ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_GD32_CLOCK_CONTROL_H_

#include <drivers/clock_control.h>
//#include <dt-bindings/clock/stm32_clock.h>


#include "gd32vf103_rcu.h"

/* common clock control device name for all GD32 chips */
#define GD32_CLOCK_CONTROL_NAME "gd32-rcu"

struct gd32_pclken {
	u32_t bus;
	u32_t enr;
};

#define GD32_CLOCK_BUS_SYS CK_SYS
#define GD32_CLOCK_BUS_AHB CK_AHB
#define GD32_CLOCK_BUS_APB1 CK_APB1
#define GD32_CLOCK_BUS_APB2 CK_APB2

#endif /* ZEPHYR_INCLUDE_DRIVERS_CLOCK_CONTROL_GD32_CLOCK_CONTROL_H_ */
