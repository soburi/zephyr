/*
 * Copyright (c) 2023 - 2024 TOKITTA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra_interrupt_controller_unit

#include <zephyr/device.h>
#include <zephyr/irq.h>
#include <soc.h>
#include <zephyr/drivers/interrupt_controller/intc_ra_icu.h>
#include <zephyr/sw_isr_table.h>
#include <errno.h>

#define NUM_IRQS_PER_REG  32
#define REG_FROM_IRQ(irq) (irq / NUM_IRQS_PER_REG)
#define BIT_FROM_IRQ(irq) (irq % NUM_IRQS_PER_REG)

#define IELSRn_REG(n) (DT_INST_REG_ADDR(0) + IELSRn_OFFSET + (n * 4))
#define IRQCRi_REG(i) (DT_INST_REG_ADDR(0) + IRQCRi_OFFSET + (i))

#define IRQCRi_IRQMD_POS  0
#define IRQCRi_IRQMD_MASK BIT_MASK(2)
#define IELSRn_IR_POS     16
#define IELSRn_IR_MASK    BIT_MASK(1)

enum {
	IRQCRi_OFFSET = 0x0,
	IELSRn_OFFSET = 0x300,
};

static inline unsigned int event_to_irq(unsigned int event)
{
	if (event >= ARRAY_SIZE(function_irq_table)) {
		return FUNCTION_IRQ_TABLE_ENTRY_INVALID;
	}

	return function_irq_table[event];
}

#ifdef CONFIG_ARM_CUSTOM_INTERRUPT_CONTROLLER
void arm_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags);

void z_soc_irq_init(void)
{
}

void z_soc_irq_enable(unsigned int event)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == FUNCTION_IRQ_TABLE_ENTRY_INVALID) {
		return;
	}

	sys_write32(event, IELSRn_REG(irq));
	NVIC_EnableIRQ((IRQn_Type)irq);
}

void z_soc_irq_disable(unsigned int event)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == FUNCTION_IRQ_TABLE_ENTRY_INVALID) {
		return;
	}

	NVIC_DisableIRQ((IRQn_Type)irq);
	sys_write32(0, IELSRn_REG(irq));
}

int z_soc_irq_is_enabled(unsigned int event)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == FUNCTION_IRQ_TABLE_ENTRY_INVALID) {
		return 0;
	}

	return NVIC->ISER[REG_FROM_IRQ(irq)] & BIT(BIT_FROM_IRQ(irq));
}

void z_arm_irq_priority_set(unsigned int event, unsigned int prio, uint32_t flags)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == FUNCTION_IRQ_TABLE_ENTRY_INVALID) {
		return;
	}

	arm_irq_priority_set(irq, prio, flags);
}

unsigned int z_soc_irq_get_active(void)
{
	return __get_IPSR();
}

void z_soc_irq_eoi(unsigned int irq)
{
}
#endif

void ra_icu_clear_event_flag(unsigned int event)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == FUNCTION_IRQ_TABLE_ENTRY_INVALID) {
		return;
	}

	sys_write32(sys_read32(IELSRn_REG(irq)) & ~BIT(IELSRn_IR_POS), IELSRn_REG(irq));
}

void ra_icu_query_event_config(unsigned int event, uint32_t *intcfg, ra_isr_handler *cb,
			       const void **cbarg)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == -1) {
		return;
	}

	*intcfg = sys_read32(IELSRn_REG(irq));
	*cb = _sw_isr_table[irq].isr;
	*cbarg = (void *)_sw_isr_table[irq].arg;
}

void ra_icu_event_configure(unsigned int event, uint32_t intcfg)
{
	const unsigned int irq = event_to_irq(event);

	if (irq == FUNCTION_IRQ_TABLE_ENTRY_INVALID) {
		return;
	}

	sys_write8((intcfg & IRQCRi_IRQMD_MASK) |
			   (sys_read8(IRQCRi_REG(irq)) & ~(IRQCRi_IRQMD_MASK)),
		   IRQCRi_REG(irq));
}
