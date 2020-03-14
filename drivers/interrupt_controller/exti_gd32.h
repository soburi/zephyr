/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Driver for External interrupt/event controller in GD32 MCUs
 *
 * Based on reference manuals:
 *   RM0008 Reference Manual: GD32F101xx, GD32F102xx, GD32F103xx, GD32F105xx
 *   and GD32F107xx advanced ARM(r)-based 32-bit MCUs
 * and
 *   RM0368 Reference manual GD32F401xB/C and GD32F401xD/E
 *   advanced ARM(r)-based 32-bit MCUs
 *
 * Chapter 10.2: External interrupt/event controller (EXTI)
 *
 */

#ifndef ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_EXTI_GD32_H_
#define ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_EXTI_GD32_H_

#include <zephyr/types.h>

/* device name */
#define GD32_EXTI_NAME "gd32-exti"

/**
 * @brief enable EXTI interrupt for specific line
 *
 * @param line EXTI# line
 */
int gd32_exti_enable(int line);

/**
 * @brief disable EXTI interrupt for specific line
 *
 * @param line EXTI# line
 */
void gd32_exti_disable(int line);

/**
 * @brief EXTI trigger flags
 */
enum gd32_exti_trigger {
	/* trigger on rising edge */
	GD32_EXTI_TRIG_RISING  = 0x1,
	/* trigger on falling endge */
	GD32_EXTI_TRIG_FALLING = 0x2,
};

/**
 * @brief set EXTI interrupt line triggers
 *
 * @param line EXTI# line
 * @param trg  OR'ed gd32_exti_trigger flags
 */
void gd32_exti_trigger(int line, int trg);

/* callback for exti interrupt */
typedef void (*gd32_exti_callback_t) (int line, void *user);

/**
 * @brief set EXTI interrupt callback
 *
 * @param line EXI# line
 * @param cb   user callback
 * @param arg  user arg
 */
int gd32_exti_set_callback(int line, int port, gd32_exti_callback_t cb,
				void *data);

/**
 * @brief unset EXTI interrupt callback
 *
 * @param line EXI# line
 */
void gd32_exti_unset_callback(int line);

#endif /* ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_EXTI_GD32_H_ */
