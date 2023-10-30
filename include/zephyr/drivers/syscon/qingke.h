/*
 * Copyright (c) 2023 Chen Xingyu <hi@xingrz.me>
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_SYSCON_QINGKE_
#define ZEPHYR_DRIVERS_SYSCON_QINGKE_

#include <zephyr/device.h>
#include <zephyr/drivers/syscon.h>

#define QINGKE_SYSCON DEVICE_DT_GET(DT_NODELABEL(syscon))

#define QINGKE_SYSCON_SAFE_ACCRESS_SIG DT_REG_ADDR(DT_NODELABEL(sig))

/* QINGKE_SYSCON_SAFE_ACCRESS_SIG Register */
#define QINGKE_SYSCON_SAFE_ACCRESS_SIG_KEY_1 (0x57)
#define QINGKE_SYSCON_SAFE_ACCRESS_SIG_KEY_2 (0xA8)

static inline void syscon_unlock(const struct device *syscon)
{
	syscon_write_reg(syscon, QINGKE_SYSCON_SAFE_ACCRESS_SIG,
			 QINGKE_SYSCON_SAFE_ACCRESS_SIG_KEY_1);
	syscon_write_reg(syscon, QINGKE_SYSCON_SAFE_ACCRESS_SIG,
			 QINGKE_SYSCON_SAFE_ACCRESS_SIG_KEY_2);
}

static inline void syscon_relock(const struct device *syscon)
{
	syscon_write_reg(syscon, QINGKE_SYSCON_SAFE_ACCRESS_SIG, 0x00);
}

static inline int syscon_read_reg_unlock(const struct device *syscon, uint16_t reg, uint32_t *val)
{
	syscon_unlock(syscon);
	return syscon_read_reg(syscon, reg, val);
}

static inline int syscon_write_reg_unlock(const struct device *syscon, uint16_t reg, uint32_t val)
{
	syscon_unlock(syscon);
	return syscon_write_reg(syscon, reg, val);
}

#endif /* ZEPHYR_DRIVERS_SYSCON_QINGKE_ */
