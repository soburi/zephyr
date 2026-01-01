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

#define RP1_ATOMIC_XOR_OFF 0x1000
#define RP1_ATOMIC_SET_OFF 0x2000
#define RP1_ATOMIC_CLR_OFF 0x3000

#define GPIO_STATUS(port, n) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x8 * n)
#define GPIO_CTRL(port, n)   (GPIO_STATUS(port, n) + 0x4)
#define PADS_CTRL(port, n)   (DEVICE_MMIO_NAMED_GET(port, pads) + 0x4 * (n))

#define GPIO_INTR(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x100)
#define GPIO_INTE(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x104)
#define GPIO_INTF(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x108)
#define GPIO_INTS(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x10c)
#define RIO_OUT(port)   (DEVICE_MMIO_NAMED_GET(port, rio) + 0x0)
#define RIO_OE(port)    (DEVICE_MMIO_NAMED_GET(port, rio) + 0x4)
#define RIO_IN(port)    (DEVICE_MMIO_NAMED_GET(port, rio) + 0x8)

#define GPIO_CTRL_SET(port, n, val) sys_write32(GPIO_CTRL(port, n) + RP1_ATOMIC_SET_OFF, val)
#define GPIO_CTRL_CLR(port, n, val) sys_write32(GPIO_CTRL(port, n) + RP1_ATOMIC_CLR_OFF, val)
#define PADS_CTRL_SET(port, n, val) sys_write32(PADS_CTRL(port, n) + RP1_ATOMIC_SET_OFF, val)
#define PADS_CTRL_CLR(port, n, val) sys_write32(PADS_CTRL(port, n) + RP1_ATOMIC_CLR_OFF, val)

#define GPIO_INTR_SET(port, val) sys_write32(GPIO_INTR(port) + RP1_ATOMIC_SET_OFF, val)
#define GPIO_INTR_CLR(port, val) sys_write32(GPIO_INTR(port) + RP1_ATOMIC_CLR_OFF, val)
#define GPIO_INTE_SET(port, val) sys_write32(GPIO_INTE(port) + RP1_ATOMIC_SET_OFF, val)
#define GPIO_INTE_CLR(port, val) sys_write32(GPIO_INTE(port) + RP1_ATOMIC_CLR_OFF, val)
#define RIO_OUT_XOR(port, val)   sys_write32(RIO_OUT(port) + RP1_ATOMIC_XOR_OFF, val)
#define RIO_OUT_SET(port, val)   sys_write32(RIO_OUT(port) + RP1_ATOMIC_SET_OFF, val)
#define RIO_OUT_CLR(port, val)   sys_write32(RIO_OUT(port) + RP1_ATOMIC_CLR_OFF, val)
#define RIO_OE_XOR(port, val)    sys_write32(RIO_OE(port) + RP1_ATOMIC_XOR_OFF, val)
#define RIO_OE_SET(port, val)    sys_write32(RIO_OE(port) + RP1_ATOMIC_SET_OFF, val)
#define RIO_OE_CLR(port, val)    sys_write32(RIO_OE(port) + RP1_ATOMIC_CLR_OFF, val)

#define GPIO_STATUS_OUT_TO_PAD    0x200
#define GPIO_STATUS_OUT_FROM_PERI 0x100

#define GPIO_CTRL_OUTOVER_MASK 0x3000
#define GPIO_CTRL_OUTOVER_PERI 0x0

#define GPIO_CTRL_OEOVER_MASK 0xc000
#define GPIO_CTRL_OEOVER_PERI 0x0

#define GPIO_CTRL_FUNCSEL_MASK 0x001f
#define GPIO_CTRL_FUNCSEL_RIO  0x5

#define PADS_OUTPUT_DISABLE 0x80
#define PADS_INPUT_ENABLE   0x40

#define PADS_PULL_UP_ENABLE   0x8
#define PADS_PULL_DOWN_ENABLE 0x4

#define GPIO_CTRL_IRQMASK_EDGE_LOW_SHIFT      20
#define GPIO_CTRL_IRQMASK_EDGE_LOW_MASK       (BIT_MASK(1) << GPIO_CTRL_IRQMASK_EDGE_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_EDGE_HIGH_SHIFT     21
#define GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK      (BIT_MASK(1) << GPIO_CTRL_IRQMASK_EDGE_HIGH_SHIFT)
#define GPIO_CTRL_IRQMASK_LEVEL_LOW_SHIFT     22
#define GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK      (BIT_MASK(1) << GPIO_CTRL_IRQMASK_LEVEL_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_LEVEL_HIGH_SHIFT    23
#define GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK     (BIT_MASK(1) << GPIO_CTRL_IRQMASK_LEVEL_HIGH_SHIFT)
#define GPIO_CTRL_IRQMASK_F_EDGE_LOW_SHIFT    24
#define GPIO_CTRL_IRQMASK_F_EDGE_LOW_MASK     (BIT_MASK(1) << GPIO_CTRL_IRQMASK_F_EDGE_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_F_EDGE_HIGH_SHIFT   25
#define GPIO_CTRL_IRQMASK_F_EDGE_HIGH_MASK    (BIT_MASK(1) << GPIO_CTRL_IRQMASK_F_EDGE_HIGH_SHIFT)
#define GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_SHIFT  26
#define GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_MASK   (BIT_MASK(1) << GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_SHIFT 27
#define GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_MASK  (BIT_MASK(1) << GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_SHIFT)
#define GPIO_CTRL_IRQRESET_SHIFT              28
#define GPIO_CTRL_IRQRESET_MASK               (BIT_MASK(1) << GPIO_CTRL_IRQRESET_SHIFT)
#define GPIO_CTRL_IRQOVER_SHIFT               30
#define GPIO_CTRL_IRQOVER_MASK                (BIT_MASK(2) << GPIO_CTRL_IRQOVER_SHIFT)

#define GPIO_PADS_SLEWFAST_SHIFT         0
#define GPIO_PADS_SLEWFAST_MASK          (BIT_MASK(1) << GPIO_PADS_SLEWFAST_SHIFT)
#define GPIO_PADS_SCHMITT_ENABLE_SHIFT   1
#define GPIO_PADS_SCHMITT_ENABLE_MASK    (BIT_MASK(1) << GPIO_PADS_SCHMITT_ENABLE_SHIFT)
#define GPIO_PADS_PULL_DOWN_ENABLE_SHIFT 2
#define GPIO_PADS_PULL_DOWN_ENABLE_MASK  (BIT_MASK(1) << GPIO_PADS_PULL_DOWN_ENABLE_SHIFT)
#define GPIO_PADS_PULL_UP_ENABLE_SHIFT   3
#define GPIO_PADS_PULL_UP_ENABLE_MASK    (BIT_MASK(1) << GPIO_PADS_PULL_UP_ENABLE_SHIFT)
#define GPIO_PADS_DRIVE_SHIFT            4
#define GPIO_PADS_DRIVE_MASK             (BIT_MASK(2) << GPIO_PADS_DRIVE_SHIFT)
#define GPIO_PADS_INPUT_ENABLE_SHIFT     6
#define GPIO_PADS_INPUT_ENABLE_MASK      (BIT_MASK(1) << GPIO_PADS_INPUT_ENABLE_SHIFT)
#define GPIO_PADS_OUTPUT_DISABLE_SHIFT   7
#define GPIO_PADS_OUTPUT_DISABLE_MASK    (BIT_MASK(1) << GPIO_PADS_OUTPUT_DISABLE_SHIFT)

#define ALL_EVENTS                                                                                 \
	(GPIO_CTRL_IRQMASK_EDGE_LOW_MASK | GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK |                      \
	 GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK | GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK |                    \
	 GPIO_CTRL_IRQMASK_F_EDGE_LOW_MASK | GPIO_CTRL_IRQMASK_F_EDGE_HIGH_MASK |                  \
	 GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_MASK | GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_MASK)

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
	sys_slist_t callbacks;
	uint32_t single_ended_mask;
	uint32_t open_drain_mask;

	DEVICE_MMIO_NAMED_RAM(gpio);
	DEVICE_MMIO_NAMED_RAM(rio);
	DEVICE_MMIO_NAMED_RAM(pads);
};

static int gpio_rp1_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	if (flags & GPIO_SINGLE_ENDED) {
		return -ENOTSUP;
	}

	/* Let RIO handle the input/output of GPIO */
	GPIO_CTRL_CLR(port, pin, GPIO_CTRL_OEOVER_MASK);
	GPIO_CTRL_SET(port, pin, GPIO_CTRL_OEOVER_PERI);

	GPIO_CTRL_CLR(port, pin, GPIO_CTRL_OUTOVER_MASK);
	GPIO_CTRL_SET(port, pin, GPIO_CTRL_OUTOVER_PERI);

	GPIO_CTRL_CLR(port, pin, GPIO_CTRL_FUNCSEL_MASK);
	GPIO_CTRL_SET(port, pin, GPIO_CTRL_FUNCSEL_RIO);

	/* Set the direction */
	if (flags & GPIO_OUTPUT) {
		RIO_OE_SET(port, BIT(pin));
		PADS_CTRL_CLR(port, pin, PADS_OUTPUT_DISABLE | PADS_INPUT_ENABLE);

		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			RIO_OUT_SET(port, BIT(pin));
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			RIO_OUT_CLR(port, BIT(pin));
		}
	} else if (flags & GPIO_INPUT) {
		RIO_OE_CLR(port, BIT(pin));
		PADS_CTRL_SET(port, pin, PADS_OUTPUT_DISABLE | PADS_INPUT_ENABLE);
	}

	/* Set pull up/down */
	PADS_CTRL_CLR(port, pin, PADS_PULL_UP_ENABLE | PADS_PULL_DOWN_ENABLE);

	if (flags & GPIO_PULL_UP) {
		PADS_CTRL_SET(port, pin, PADS_PULL_UP_ENABLE);
	} else if (flags & GPIO_PULL_DOWN) {
		PADS_CTRL_SET(port, pin, PADS_PULL_DOWN_ENABLE);
	}

	return 0;
}

#ifdef CONFIG_GPIO_GET_CONFIG
static int gpio_rp1_get_config(const struct device *dev, gpio_pin_t pin, gpio_flags_t *flags)
{
	struct gpio_rp1_data *data = dev->data;
	uint32_t reg = sys_read32(PADS_CTRL(dev, pin));

	*flags = 0;

	/* RP1 supports Bus Keeper mode where both pull-up and pull-down are enabled. */
	if (reg & GPIO_PADS_PULL_UP_ENABLE_MASK) {
		*flags |= GPIO_PULL_UP;
	}
	if (reg & GPIO_PADS_PULL_DOWN_ENABLE_MASK) {
		*flags |= GPIO_PULL_DOWN;
	}

	if (!(reg & GPIO_PADS_OUTPUT_DISABLE_MASK)) {
		*flags |= sys_read32(RIO_OUT(dev)) & BIT(pin) ? GPIO_OUTPUT_HIGH : GPIO_OUTPUT_LOW;
		if (data->single_ended_mask & BIT(pin)) {
			*flags |=
				data->open_drain_mask & BIT(pin) ? GPIO_OPEN_DRAIN : GPIO_PUSH_PULL;
		}
	}

	if (reg & GPIO_PADS_INPUT_ENABLE_MASK) {
		*flags |= GPIO_INPUT;
	}

	return 0;
}
#endif

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

static int gpio_rp1_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					    enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	uint32_t events = 0;

	GPIO_INTR_CLR(dev, BIT(pin));
	GPIO_CTRL_CLR(dev, pin, ALL_EVENTS);
	if (mode != GPIO_INT_DISABLE) {
		if (mode & GPIO_INT_EDGE) {
			if (trig & GPIO_INT_LOW_0) {
				events |= GPIO_CTRL_IRQMASK_EDGE_LOW_MASK;
			}
			if (trig & GPIO_INT_HIGH_1) {
				events |= GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK;
			}
		} else {
			if (trig & GPIO_INT_LOW_0) {
				events |= GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK;
			}
			if (trig & GPIO_INT_HIGH_1) {
				events |= GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK;
			}
		}
		GPIO_INTR_SET(dev, BIT(pin));
		GPIO_CTRL_SET(dev, pin, events);
	}

	return 0;
}

static int gpio_rp1_manage_callback(const struct device *dev, struct gpio_callback *callback,
				    bool set)
{
	struct gpio_rp1_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static uint32_t gpio_rp1_get_pending_int(const struct device *dev)
{
	const uint32_t reg = sys_read32(GPIO_INTS(dev));

	return reg;
}

#ifdef CONFIG_GPIO_GET_DIRECTION
static int gpio_rp1_port_get_direction(const struct device *port, gpio_port_pins_t map,
				       gpio_port_pins_t *inputs, gpio_port_pins_t *outputs)
{
	const struct gpio_rp1_config *config = port->config;

	/* The Zephyr API considers a disconnected pin to be neither an input nor output.
	 * Since we disable both OE and IE for disconnected pins clear the mask bits.
	 */
	for (int pin = 0; pin < config->ngpios; pin++) {
		uint32_t reg = sys_read32(PADS_CTRL(port, pin));
		if (reg & GPIO_PADS_OUTPUT_DISABLE_MASK) {
			map &= ~BIT(pin);
		}
		if (inputs && (reg & GPIO_PADS_INPUT_ENABLE_MASK)) {
			*inputs |= BIT(pin);
		}
	}
	if (inputs) {
		*inputs &= map;
	}
	if (outputs) {
		*outputs = sys_read32(RIO_OE(port)) & map;
	}

	return 0;
}
#endif

static DEVICE_API(gpio, gpio_rp1_api) = {
	.pin_configure = gpio_rp1_pin_configure,
#ifdef CONFIG_GPIO_GET_CONFIG
	.pin_get_config = gpio_rp1_get_config,
#endif
	.port_get_raw = gpio_rp1_port_get_raw,
	.port_set_masked_raw = gpio_rp1_port_set_masked_raw,
	.port_set_bits_raw = gpio_rp1_port_set_bits_raw,
	.port_clear_bits_raw = gpio_rp1_port_clear_bits_raw,
	.port_toggle_bits = gpio_rp1_port_toggle_bits,
	.pin_interrupt_configure = gpio_rp1_pin_interrupt_configure,
	.manage_callback = gpio_rp1_manage_callback,
	.get_pending_int = gpio_rp1_get_pending_int,
#ifdef CONFIG_GPIO_GET_DIRECTION
	.port_get_direction = gpio_rp1_port_get_direction,
#endif
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
