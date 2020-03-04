/*
 * Copyright (c) 2017 Jean-Paul Etienne <fractalclone@gmail.com>
 * Contributors: 2018 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Platform Level Interrupt Controller (PLIC) driver
 *        for RISC-V processors
 */

#include <kernel.h>
#include <arch/cpu.h>
#include <init.h>
#include <soc.h>

#include <sw_isr_table.h>

/**
 *
 * @brief Initialize the Platform Level Interrupt Controller
 * @return N/A
 */
static int _eclic_init(struct device *dev)
{
	//ECLIC init
	eclic_init(ECLIC_NUM_INTERRUPTS);
	eclic_mode_enable();
	eclic_global_interrupt_enable();

	return 0;
}

SYS_INIT(_eclic_init, PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT);

//eclic_priority_group_set(ECLIC_PRIGROUP_LEVEL3_PRIO1);
//eclic_set_vector(RISCV_MACHINE_TIMER_IRQ, eclic_mtip_handler);

void arch_irq_enable(unsigned int irq)
{
        eclic_enable_interrupt(irq);
	riscv_irq_enable(irq);
}

void arch_irq_disable(unsigned int irq)
{
        eclic_disable_interrupt(irq);
	riscv_irq_disable(irq);
};

void arch_irq_priority_set(unsigned int irq, unsigned int prio)
{
        eclic_set_irq_priority(irq, prio);
	riscv_irq_priority_set(irq, prio);
}

int arch_irq_is_enabled(unsigned int irq)
{
	return arch_irq_is_enabled(irq);
}
