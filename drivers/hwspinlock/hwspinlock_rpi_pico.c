/*
 * Copyright (c) 2023 Sequans Communications
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT hwspinlock_rpi_pico
/*
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef _HARDWARE_SYNC_H
#define _HARDWARE_SYNC_H

#include "pico.h"
#include "hardware/address_mapped.h"
#include "hardware/regs/sio.h"

#ifdef __cplusplus
extern "C" {
#endif

/** \file hardware/sync.h
 *  \defgroup hardware_sync hardware_sync
 *
 * Low level hardware spin locks, barrier and processor event APIs
 *
 * Spin Locks
 * ----------
 *
 * The RP2040 provides 32 hardware spin locks, which can be used to manage mutually-exclusive access to shared software
 * and hardware resources.
 *
 * Generally each spin lock itself is a shared resource,
 * i.e. the same hardware spin lock can be used by multiple higher level primitives (as long as the spin locks are neither held for long periods, nor
 * held concurrently with other spin locks by the same core - which could lead to deadlock). A hardware spin lock that is exclusively owned can be used
 * individually without more flexibility and without regard to other software. Note that no hardware spin lock may
 * be acquired re-entrantly (i.e. hardware spin locks are not on their own safe for use by both thread code and IRQs) however the default spinlock related
 * methods here (e.g. \ref spin_lock_blocking) always disable interrupts while the lock is held as use by IRQ handlers and user code is common/desirable,
 * and spin locks are only expected to be held for brief periods.
 *
 * The SDK uses the following default spin lock assignments, classifying which spin locks are reserved for exclusive/special purposes
 * vs those suitable for more general shared use:
 *
 * Number (ID) | Description
 * :---------: | -----------
 * 0-13        | Currently reserved for exclusive use by the SDK and other libraries. If you use these spin locks, you risk breaking SDK or other library functionality. Each reserved spin lock used individually has its own PICO_SPINLOCK_ID so you can search for those.
 * 14,15       | (\ref PICO_SPINLOCK_ID_OS1 and \ref PICO_SPINLOCK_ID_OS2). Currently reserved for exclusive use by an operating system (or other system level software) co-existing with the SDK.
 * 16-23       | (\ref PICO_SPINLOCK_ID_STRIPED_FIRST - \ref PICO_SPINLOCK_ID_STRIPED_LAST). Spin locks from this range are assigned in a round-robin fashion via \ref next_striped_spin_lock_num(). These spin locks are shared, but assigning numbers from a range reduces the probability that two higher level locking primitives using _striped_ spin locks will actually be using the same spin lock.
 * 24-31       | (\ref PICO_SPINLOCK_ID_CLAIM_FREE_FIRST - \ref PICO_SPINLOCK_ID_CLAIM_FREE_LAST). These are reserved for exclusive use and are allocated on a first come first served basis at runtime via \ref spin_lock_claim_unused()
 */

// PICO_CONFIG: PARAM_ASSERTIONS_ENABLED_SYNC, Enable/disable assertions in the HW sync module, type=bool, default=0, group=hardware_sync
#ifndef PARAM_ASSERTIONS_ENABLED_SYNC
#define PARAM_ASSERTIONS_ENABLED_SYNC 0
#endif

/** \brief A spin lock identifier
 * \ingroup hardware_sync
 */
typedef volatile uint32_t spin_lock_t;

// PICO_CONFIG: PICO_SPINLOCK_ID_IRQ, Spinlock ID for IRQ protection, min=0, max=31, default=9, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_IRQ
#define PICO_SPINLOCK_ID_IRQ 9
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_TIMER, Spinlock ID for Timer protection, min=0, max=31, default=10, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_TIMER
#define PICO_SPINLOCK_ID_TIMER 10
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_HARDWARE_CLAIM, Spinlock ID for Hardware claim protection, min=0, max=31, default=11, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_HARDWARE_CLAIM
#define PICO_SPINLOCK_ID_HARDWARE_CLAIM 11
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_RAND, Spinlock ID for Random Number Generator, min=0, max=31, default=12, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_RAND
#define PICO_SPINLOCK_ID_RAND 12
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_OS1, First Spinlock ID reserved for use by low level OS style software, min=0, max=31, default=14, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_OS1
#define PICO_SPINLOCK_ID_OS1 14
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_OS2, Second Spinlock ID reserved for use by low level OS style software, min=0, max=31, default=15, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_OS2
#define PICO_SPINLOCK_ID_OS2 15
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_STRIPED_FIRST, Lowest Spinlock ID in the 'striped' range, min=0, max=31, default=16, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_STRIPED_FIRST
#define PICO_SPINLOCK_ID_STRIPED_FIRST 16
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_STRIPED_LAST, Highest Spinlock ID in the 'striped' range, min=0, max=31, default=23, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_STRIPED_LAST
#define PICO_SPINLOCK_ID_STRIPED_LAST 23
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_CLAIM_FREE_FIRST, Lowest Spinlock ID in the 'claim free' range, min=0, max=31, default=24, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_CLAIM_FREE_FIRST
#define PICO_SPINLOCK_ID_CLAIM_FREE_FIRST 24
#endif

#ifdef PICO_SPINLOCK_ID_CLAIM_FREE_END
#warning PICO_SPINLOCK_ID_CLAIM_FREE_END has been renamed to PICO_SPINLOCK_ID_CLAIM_FREE_LAST
#endif

// PICO_CONFIG: PICO_SPINLOCK_ID_CLAIM_FREE_LAST, Highest Spinlock ID in the 'claim free' range, min=0, max=31, default=31, group=hardware_sync
#ifndef PICO_SPINLOCK_ID_CLAIM_FREE_LAST
#define PICO_SPINLOCK_ID_CLAIM_FREE_LAST 31
#endif

/*! \brief Insert a SEV instruction in to the code path.
 *  \ingroup hardware_sync

 * The SEV (send event) instruction sends an event to both cores.
 */
__force_inline static void __sev(void) {
    __asm volatile ("sev");
}

/*! \brief Insert a WFE instruction in to the code path.
 *  \ingroup hardware_sync
 *
 * The WFE (wait for event) instruction waits until one of a number of
 * events occurs, including events signalled by the SEV instruction on either core.
 */
__force_inline static void __wfe(void) {
    __asm volatile ("wfe");
}

/*! \brief Insert a WFI instruction in to the code path.
  *  \ingroup hardware_sync
*
 * The WFI (wait for interrupt) instruction waits for a interrupt to wake up the core.
 */
__force_inline static void __wfi(void) {
    __asm volatile ("wfi");
}

/*! \brief Insert a DMB instruction in to the code path.
 *  \ingroup hardware_sync
 *
 * The DMB (data memory barrier) acts as a memory barrier, all memory accesses prior to this
 * instruction will be observed before any explicit access after the instruction.
 */
__force_inline static void __dmb(void) {
    __asm volatile ("dmb" : : : "memory");
}

/*! \brief Insert a DSB instruction in to the code path.
 *  \ingroup hardware_sync
 *
 * The DSB (data synchronization barrier) acts as a special kind of data
 * memory barrier (DMB). The DSB operation completes when all explicit memory
 * accesses before this instruction complete.
 */
__force_inline static void __dsb(void) {
    __asm volatile ("dsb" : : : "memory");
}

/*! \brief Insert a ISB instruction in to the code path.
 *  \ingroup hardware_sync
 *
 * ISB acts as an instruction synchronization barrier. It flushes the pipeline of the processor,
 * so that all instructions following the ISB are fetched from cache or memory again, after
 * the ISB instruction has been completed.
 */
__force_inline static void __isb(void) {
    __asm volatile ("isb");
}

/*! \brief Acquire a memory fence
 *  \ingroup hardware_sync
 */
__force_inline static void __mem_fence_acquire(void) {
    // the original code below makes it hard for us to be included from C++ via a header
    // which itself is in an extern "C", so just use __dmb instead, which is what
    // is required on Cortex M0+
    __dmb();
//#ifndef __cplusplus
//    atomic_thread_fence(memory_order_acquire);
//#else
//    std::atomic_thread_fence(std::memory_order_acquire);
//#endif
}

/*! \brief Release a memory fence
 *  \ingroup hardware_sync
 *
 */
__force_inline static void __mem_fence_release(void) {
    // the original code below makes it hard for us to be included from C++ via a header
    // which itself is in an extern "C", so just use __dmb instead, which is what
    // is required on Cortex M0+
    __dmb();
//#ifndef __cplusplus
//    atomic_thread_fence(memory_order_release);
//#else
//    std::atomic_thread_fence(std::memory_order_release);
//#endif
}

/*! \brief Save and disable interrupts
 *  \ingroup hardware_sync
 *
 * \return The prior interrupt enable status for restoration later via restore_interrupts()
 */
__force_inline static uint32_t save_and_disable_interrupts(void) {
    uint32_t status;
    __asm volatile ("mrs %0, PRIMASK" : "=r" (status)::);
    __asm volatile ("cpsid i");
    return status;
}

/*! \brief Restore interrupts to a specified state
 *  \ingroup hardware_sync
 *
 * \param status Previous interrupt status from save_and_disable_interrupts()
  */
__force_inline static void restore_interrupts(uint32_t status) {
    __asm volatile ("msr PRIMASK,%0"::"r" (status) : );
}

/*! \brief Get HW Spinlock instance from number
 *  \ingroup hardware_sync
 *
 * \param lock_num Spinlock ID
 * \return The spinlock instance
 */
__force_inline static spin_lock_t *spin_lock_instance(uint lock_num) {
    invalid_params_if(SYNC, lock_num >= NUM_SPIN_LOCKS);
    return (spin_lock_t *) (SIO_BASE + SIO_SPINLOCK0_OFFSET + lock_num * 4);
}

/*! \brief Get HW Spinlock number from instance
 *  \ingroup hardware_sync
 *
 * \param lock The Spinlock instance
 * \return The Spinlock ID
 */
__force_inline static uint spin_lock_get_num(spin_lock_t *lock) {
    invalid_params_if(SYNC, (uint) lock < SIO_BASE + SIO_SPINLOCK0_OFFSET ||
                            (uint) lock >= NUM_SPIN_LOCKS * sizeof(spin_lock_t) + SIO_BASE + SIO_SPINLOCK0_OFFSET ||
                            ((uint) lock - SIO_BASE + SIO_SPINLOCK0_OFFSET) % sizeof(spin_lock_t) != 0);
    return (uint) (lock - (spin_lock_t *) (SIO_BASE + SIO_SPINLOCK0_OFFSET));
}

/*! \brief Acquire a spin lock without disabling interrupts (hence unsafe)
 *  \ingroup hardware_sync
 *
 * \param lock Spinlock instance
 */
__force_inline static void spin_lock_unsafe_blocking(spin_lock_t *lock) {
    // Note we don't do a wfe or anything, because by convention these spin_locks are VERY SHORT LIVED and NEVER BLOCK and run
    // with INTERRUPTS disabled (to ensure that)... therefore nothing on our core could be blocking us, so we just need to wait on another core
    // anyway which should be finished soon
    while (__builtin_expect(!*lock, 0));
    __mem_fence_acquire();
}

/*! \brief Release a spin lock without re-enabling interrupts
 *  \ingroup hardware_sync
 *
 * \param lock Spinlock instance
 */
__force_inline static void spin_unlock_unsafe(spin_lock_t *lock) {
    __mem_fence_release();
    *lock = 0;
}

/*! \brief Acquire a spin lock safely
 *  \ingroup hardware_sync
 *
 * This function will disable interrupts prior to acquiring the spinlock
 *
 * \param lock Spinlock instance
 * \return interrupt status to be used when unlocking, to restore to original state
 */
__force_inline static uint32_t spin_lock_blocking(spin_lock_t *lock) {
    uint32_t save = save_and_disable_interrupts();
    spin_lock_unsafe_blocking(lock);
    return save;
}

/*! \brief Check to see if a spinlock is currently acquired elsewhere.
 *  \ingroup hardware_sync
 *
 * \param lock Spinlock instance
 */
inline static bool is_spin_locked(spin_lock_t *lock) {
    check_hw_size(spin_lock_t, 4);
    uint lock_num = spin_lock_get_num(lock);
    return 0 != (*(io_ro_32 *) (SIO_BASE + SIO_SPINLOCK_ST_OFFSET) & (1u << lock_num));
}

/*! \brief Release a spin lock safely
 *  \ingroup hardware_sync
 *
 * This function will re-enable interrupts according to the parameters.
 *
 * \param lock Spinlock instance
 * \param saved_irq Return value from the \ref spin_lock_blocking() function.
 * \return interrupt status to be used when unlocking, to restore to original state
 *
 * \sa spin_lock_blocking()
 */
__force_inline static void spin_unlock(spin_lock_t *lock, uint32_t saved_irq) {
    spin_unlock_unsafe(lock);
    restore_interrupts(saved_irq);
}

/*! \brief Initialise a spin lock
 *  \ingroup hardware_sync
 *
 * The spin lock is initially unlocked
 *
 * \param lock_num The spin lock number
 * \return The spin lock instance
 */
spin_lock_t *spin_lock_init(uint lock_num);

/*! \brief Release all spin locks
 *  \ingroup hardware_sync
 */
void spin_locks_reset(void);

/*! \brief Return a spin lock number from the _striped_ range
 *  \ingroup hardware_sync
 *
 * Returns a spin lock number in the range PICO_SPINLOCK_ID_STRIPED_FIRST to PICO_SPINLOCK_ID_STRIPED_LAST
 * in a round robin fashion. This does not grant the caller exclusive access to the spin lock, so the caller
 * must:
 *
 * -# Abide (with other callers) by the contract of only holding this spin lock briefly (and with IRQs disabled - the default via \ref spin_lock_blocking()),
 * and not whilst holding other spin locks.
 * -# Be OK with any contention caused by the - brief due to the above requirement - contention with other possible users of the spin lock.
 *
 * \return lock_num a spin lock number the caller may use (non exclusively)
 * \see PICO_SPINLOCK_ID_STRIPED_FIRST
 * \see PICO_SPINLOCK_ID_STRIPED_LAST
 */
uint next_striped_spin_lock_num(void);

/*! \brief Mark a spin lock as used
 *  \ingroup hardware_sync
 *
 * Method for cooperative claiming of hardware. Will cause a panic if the spin lock
 * is already claimed. Use of this method by libraries detects accidental
 * configurations that would fail in unpredictable ways.
 *
 * \param lock_num the spin lock number
 */
void spin_lock_claim(uint lock_num);

/*! \brief Mark multiple spin locks as used
 *  \ingroup hardware_sync
 *
 * Method for cooperative claiming of hardware. Will cause a panic if any of the spin locks
 * are already claimed. Use of this method by libraries detects accidental
 * configurations that would fail in unpredictable ways.
 *
 * \param lock_num_mask Bitfield of all required spin locks to claim (bit 0 == spin lock 0, bit 1 == spin lock 1 etc)
 */
void spin_lock_claim_mask(uint32_t lock_num_mask);

/*! \brief Mark a spin lock as no longer used
 *  \ingroup hardware_sync
 *
 * Method for cooperative claiming of hardware.
 *
 * \param lock_num the spin lock number to release
 */
void spin_lock_unclaim(uint lock_num);

/*! \brief Claim a free spin lock
 *  \ingroup hardware_sync
 *
 * \param required if true the function will panic if none are available
 * \return the spin lock number or -1 if required was false, and none were free
 */
int spin_lock_claim_unused(bool required);

/*! \brief Determine if a spin lock is claimed
 *  \ingroup hardware_sync
 *
 * \param lock_num the spin lock number
 * \return true if claimed, false otherwise
 * \see spin_lock_claim
 * \see spin_lock_claim_mask
 */
bool spin_lock_is_claimed(uint lock_num);

#define remove_volatile_cast(t, x) ({__mem_fence_acquire(); (t)(x); })

static_assert(PICO_SPINLOCK_ID_STRIPED_LAST >= PICO_SPINLOCK_ID_STRIPED_FIRST, "");
static uint8_t striped_spin_lock_num = PICO_SPINLOCK_ID_STRIPED_FIRST;
static uint32_t claimed;

#ifdef __cplusplus
}
#endif

#endif

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
		//k_busy_wait(CONFIG_HWSPINLOCK_RPI_PICO_RELAX_TIME);
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



static void check_lock_num(uint __unused lock_num) {
    invalid_params_if(SYNC, lock_num >= 32);
}

void spin_locks_reset(void) {
    for (uint i = 0; i < NUM_SPIN_LOCKS; i++) {
        spin_unlock_unsafe(spin_lock_instance(i));
    }
}

spin_lock_t *spin_lock_init(uint lock_num) {
    assert(lock_num < NUM_SPIN_LOCKS);
    spin_lock_t *lock = spin_lock_instance(lock_num);
    spin_unlock_unsafe(lock);
    return lock;
}

uint next_striped_spin_lock_num() {
    uint rc = striped_spin_lock_num++;
    if (striped_spin_lock_num > PICO_SPINLOCK_ID_STRIPED_LAST) {
        striped_spin_lock_num = PICO_SPINLOCK_ID_STRIPED_FIRST;
    }
    return rc;
}

void spin_lock_claim(uint lock_num) {
    check_lock_num(lock_num);
    hw_claim_or_assert((uint8_t *) &claimed, lock_num, "Spinlock %d is already claimed");
}

void spin_lock_claim_mask(uint32_t mask) {
    for(uint i = 0; mask; i++, mask >>= 1u) {
        if (mask & 1u) spin_lock_claim(i);
    }
}

void spin_lock_unclaim(uint lock_num) {
    check_lock_num(lock_num);
    spin_unlock_unsafe(spin_lock_instance(lock_num));
    hw_claim_clear((uint8_t *) &claimed, lock_num);
}

int spin_lock_claim_unused(bool required) {
    return hw_claim_unused_from_range((uint8_t*)&claimed, required, PICO_SPINLOCK_ID_CLAIM_FREE_FIRST, PICO_SPINLOCK_ID_CLAIM_FREE_LAST, "No spinlocks are available");
}

bool spin_lock_is_claimed(uint lock_num) {
    check_lock_num(lock_num);
    return hw_is_claimed((uint8_t *) &claimed, lock_num);
}

DT_INST_FOREACH_STATUS_OKAY(HWSPINLOCK_RPI_PICO_INIT);
