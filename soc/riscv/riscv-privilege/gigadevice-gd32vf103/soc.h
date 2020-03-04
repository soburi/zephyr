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

/* PINMUX Configuration */
#define SIFIVE_PINMUX_0_BASE_ADDR     (DT_INST_0_SIFIVE_GPIO0_BASE_ADDRESS + 0x38)

/* PINMUX IO Hardware Functions */
#define SIFIVE_PINMUX_IOF0            0x00
#define SIFIVE_PINMUX_IOF1            0x01

/* PINMUX MAX PINS */
#define SIFIVE_PINMUX_PINS            32

/* Clock controller. */
#define PRCI_BASE_ADDR               0x10008000

/* Timer configuration */
#define RISCV_MTIME_BASE             (TIMER_CTRL_ADDR + TIMER_MTIME)
#define RISCV_MTIMECMP_BASE          (TIMER_CTRL_ADDR + TIMER_MTIMECMP)

/* Always ON Domain */
#define SIFIVE_PMUIE		     0x10000140
#define SIFIVE_PMUCAUSE		     0x10000144
#define SIFIVE_PMUSLEEP		     0x10000148
#define SIFIVE_PMUKEY		     0x1000014C
#define SIFIVE_SLEEP_KEY_VAL	     0x0051F15E

#define SIFIVE_BACKUP_REG_BASE	     0x10000080

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
