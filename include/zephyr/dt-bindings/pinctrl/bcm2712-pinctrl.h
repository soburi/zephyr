/*
 * Copyright (c) 2025
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DT_BINDINGS_PINCTRL_BCM2712_PINCTRL_H_
#define ZEPHYR_DT_BINDINGS_PINCTRL_BCM2712_PINCTRL_H_

#define RP1_FUNC_SPI  1
#define RP1_FUNC_UART 2
#define RP1_FUNC_I2C  3
#define RP1_FUNC_PWM  4
#define RP1_FUNC_RIO  5
#define RP1_FUNC_SIO  RP1_FUNC_RIO
#define RP1_FUNC_GPCK 8
#define RP1_FUNC_USB  9
#define RP1_FUNC_NULL 31

#define RP1_GPIO_OVERRIDE_NORMAL 0
#define RP1_GPIO_OVERRIDE_INVERT 1
#define RP1_GPIO_OVERRIDE_LOW    2
#define RP1_GPIO_OVERRIDE_HIGH   3

#define RP1_PINMUX_FUNC_POS  0
#define RP1_PINMUX_FUNC_MASK 0x1f

#define RP1_PINMUX_PIN_POS  5
#define RP1_PINMUX_PIN_MASK 0x1f

#define RP1_PINMUX_BANK_POS  10
#define RP1_PINMUX_BANK_MASK 0x7

#define RP1_GET_BANK(pinctrl) (((pinctrl) >> RP1_PINMUX_BANK_POS) & RP1_PINMUX_BANK_MASK)
#define RP1_GET_PIN(pinctrl)  (((pinctrl) >> RP1_PINMUX_PIN_POS) & RP1_PINMUX_PIN_MASK)
#define RP1_GET_FUNC(pinctrl) (((pinctrl) >> RP1_PINMUX_FUNC_POS) & RP1_PINMUX_FUNC_MASK)

#define RP1_BANK0_PIN(pin, func) RP1_PINMUX(0, pin, func)
#define RP1_BANK1_PIN(pin, func) RP1_PINMUX(1, pin, func)
#define RP1_BANK2_PIN(pin, func) RP1_PINMUX(2, pin, func)

#endif /* ZEPHYR_DT_BINDINGS_PINCTRL_BCM2712_PINCTRL_H_ */
