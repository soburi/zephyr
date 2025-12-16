/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PINCTRL_PINCTRL_BCM2712_COMMON_H_
#define ZEPHYR_INCLUDE_DRIVERS_PINCTRL_PINCTRL_BCM2712_COMMON_H_

#include <stdint.h>
#include <zephyr/dt-bindings/pinctrl/bcm2712-pinctrl.h>

#define BCM2712_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                         \
        {                                                                                          \
        }

struct bcm2712_pinctrl_soc_pin {
        uint32_t pin_num: 5;
        uint32_t alt_func: 5;
        uint32_t bank: 3;
        uint32_t drive_strength: 4;
        uint32_t slew_rate: 1;
        uint32_t pullup: 1;
        uint32_t pulldown: 1;
        uint32_t input_enable: 1;
        uint32_t schmitt_enable: 1;
        uint32_t oe_override: 2;
        uint32_t out_override: 2;
        uint32_t in_override: 2;
        uint32_t irq_override: 2;
};

#endif /* ZEPHYR_INCLUDE_DRIVERS_PINCTRL_PINCTRL_BCM2712_COMMON_H_ */
