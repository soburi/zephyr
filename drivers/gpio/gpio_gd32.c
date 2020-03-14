/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "gd32vf103_gpio.h"

#include <errno.h>

#include <kernel.h>
#include <device.h>
#include <soc.h>
#include <drivers/gpio.h>
#include <drivers/clock_control.h>
#include <drivers/clock_control/gd32_clock_control.h>
//#include <pinmux/gd32/pinmux_gd32.h>
#include <drivers/pinmux.h>
#include <sys/util.h>
//#include <interrupt_controller/exti_gd32.h>

#include "gpio_gd32.h"
#include "gpio_utils.h"

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
	int direction = flags & GPIO_DIR_MASK;
	int pud = flags & GPIO_PUD_MASK;

	if (pincfg == NULL) {
		return -EINVAL;
	}

	if (direction == GPIO_DIR_OUT) {
		*pincfg = GPIO_MODE_OUT_PP | GPIO_OSPEED_50MHZ;
	} else {
		/* pull-{up,down} maybe? */
		//*pincfg = GD32_PINCFG_MODE_INPUT;
		if (pud == GPIO_PUD_PULL_UP) {
			*pincfg |= GPIO_MODE_IPU;
		} else if (pud == GPIO_PUD_PULL_DOWN) {
			*pincfg |= GPIO_MODE_IPD;
		} else {
			/* floating */
			*pincfg |= GPIO_MODE_IN_FLOATING;
		}
	}

	return 0;
}

/**
 * @brief Translate pin to pinval that the LL library needs
 */
static inline u32_t gd32_pinval_get(int pin)
{
	u32_t pinval;

#ifdef CONFIG_SOC_SERIES_GD32F1X
	pinval = (1 << pin) << GPIO_PIN_MASK_POS;
	if (pin < 8) {
		pinval |= 1 << pin;
	} else {
		pinval |= (1 << (pin % 8)) | 0x04000000;
	}
#else
	pinval = 1 << pin;
#endif
	return pinval;
}

/**
 * @brief Configure the hardware.
 */
int gpio_gd32_configure(u32_t port, int pin, int conf, int altf)
{
	int pin_ll = gd32_pinval_get(pin);

	ARG_UNUSED(altf);

	u32_t temp = conf & (GD32_MODE_INOUT_MASK << GD32_MODE_INOUT_SHIFT);

	if (temp == GD32_MODE_INPUT) {
		temp = conf & (GD32_CNF_IN_MASK << GD32_CNF_IN_SHIFT);

		if (temp == GD32_CNF_IN_ANALOG) {
			gpio_init(port, GPIO_MODE_AIN, 0, pin_ll);
		} else if (temp == GD32_CNF_IN_FLOAT) {
			gpio_init(port, GPIO_MODE_IN_FLOATING, 0, pin_ll);
		} else {
			temp = conf & (GD32_PUPD_MASK << GD32_PUPD_SHIFT);

			if (temp == GD32_PUPD_PULL_UP) {
				gpio_init(port, GPIO_MODE_IPU, 0, pin_ll);
			} else {
				gpio_init(port, GPIO_MODE_IPD, 0, pin_ll);
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
			gpio_init(port, GPIO_MODE_AF_PP, max_hz, pin_ll);
		} else if (temp == GD32_CNF_AF_OD) {
			gpio_init(port, GPIO_MODE_AF_OD, max_hz, pin_ll);
		} else if (temp == GD32_CNF_OUT_PP) {
			gpio_init(port, GPIO_MODE_OUT_PP, max_hz, pin_ll);
		} else {
			gpio_init(port, GPIO_MODE_OUT_OD, max_hz, pin_ll);
		}


	}

	return 0;
}

static inline uint32_t gpio_gd32_pin_to_exti_line(int pin)
{
#if defined(CONFIG_SOC_SERIES_GD32L0X) || \
	defined(CONFIG_SOC_SERIES_GD32F0X)
	return ((pin % 4 * 4) << 16) | (pin / 4);
#elif defined(CONFIG_SOC_SERIES_GD32MP1X)
	return (((pin * 8) % 32) << 16) | (pin / 4);
#elif defined(CONFIG_SOC_SERIES_GD32G0X)
	return ((pin & 0x3) << (16 + 3)) | (pin >> 2);
#else
	return (0xF << ((pin % 4 * 4) + 16)) | (pin / 4);
#endif
}

static void gpio_gd32_set_exti_source(int port, int pin)
{
	//TODO uint32_t line = gpio_gd32_pin_to_exti_line(pin);

#if defined(CONFIG_SOC_SERIES_GD32L0X) && defined(LL_SYSCFG_EXTI_PORTH)
	/*
	 * Ports F and G are not present on some GD32L0 parts, so
	 * for these parts port H external interrupt should be enabled
	 * by writing value 0x5 instead of 0x7.
	 */
	if (port == GD32_PORTH) {
		port = LL_SYSCFG_EXTI_PORTH;
	}
#endif

#ifdef CONFIG_SOC_SERIES_GD32F1X
	LL_GPIO_AF_SetEXTISource(port, line);
#elif CONFIG_SOC_SERIES_GD32MP1X
	LL_EXTI_SetEXTISource(port, line);
#elif defined(CONFIG_SOC_SERIES_GD32G0X)
	LL_EXTI_SetEXTISource(port, line);
#else
	//TODO LL_SYSCFG_SetEXTISource(port, line);
#endif
}

static int gpio_gd32_get_exti_source(int pin)
{
	//TODO uint32_t line = gpio_gd32_pin_to_exti_line(pin);
	int port;

#ifdef CONFIG_SOC_SERIES_GD32F1X
	port = LL_GPIO_AF_GetEXTISource(line);
#elif CONFIG_SOC_SERIES_GD32MP1X
	port = LL_EXTI_GetEXTISource(line);
#elif defined(CONFIG_SOC_SERIES_GD32G0X)
	port = LL_EXTI_GetEXTISource(line);
#else
	//TODO port = LL_SYSCFG_GetEXTISource(line);
#endif

#if defined(CONFIG_SOC_SERIES_GD32L0X) && defined(LL_SYSCFG_EXTI_PORTH)
	/*
	 * Ports F and G are not present on some GD32L0 parts, so
	 * for these parts port H external interrupt is enabled
	 * by writing value 0x5 instead of 0x7.
	 */
	if (port == LL_SYSCFG_EXTI_PORTH) {
		port = GD32_PORTH;
	}
#endif

	return port;
}

/**
 * @brief Enable EXTI of the specific line
 */
static int gpio_gd32_enable_int(int port, int pin)
{
#if defined(CONFIG_SOC_SERIES_GD32F2X) ||     \
	defined(CONFIG_SOC_SERIES_GD32F3X) || \
	defined(CONFIG_SOC_SERIES_GD32F4X) || \
	defined(CONFIG_SOC_SERIES_GD32F7X) || \
	defined(CONFIG_SOC_SERIES_GD32H7X) || \
	defined(CONFIG_SOC_SERIES_GD32L1X) || \
	defined(CONFIG_SOC_SERIES_GD32L4X) || \
	defined(CONFIG_SOC_SERIES_GD32G4X)
	struct device *clk = device_get_binding(GD32_CLOCK_CONTROL_NAME);
	struct gd32_pclken pclken = {
#ifdef CONFIG_SOC_SERIES_GD32H7X
		.bus = GD32_CLOCK_BUS_APB4,
		.enr = LL_APB4_GRP1_PERIPH_SYSCFG
#else
		.bus = GD32_CLOCK_BUS_APB2,
		.enr = LL_APB2_GRP1_PERIPH_SYSCFG
#endif /* CONFIG_SOC_SERIES_GD32H7X */
	};
	/* Enable SYSCFG clock */
	clock_control_on(clk, (clock_control_subsys_t *) &pclken);
#endif

	if (pin > 15) {
		return -EINVAL;
	}

	gpio_gd32_set_exti_source(port, pin);

	return 0;
}

/**
 * @brief Get enabled GPIO port for EXTI of the specific pin number
 */
static int gpio_gd32_int_enabled_port(int pin)
{
	if (pin > 15) {
		return -EINVAL;
	}

	return gpio_gd32_get_exti_source(pin);
}

/**
 * @brief Configure pin or port
 */
static int gpio_gd32_config(struct device *dev, int access_op,
			     u32_t pin, int flags)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;
	int err = 0;
	int pincfg = 0;
	int map_res;

	if (access_op != GPIO_ACCESS_BY_PIN) {
		return -ENOTSUP;
	}

	if ((flags & GPIO_POL_MASK) == GPIO_POL_INV) {
		/* hardware cannot invert signal */
		return -ENOTSUP;
	}

	/* figure out if we can map the requested GPIO
	 * configuration
	 */
	map_res = gpio_gd32_flags_to_conf(flags, &pincfg);
	if (map_res != 0) {
		err = map_res;
		goto release_lock;
	}

	if (gpio_gd32_configure(cfg->port, pin, pincfg, 0) != 0) {
		err = -EIO;
		goto release_lock;
	}

	if (!IS_ENABLED(CONFIG_EXTI_GD32)) {
		goto release_lock;
	}

	if (flags & GPIO_INT) {
#if 0
		if (gd32_exti_set_callback(pin, cfg->port,
					    gpio_gd32_isr, dev) != 0) {
			err = -EBUSY;
			goto release_lock;
		}

		gpio_gd32_enable_int(cfg->port, pin);

		if ((flags & GPIO_INT_EDGE) != 0) {
			int edge = 0;

			if ((flags & GPIO_INT_DOUBLE_EDGE) != 0) {
				edge = GD32_EXTI_TRIG_RISING |
				       GD32_EXTI_TRIG_FALLING;
			} else if ((flags & GPIO_INT_ACTIVE_HIGH) != 0) {
				edge = GD32_EXTI_TRIG_RISING;
			} else {
				edge = GD32_EXTI_TRIG_FALLING;
			}

			gd32_exti_trigger(pin, edge);
		} else {
			/* Level trigger interrupts not supported */
			err = -ENOTSUP;
			goto release_lock;
		}

		if (gd32_exti_enable(pin) != 0) {
			err = -EIO;
			goto release_lock;
		}
#endif
	} else {
		if (gpio_gd32_int_enabled_port(pin) == cfg->port) {
//			gd32_exti_disable(pin);
//			gd32_exti_unset_callback(pin);
		}
	}

release_lock:

	return err;
}

/**
 * @brief Set the pin or port output
 */
static int gpio_gd32_write(struct device *dev, int access_op,
			    u32_t pin, u32_t value)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;

	if (access_op != GPIO_ACCESS_BY_PIN) {
		return -ENOTSUP;
	}

	pin = gd32_pinval_get(pin);
	if (value != 0U) {
		gpio_bit_set(cfg->port, pin);
	} else {
		gpio_bit_reset(cfg->port, pin);
	}

	return 0;
}

/**
 * @brief Read the pin or port status
 */
static int gpio_gd32_read(struct device *dev, int access_op,
			   u32_t pin, u32_t *value)
{
	const struct gpio_gd32_config *cfg = dev->config->config_info;

	if (access_op != GPIO_ACCESS_BY_PIN) {
		return -ENOTSUP;
	}

	*value = (gpio_input_port_get(cfg->port) >> pin) & 0x1;

	return 0;
}

static int gpio_gd32_manage_callback(struct device *dev,
				      struct gpio_callback *callback,
				      bool set)
{
	struct gpio_gd32_data *data = dev->driver_data;

	return gpio_manage_callback(&data->cb, callback, set);
}

static int gpio_gd32_enable_callback(struct device *dev,
				      int access_op, u32_t pin)
{
	struct gpio_gd32_data *data = dev->driver_data;

	if (access_op != GPIO_ACCESS_BY_PIN) {
		return -ENOTSUP;
	}

	data->cb_pins |= BIT(pin);

	return 0;
}

static int gpio_gd32_disable_callback(struct device *dev,
				       int access_op, u32_t pin)
{
	struct gpio_gd32_data *data = dev->driver_data;

	if (access_op != GPIO_ACCESS_BY_PIN) {
		return -ENOTSUP;
	}

	data->cb_pins &= ~BIT(pin);

	return 0;
}

static const struct gpio_driver_api gpio_gd32_driver = {
	.config = gpio_gd32_config,
	.write = gpio_gd32_write,
	.read = gpio_gd32_read,
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
	struct device *clk = device_get_binding(GD32_CLOCK_CONTROL_NAME);
	clock_control_on(clk, (clock_control_subsys_t *) &cfg->pclken);

	return 0;
}

#define GPIO_DEVICE_INIT(__name, __suffix, __base_addr, __port, __cenr, __bus) \
	static const struct gpio_gd32_config gpio_gd32_cfg_## __suffix = {   \
		.base = (u32_t *)__base_addr,				       \
		.port = __port,						       \
		.pclken = {                                                    \
			.enr = __cenr,                                         \
			.bus = __bus,                                          \
		}                                                              \
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
			 GPIO##__SUFFIX,			     \
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

#if 0
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
#endif

#if defined(CONFIG_SOC_SERIES_GD32F1X)

static int gpio_gd32_afio_init(struct device *device)
{
	UNUSED(device);

	LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_AFIO);

#if defined(CONFIG_GPIO_GD32_SWJ_NONJTRST)
	/* released PB4 */
	__HAL_AFIO_REMAP_SWJ_NONJTRST();
#elif defined(CONFIG_GPIO_GD32_SWJ_NOJTAG)
	/* released PB4 PB3 PA15 */
	__HAL_AFIO_REMAP_SWJ_NOJTAG();
#elif defined(CONFIG_GPIO_GD32_SWJ_DISABLE)
	/* released PB4 PB3 PA13 PA14 PA15 */
	__HAL_AFIO_REMAP_SWJ_DISABLE();
#endif

	LL_APB2_GRP1_DisableClock(LL_APB2_GRP1_PERIPH_AFIO);

	return 0;
}

DEVICE_INIT(gpio_gd32_afio, "", gpio_gd32_afio_init, NULL, NULL, PRE_KERNEL_2, 0);

#endif /* CONFIG_SOC_SERIES_GD32F1X */
