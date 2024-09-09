/*
 * Copyright (c) 2024, Andrew Featherstone
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef __RP2350A_PINCTRL_H__
#define __RP2350A_PINCTRL_H__

#include "rpi-pico-rp2350-pinctrl-common.h"

/* ADC channel allocations differ between the RP2350A and RP2350B.
 * Refer to Table 1115 in the datasheet.
 */
#define ADC_CH0_P26 RP2XXX_PINMUX(26, RP2_PINCTRL_GPIO_FUNC_NULL)
#define ADC_CH1_P27 RP2XXX_PINMUX(27, RP2_PINCTRL_GPIO_FUNC_NULL)
#define ADC_CH2_P28 RP2XXX_PINMUX(28, RP2_PINCTRL_GPIO_FUNC_NULL)
#define ADC_CH3_P29 RP2XXX_PINMUX(29, RP2_PINCTRL_GPIO_FUNC_NULL)

#endif /* __RP2350A_PINCTRL_H__ */
