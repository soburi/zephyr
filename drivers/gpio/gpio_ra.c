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
#include <zephyr/drivers/gpio.h>
#include <zephyr/irq.h>

#include <zephyr/drivers/gpio/gpio_utils.h>

#include <r_ioport.h>

#define PODR_POS  0
#define PIDR_POS  1
#define PDR_POS   2
#define PCR_POS   4
#define PIM_POS   5
#define NCODR_POS 6
#define ISEL_POS  14
#define ASEL_POS  15
#define PMR_POS   16

struct gpio_ra_config {
	struct gpio_driver_config common;
	ioport_ctrl_t *const p_ctrl;
	bsp_io_port_t port;
};

struct gpio_ra_data {
	struct gpio_driver_data common;
	ioport_instance_ctrl_t ctrl;
};

static int gpio_ra_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_ra_config *config = dev->config;
	fsp_err_t ferr;
	uint32_t pincfg = 0;

	if ((flags & GPIO_OUTPUT) && (flags & GPIO_INPUT)) {
		/* Pin cannot be configured as input and output */
		return -ENOTSUP;
	} else if (!(flags & (GPIO_INPUT | GPIO_OUTPUT))) {
		/* Pin has to be configured as input or output */
		return -ENOTSUP;
	}

	if (flags & GPIO_OUTPUT) {
		pincfg |= BIT(PDR_POS);
	}

	if (flags & GPIO_PULL_UP) {
		pincfg |= BIT(PCR_POS);
	}

	pincfg &= ~BIT(PMR_POS);

	ferr = R_IOPORT_PinCfg(config->p_ctrl, config->port | pin, pincfg);

	return 0;
}

static int gpio_ra_port_get_raw(const struct device *dev, gpio_port_value_t *value)
{
	const struct gpio_ra_config *config = dev->config;
	ioport_size_t port_val;
	fsp_err_t ferr;

	ferr = R_IOPORT_PortRead(config->p_ctrl, config->port, &port_val);

	*value = port_val;

	return 0;
}

static int gpio_ra_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
				       gpio_port_value_t value)
{
	const struct gpio_ra_config *config = dev->config;
	ioport_size_t port_val;
	fsp_err_t ferr;

	ferr = R_IOPORT_PortRead(config->p_ctrl, config->port, &port_val);
	port_val = (port_val & ~mask) | (value & mask);
	ferr = R_IOPORT_PortWrite(config->p_ctrl, config->port, port_val, UINT16_MAX);

	return 0;
}

static int gpio_ra_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_ra_config *config = dev->config;
	ioport_size_t port_val;
	fsp_err_t ferr;

	ferr = R_IOPORT_PortRead(config->p_ctrl, config->port, &port_val);
	port_val |= pins;
	ferr = R_IOPORT_PortWrite(config->p_ctrl, config->port, port_val, UINT16_MAX);

	return 0;
}

static int gpio_ra_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_ra_config *config = dev->config;
	ioport_size_t port_val;
	fsp_err_t ferr;

	ferr = R_IOPORT_PortRead(config->p_ctrl, config->port, &port_val);
	port_val &= ~pins;
	ferr = R_IOPORT_PortWrite(config->p_ctrl, config->port, port_val, UINT16_MAX);

	return 0;
}

static int gpio_ra_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_ra_config *config = dev->config;
	ioport_size_t port_val;
	fsp_err_t ferr;

	ferr = R_IOPORT_PortRead(config->p_ctrl, config->port, &port_val);
	port_val ^= pins;
	ferr = R_IOPORT_PortWrite(config->p_ctrl, config->port, port_val, UINT16_MAX);

	return 0;
}

static int gpio_ra_init(const struct device *dev)
{
	const struct gpio_ra_config *config = dev->config;
	const ioport_cfg_t portcfg = {.number_of_pins = 0, .p_pin_cfg_data = NULL};

	R_IOPORT_Open(config->p_ctrl, &portcfg);

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
	static struct gpio_ra_config gpio_ra_config_##n = {.p_ctrl = &gpio_ra_data_##n.ctrl,       \
							   .port = DT_INST_PROP(n, portno) << 8};  \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_ra_init, NULL, &gpio_ra_data_##n, &gpio_ra_config_##n,       \
			      PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY, &gpio_ra_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_RA_INIT)
