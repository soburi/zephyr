/*
 * Copyright (c) 2021, Yonatan Schachter
 * Copyright (c) 2025, Andrew Featherstone
 * Copyright (c) 2025, TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_GPIO_GPIO_RPI_PICO_H_
#define ZEPHYR_DRIVERS_GPIO_GPIO_RPI_PICO_H_

#include <stdint.h>
#include <zephyr/device.h>

#define ADDR_IS_ZERO(node_id, x)     (DT_REG_ADDR(node_id) == 0) |
#define ADDR_IS_NON_ZERO(node_id, x) (DT_REG_ADDR(node_id) != 0) |

#if DT_HAS_RASPBERRYPI_RP1_GPIO_ENABLED
#define GPIO_RPI_LO_AVAILABLE (DT_FOREACH_STATUS_OKAY_VARGS(raspberrypi_rp1_gpio, ADDR_IS_ZERO) 0)
#define GPIO_RPI_HI_AVAILABLE                                                                      \
	(DT_FOREACH_STATUS_OKAY_VARGS(raspberrypi_rp1_gpio, ADDR_IS_NON_ZERO) 0)
#else
#define GPIO_RPI_LO_AVAILABLE                                                                      \
	(DT_FOREACH_STATUS_OKAY_VARGS(raspberrypi_pico_gpio_port, ADDR_IS_ZERO) 0)
#define GPIO_RPI_HI_AVAILABLE                                                                      \
	(DT_FOREACH_STATUS_OKAY_VARGS(raspberrypi_pico_gpio_port, ADDR_IS_NON_ZERO) 0)
#endif

#define HAS_REG(node_id, regname) DT_REG_HAS_NAME(DT_PARENT(node_id), regname) ||

#define GPIO_RPI_ANY_HAS_REG_NAME(reg_name) \
	(DT_FOREACH_STATUS_OKAY_VARGS(raspberrypi_rp1_gpio, HAS_REG, reg_name) \
	 DT_FOREACH_STATUS_OKAY_VARGS(raspberrypi_pico_gpio_port, HAS_REG, reg_name) 0)

#define DEV_CFG(port)  ((const struct gpio_rpi_config *)(port)->config)
#define DEV_DATA(port) ((struct gpio_rpi_data *)(port)->data)

struct gpio_rpi_config {
	struct gpio_driver_config common;
	void (*bank_config_func)(void);
	void (*mmio_map_func)(void);
	uint8_t ngpios;
#if GPIO_RPI_ANY_HAS_REG_NAME(gpio)
	DEVICE_MMIO_NAMED_ROM(gpio);
#endif
#if GPIO_RPI_ANY_HAS_REG_NAME(sio)
	DEVICE_MMIO_NAMED_ROM(sio);
#endif
#if GPIO_RPI_ANY_HAS_REG_NAME(rio)
	DEVICE_MMIO_NAMED_ROM(rio);
#endif
#if GPIO_RPI_ANY_HAS_REG_NAME(pads)
	DEVICE_MMIO_NAMED_ROM(pads);
#endif
#if GPIO_RPI_HI_AVAILABLE
	const struct device *high_dev;
#endif
};

struct gpio_rpi_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	uint32_t single_ended_mask;
	uint32_t open_drain_mask;
#if GPIO_RPI_ANY_HAS_REG_NAME(gpio)
	DEVICE_MMIO_NAMED_RAM(gpio);
#endif
#if GPIO_RPI_ANY_HAS_REG_NAME(sio)
	DEVICE_MMIO_NAMED_RAM(sio);
#endif
#if GPIO_RPI_ANY_HAS_REG_NAME(rio)
	DEVICE_MMIO_NAMED_RAM(rio);
#endif
#if GPIO_RPI_ANY_HAS_REG_NAME(pads)
	DEVICE_MMIO_NAMED_RAM(pads);
#endif
};

#ifdef CONFIG_DT_HAS_RASPBERRYPI_RP1_GPIO_ENABLED
#include "gpio_rp1_hal.h"
#else
#include "gpio_rpi_pico_hal.h"
#endif

#endif /* ZEPHYR_DRIVERS_GPIO_GPIO_RPI_PICO_H_ */
