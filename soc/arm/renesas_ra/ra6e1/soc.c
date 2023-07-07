/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include "soc.h"

uint32_t SystemCoreClock;

__weak void bsp_init(void *p_args)
{
}

static int soc_init(void)
{
	bsp_clock_init();

	/* Initialize SystemCoreClock variable. */
	SystemCoreClockUpdate();

	return 0;
}

SYS_INIT(soc_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
