/*
 * Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Driver for Nuclie's Extended Core Interrupt Controller
 */

#include <kernel.h>
#include <arch/cpu.h>
#include <init.h>
#include <soc.h>

#include <sw_isr_table.h>

/**
 * @brief Initialize the Platform Level Interrupt Controller
 * @return N/A
 */
static int _eclic_init(const struct device *dev)
{
	ECLIC_SetMth(0);
	ECLIC_SetCfgNlbits(__ECLIC_INTCTLBITS);
	__set_exc_entry(__RV_CSR_READ(CSR_MTVEC));
	__RV_CSR_SET(CSR_MSTATUS, MSTATUS_MIE);

	return 0;
}

SYS_INIT(_eclic_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);
