/*
 * Copyright (c) 2017 Jean-Paul Etienne <fractalclone@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file SoC configuration macros for the GigaDevice GD32VF103 processor
 */

#ifndef __RISCV_GIGADEVICE_GD32VF103_SOC_H_
#define __RISCV_GIGADEVICE_GD32VF103_SOC_H_

#include <soc_common.h>
#include <generated_dts_board.h>

#include "n200_timer.h"

/* Timer configuration */
#define RISCV_MTIME_BASE             (TIMER_CTRL_ADDR + TIMER_MTIME)
#define RISCV_MTIMECMP_BASE          (TIMER_CTRL_ADDR + TIMER_MTIMECMP)

/* lib-c hooks required RAM defined variables */
#define RISCV_RAM_BASE               DT_SRAM_BASE_ADDRESS
#define RISCV_RAM_SIZE               KB(DT_SRAM_SIZE)

#ifndef _ASMLANGUAGE
#include "gd32vf103.h"
#include "n200_func.h"

#define ARCH_IRQ_CONNECT(irq_p, priority_p, isr_p, isr_param_p, flags_p) \
({ \
	Z_ISR_DECLARE(irq_p, 0, isr_p, isr_param_p); \
	eclic_set_nonvmode(irq_p); \
	eclic_set_level_trig(irq_p); \
	eclic_set_irq_lvl_abs(irq_p, 1); \
	arch_irq_priority_set(irq_p, priority_p); \
	irq_p; \
})

#endif /* !_ASMLANGUAGE */

#endif /* __RISCV_GIGADEVICE_GD32VF103_SOC_H_ */
