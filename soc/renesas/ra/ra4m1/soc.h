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

#include <bsp_api.h>

#ifndef BSP_EXCEPTIONS_H
#include <bsp_exceptions.h>
#endif
#ifndef R7FA4M1AB_H
#include <R7FA4M1AB.h>
#endif

#ifndef __NVIC_PRIO_BITS
/*
 * The FSP configuration headers include soc.h before they pull in renesas.h.
 * Prime the CMSIS core feature macros so <zephyr/irq.h> sees the correct RA4M1
 * defaults even on that first pass; the device header will reassert the same
 * values once it is parsed later in the include chain.
 */
#define __NVIC_PRIO_BITS 4
#endif
#ifndef __MPU_PRESENT
#define __MPU_PRESENT 1
#endif
#ifndef __FPU_PRESENT
#define __FPU_PRESENT 1
#endif
#ifndef __VTOR_PRESENT
#define __VTOR_PRESENT 1
#endif

#include <zephyr/irq.h>

#ifndef _ASMLANGUAGE
/*
 * Replace the FSP critical section helpers with wrappers that delegate to
 * Zephyr's IRQ locking API. The FSP headers only provide their defaults when
 * these macros are not defined, so undefine the vendor versions and plug in
 * implementations that observe Zephyr's nesting semantics.
 */
#undef FSP_CRITICAL_SECTION_DEFINE
#undef FSP_CRITICAL_SECTION_ENTER
#undef FSP_CRITICAL_SECTION_EXIT
#define FSP_CRITICAL_SECTION_DEFINE            unsigned int fsp_irq_lock_key __maybe_unused
#define FSP_CRITICAL_SECTION_ENTER             do { fsp_irq_lock_key = irq_lock(); } while (false)
#define FSP_CRITICAL_SECTION_EXIT              do { irq_unlock(fsp_irq_lock_key); } while (false)
#endif

#endif /* ZEPHYR_SOC_RENESAS_RA4M1_SOC_H_ */
