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
#include <drivers/pinmux.h>
#include <gpio/gpio_gd32.h>
//#include <clock_control/gd32_clock_control.h>
//#include <pinmux/gd32/pinmux_gd32.h>

#include "gd32vf103_gpio.h"

#define MODE_MASK  0x7Cu
#define SPEED_MASK 0x03u
#define INOUT_MASK 0x10u

static inline u32_t gpio_speed(u32_t func)
{
	return (func & INOUT_MASK) ? (func & SPEED_MASK) : 0;
}

static inline u32_t gpio_mode(u32_t func)
{
	return (func & MODE_MASK);
}

static int pinmux_gd32_set(struct device *dev, u32_t pin, u32_t func)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;

	/* enable clock for subsystem */
	struct device *clk =
		device_get_binding(GD32_CLOCK_CONTROL_NAME);

	if (clock_control_on(clk,
			     (clock_control_subsys_t *)&cfg->pclken) != 0) {
		return -EIO;
	}

        const struct gpio_gd32_config *config = dev->config->config_info;
        uint32_t base = (uint32_t)config->base;

	gpio_init(base, gpio_mode(func), gpio_speed(func), 1<<pin);

        return 0;
}

static int pinmux_gd32_get(struct device *dev, u32_t pin, u32_t *func)
{
        const struct gpio_gd32_config *config = dev->config->config_info;
        uint32_t base = (uint32_t)config->base;

        //*func = base->PCR[pin];

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

#define PINMUX_DEVICE_INIT(__name, __suffix, __base_addr, __port, __cenr, __bus) \
	static const struct gpio_gd32_config pinmux_gd32_cfg_## __suffix = {   \
		.base = (u32_t *)__base_addr,				       \
		.port = __port - GD32_PORTA,				       \
		.pclken = {                                                    \
			.enr = __cenr,                                         \
			.bus = __bus,                                          \
		}                                                              \
	};								       \
	DEVICE_AND_API_INIT(pinmux_gd32_## __suffix,			       \
			    __name,					       \
			    pinmux_gd32_init,				       \
			    NULL,					       \
			    &pinmux_gd32_cfg_## __suffix,		       \
			    PRE_KERNEL_1,				       \
			    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,	       \
			    &pinmux_gd32_driver_api)

#define PINMUX_DEVICE_INIT_GD32(__suffix, __SUFFIX)		      \
	PINMUX_DEVICE_INIT(DT_GPIO_GD32_GPIO##__SUFFIX##_LABEL,	      \
			 __suffix,				      \
			 DT_GPIO_GD32_GPIO##__SUFFIX##_BASE_ADDRESS, \
			 GD32_PORT##__SUFFIX,			      \
			 DT_GPIO_GD32_GPIO##__SUFFIX##_CLOCK_BITS,   \
			 DT_GPIO_GD32_GPIO##__SUFFIX##_CLOCK_BUS)

//#ifdef CONFIG_PINMUX_GD32_PORTA
PINMUX_DEVICE_INIT_GD32(a, A);
//#endif /* CONFIG_PINMUX_GD32_PORTA */

#ifdef CONFIG_PINMUX_GD32_PORTB
PINMUX_DEVICE_INIT_GD32(b, B);
#endif /* CONFIG_PINMUX_GD32_PORTB */

#ifdef CONFIG_PINMUX_GD32_PORTC
PINMUX_DEVICE_INIT_GD32(c, C);
#endif /* CONFIG_PINMUX_GD32_PORTC */

#ifdef CONFIG_PINMUX_GD32_PORTD
PINMUX_DEVICE_INIT_GD32(d, D);
#endif /* CONFIG_PINMUX_GD32_PORTD */

#ifdef CONFIG_PINMUX_GD32_PORTE
PINMUX_DEVICE_INIT_GD32(e, E);
#endif /* CONFIG_PINMUX_GD32_PORTE */

#ifdef CONFIG_PINMUX_GD32_PORTF
PINMUX_DEVICE_INIT_GD32(f, F);
#endif /* CONFIG_PINMUX_GD32_PORTF */

#ifdef CONFIG_PINMUX_GD32_PORTG
PINMUX_DEVICE_INIT_GD32(g, G);
#endif /* CONFIG_PINMUX_GD32_PORTG */

#ifdef CONFIG_PINMUX_GD32_PORTH
PINMUX_DEVICE_INIT_GD32(h, H);
#endif /* CONFIG_PINMUX_GD32_PORTH */

#ifdef CONFIG_PINMUX_GD32_PORTI
PINMUX_DEVICE_INIT_GD32(i, I);
#endif /* CONFIG_PINMUX_GD32_PORTI */

#ifdef CONFIG_PINMUX_GD32_PORTJ
PINMUX_DEVICE_INIT_GD32(j, J);
#endif /* CONFIG_PINMUX_GD32_PORTJ */

#ifdef CONFIG_PINMUX_GD32_PORTK
PINMUX_DEVICE_INIT_GD32(k, K);
#endif /* CONFIG_PINMUX_GD32_PORTK */
