/*
 * Copyright (c) 2024 Junho Lee <junho@tsnlab.com>
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_GPIO_GPIO_RP1_HAL_H_
#define ZEPHYR_DRIVERS_GPIO_GPIO_RP1_HAL_H_

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_rp1.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/drivers/interrupt_controller/gic.h>
#include <zephyr/drivers/pcie/cap.h>
#include <zephyr/drivers/pcie/controller.h>
#include <zephyr/drivers/pcie/msi.h>
#include <zephyr/irq.h>

#include "gpio_rpi_pico.h"

#define RP1_ATOMIC_RAW_OFF 0x0000
#define RP1_ATOMIC_XOR_OFF 0x1000
#define RP1_ATOMIC_SET_OFF 0x2000
#define RP1_ATOMIC_CLR_OFF 0x3000

#define GPIO_STATUS_ADDR(port, n) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x8 * n)
#define GPIO_CTRL_ADDR(port, n)   (GPIO_STATUS_ADDR(port, n) + 0x4)
#define PADS_CTRL_ADDR(port, n)   (DEVICE_MMIO_NAMED_GET(port, pads) + 0x4 * (n))

/*
 * Interrupt summary registers of the PCIE destination, which is the
 * one wired towards the host on the Raspberry Pi 5. The registers at
 * 0x100-0x118 are the raw INTR and the PROC0/PROC1 destinations.
 */
#define GPIO_INTE_ADDR(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x11c)
#define GPIO_INTF_ADDR(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x120)
#define GPIO_INTS_ADDR(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x124)
#define RIO_OUT_ADDR(port)   (DEVICE_MMIO_NAMED_GET(port, rio) + 0x0)
#define RIO_OE_ADDR(port)    (DEVICE_MMIO_NAMED_GET(port, rio) + 0x4)
#define RIO_IN_ADDR(port)    (DEVICE_MMIO_NAMED_GET(port, rio) + 0x8)

#define GPIO_CTRL(port, n)          sys_read32(GPIO_CTRL_ADDR(port, n))
#define GPIO_CTRL_RAW(port, n, val) sys_write32(val, GPIO_CTRL_ADDR(port, n) + RP1_ATOMIC_RAW_OFF)
#define GPIO_CTRL_XOR(port, n, val) sys_write32(val, GPIO_CTRL_ADDR(port, n) + RP1_ATOMIC_XOR_OFF)
#define GPIO_CTRL_SET(port, n, val) sys_write32(val, GPIO_CTRL_ADDR(port, n) + RP1_ATOMIC_SET_OFF)
#define GPIO_CTRL_CLR(port, n, val) sys_write32(val, GPIO_CTRL_ADDR(port, n) + RP1_ATOMIC_CLR_OFF)
#define PADS_CTRL(port, n)          sys_read32(PADS_CTRL_ADDR(port, n))
#define PADS_CTRL_RAW(port, n, val) sys_write32(val, PADS_CTRL_ADDR(port, n) + RP1_ATOMIC_RAW_OFF)
#define PADS_CTRL_XOR(port, n, val) sys_write32(val, PADS_CTRL_ADDR(port, n) + RP1_ATOMIC_XOR_OFF)
#define PADS_CTRL_SET(port, n, val) sys_write32(val, PADS_CTRL_ADDR(port, n) + RP1_ATOMIC_SET_OFF)
#define PADS_CTRL_CLR(port, n, val) sys_write32(val, PADS_CTRL_ADDR(port, n) + RP1_ATOMIC_CLR_OFF)

#define GPIO_INTE(port)          sys_read32(GPIO_INTE_ADDR(port))
#define GPIO_INTE_RAW(port, val) sys_write32(val, GPIO_INTE_ADDR(port) + RP1_ATOMIC_RAW_OFF)
#define GPIO_INTE_XOR(port, val) sys_write32(val, GPIO_INTE_ADDR(port) + RP1_ATOMIC_XOR_OFF)
#define GPIO_INTE_SET(port, val) sys_write32(val, GPIO_INTE_ADDR(port) + RP1_ATOMIC_SET_OFF)
#define GPIO_INTE_CLR(port, val) sys_write32(val, GPIO_INTE_ADDR(port) + RP1_ATOMIC_CLR_OFF)
#define GPIO_INTF(port)          sys_read32(GPIO_INTF_ADDR(port))
#define GPIO_INTF_RAW(port, val) sys_write32(val, GPIO_INTF_ADDR(port) + RP1_ATOMIC_RAW_OFF)
#define GPIO_INTF_XOR(port, val) sys_write32(val, GPIO_INTF_ADDR(port) + RP1_ATOMIC_XOR_OFF)
#define GPIO_INTF_SET(port, val) sys_write32(val, GPIO_INTF_ADDR(port) + RP1_ATOMIC_SET_OFF)
#define GPIO_INTF_CLR(port, val) sys_write32(val, GPIO_INTF_ADDR(port) + RP1_ATOMIC_CLR_OFF)
#define GPIO_INTS(port)          sys_read32(GPIO_INTS_ADDR(port))
#define RIO_OUT(port)            sys_read32(RIO_OUT_ADDR(port))
#define RIO_OUT_RAW(port, val)   sys_write32(val, RIO_OUT_ADDR(port) + RP1_ATOMIC_RAW_OFF)
#define RIO_OUT_XOR(port, val)   sys_write32(val, RIO_OUT_ADDR(port) + RP1_ATOMIC_XOR_OFF)
#define RIO_OUT_SET(port, val)   sys_write32(val, RIO_OUT_ADDR(port) + RP1_ATOMIC_SET_OFF)
#define RIO_OUT_CLR(port, val)   sys_write32(val, RIO_OUT_ADDR(port) + RP1_ATOMIC_CLR_OFF)
#define RIO_OE(port)             sys_read32(RIO_OE_ADDR(port))
#define RIO_OE_RAW(port, val)    sys_write32(val, RIO_OE_ADDR(port) + RP1_ATOMIC_RAW_OFF)
#define RIO_OE_XOR(port, val)    sys_write32(val, RIO_OE_ADDR(port) + RP1_ATOMIC_XOR_OFF)
#define RIO_OE_SET(port, val)    sys_write32(val, RIO_OE_ADDR(port) + RP1_ATOMIC_SET_OFF)
#define RIO_OE_CLR(port, val)    sys_write32(val, RIO_OE_ADDR(port) + RP1_ATOMIC_CLR_OFF)
#define RIO_IN(port)             sys_read32(RIO_IN_ADDR(port))

#define GPIO_CTRL_FUNCSEL_RIO 0x5
#define GPIO_FUNC_SIO         0x5
#define GPIO_IN               0
#define GPIO_OUT              1

#define NUM_BANK0_GPIOS        28
#define GPIO_RPI_PINS_PER_PORT 28
#define GPIO_CTRL_IRQMASK_ALL                                                                      \
	(GPIO_CTRL_IRQMASK_EDGE_LOW_MASK | GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK |                      \
	 GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK | GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK |                    \
	 GPIO_CTRL_IRQMASK_F_EDGE_LOW_MASK | GPIO_CTRL_IRQMASK_F_EDGE_HIGH_MASK |                  \
	 GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_MASK | GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_MASK)

/* The leading bank (child index 0) is the low node; later banks are high nodes. */
#define GPIO_RPI_CHILD_IDX_0             1
#define IS_GPIO_RPI_LO_NODE(node_id) UTIL_CAT(GPIO_RPI_CHILD_IDX_, DT_NODE_CHILD_IDX(node_id))

enum {
	GPIO_IRQ_LEVEL_LOW = 0x1u,
	GPIO_IRQ_LEVEL_HIGH = 0x2u,
	GPIO_IRQ_EDGE_FALL = 0x4u,
	GPIO_IRQ_EDGE_RISE = 0x8u,
};

#define ALL_EVENTS                                                                                 \
	(GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE | GPIO_IRQ_LEVEL_LOW | GPIO_IRQ_LEVEL_HIGH)

typedef unsigned int uint;

static const struct device *rp1_port0 =
	DEVICE_DT_GET(DT_COMPAT_GET_ANY_STATUS_OKAY(raspberrypi_rp1_gpio));

/*
 * The RP1 IO bank interrupts reach the host as PCIe MSI-X messages. The
 * bank interrupt is wired to a fixed MSI-X vector, whose message is
 * delivered to the BCM2712 MIP and converted into a GIC SPI. The setup
 * below programs that vector and enables the capability on the endpoint.
 *
 * The MSI-X glue lives in the RP1 PCIE APB block, which uses the same
 * atomic SET/CLR aliasing as the IO banks but at +0x800/+0xc00. Bank 0
 * is hardwired to MSI-X vector 0, which is the only bank wired for
 * interrupts here.
 */
#define RP1_GPIO_MSIX_VECTOR 0

#define RP1_PCIE_SET_OFFSET       0x800
#define RP1_PCIE_CLR_OFFSET       0xc00
#define RP1_PCIE_MSIX_CFG(base, v) ((base) + 0x8 + 0x4 * (v))
#define RP1_PCIE_MSIX_CFG_IACK_EN 0x8
#define RP1_PCIE_MSIX_CFG_IACK    0x4
#define RP1_PCIE_MSIX_CFG_ENABLE  0x1

#define RP1_PERIPH_BAR           1
#define RP1_PERIPH_BUS_BASE      0x40000000UL
#define RP1_PCIE_CFG_BUS_BASE    0x40108000UL
#define RP1_PCIE_CFG_BAR_OFFSET  (RP1_PCIE_CFG_BUS_BASE - RP1_PERIPH_BUS_BASE)
#define RP1_PCIE_CFG_MAP_SIZE    0x1000

#define RP1_GPIO_NODE     DT_COMPAT_GET_ANY_STATUS_OKAY(raspberrypi_rp1_gpio)
#define RP1_PINCTRL_NODE  DT_PARENT(RP1_GPIO_NODE)
#define RP1_PCIE_NODE     DT_GPARENT(RP1_PINCTRL_NODE)
#define RP1_MSI_PARENT    DT_PHANDLE(RP1_PCIE_NODE, msi_parent)

/* The MSI data is the offset of the bank's GIC SPI from the MIP's SPI base */
#define RP1_GPIO_MSIX_MSG_ADDR DT_REG_ADDR_BY_IDX(RP1_MSI_PARENT, 1)
#define RP1_GPIO_MSIX_MSG_DATA                                                                      \
	(DT_IRQN(RP1_PINCTRL_NODE) - GIC_SPI_INT_BASE -                                             \
	 DT_PROP(RP1_MSI_PARENT, brcm_msi_base_spi))

/* Mapped base of the PCIE APB block, retained for IACK on each interrupt */
static mm_reg_t rp1_msix_apb_base;

static inline bool rp1_pcie_get_bar_phys(const struct device *pcie, unsigned int bar,
					 uintptr_t *phys)
{
	unsigned int reg = PCIE_CONF_BAR0 + bar;
	uintptr_t bar_addr;
	uint32_t val;

	if (reg > PCIE_CONF_BAR5) {
		return false;
	}

	val = pcie_ctrl_conf_read(pcie, 0, reg);
	if (PCIE_CONF_BAR_INVAL_FLAGS(val) || !PCIE_CONF_BAR_MEM(val)) {
		return false;
	}

	bar_addr = PCIE_CONF_BAR_ADDR(val);
	if (IS_ENABLED(CONFIG_64BIT) && PCIE_CONF_BAR_64(val)) {
		bar_addr |= (uint64_t)pcie_ctrl_conf_read(pcie, 0, reg + 1) << 32;
	}

	return pcie_ctrl_region_translate(pcie, 0, true, PCIE_CONF_BAR_64(val), bar_addr, phys);
}

static inline bool gpio_is_pulled_up(uint pin)
{
	return (PADS_CTRL(rp1_port0, pin) & GPIO_PADS_PULL_UP_ENABLE_MASK) != 0;
}

static inline bool gpio_is_pulled_down(uint pin)
{
	return (PADS_CTRL(rp1_port0, pin) & GPIO_PADS_PULL_DOWN_ENABLE_MASK) != 0;
}

static inline uint32_t gpio_get_irq_event_mask(uint pin)
{
	if (GPIO_INTS(rp1_port0) & BIT(pin)) {
		uint32_t ctrl = GPIO_CTRL(rp1_port0, pin);
		uint32_t events = 0;

		if (ctrl & GPIO_CTRL_IRQMASK_EDGE_LOW_MASK) {
			events |= GPIO_IRQ_EDGE_FALL;
		}
		if (ctrl & GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK) {
			events |= GPIO_IRQ_EDGE_RISE;
		}
		if (ctrl & GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK) {
			events |= GPIO_IRQ_LEVEL_LOW;
		}
		if (ctrl & GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK) {
			events |= GPIO_IRQ_LEVEL_HIGH;
		}

		return events;
	}

	return 0;
}

static inline void gpio_acknowledge_irq(uint pin, uint32_t event_mask)
{
	(void)event_mask;
	/* Clear the latched edge events */
	GPIO_CTRL_SET(rp1_port0, pin, GPIO_CTRL_IRQRESET_MASK);

	/*
	 * Acknowledge the MSI-X vector. The bank interrupt is level-triggered,
	 * so this re-sends the (edge) MSI if the bank is still asserting once
	 * the source has been serviced.
	 */
	if (rp1_msix_apb_base != 0) {
		sys_write32(RP1_PCIE_MSIX_CFG_IACK,
			    RP1_PCIE_MSIX_CFG(rp1_msix_apb_base + RP1_PCIE_SET_OFFSET,
					      RP1_GPIO_MSIX_VECTOR));
	}
}

static inline void gpio_set_mask_n(uint n, uint32_t mask)
{
	RIO_OUT_SET(rp1_port0, mask);
}

static inline void gpio_clr_mask_n(uint n, uint32_t mask)
{
	RIO_OUT_CLR(rp1_port0, mask);
}

static inline void gpio_xor_mask_n(uint n, uint32_t mask)
{
	RIO_OUT_XOR(rp1_port0, mask);
}

static inline void gpio_put_masked_n(uint n, uint32_t mask, uint32_t value)
{
	RIO_OUT_XOR(rp1_port0, (RIO_OUT(rp1_port0) ^ value) & mask);
}

static inline void gpio_put(uint pin, bool value)
{
	if (value) {
		RIO_OUT_SET(rp1_port0, BIT(pin));
	} else {
		RIO_OUT_CLR(rp1_port0, BIT(pin));
	}
}

static inline bool gpio_get_out_level(uint pin)
{
	return !!(RIO_OUT(rp1_port0) & BIT(pin));
}

static inline void gpio_set_dir(uint pin, bool value)
{
	if (value == GPIO_OUT) {
		RIO_OE_SET(rp1_port0, BIT(pin));
	} else {
		RIO_OE_CLR(rp1_port0, BIT(pin));
	}
}

static inline bool gpio_get_dir(uint pin)
{
	return !!(RIO_OE(rp1_port0) & BIT(pin));
}

static inline void gpio_set_function(uint pin, uint32_t fn)
{
	uint32_t ctrl = GPIO_CTRL(rp1_port0, pin) & GPIO_CTRL_RESERVED_MASK;

	PADS_CTRL_SET(rp1_port0, pin, GPIO_PADS_INPUT_ENABLE_MASK);
	PADS_CTRL_CLR(rp1_port0, pin, GPIO_PADS_OUTPUT_DISABLE_MASK);

	ctrl |= (fn << GPIO_CTRL_FUNCSEL_SHIFT) & GPIO_CTRL_FUNCSEL_MASK;
	GPIO_CTRL_RAW(rp1_port0, pin, ctrl);
}

static inline void gpio_set_pulls(uint pin, bool up, bool down)
{
	PADS_CTRL_XOR(rp1_port0, pin,
		      (PADS_CTRL(rp1_port0, pin) ^ ((up << GPIO_PADS_PULL_UP_ENABLE_SHIFT) |
						    (down << GPIO_PADS_PULL_DOWN_ENABLE_SHIFT))) &
			      (GPIO_PADS_PULL_UP_ENABLE_MASK | GPIO_PADS_PULL_DOWN_ENABLE_MASK));
}

static inline void gpio_set_input_enabled(uint pin, bool enabled)
{
	if (enabled) {
		PADS_CTRL_SET(rp1_port0, pin, GPIO_PADS_INPUT_ENABLE_MASK);
	} else {
		PADS_CTRL_CLR(rp1_port0, pin, GPIO_PADS_INPUT_ENABLE_MASK);
	}
}

static inline int gpio_set_irq_enabled(uint pin, uint32_t events, bool value)
{
	uint32_t ctrl_events = 0;

	if (events & GPIO_IRQ_EDGE_FALL) {
		ctrl_events |= GPIO_CTRL_IRQMASK_EDGE_LOW_MASK;
	}
	if (events & GPIO_IRQ_EDGE_RISE) {
		ctrl_events |= GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK;
	}
	if (events & GPIO_IRQ_LEVEL_LOW) {
		ctrl_events |= GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK;
	}
	if (events & GPIO_IRQ_LEVEL_HIGH) {
		ctrl_events |= GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK;
	}

	if (value) {
		GPIO_CTRL_SET(rp1_port0, pin, GPIO_CTRL_IRQRESET_MASK);
		GPIO_CTRL_SET(rp1_port0, pin, ctrl_events);
		GPIO_INTE_SET(rp1_port0, BIT(pin));
	} else {
		GPIO_INTE_CLR(rp1_port0, BIT(pin));
		GPIO_CTRL_CLR(rp1_port0, pin, GPIO_CTRL_IRQMASK_ALL);
		GPIO_CTRL_SET(rp1_port0, pin, GPIO_CTRL_IRQRESET_MASK);
	}

	return 0;
}

static inline void gpio_set_dir_out_masked_n(uint n, uint32_t mask)
{
	RIO_OE_SET(rp1_port0, mask);
}

static inline void gpio_set_dir_in_masked_n(uint n, uint32_t mask)
{
	RIO_OE_CLR(rp1_port0, mask);
}

static inline void gpio_set_dir_masked_n(uint n, uint32_t mask, uint32_t value)
{
	(void)n;
	RIO_OE_XOR(rp1_port0, (RIO_OE(rp1_port0) ^ value) & mask);
}

static inline uint32_t gpio_get_all_n(uint n)
{
	(void)n;
	return RIO_IN(rp1_port0);
}

static inline void gpio_toggle_dir_masked_n(uint n, uint32_t mask)
{
	(void)n;
	RIO_OE_XOR(rp1_port0, mask);
}

static inline uint32_t gpio_get_dir_all_bits_n(uint n)
{
	(void)n;
	return RIO_OE(rp1_port0);
}

static inline bool gpio_is_input_enabled(uint pin)
{
	return !!(PADS_CTRL(rp1_port0, pin) & GPIO_PADS_INPUT_ENABLE_MASK);
}

static inline bool gpio_is_output_disabled(uint pin)
{
	return !!(PADS_CTRL(rp1_port0, pin) & GPIO_PADS_OUTPUT_DISABLE_MASK);
}

static inline void gpio_set_input_enabled_output_disabled(uint pin, bool ie, bool od)
{
	PADS_CTRL_XOR(rp1_port0, pin,
		      (PADS_CTRL(rp1_port0, pin) ^ ((ie << GPIO_PADS_INPUT_ENABLE_SHIFT) |
						    (od << GPIO_PADS_OUTPUT_DISABLE_SHIFT))) &
			      (GPIO_PADS_INPUT_ENABLE_MASK | GPIO_PADS_OUTPUT_DISABLE_MASK));
}

static inline bool gpio_has_pending_irq()
{
	return !!GPIO_INTS(rp1_port0);
}

/*
 * Route the bank 0 interrupt to the host: program the RP1 MSI-X table
 * entry of the bank's vector with the message that makes the MIP raise
 * the bank's GIC SPI, enable the capability on the endpoint, and enable
 * the vector. Called once, from the bank 0 init, after the PCIe root
 * complex has assigned the RP1 endpoint's BARs.
 */
static inline int gpio_rpi_hal_irq_setup(void)
{
	const struct device *pcie = DEVICE_DT_GET(RP1_PCIE_NODE);
	mm_reg_t table_base;
	mem_addr_t entry;
	uintptr_t apbs_phys;
	uintptr_t table_phys;
	unsigned int ptr;
	uint32_t reg;
	int cap_count = 0;

	if (!device_is_ready(pcie)) {
		return -ENODEV;
	}

	/*
	 * Find the MSI-X capability of the RP1 endpoint. BDF 0 is used since
	 * the controller reaches the single endpoint behind the root complex
	 * through the EXT_CFG window with index 0, the same way its init
	 * assigns the endpoint BARs.
	 */
	reg = pcie_ctrl_conf_read(pcie, 0, PCIE_CONF_CAPPTR);
	ptr = PCIE_CONF_CAPPTR_FIRST(reg);
	while (ptr != 0 && cap_count++ < 32) {
		reg = pcie_ctrl_conf_read(pcie, 0, ptr);
		if (PCIE_CONF_CAP_ID(reg) == PCI_CAP_ID_MSIX) {
			break;
		}
		ptr = PCIE_CONF_CAP_NEXT(reg);
	}

	if (ptr == 0) {
		return -ENOTSUP;
	}

	/* Map the MSI-X table from the BAR indicated by the endpoint capability. */
	reg = pcie_ctrl_conf_read(pcie, 0, ptr + 1);
	if (!rp1_pcie_get_bar_phys(pcie, reg & PCIE_MSIX_TR_BIR, &table_phys)) {
		return -ENOTSUP;
	}

	if (!rp1_pcie_get_bar_phys(pcie, RP1_PERIPH_BAR, &apbs_phys)) {
		return -ENOTSUP;
	}

	device_map(&rp1_msix_apb_base, apbs_phys + RP1_PCIE_CFG_BAR_OFFSET,
		   RP1_PCIE_CFG_MAP_SIZE, K_MEM_CACHE_NONE);
	device_map(&table_base,
		   table_phys + (reg & PCIE_MSIX_TR_OFFSET) +
			   RP1_GPIO_MSIX_VECTOR * PCIE_MSIR_TABLE_ENTRY_SIZE,
		   PCIE_MSIR_TABLE_ENTRY_SIZE, K_MEM_CACHE_NONE);

	entry = table_base;
	sys_write32((uint32_t)RP1_GPIO_MSIX_MSG_ADDR, entry + PCIE_VTBL_MA);
	sys_write32((uint32_t)((uint64_t)RP1_GPIO_MSIX_MSG_ADDR >> 32),
		    entry + PCIE_VTBL_MUA);
	sys_write32(RP1_GPIO_MSIX_MSG_DATA, entry + PCIE_VTBL_MD);
	sys_write32(0, entry + PCIE_VTBL_VCTRL);

	/* Enable the MSI-X capability and clear the function mask */
	reg = pcie_ctrl_conf_read(pcie, 0, ptr);
	reg |= PCIE_MSIX_MCR_EN;
	reg &= ~PCIE_MSIX_MCR_FMASK;
	pcie_ctrl_conf_write(pcie, 0, ptr, reg);

	/*
	 * Enable the vector with the IACK mechanism, which re-sends the MSI
	 * on acknowledge while the (level-triggered) bank is still pending.
	 */
	sys_write32(RP1_PCIE_MSIX_CFG_ENABLE | RP1_PCIE_MSIX_CFG_IACK_EN,
		    RP1_PCIE_MSIX_CFG(rp1_msix_apb_base + RP1_PCIE_SET_OFFSET,
				      RP1_GPIO_MSIX_VECTOR));

	return 0;
}

#endif /* ZEPHYR_DRIVERS_GPIO_GPIO_RP1_HAL_H_ */
