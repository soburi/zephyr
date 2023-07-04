/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra_gpio

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/irq.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/pinctrl.h>

struct port_control {
	union {
		volatile uint32_t PCNTR1;
		struct {
			volatile uint16_t PDR;
			volatile uint16_t PODR;
		};
	};

	union {
		volatile uint32_t PCNTR2;
		struct {
			volatile uint16_t PIDR;
			volatile uint16_t EIDR;
		};
	};

	union {
		volatile uint32_t PCNTR3;
		struct {
			volatile uint16_t POSR;
			volatile uint16_t PORR;
		};
	};

	union {
		volatile uint32_t PCNTR4;

		struct {
			volatile uint16_t EOSR;
			volatile uint16_t EORR;
		};
	};
};

struct gpio_ra_config {
	struct gpio_driver_config common;
	volatile struct port_control *const regs;
	uint16_t port;
};

struct gpio_ra_data {
	struct gpio_driver_data common;
};

static inline uint16_t port_read(const struct device *dev)
{
	const struct gpio_ra_config *config = dev->config;
	/* Read current value of PCNTR2 regsister for the specified port */
	return config->regs->PCNTR2 & UINT16_MAX;
}

static int port_write(const struct device *dev, uint16_t value, uint16_t mask)
{
	const struct gpio_ra_config *config = dev->config;
	uint16_t setbits = value & mask;
	uint16_t clrbits = (~value) & mask;

	/* PCNTR3 regsister: lower word = set data, upper word = reset_data */
	config->regs->PCNTR3 = ((clrbits << 16) | setbits);

	return 0;
}

static int gpio_ra_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_ra_config *config = dev->config;
	struct ra_pinctrl_soc_pin pincfg = {0};

	if ((flags & GPIO_OUTPUT) && (flags & GPIO_INPUT)) {
		/* Pin cannot be configured as input and output */
		return -ENOTSUP;
	} else if (!(flags & (GPIO_INPUT | GPIO_OUTPUT))) {
		/* Pin has to be configured as input or output */
		return -ENOTSUP;
	}

	if (flags & GPIO_OUTPUT) {
		pincfg.config |= BIT(PDR_POS);
	}

	if (flags & GPIO_PULL_UP) {
		pincfg.config |= BIT(PCR_POS);
	}

	pincfg.config &= ~BIT(PMR_POS);

	pincfg.pin = pin;
	pincfg.port = config->port;

	pinctrl_configure_pins(&pincfg, 1, PINCTRL_REG_NONE);

	return 0;
}

static int gpio_ra_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	*value = port_read(dev);

	return 0;
}

static int gpio_ra_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
				       gpio_port_value_t value)
{
	uint16_t port_val;

	port_val = port_read(dev);
	port_val = (port_val & ~mask) | (value & mask);
	return port_write(dev, port_val, UINT16_MAX);
}

static int gpio_ra_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	uint16_t port_val;

	port_val = port_read(dev);
	port_val |= pins;
	return port_write(dev, port_val, UINT16_MAX);
}

static int gpio_ra_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	uint16_t port_val;

	port_val = port_read(dev);
	port_val &= ~pins;
	return port_write(dev, port_val, UINT16_MAX);
}

static int gpio_ra_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	uint16_t port_val;

	port_val = port_read(dev);
	port_val ^= pins;
	return port_write(dev, port_val, UINT16_MAX);
}

static int gpio_ra_init(const struct device *dev)
{
	return 0;
}

static const struct gpio_driver_api gpio_ra_driver_api = {
	.pin_configure = gpio_ra_configure,
	.port_get_raw = gpio_ra_port_get_raw,
	.port_set_masked_raw = gpio_ra_port_set_masked_raw,
	.port_set_bits_raw = gpio_ra_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ra_port_clear_bits_raw,
	.port_toggle_bits = gpio_ra_port_toggle_bits,
};

/* Device Instantiation */
#define GPIO_RA_INIT(n)                                                                            \
	static struct gpio_ra_data gpio_ra_data_##n;                                               \
	static struct gpio_ra_config gpio_ra_config_##n = {                                        \
		.regs = (struct port_control *)DT_INST_REG_ADDR(n),                                \
		.port = DT_INST_PROP(n, portno)};                                                  \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_ra_init, NULL, &gpio_ra_data_##n, &gpio_ra_config_##n,       \
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, &gpio_ra_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_RA_INIT)
