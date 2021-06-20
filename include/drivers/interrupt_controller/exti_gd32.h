/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Driver for External interrupt/event controller in GD32 MCUs
 */

#ifndef ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_EXTI_GD32_H_
#define ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_EXTI_GD32_H_

#include <zephyr/types.h>

/* device name */
#define GD32_EXTI_NAME "gd32-exti"

/* constants definitions */
/* EXTI line number */
typedef enum {
    EXTI_0 = BIT(0),                                          /*!< EXTI line 0 */
    EXTI_1 = BIT(1),                                          /*!< EXTI line 1 */
    EXTI_2 = BIT(2),                                          /*!< EXTI line 2 */
    EXTI_3 = BIT(3),                                          /*!< EXTI line 3 */
    EXTI_4 = BIT(4),                                          /*!< EXTI line 4 */
    EXTI_5 = BIT(5),                                          /*!< EXTI line 5 */
    EXTI_6 = BIT(6),                                          /*!< EXTI line 6 */
    EXTI_7 = BIT(7),                                          /*!< EXTI line 7 */
    EXTI_8 = BIT(8),                                          /*!< EXTI line 8 */
    EXTI_9 = BIT(9),                                          /*!< EXTI line 9 */
    EXTI_10 = BIT(10),                                        /*!< EXTI line 10 */
    EXTI_11 = BIT(11),                                        /*!< EXTI line 11 */
    EXTI_12 = BIT(12),                                        /*!< EXTI line 12 */
    EXTI_13 = BIT(13),                                        /*!< EXTI line 13 */
    EXTI_14 = BIT(14),                                        /*!< EXTI line 14 */
    EXTI_15 = BIT(15),                                        /*!< EXTI line 15 */
    EXTI_16 = BIT(16),                                        /*!< EXTI line 16 */
    EXTI_17 = BIT(17),                                        /*!< EXTI line 17 */
    EXTI_18 = BIT(18),                                        /*!< EXTI line 18 */
} exti_line_enum;

/* external interrupt and event  */
typedef enum {
    EXTI_INTERRUPT = 0,                                       /*!< EXTI interrupt mode */
    EXTI_EVENT                                                /*!< EXTI event mode */
} exti_mode_enum;

/* interrupt trigger mode */
typedef enum {
    EXTI_TRIG_RISING = 0,                                     /*!< EXTI rising edge trigger */
    EXTI_TRIG_FALLING,                                        /*!< EXTI falling edge trigger */
    EXTI_TRIG_BOTH,                                           /*!< EXTI rising edge and falling edge trigger */
    EXTI_TRIG_NONE                                            /*!< without rising edge or falling edge trigger */
} exti_trig_type_enum;

/**
 * @brief enable EXTI interrupt for specific line
 *
 * @param line EXTI# line
 */
void gd32_exti_enable(int line);

/**
 * @brief disable EXTI interrupt for specific line
 *
 * @param line EXTI# line
 */
void gd32_exti_disable(int line);

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
int gd32_exti_set_callback(int line, gd32_exti_callback_t cb, void *data);

/**
 * @brief unset EXTI interrupt callback
 *
 * @param line EXI# line
 */
void gd32_exti_unset_callback(int line);

#endif /* ZEPHYR_DRIVERS_INTERRUPT_CONTROLLER_INTC_EXTI_GD32_H_ */
