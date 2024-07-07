/*
 * Copyright (c) 2023 Sequans Communications
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hwspinlock_rpi_pico

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/drivers/hwspinlock.h>

#include <zephyr/sys/printk.h>
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(hwspinlock_rpi_pico);

struct hwspinlock_rpi_pico_data {
	DEVICE_MMIO_RAM;
};

struct hwspinlock_rpi_pico_config {
	DEVICE_MMIO_ROM;
	uint32_t num_locks;
};

static inline mem_addr_t get_lock_addr(const struct device *dev, uint32_t id)
{
	return (mem_addr_t)(DEVICE_MMIO_GET(dev) + id * sizeof(uint32_t));
}

/*
 * To define CPU id, we use the affinity2 and affinity1
 * fields of the MPIDR register.
 */
static uint8_t mpidr_to_cpuid(uint64_t mpidr_val)
{
	uint8_t cpuid = ((mpidr_val >> 8) & 0x0F) | ((mpidr_val >> 12) & 0xF0);

	return ++cpuid;
}

static int hwspinlock_rpi_pico_trylock(const struct device *dev, uint32_t id)
{
	const struct hwspinlock_rpi_pico_config *config = dev->config;
	uint8_t cpuid;

	if (id > config->num_locks)
		return -EINVAL;

	/*
	 * If the register value is equal to cpuid, this means that the current
	 * core has already locked the HW spinlock.
	 * If not, we try to lock the HW spinlock by writing cpuid, then check
	 * whether it is locked.
	 */

	cpuid = mpidr_to_cpuid(read_mpidr_el1());
	if (sys_read8(get_lock_addr(dev, id)) == cpuid)
		return 0;

	sys_write8(cpuid, get_lock_addr(dev, id));
	if (sys_read8(get_lock_addr(dev, id)) == cpuid)
		return 0;

	return -EBUSY;
}

static void hwspinlock_rpi_pico_lock(const struct device *dev, uint32_t id)
{
	const struct hwspinlock_rpi_pico_config *config = dev->config;
	uint8_t cpuid;

	if (id > config->num_locks) {
		LOG_ERR("unsupported hwspinlock id '%d'", id);
		return;
	}

	/*
	 * Writing cpuid is equivalent to trying to lock HW spinlock, after
	 * which we check whether we've locked by reading the register value
	 * and comparing it with cpuid.
	 */

	cpuid = mpidr_to_cpuid(read_mpidr_el1());
	if (sys_read8(get_lock_addr(dev, id)) == 0) {
		sys_write8(cpuid, get_lock_addr(dev, id));
	}

	while (sys_read8(get_lock_addr(dev, id)) != cpuid) {
		k_busy_wait(CONFIG_HWSPINLOCK_RPI_PICO_RELAX_TIME);
		sys_write8(cpuid, get_lock_addr(dev, id));
	}
}

static void hwspinlock_rpi_pico_unlock(const struct device *dev, uint32_t id)
{
	const struct hwspinlock_rpi_pico_config *config = dev->config;
	uint8_t cpuid;

	if (id > config->num_locks) {
		LOG_ERR("unsupported hwspinlock id '%d'", id);
		return;
	}

	/*
	 * If the HW spinlock register value is equal to the cpuid and we write
	 * the cpuid, then the register value will be 0. So to unlock the
	 * hwspinlock, we write cpuid.
	 */

	cpuid = mpidr_to_cpuid(read_mpidr_el1());
	sys_write8(cpuid, get_lock_addr(dev, id));
}

static uint32_t hwspinlock_rpi_pico_get_max_id(const struct device *dev)
{
	const struct hwspinlock_rpi_pico_config *config = dev->config;

	return config->num_locks;
}

static const struct hwspinlock_driver_api hwspinlock_api = {
	.trylock = hwspinlock_rpi_pico_trylock,
	.lock = hwspinlock_rpi_pico_lock,
	.unlock = hwspinlock_rpi_pico_unlock,
	.get_max_id = hwspinlock_rpi_pico_get_max_id,
};

static int hwspinlock_rpi_pico_init(const struct device *dev)
{
	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);

	return 0;
}

#define HWSPINLOCK_RPI_PICO_INIT(idx)                                                \
	static struct hwspinlock_rpi_pico_data hwspinlock_rpi_pico##idx##_data;           \
	static const struct hwspinlock_rpi_pico_config hwspinlock_rpi_pico##idx##_config = {    \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(idx)),                         \
		.num_locks = 0,                      \
	};                                                                      \
	DEVICE_DT_INST_DEFINE(idx,                                              \
			      hwspinlock_rpi_pico_init,                              \
			      NULL,                                             \
			      &hwspinlock_rpi_pico##idx##_data,                      \
			      &hwspinlock_rpi_pico##idx##_config,                    \
			      PRE_KERNEL_1, CONFIG_HWSPINLOCK_INIT_PRIORITY,    \
			      &hwspinlock_api)

DT_INST_FOREACH_STATUS_OKAY(HWSPINLOCK_RPI_PICO_INIT);
