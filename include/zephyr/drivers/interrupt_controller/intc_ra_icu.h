/*
 * Copyright (c) 2023 - 2024 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_RA_ICU_H_
#define ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_RA_ICU_H_

/**
 * The Renesas RA ICU is an interrupt controller that works in conjunction with the NVIC.
 * In the Renesas RA, interrupt numbers are not assigned to specific functions;
 * rather, the ICU settings associate the numbers and functions.
 *
 * This is a specification that is difficult to support with the original isr_table,
 * so the ICU supports it as a custom interrupt controller.
 *
 * Specifically, the following points differ from normal IRQ APIs.
 *
 * 1. Interrupt handlers are set in the isr_table in the order they are found during
 *    linking. At the same time, a lookup table is generated, which associates event
 *    numbers with interrupts.
 *
 * 2. All arguments that specify IRQs in IRQ APIs, such as `irq_enable`, are interpreted
 *    as event numbers. The IRQ APIs implemented in the ICU resolve this using the lookup
 *    table above. In other words, the API does not use interrupt numbers but instead
 *    specifies all events by number.
 */

enum icu_irq_mode {
	ICU_FALLING,
	ICU_RISING,
	ICU_BOTH_EDGE,
	ICU_LOW_LEVEL,
};

typedef void (*ra_isr_handler)(const void *);

void ra_icu_clear_event_flag(unsigned int event);

void ra_icu_query_event_config(unsigned int event, uint32_t *intcfg, ra_isr_handler *pisr,
			       const void **cbarg);

void ra_icu_event_configure(unsigned int event, uint32_t intcfg);

#endif /* ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_RA_ICU_H_ */
