/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief
 *
 * A common driver for GD32 pinmux. Each SoC must implement a SoC
 * specific part of the driver.
 */

#include <errno.h>

#include <kernel.h>
#include <device.h>
#include <soc.h>
#include <drivers/clock_control.h>
#include <drivers/pinmux.h>

#include "gd32vf103_gpio.h"

#define MODE_MASK  0x7Cu
#define SPEED_MASK 0x03u
#define INOUT_MASK 0x10u

static inline u32_t gpio_speed(u32_t flags)
{
	return (flags & INOUT_MASK) ? (flags & SPEED_MASK) : 0;
}

static inline u32_t gpio_mode(u32_t flags)
{
	return (flags & ~SPEED_MASK);
}


static inline u32_t pin_to_base(u32_t pin)
{
	static const u32_t base[] = {
		GPIOA,
		GPIOB,
		GPIOB,
		GPIOC,
		GPIOD,
	};

	if((pin/16) >= ARRAY_SIZE(base)) return 0;

	return base[pin/16];
}

static inline u32_t pin_to_bit(u32_t pin)
{
	return 1<<(pin%16);
}

static int pinmux_gd32_set(struct device *dev, u32_t pin, u32_t flags)
{
	ARG_UNUSED(dev);

	gpio_init(pin_to_base(pin), gpio_mode(flags), gpio_speed(flags), pin_to_bit(pin)) ;

        return 0;
}

static int pinmux_gd32_get(struct device *dev, u32_t pin, u32_t *func)
{
        u32_t base = pin_to_base(pin);

	ARG_UNUSED(dev);

	if(base == 0) {
		return -EINVAL;
	}

	if(pin < 8) {
		*func = (GPIO_CTL0(base) >> (4*pin)) & 0xF;
	}
	else if(pin < 16) {
		*func = (GPIO_CTL1(base) >> (4*(pin-8))) & 0xF;
	}
	else {
		return -EIO;
	}

        return 0;
}

static int pinmux_gd32_pullup(struct device *dev, u32_t pin, u8_t func)
{
        return -ENOTSUP;
}

static int pinmux_gd32_input(struct device *dev, u32_t pin, u8_t func)
{
        return -ENOTSUP;
}

static int pinmux_gd32_init(struct device *dev)
{
        return 0;
}

static const struct pinmux_driver_api pinmux_gd32_driver_api = {
        .set = pinmux_gd32_set,
        .get = pinmux_gd32_get,
        .pullup = pinmux_gd32_pullup,
        .input = pinmux_gd32_input,
};

DEVICE_AND_API_INIT(pinmux_gd32_dev, CONFIG_PINMUX_NAME,
		    &pinmux_gd32_init, NULL, NULL,
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,
		    &pinmux_gd32_driver_api);
