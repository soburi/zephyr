/*
 * Copyright (c) 2023 Chen Xingyu <hi@xingrz.me>
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT wch_ch56x_gpio

#include <zephyr/arch/cpu.h>
#include <zephyr/irq.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/syscon.h>
#include <zephyr/dt-bindings/gpio/ch56x-gpio.h>

#define GPIO_PAD_DIR(base) (base + 0x00)
#define GPIO_PAD_PIN(base) (base + 0x04)
#define GPIO_PAD_OUT(base) (base + 0x08)
#define GPIO_PAD_CLR(base) (base + 0x0C)
#define GPIO_PAD_PU(base)  (base + 0x10)
#define GPIO_PAD_PD(base)  (base + 0x14)
#define GPIO_PAD_DRV(base) (base + 0x18)
#define GPIO_PAD_SMT(base) (base + 0x1C)

#define SYSCON_GPIO_INT_FLAG   (0x1C)
#define SYSCON_GPIO_INT_ENABLE (0x1D)
#define SYSCON_GPIO_INT_MODE   (0x1E)
#define SYSCON_GPIO_INT_POLAR  (0x1F)

#define DEV_CFG(dev)  ((const struct gpio_ch56x_config *const)(dev)->config)
#define DEV_DATA(dev) ((struct gpio_ch56x_data *const)(dev)->data)

struct gpio_ch56x_config {
	struct gpio_driver_config common;
	void (*irq_config_func)(void);
	mem_addr_t base;
	const struct device *syscon;
	const uint32_t *int_pins;
	uint32_t int_pins_cnt;
};

struct gpio_ch56x_data {
	struct gpio_driver_data common;
	sys_slist_t cb;
};

static int gpio_ch56x_pin_configure(const struct device *port, gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);
	uint32_t regval;

	/* Reset */
	{
		regval = sys_read32(GPIO_PAD_PU(cfg->base));
		regval &= ~BIT(pin);
		sys_write32(regval, GPIO_PAD_PU(cfg->base));

		regval = sys_read32(GPIO_PAD_PD(cfg->base));
		regval &= ~BIT(pin);
		sys_write32(regval, GPIO_PAD_PD(cfg->base));

		regval = sys_read32(GPIO_PAD_DRV(cfg->base));
		regval &= ~BIT(pin);
		sys_write32(regval, GPIO_PAD_DRV(cfg->base));

		regval = sys_read32(GPIO_PAD_SMT(cfg->base));
		regval &= ~BIT(pin);
		sys_write32(regval, GPIO_PAD_SMT(cfg->base));
	}

	if (flags & GPIO_INPUT) {
		/* Set input direction */
		{
			regval = sys_read32(GPIO_PAD_DIR(cfg->base));
			regval &= ~BIT(pin);
			sys_write32(regval, GPIO_PAD_DIR(cfg->base));
		}

		/* Set pulls */
		if (flags & GPIO_PULL_UP) {
			regval = sys_read32(GPIO_PAD_PU(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_PU(cfg->base));
		} else if (flags & GPIO_PULL_DOWN) {
			regval = sys_read32(GPIO_PAD_PD(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_PD(cfg->base));
		}

		/* Set schmitt trigger */
		if (flags & CH56X_GPIO_SCHMITT_TRIGGER) {
			regval = sys_read32(GPIO_PAD_SMT(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_SMT(cfg->base));
		}
	} else if (flags & GPIO_OUTPUT) {
		/* Set output direction */
		{
			regval = sys_read32(GPIO_PAD_DIR(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_DIR(cfg->base));
		}

		/* Set open drain */
		if (flags & GPIO_OPEN_DRAIN) {
			regval = sys_read32(GPIO_PAD_PD(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_PD(cfg->base));
		}

		/* Set drive strength */
		if (flags & CH56X_GPIO_DRIVE_STRENGTH_16MA) {
			regval = sys_read32(GPIO_PAD_DRV(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_DRV(cfg->base));
		}

		/* Set initial level */
		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			regval = sys_read32(GPIO_PAD_OUT(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_OUT(cfg->base));
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			regval = sys_read32(GPIO_PAD_CLR(cfg->base));
			regval |= BIT(pin);
			sys_write32(regval, GPIO_PAD_CLR(cfg->base));
		}
	}

	return 0;
}

static int gpio_ch56x_port_get_raw(const struct device *port, gpio_port_value_t *value)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);

	*value = sys_read32(GPIO_PAD_PIN(cfg->base));

	return 0;
}

static int gpio_ch56x_port_set_masked_raw(const struct device *port, gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);
	uint32_t regval;

	regval = sys_read32(GPIO_PAD_OUT(cfg->base));
	regval &= ~mask;
	regval |= value;
	sys_write32(regval, GPIO_PAD_OUT(cfg->base));

	return 0;
}

static int gpio_ch56x_port_set_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);

	sys_write32(pins, GPIO_PAD_OUT(cfg->base));

	return 0;
}

static int gpio_ch56x_port_clear_bits_raw(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);

	sys_write32(pins, GPIO_PAD_CLR(cfg->base));

	return 0;
}

static int gpio_ch56x_port_toggle_bits(const struct device *port, gpio_port_pins_t pins)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);
	uint32_t regval;

	regval = sys_read32(GPIO_PAD_OUT(cfg->base));
	regval ^= pins;
	sys_write32(regval, GPIO_PAD_OUT(cfg->base));

	return 0;
}

static int gpio_ch56x_pin_interrupt_configure(const struct device *port, gpio_pin_t pin,
					      enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);
	uint32_t regval;
	uint32_t offset = 0;

	if (trig == GPIO_INT_TRIG_BOTH) {
		/* Both edge not supported */
		return -ENOTSUP;
	}

	for (uint32_t i = 0; i < cfg->int_pins_cnt; i++) {
		if (pin == cfg->int_pins[i * 2]) {
			offset = cfg->int_pins[i * 2 + 1];
			break;
		}
	}

	if (offset == 0) {
		/* Interrupt not supported on this pin */
		return -ENOTSUP;
	}

	/* Disable interrupt first */
	syscon_read_reg(cfg->syscon, SYSCON_GPIO_INT_ENABLE, &regval);
	regval &= ~BIT(offset);
	syscon_write_reg(cfg->syscon, SYSCON_GPIO_INT_ENABLE, regval);

	if (mode == GPIO_INT_MODE_DISABLED) {
		return 0;
	}

	/* Set interrupt mode */
	syscon_read_reg(cfg->syscon, SYSCON_GPIO_INT_MODE, &regval);
	if (mode == GPIO_INT_MODE_EDGE) {
		regval |= BIT(offset);
	} else {
		regval &= ~BIT(offset);
	}
	syscon_write_reg(cfg->syscon, SYSCON_GPIO_INT_MODE, regval);

	/* Set interrupt trigger */
	syscon_read_reg(cfg->syscon, SYSCON_GPIO_INT_POLAR, &regval);
	if (trig == GPIO_INT_TRIG_HIGH) {
		regval |= BIT(offset);
	} else if (trig == GPIO_INT_TRIG_LOW) {
		regval &= ~BIT(offset);
	}
	syscon_write_reg(cfg->syscon, SYSCON_GPIO_INT_POLAR, regval);

	/* Enable interrupt */
	syscon_read_reg(cfg->syscon, SYSCON_GPIO_INT_ENABLE, &regval);
	regval |= BIT(offset);
	syscon_write_reg(cfg->syscon, SYSCON_GPIO_INT_ENABLE, regval);

	return 0;
}

static int gpio_ch56x_manage_callback(const struct device *port, struct gpio_callback *cb, bool set)
{
	struct gpio_ch56x_data *data = DEV_DATA(port);

	return gpio_manage_callback(&data->cb, cb, set);
}

static void gpio_ch56x_isr(const struct device *port)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);
	struct gpio_ch56x_data *data = DEV_DATA(port);
	uint32_t regval;
	uint32_t pin, offset;
	uint32_t status = 0, regset = 0;

	syscon_read_reg(cfg->syscon, SYSCON_GPIO_INT_FLAG, &regval);
	for (uint32_t i = 0; i < cfg->int_pins_cnt; i++) {
		pin = cfg->int_pins[i * 2];
		offset = cfg->int_pins[i * 2 + 1];
		if (BIT(offset) & regval) {
			status |= BIT(pin);
			regset |= BIT(offset);
		}
	}

	/* Clear interrupt */
	syscon_write_reg(cfg->syscon, SYSCON_GPIO_INT_FLAG, regset);

	gpio_fire_callbacks(&data->cb, port, status);
}

int gpio_ch56x_init(const struct device *port)
{
	const struct gpio_ch56x_config *cfg = DEV_CFG(port);

	cfg->irq_config_func();

	return 0;
}

static const struct gpio_driver_api gpio_ch56x_api = {
	.pin_configure = gpio_ch56x_pin_configure,
	.port_get_raw = gpio_ch56x_port_get_raw,
	.port_set_masked_raw = gpio_ch56x_port_set_masked_raw,
	.port_set_bits_raw = gpio_ch56x_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ch56x_port_clear_bits_raw,
	.port_toggle_bits = gpio_ch56x_port_toggle_bits,
	.pin_interrupt_configure = gpio_ch56x_pin_interrupt_configure,
	.manage_callback = gpio_ch56x_manage_callback,
};

#define GPIO_CH56X_INST(n)                                                                         \
	static struct gpio_ch56x_data gpio_ch56x_data_##n;                                         \
                                                                                                   \
	static void gpio_ch56x_irq_config_func_##n(void)                                           \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(n), DT_INST_IRQ(n, priority), gpio_ch56x_isr,             \
			    DEVICE_DT_INST_GET(n), 0);                                             \
		irq_enable(DT_INST_IRQN(n));                                                       \
	}                                                                                          \
                                                                                                   \
	static const uint32_t gpio_ch56x_int_pins_##n[] = DT_INST_PROP(n, interruptible_pins);     \
                                                                                                   \
	static const struct gpio_ch56x_config gpio_ch56x_cfg_##n = {                               \
		.common = {.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_DT_INST(0)},                   \
		.irq_config_func = gpio_ch56x_irq_config_func_##n,                                 \
		.base = DT_INST_REG_ADDR(n),                                                       \
		.syscon = DEVICE_DT_GET(DT_INST_PHANDLE(n, syscon)),                               \
		.int_pins = gpio_ch56x_int_pins_##n,                                               \
		.int_pins_cnt = DT_INST_PROP_LEN(n, interruptible_pins) / 2,                       \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, gpio_ch56x_init, NULL, &gpio_ch56x_data_##n, &gpio_ch56x_cfg_##n, \
			      PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &gpio_ch56x_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_CH56X_INST)
