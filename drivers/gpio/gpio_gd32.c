/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>

#include <kernel.h>
#include <device.h>
#include <soc.h>
#include <drivers/gpio.h>
#include <drivers/clock_control.h>
#include <dt-bindings/clock/gd32_clock.h>
#include <drivers/pinmux.h>
#include <sys/util.h>
#include <drivers/interrupt_controller/exti_gd32.h>

#include "gpio_gd32.h"
#include "gpio_utils.h"

#include "gd32vf103_gpio.h"

#define AFIO_EXTI_PORT_MASK	((uint8_t)0x70)
#define AFIO_EXTI_SOURCE_FIELDS            ((uint8_t)0x04U)         /*!< select AFIO exti source registers */

/**
 * @brief Common GPIO driver for GD32 MCUs.
 */

/**
 * @brief EXTI interrupt callback
 */
static void gpio_gd32_isr(int line, void *arg)
{
	struct device *dev = arg;
	struct gpio_gd32_data *data = dev->driver_data;

	if ((BIT(line) & data->cb_pins) != 0) {
		gpio_fire_callbacks(&data->cb, dev, BIT(line));
	}
}

/**
 * @brief Common gpio flags to custom flags
 */
const int gpio_gd32_flags_to_conf(int flags, int *pincfg)
{

	if ((flags & GPIO_OUTPUT) != 0) {
		/* Output only or Output/Input */

		*pincfg = GPIO_MODE_OUT_PP | GPIO_OSPEED_50MHZ;

	} else if  ((flags & GPIO_INPUT) != 0) {
		/* Input */



		if ((flags & GPIO_PULL_UP) != 0) {
			*pincfg |= GPIO_MODE_IPU;
		} else if ((flags & GPIO_PULL_DOWN) != 0) {
			*pincfg |= GPIO_MODE_IPD;
		} else {
			*pincfg |= GPIO_MODE_IN_FLOATING;
		}
	} else {
		/* Desactivated: Analog */
		*pincfg = GPIO_MODE_IN_FLOATING;
	}

	return 0;
}

/**
 * @brief Translate pin to pinval that the LL library needs
 */
static inline u32_t gd32_pinval_get(int pin)
{
	u32_t pinval;

	pinval = 1 << pin;
	return pinval;
}

/**
 * @brief Configure the hardware.
 */
static int gpio_gd32_configure(u32_t *base_addr, int pin, int conf, int altf)
{
	uint32_t gpio = (uint32_t)base_addr;

	int pin_ll = gd32_pinval_get(pin);


	ARG_UNUSED(altf);

	u32_t temp = conf & (GD32_MODE_INOUT_MASK << GD32_MODE_INOUT_SHIFT);

	if (temp == GD32_MODE_INPUT) {
		temp = conf & (GD32_CNF_IN_MASK << GD32_CNF_IN_SHIFT);

		if (temp == GD32_CNF_IN_ANALOG) {
			gpio_init(gpio, GPIO_MODE_AIN, 0, pin_ll);
		} else if (temp == GD32_CNF_IN_FLOAT) {
			gpio_init(gpio, GPIO_MODE_IN_FLOATING, 0, pin_ll);
		} else {


			temp = conf & (GD32_PUPD_MASK << GD32_PUPD_SHIFT);

			if (temp == GD32_PUPD_PULL_UP) {
				gpio_init(gpio, GPIO_MODE_IPU, 0, pin_ll);
			} else {
				gpio_init(gpio, GPIO_MODE_IPD, 0, pin_ll);
			}
		}

	} else {
		uint32_t max_hz;
		temp = conf & (GD32_MODE_OSPEED_MASK << GD32_MODE_OSPEED_SHIFT);

		if (temp == GD32_MODE_OUTPUT_MAX_2) {
			max_hz = GPIO_OSPEED_2MHZ;
		} else if (temp == GD32_MODE_OUTPUT_MAX_10) {
			max_hz = GPIO_OSPEED_10MHZ;
		} else {
			max_hz = GPIO_OSPEED_50MHZ;
		}

		temp = conf & (GD32_CNF_OUT_MASK << GD32_CNF_OUT_SHIFT);

		if (temp == GD32_CNF_AF_PP) {
			gpio_init((uint32_t)base_addr, GPIO_MODE_AF_PP, max_hz, pin_ll);
		} else if (temp == GD32_CNF_AF_OD) {
			gpio_init((uint32_t)base_addr, GPIO_MODE_AF_OD, max_hz, pin_ll);
		} else if (temp == GD32_CNF_OUT_PP) {
			gpio_init((uint32_t)base_addr, GPIO_MODE_OUT_PP, max_hz, pin_ll);
		} else {
			gpio_init((uint32_t)base_addr, GPIO_MODE_OUT_OD, max_hz, pin_ll);
		}
	}

	return 0;
}

static void gpio_gd32_set_exti_source(int port, int pin)
{
	gpio_exti_source_select(port, pin);
}

static int gpio_gd32_get_exti_source(int pin)
{
	int port;

	if (GPIO_PIN_SOURCE_4 > pin) {
		port = ((AFIO_EXTISS0 & AFIO_EXTI_PORT_MASK) >> AFIO_EXTI_SOURCE_FIELDS);
	} else if (GPIO_PIN_SOURCE_8 > pin) {
		port = ((AFIO_EXTISS1 & AFIO_EXTI_PORT_MASK) >> AFIO_EXTI_SOURCE_FIELDS);
	} else if (GPIO_PIN_SOURCE_12 > pin) {
		port = ((AFIO_EXTISS2 & AFIO_EXTI_PORT_MASK) >> AFIO_EXTI_SOURCE_FIELDS);
	} else {
		port = ((AFIO_EXTISS3 & AFIO_EXTI_PORT_MASK) >> AFIO_EXTI_SOURCE_FIELDS);
	}

	return port;
}

/**
 * @brief Enable EXTI of the specific line
 */
static int gpio_gd32_enable_int(int port, int pin)
{

	gpio_gd32_set_exti_source(port, pin);

	return 0;
}

static int gpio_gd32_port_get_raw(struct device *dev, u32_t *value)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	uint32_t gpio = (uint32_t)cfg->base;

	*value = gpio_input_port_get(gpio);

	return 0;
}

static int gpio_gd32_port_set_masked_raw(struct device *dev,
					  gpio_port_pins_t mask,
					  gpio_port_value_t value)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	uint32_t gpio = (uint32_t)cfg->base;
	u32_t port_value;

	port_value = gpio_output_port_get(gpio);
	gpio_port_write(gpio, (port_value & ~mask) | (mask & value));

	return 0;
}

static int gpio_gd32_port_set_bits_raw(struct device *dev,
					gpio_port_pins_t pins)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	uint32_t gpio = (uint32_t)cfg->base;

	gpio_bit_set(gpio, pins);

	return 0;
}

static int gpio_gd32_port_clear_bits_raw(struct device *dev,
					  gpio_port_pins_t pins)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	uint32_t gpio = (uint32_t)cfg->base;

	gpio_bit_reset(gpio, pins);

	return 0;
}

static int gpio_gd32_port_toggle_bits(struct device *dev,
				       gpio_port_pins_t pins)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	uint32_t gpio = (uint32_t)cfg->base;

	gpio_bit_set(gpio, gpio_output_port_get(gpio) ^ pins);

	return 0;
}

/**
 * @brief Configure pin or port
 */
static int gpio_gd32_config(struct device *dev,
			     gpio_pin_t pin, gpio_flags_t flags)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	int err = 0;
	int pincfg;


	/* figure out if we can map the requested GPIO
	 * configuration
	 */
	err = gpio_gd32_flags_to_conf(flags, &pincfg);
	if (err != 0) {
		goto release_lock;
	}

	if ((flags & GPIO_OUTPUT) != 0) {
		if ((flags & GPIO_OUTPUT_INIT_HIGH) != 0) {
			gpio_gd32_port_set_bits_raw(dev, BIT(pin));
		} else if ((flags & GPIO_OUTPUT_INIT_LOW) != 0) {
			gpio_gd32_port_clear_bits_raw(dev, BIT(pin));
		}
	}

	gpio_gd32_configure(cfg->base, pin, pincfg, 0);

release_lock:

	return err;
}

static int gpio_gd32_pin_interrupt_configure(struct device *dev,
		gpio_pin_t pin, enum gpio_int_mode mode,
		enum gpio_int_trig trig)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	struct gpio_gd32_data *data = dev->driver_data;
	int edge = 0;
	int err = 0;

	if (mode == GPIO_INT_MODE_DISABLED) {
		if (gpio_gd32_get_exti_source(pin) == cfg->port) {
			gd32_exti_disable(pin);
			gd32_exti_unset_callback(pin);
			gd32_exti_trigger(pin, EXTI_TRIG_NONE);
			data->cb_pins &= ~BIT(pin);
		}
		/* else: No irq source configured for pin. Nothing to disable */
		goto release_lock;
	}

	/* Level trigger interrupts not supported */
	if (mode == GPIO_INT_MODE_LEVEL) {
		err = -ENOTSUP;
		goto release_lock;
	}

	if (gd32_exti_set_callback(pin, gpio_gd32_isr, dev) != 0) {
		err = -EBUSY;
		goto release_lock;
	}

	data->cb_pins |= BIT(pin);

	gpio_gd32_enable_int(cfg->port, pin);

	switch (trig) {
	case GPIO_INT_TRIG_LOW:
		edge = EXTI_TRIG_FALLING;
		break;
	case GPIO_INT_TRIG_HIGH:
		edge = EXTI_TRIG_RISING;
		break;
	case GPIO_INT_TRIG_BOTH:
		edge = EXTI_TRIG_BOTH;
		break;
	}

	gd32_exti_trigger(pin, edge);

	gd32_exti_enable(pin);

release_lock:
	return err;
}

static int gpio_gd32_manage_callback(struct device *dev,
				      struct gpio_callback *callback,
				      bool set)
{
	struct gpio_gd32_data *data = dev->driver_data;

	return gpio_manage_callback(&data->cb, callback, set);
}

static int gpio_gd32_enable_callback(struct device *dev,
				      gpio_pin_t pin)
{
	struct gpio_gd32_data *data = dev->driver_data;

	data->cb_pins |= BIT(pin);

	return 0;
}

static int gpio_gd32_disable_callback(struct device *dev,
				       gpio_pin_t pin)
{
	struct gpio_gd32_data *data = dev->driver_data;

	data->cb_pins &= ~BIT(pin);

	return 0;
}

static const struct gpio_driver_api gpio_gd32_driver = {
	.pin_configure = gpio_gd32_config,
	.port_get_raw = gpio_gd32_port_get_raw,
	.port_set_masked_raw = gpio_gd32_port_set_masked_raw,
	.port_set_bits_raw = gpio_gd32_port_set_bits_raw,
	.port_clear_bits_raw = gpio_gd32_port_clear_bits_raw,
	.port_toggle_bits = gpio_gd32_port_toggle_bits,
	.pin_interrupt_configure = gpio_gd32_pin_interrupt_configure,
	.manage_callback = gpio_gd32_manage_callback,
	.enable_callback = gpio_gd32_enable_callback,
	.disable_callback = gpio_gd32_disable_callback,

};

/**
 * @brief Initialize GPIO port
 *
 * Perform basic initialization of a GPIO port. The code will
 * enable the clock for corresponding peripheral.
 *
 * @param dev GPIO device struct
 *
 * @return 0
 */
static int gpio_gd32_init(struct device *device)
{
	const struct gpio_gd32_config *cfg = device->config->config_info;

	/* enable clock for subsystem */
	struct device *clk =
		device_get_binding(GD32_CLOCK_CONTROL_NAME);

	if (clock_control_on(clk,
			     (clock_control_subsys_t *)&cfg->pclken) != 0) {
		return -EIO;
	}

	return 0;
}

#define GPIO_DEVICE_INIT(__name, __suffix, __base_addr, __port, __cenr, __bus) \
	static const struct gpio_gd32_config gpio_gd32_cfg_## __suffix = {   \
		.common = {						       \
			 .port_pin_mask = GPIO_PORT_PIN_MASK_FROM_NGPIOS(16U),		       \
		},							       \
		.base = (u32_t *)__base_addr,				       \
		.port = __port - GD32_PORTA,				       \
		.pclken = { .bus = __bus, .enr = __cenr }		       \
	};								       \
	static struct gpio_gd32_data gpio_gd32_data_## __suffix;	       \
	DEVICE_AND_API_INIT(gpio_gd32_## __suffix,			       \
			    __name,					       \
			    gpio_gd32_init,				       \
			    &gpio_gd32_data_## __suffix,		       \
			    &gpio_gd32_cfg_## __suffix,		       \
			    POST_KERNEL,				       \
			    CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,	       \
			    &gpio_gd32_driver)

#define GPIO_DEVICE_INIT_GD32(__suffix, __SUFFIX)		      \
	GPIO_DEVICE_INIT(DT_GPIO_GD32_GPIO##__SUFFIX##_LABEL,	      \
			 __suffix,				      \
			 DT_GPIO_GD32_GPIO##__SUFFIX##_BASE_ADDRESS, \
			 GD32_PORT##__SUFFIX,			      \
			 DT_GPIO_GD32_GPIO##__SUFFIX##_CLOCK_BITS,   \
			 DT_GPIO_GD32_GPIO##__SUFFIX##_CLOCK_BUS)

#ifdef CONFIG_GPIO_GD32_PORTA
GPIO_DEVICE_INIT_GD32(a, A);
#endif /* CONFIG_GPIO_GD32_PORTA */

#ifdef CONFIG_GPIO_GD32_PORTB
GPIO_DEVICE_INIT_GD32(b, B);
#endif /* CONFIG_GPIO_GD32_PORTB */

#ifdef CONFIG_GPIO_GD32_PORTC
GPIO_DEVICE_INIT_GD32(c, C);
#endif /* CONFIG_GPIO_GD32_PORTC */

#ifdef CONFIG_GPIO_GD32_PORTD
GPIO_DEVICE_INIT_GD32(d, D);
#endif /* CONFIG_GPIO_GD32_PORTD */

#ifdef CONFIG_GPIO_GD32_PORTE
GPIO_DEVICE_INIT_GD32(e, E);
#endif /* CONFIG_GPIO_GD32_PORTE */

#ifdef CONFIG_GPIO_GD32_PORTF
GPIO_DEVICE_INIT_GD32(f, F);
#endif /* CONFIG_GPIO_GD32_PORTF */

#ifdef CONFIG_GPIO_GD32_PORTG
GPIO_DEVICE_INIT_GD32(g, G);
#endif /* CONFIG_GPIO_GD32_PORTG */

#ifdef CONFIG_GPIO_GD32_PORTH
GPIO_DEVICE_INIT_GD32(h, H);
#endif /* CONFIG_GPIO_GD32_PORTH */

#ifdef CONFIG_GPIO_GD32_PORTI
GPIO_DEVICE_INIT_GD32(i, I);
#endif /* CONFIG_GPIO_GD32_PORTI */

#ifdef CONFIG_GPIO_GD32_PORTJ
GPIO_DEVICE_INIT_GD32(j, J);
#endif /* CONFIG_GPIO_GD32_PORTJ */

#ifdef CONFIG_GPIO_GD32_PORTK
GPIO_DEVICE_INIT_GD32(k, K);
#endif /* CONFIG_GPIO_GD32_PORTK */

static int gpio_gd32_afio_init(struct device *device)
{
	//UNUSED(device);

	struct device *clk = device_get_binding(GD32_CLOCK_CONTROL_NAME);
	struct gd32_pclken pclken = {
		.bus = GD32_CLOCK_BUS_APB2,
		.enr = 0x1,
	};

	clock_control_on(clk, (clock_control_subsys_t *) &pclken);

	return 0;
}

DEVICE_INIT(gpio_gd32_afio, "", gpio_gd32_afio_init, NULL, NULL, PRE_KERNEL_2, 0);
