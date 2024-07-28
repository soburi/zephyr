/*
 * Copyright (c) 2024 Renesas Electronics Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra8_gpio

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/dt-bindings/gpio/renesas-ra8-gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/interrupt_controller/intc_ra_icu.h>
#include <zephyr/irq.h>
#include <zephyr/sys/util_macro.h>
#include <soc.h>

struct gpio_ra8_irq_info {
	uint16_t pin_bits;
	uint16_t event;
	uint32_t priority;
};

struct gpio_ra8_config {
	struct gpio_driver_config common;
	uint8_t port_num;
	R_PORT0_Type *port;
	const struct gpio_ra8_irq_info *irq_info;
	uint8_t num_ext_irqs;
	gpio_pin_t vbatt_pins[];
};

struct gpio_ra8_data {
	struct gpio_driver_data common;
	uint16_t *enabled_pin_bits;
	sys_slist_t callbacks;
};

__maybe_unused static void gpio_ra8_isr(const struct device *dev, uint8_t ext_irq)
{
	const struct gpio_ra8_config *config = dev->config;
	struct gpio_ra8_data *data = dev->data;

	gpio_fire_callbacks(&data->callbacks, dev, data->enabled_pin_bits[ext_irq]);
	ra_icu_event_clear_flag(config->irq_info[ext_irq].event);
}

__maybe_unused static int query_ext_irq(const struct device *dev, uint32_t pin)
{
	const struct gpio_ra8_config *config = dev->config;

	for (int i = 0; i < config->num_ext_irqs; i++) {
		if (config->irq_info[i].pin_bits & BIT(pin)) {
			return i;
		}
	}

	return -1;
}

static int gpio_ra8_pin_configure(const struct device *dev, gpio_pin_t pin, gpio_flags_t flags)
{
	const enum gpio_int_mode mode =
		flags & (GPIO_INT_EDGE | GPIO_INT_DISABLE | GPIO_INT_ENABLE);
	const enum gpio_int_trig trig = flags & (GPIO_INT_LOW_0 | GPIO_INT_HIGH_1);
	const struct gpio_ra8_config *config = dev->config;
	struct gpio_ra8_data *data = dev->data;

	struct ra_pinctrl_soc_pin pincfg = {0};
	int ext_irq;

	if (((flags & GPIO_INPUT) != 0U) && ((flags & GPIO_OUTPUT) != 0U)) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_PULL_DOWN) != 0U) {
		return -ENOTSUP;
	}

	if (config->vbatt_pins[0] != 0xFF) {
		uint32_t clear = 0;

		for (int i = 0; config->vbatt_pins[i] != '\0'; i++) {
			if (config->vbatt_pins[i] == pin) {
				WRITE_BIT(clear, i, 1);
			}
		}

		R_BSP_RegisterProtectDisable(BSP_REG_PROTECT_OM_LPC_BATT);

		R_SYSTEM->VBTICTLR &= (uint8_t)~clear;

		R_BSP_RegisterProtectEnable(BSP_REG_PROTECT_OM_LPC_BATT);
	}

	pincfg.port_num = config->port_num;
	pincfg.pin_num = pin;

	/* Change mode to general IO mode and Analog input */
	WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_PMR_Pos, 0);
	WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_ASEL_Pos, 0);

	if ((flags & GPIO_OUTPUT) != 0U) {
		/* Set output pin initial value */
		if ((flags & GPIO_OUTPUT_INIT_LOW) != 0U) {
			WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_PODR_Pos, 0);
		} else if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0U) {
			WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_PODR_Pos, 1);
		}

		WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_PDR_Pos, 1);
	} else {
		WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_PDR_Pos, 0);
	}

	if ((flags & GPIO_LINE_OPEN_DRAIN) != 0) {
		WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_NCODR_Pos, 1);
	}

	if ((flags & GPIO_PULL_UP) != 0) {
		WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_PCR_Pos, 1);
	}

	if (flags & GPIO_INT_ENABLE) {
		WRITE_BIT(pincfg.cfg, R_PFS_PORT_PIN_PmnPFS_ISEL_Pos, 1);
	}

	pincfg.cfg = pincfg.cfg |
		     (((flags & RENESAS_GPIO_DS_MSK) >> 8) << R_PFS_PORT_PIN_PmnPFS_DSCR_Pos);

	ext_irq = query_ext_irq(dev, pin);

	if (flags & GPIO_INT_ENABLE) {
		uint32_t intcfg;

		if (ext_irq < 0) {
			return -EINVAL;
		}

		if (mode == GPIO_INT_MODE_LEVEL) {
			if (trig != GPIO_INT_TRIG_LOW) {
				return -ENOTSUP;
			}

			intcfg = ICU_LOW_LEVEL;
		} else if (mode == GPIO_INT_MODE_EDGE) {
			switch (trig) {
			case GPIO_INT_TRIG_LOW:
				intcfg = ICU_FALLING;
				break;
			case GPIO_INT_TRIG_HIGH:
				intcfg = ICU_RISING;
				break;
			case GPIO_INT_TRIG_BOTH:
				intcfg = ICU_BOTH_EDGE;
				break;
			default:
				return -ENOTSUP;
			}
		} else {
			return -ENOTSUP;
		}

		intcfg = R_ICU->IRQCR[ext_irq] = intcfg;
		data->enabled_pin_bits[ext_irq] |= BIT(pin);
		ra_icu_event_enable(config->irq_info[ext_irq].event);
	} else {
		if (ext_irq >= 0) {
			data->enabled_pin_bits[ext_irq] &= ~BIT(pin);
			if (data->enabled_pin_bits[ext_irq] == 0) {
				ra_icu_event_disable(config->irq_info[ext_irq].event);
			}
		}
	}

	return pinctrl_configure_pins(&pincfg, 1, PINCTRL_REG_NONE);
}

#ifdef CONFIG_GPIO_GET_CONFIG
static int gpio_ra8_pin_get_config(const struct device *dev, gpio_pin_t pin, gpio_flags_t *flags)
{
	const struct gpio_ra8_config *config = dev->config;
	struct ra_pinctrl_soc_pin pincfg = {0};
	uint32_t intcfg;
	int ext_irq;
	int err;

	memset(flags, 0, sizeof(gpio_flags_t));

	err = ra_pinctrl_query_config(config->port_num, pin, &pincfg);
	if (err < 0) {
		return err;
	}

	if (pincfg.cfg & BIT(R_PFS_PORT_PIN_PmnPFS_PDR_Pos)) {
		*flags |= GPIO_OUTPUT;
	} else {
		*flags |= GPIO_INPUT;
	}

	if (pincfg.cfg & BIT(R_PFS_PORT_PIN_PmnPFS_ISEL_Pos)) {
		*flags |= GPIO_INT_ENABLE;
	}

	if (pincfg.cfg & BIT(R_PFS_PORT_PIN_PmnPFS_PCR_Pos)) {
		*flags |= GPIO_PULL_UP;
	}

	if (pincfg.cfg & BIT(R_PFS_PORT_PIN_PmnPFS_NCODR_Pos)) {
		*flags |= GPIO_LINE_OPEN_DRAIN;
	}

	*flags |= ((pincfg.cfg & R_PFS_PORT_PIN_PmnPFS_DSCR_Msk) >>
		   (R_PFS_PORT_PIN_PmnPFS_DSCR_Pos - RENESAS_GPIO_DS_POS));

	ext_irq = query_ext_irq(dev, pin);
	if (ext_irq < 0) {
		return 0;
	}

	intcfg = R_ICU->IRQCR[ext_irq];

	if (intcfg == ICU_FALLING) {
		*flags |= GPIO_INT_TRIG_LOW;
		*flags |= GPIO_INT_MODE_EDGE;
	} else if (intcfg == ICU_RISING) {
		*flags |= GPIO_INT_TRIG_HIGH;
		*flags |= GPIO_INT_MODE_EDGE;
	} else if (intcfg == ICU_BOTH_EDGE) {
		*flags |= GPIO_INT_TRIG_BOTH;
		*flags |= GPIO_INT_MODE_EDGE;
	} else if (intcfg == ICU_LOW_LEVEL) {
		*flags |= GPIO_INT_TRIG_LOW;
		*flags |= GPIO_INT_MODE_LEVEL;
	}

	return 0;
}
#endif

static int gpio_ra8_port_get_raw(const struct device *dev, uint32_t *value)
{
	const struct gpio_ra8_config *config = dev->config;
	R_PORT0_Type *port = config->port;

	*value = port->PIDR;

	return 0;
}

static int gpio_ra8_port_set_masked_raw(const struct device *dev, gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	const struct gpio_ra8_config *config = dev->config;
	R_PORT0_Type *port = config->port;

	port->PODR = ((port->PODR & ~mask) | (value & mask));

	return 0;
}

static int gpio_ra8_port_set_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_ra8_config *config = dev->config;
	R_PORT0_Type *port = config->port;

	port->PODR = (port->PODR | pins);

	return 0;
}

static int gpio_ra8_port_clear_bits_raw(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_ra8_config *config = dev->config;
	R_PORT0_Type *port = config->port;

	port->PODR = (port->PODR & ~pins);

	return 0;
}

static int gpio_ra8_port_toggle_bits(const struct device *dev, gpio_port_pins_t pins)
{
	const struct gpio_ra8_config *config = dev->config;
	R_PORT0_Type *port = config->port;

	port->PODR = (port->PODR ^ pins);

	return 0;
}

static int gpio_ra8_manage_callback(const struct device *dev, struct gpio_callback *callback,
				    bool set)
{
	struct gpio_ra8_data *data = dev->data;

	return gpio_manage_callback(&data->callbacks, callback, set);
}

static int gpio_ra8_pin_interrupt_configure(const struct device *dev, gpio_pin_t pin,
					    enum gpio_int_mode mode, enum gpio_int_trig trig)
{
	gpio_flags_t pincfg;
	int err;

	err = gpio_ra8_pin_get_config(dev, pin, &pincfg);
	if (err < 0) {
		return err;
	}

	return gpio_ra8_pin_configure(dev, pin, pincfg | mode | trig);
}

static const struct gpio_driver_api gpio_ra8_drv_api_funcs = {
	.pin_configure = gpio_ra8_pin_configure,
#ifdef CONFIG_GPIO_GET_CONFIG
	.pin_get_config = gpio_ra8_pin_get_config,
#endif
	.port_get_raw = gpio_ra8_port_get_raw,
	.port_set_masked_raw = gpio_ra8_port_set_masked_raw,
	.port_set_bits_raw = gpio_ra8_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ra8_port_clear_bits_raw,
	.port_toggle_bits = gpio_ra8_port_toggle_bits,
	.pin_interrupt_configure = gpio_ra8_pin_interrupt_configure,
	.manage_callback = gpio_ra8_manage_callback,
};

#define RA8_NUM_EXT_IRQ0  0
#define RA8_NUM_EXT_IRQ1  1
#define RA8_NUM_EXT_IRQ2  2
#define RA8_NUM_EXT_IRQ3  3
#define RA8_NUM_EXT_IRQ4  4
#define RA8_NUM_EXT_IRQ5  5
#define RA8_NUM_EXT_IRQ6  6
#define RA8_NUM_EXT_IRQ7  7
#define RA8_NUM_EXT_IRQ8  8
#define RA8_NUM_EXT_IRQ9  9
#define RA8_NUM_EXT_IRQ10 10
#define RA8_NUM_EXT_IRQ11 11
#define RA8_NUM_EXT_IRQ12 12
#define RA8_NUM_EXT_IRQ13 13
#define RA8_NUM_EXT_IRQ14 14
#define RA8_NUM_EXT_IRQ15 15

#define PIN_BITS(n, p, i) BIT(DT_PROP_BY_IDX(n, p, i))

#define PINS_BIT_OR(n, p, i)                                                                       \
	COND_CODE_1(DT_NODE_HAS_PROP(n, UTIL_CAT(DT_STRING_TOKEN_BY_IDX(n, p, i), _pins)),         \
		    DT_FOREACH_PROP_ELEM_SEP(n, UTIL_CAT(DT_STRING_TOKEN_BY_IDX(n, p, i), _pins),  \
					     PIN_BITS, (|)),                                       \
		    (0))

#define GPIO_RA8_IRQ_INFO(n, p, i)                                                                 \
	IF_ENABLED(DT_NODE_HAS_PROP(n, UTIL_CAT(DT_STRING_TOKEN_BY_IDX(n, p, i), _pins)),          \
		   ([UTIL_CAT(RA8_NUM_, DT_STRING_UPPER_TOKEN_BY_IDX(n, p, i))] = {                \
			    .event = DT_IRQ_BY_IDX(n, i, event),                                   \
			    .priority = DT_IRQ_BY_IDX(n, i, priority),                             \
			    .pin_bits = PINS_BIT_OR(n, p, i),                                      \
		    }, ))

#define ISR_DECL(n, p, i, suffix)                                                                  \
	(static void UTIL_CAT(UTIL_CAT(gpio_ra8_isr_, DT_STRING_TOKEN_BY_IDX(n, p, i)),            \
			      _##suffix)(const void *arg) {                                        \
		gpio_ra8_isr((const struct device *)arg,                                           \
			     UTIL_CAT(RA8_NUM_, DT_STRING_UPPER_TOKEN_BY_IDX(n, p, i)));           \
	})

#define GPIO_RA8_ISR_DECL(n, p, i, suffix)                                                         \
	IF_ENABLED(DT_NODE_HAS_PROP(n, UTIL_CAT(DT_STRING_TOKEN_BY_IDX(n, p, i), _pins)),          \
		   ISR_DECL(n, p, i, suffix))

#define GPIO_RA8_IRQ_CONNECT(i, n, suffix)                                                         \
	IF_ENABLED(DT_NODE_HAS_PROP(n, UTIL_CAT(UTIL_CAT(ext_irq, i), _pins)),                     \
		   (IRQ_CONNECT(DT_IRQ_BY_NAME(n, UTIL_CAT(ext_irq, i), event),                    \
				DT_IRQ_BY_NAME(n, UTIL_CAT(ext_irq, i), priority),                 \
				gpio_ra8_isr_ext_irq##i##_##suffix, DEVICE_DT_GET(n), 0);))

#define GPIO_DEVICE_INIT(node, port_number, suffix, addr)                                          \
	DT_FOREACH_PROP_ELEM_VARGS(node, interrupt_names, GPIO_RA8_ISR_DECL, suffix);              \
	static const struct gpio_ra8_irq_info irq_info_##suffix[DT_PROP(node, num_ext_irqs)] = {   \
		DT_FOREACH_PROP_ELEM(node, interrupt_names, GPIO_RA8_IRQ_INFO)};                   \
	static const struct gpio_ra8_config gpio_ra8_config_##suffix = {                           \
		.common =                                                                          \
			{                                                                          \
				.port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(16U),              \
			},                                                                         \
		.port_num = port_number,                                                           \
		.port = (R_PORT0_Type *)addr,                                                      \
		.irq_info = irq_info_##suffix,                                                     \
		.num_ext_irqs = DT_PROP(node, num_ext_irqs),                                       \
		.vbatt_pins = DT_PROP_OR(DT_NODELABEL(ioport##suffix), vbatts_pins, {0xFF}),       \
	};                                                                                         \
	static uint16_t UTIL_CAT(enabled_pin_bits_, port_number)[DT_PROP(node, num_ext_irqs)];     \
	static int UTIL_CAT(gpio_ra8_init_, port_number)(const struct device *dev)                 \
	{                                                                                          \
		LISTIFY(DT_PROP(node, num_ext_irqs), GPIO_RA8_IRQ_CONNECT, (), node, suffix);      \
		return 0;                                                                          \
	}                                                                                          \
	static struct gpio_ra8_data gpio_ra8_data_##suffix = {                                     \
		.enabled_pin_bits = enabled_pin_bits_##suffix,                                     \
	};                                                                                         \
	DEVICE_DT_DEFINE(node, gpio_ra8_init_##suffix, NULL, &gpio_ra8_data_##suffix,              \
			 &gpio_ra8_config_##suffix, PRE_KERNEL_1, CONFIG_GPIO_INIT_PRIORITY,       \
			 &gpio_ra8_drv_api_funcs)

#define GPIO_DEVICE_INIT_RA8(suffix)                                                               \
	GPIO_DEVICE_INIT(DT_NODELABEL(ioport##suffix),                                             \
			 DT_PROP(DT_NODELABEL(ioport##suffix), port), suffix,                      \
			 DT_REG_ADDR(DT_NODELABEL(ioport##suffix)))

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport0), okay)
GPIO_DEVICE_INIT_RA8(0);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport0), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport1), okay)
GPIO_DEVICE_INIT_RA8(1);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport1), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport2), okay)
GPIO_DEVICE_INIT_RA8(2);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport2), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport3), okay)
GPIO_DEVICE_INIT_RA8(3);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport3), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport4), okay)
GPIO_DEVICE_INIT_RA8(4);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport4), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport5), okay)
GPIO_DEVICE_INIT_RA8(5);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport5), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport6), okay)
GPIO_DEVICE_INIT_RA8(6);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport6), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport7), okay)
GPIO_DEVICE_INIT_RA8(7);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport7), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport8), okay)
GPIO_DEVICE_INIT_RA8(8);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport8), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioport9), okay)
GPIO_DEVICE_INIT_RA8(9);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioport9), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioporta), okay)
GPIO_DEVICE_INIT_RA8(a);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioporta), okay) */

#if DT_NODE_HAS_STATUS(DT_NODELABEL(ioportb), okay)
GPIO_DEVICE_INIT_RA8(b);
#endif /* DT_NODE_HAS_STATUS(DT_NODELABEL(ioportb), okay) */
