/*
 * Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <soc.h>
#include <stdint.h>
#include <stdio.h>
#include <init.h>

static void __gd32_exti_isr_10_15(const void *arg)
{
	//__gd32_exti_isr(10, 16, arg);
}

static int _init(const struct device *dev)
{
	ARG_UNUSED(dev);

	IRQ_CONNECT(EXTI10_15_IRQn,
		    0,
		    __gd32_exti_isr_10_15, NULL,
		    0);

	/* Enable mcycle and minstret counter */
	__asm__ ("csrsi mcountinhibit, 0x5");
	return 0;
}

SYS_INIT(_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
