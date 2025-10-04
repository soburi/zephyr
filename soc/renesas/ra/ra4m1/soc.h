/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file SoC configuration macros for the Renesas RA4M1 family MCU
 */

#ifndef ZEPHYR_SOC_RENESAS_RA4M1_SOC_H_
#define ZEPHYR_SOC_RENESAS_RA4M1_SOC_H_

#include <zephyr/irq.h>

#define FSP_CRITICAL_SECTION_DEFINE            unsigned int irq_lock_key
#define FSP_CRITICAL_SECTION_ENTER             irq_lock_key = irq_lock();
#define FSP_CRITICAL_SECTION_EXIT              irq_unlock(irq_lock_key);

#include <bsp_api.h>

#endif /* ZEPHYR_SOC_RENESAS_RA4M1_SOC_H_ */
