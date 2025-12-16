/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <zephyr/arch/common/sys_bitops.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/device_mmio.h>
#include "pinctrl_soc.h"

int pinctrl_bcm2712_configure_pin(const struct bcm2712_pinctrl_soc_pin *pin);
int pinctrl_rp1_configure_pin(const struct rp1_pinctrl_soc_pin *pin);

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
        int ret = 0;

        ARG_UNUSED(reg);

        for (uint8_t i = 0U; i < pin_cnt; i++) {
		if (pins[i].type == 0) {
			ret = pinctrl_bcm2712_configure_pin(&pins[i].bcm2712);

			if (ret != 0) {
				break;
			}
		} else {
			ret = pinctrl_rp1_configure_pin(&pins[i].rp1);

			if (ret != 0) {
				break;
			}
		}
        }

        return ret;
}
