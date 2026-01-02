/*
 * Copyright (c) 2024 Junho Lee <junho@tsnlab.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT raspberrypi_rp1_gpio

#include <zephyr/arch/common/sys_bitops.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>

#define GPIO_STATUS(port, n) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x8 * n)
#define GPIO_CTRL(port, n)   (GPIO_STATUS(port, n) + 0x4)

#define GPIO_STATUS_OUT_TO_PAD    0x200
#define GPIO_STATUS_OUT_FROM_PERI 0x100

#define GPIO_CTRL_OUTOVER_MASK 0x3000
#define GPIO_CTRL_OUTOVER_PERI 0x0

#define GPIO_CTRL_OEOVER_MASK 0xc000
#define GPIO_CTRL_OEOVER_PERI 0x0

#define GPIO_CTRL_FUNCSEL_MASK 0x001f
#define GPIO_CTRL_FUNCSEL_RIO  0x5

#define RIO_OUT(port) (DEVICE_MMIO_NAMED_GET(port, rio) + 0x0)
#define RIO_OE(port)  (DEVICE_MMIO_NAMED_GET(port, rio) + 0x4)
#define RIO_IN(port)  (DEVICE_MMIO_NAMED_GET(port, rio) + 0x8)

#define RIO_SET 0x2000
#define RIO_CLR 0x3000

#define RIO_OUT_SET(port) (RIO_OUT(port) + RIO_SET)
#define RIO_OUT_CLR(port) (RIO_OUT(port) + RIO_CLR)

#define RIO_OE_SET(port) (RIO_OE(port) + RIO_SET)
#define RIO_OE_CLR(port) (RIO_OE(port) + RIO_CLR)

#define PADS_CTRL(port, n) (DEVICE_MMIO_NAMED_GET(port, ctrl) + 0x4 * (n + 1))

#define PADS_OUTPUT_DISABLE 0x80
#define PADS_INPUT_ENABLE   0x40

#define PADS_PULL_UP_ENABLE   0x8
#define PADS_PULL_DOWN_ENABLE 0x4

#define DEV_CFG(port)  ((const struct gpio_rp1_config *)(port)->config)
#define DEV_DATA(port) ((struct gpio_rp1_data *)(port)->data)

struct gpio_rp1_config {
	struct gpio_driver_config common;

	DEVICE_MMIO_NAMED_ROM(gpio);
	DEVICE_MMIO_NAMED_ROM(rio);
	DEVICE_MMIO_NAMED_ROM(pads);

	uint8_t ngpios;
};

struct gpio_rp1_data {
	struct gpio_driver_data common;

	DEVICE_MMIO_NAMED_RAM(gpio);
	DEVICE_MMIO_NAMED_RAM(rio);
	DEVICE_MMIO_NAMED_RAM(pads);
};

static int gpio_rp1_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	struct gpio_rp1_data *data = port->data;

	if (flags & GPIO_SINGLE_ENDED) {
		return -ENOTSUP;
	}

	/* Let RIO handle the input/output of GPIO */
	sys_clear_bits(GPIO_CTRL(port, pin), GPIO_CTRL_OEOVER_MASK);
	sys_set_bits(GPIO_CTRL(port, pin), GPIO_CTRL_OEOVER_PERI);

	sys_clear_bits(GPIO_CTRL(port, pin), GPIO_CTRL_OUTOVER_MASK);
	sys_set_bits(GPIO_CTRL(port, pin), GPIO_CTRL_OUTOVER_PERI);

	sys_clear_bits(GPIO_CTRL(port, pin), GPIO_CTRL_FUNCSEL_MASK);
	sys_set_bits(GPIO_CTRL(port, pin), GPIO_CTRL_FUNCSEL_RIO);

	/* Set the direction */
	if (flags & GPIO_OUTPUT) {
		sys_set_bit(RIO_OE_SET(port), pin);
		sys_clear_bits(PADS_CTRL(port, pin), PADS_OUTPUT_DISABLE | PADS_INPUT_ENABLE);

		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			sys_set_bit(RIO_OUT_SET(port), pin);
			sys_clear_bit(RIO_OUT_CLR(port), pin);
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			sys_set_bit(RIO_OUT_CLR(port), pin);
			sys_clear_bit(RIO_OUT_SET(port), pin);
		}
	} else if (flags & GPIO_INPUT) {
		sys_set_bit(RIO_OE_CLR(port), pin);
		sys_set_bits(PADS_CTRL(port, pin), PADS_OUTPUT_DISABLE | PADS_INPUT_ENABLE);
	}

	/* Set pull up/down */
	sys_clear_bits(PADS_CTRL(port, pin), PADS_PULL_UP_ENABLE | PADS_PULL_DOWN_ENABLE);

	if (flags & GPIO_PULL_UP) {
		sys_set_bits(PADS_CTRL(port, pin), PADS_PULL_UP_ENABLE);
	} else if (flags & GPIO_PULL_DOWN) {
		sys_set_bits(PADS_CTRL(port, pin), PADS_PULL_DOWN_ENABLE);
	}

	return 0;
}

static int gpio_rp1_port_get_raw(const struct device *port, gpio_port_value_t *value)
{
	*value = sys_read32(RIO_IN(port));

	return 0;
}

static int gpio_rp1_port_set_masked_raw(const struct device *port, gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	RIO_OUT_SET(port, (value & mask));
	RIO_OUT_CLR(port, (~value & mask));

	return 0;
}

static int gpio_rp1_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	RIO_OUT_SET(port, pins);

	return 0;
}

static int gpio_rp1_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	RIO_OUT_CLR(port, pins);

	return 0;
}

static int gpio_rp1_port_toggle_bits(const struct device *port, gpio_port_pins_t pins)
{
	RIO_OUT_XOR(port, pins);

	return 0;
}

static DEVICE_API(gpio, gpio_rp1_api) = {
	.pin_configure = gpio_rp1_pin_configure,
	.port_get_raw = gpio_rp1_port_get_raw,
	.port_set_masked_raw = gpio_rp1_port_set_masked_raw,
	.port_set_bits_raw = gpio_rp1_port_set_bits_raw,
	.port_clear_bits_raw = gpio_rp1_port_clear_bits_raw,
	.port_toggle_bits = gpio_rp1_port_toggle_bits,
};

static int gpio_rp1_init(const struct device *port)
{
	DEVICE_MMIO_NAMED_MAP(port, gpio, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(port, rio, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(port, pads, K_MEM_CACHE_NONE);

	return 0;
}

#define GPIO_RP1_INIT(n)                                                                           \
	static struct gpio_rp1_data gpio_rp1_data_##n;                                             \
                                                                                                   \
	static const struct gpio_rp1_config gpio_rp1_cfg_##n = {                                   \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(0)},                   \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME_WITH_OFFSET(gpio, DT_INST_PARENT(n),            \
							       DT_INST_REG_ADDR_BY_IDX(n, 0)),     \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME_WITH_OFFSET(rio, DT_INST_PARENT(n),             \
							       DT_INST_REG_ADDR_BY_IDX(n, 1)),     \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME_WITH_OFFSET(pads, DT_INST_PARENT(n),            \
							       DT_INST_REG_ADDR_BY_IDX(n, 2)),     \
		.ngpios = DT_INST_PROP(n, ngpios),                                                 \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_rp1_init, NULL, &gpio_rp1_data_##n, &gpio_rp1_cfg_##n,       \
			      POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, &gpio_rp1_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_RP1_INIT)
