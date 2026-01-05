/*
 * RP1 Interrupt Trigger Functions
 *
 * Based on RP1 Peripherals documentation:
 * https://datasheets.raspberrypi.com/rp1/rp1-peripherals.pdf
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP1_IRQ_TRIGGER_H
#define RP1_IRQ_TRIGGER_H

#include <zephyr/kernel.h>
#include <zephyr/drivers/pcie/pcie.h>
#include <zephyr/sys/sys_io.h>

/*
 * RP1 PCIe configuration registers (APBS block)
 * Base offset from RP1 BAR (typically BAR1) for the PCIe APBS window.
 */
#ifndef RP1_PCIE_CFG_BASE_OFFSET
#define RP1_PCIE_CFG_BASE_OFFSET 0x00000000U
#endif

#ifndef RP1_CFG_USE_PCIE_CFG
#define RP1_CFG_USE_PCIE_CFG 0U
#endif

#ifndef RP1_PCIE_APBS_BASE
#define RP1_PCIE_APBS_BASE 0x00108000U
#endif

#ifndef RP1_PCIE_REG_SET_OFFSET
#define RP1_PCIE_REG_SET_OFFSET 0x00000800U
#endif

#ifndef RP1_PCIE_REG_CLR_OFFSET
#define RP1_PCIE_REG_CLR_OFFSET 0x00000c00U
#endif

#ifndef RP1_USE_APBS_SETCLR
#define RP1_USE_APBS_SETCLR 1U
#endif

#if RP1_CFG_USE_PCIE_CFG
#define RP1_PCIE_CFG_BASE   0x00000000U
#define RP1_MSIX_CFG(n)     (RP1_PCIE_CFG_BASE + 0x8c + ((n) * 4))
#define RP1_PCIE_INTSTATL   (RP1_PCIE_CFG_BASE + 0x0050)
#define RP1_PCIE_INTSTATH   (RP1_PCIE_CFG_BASE + 0x0054)
#else
#define RP1_PCIE_CFG_BASE   (RP1_PCIE_APBS_BASE + RP1_PCIE_CFG_BASE_OFFSET)
#define RP1_MSIX_CFG(n)     (RP1_PCIE_CFG_BASE + 0x8 + ((n) * 4))
#define RP1_PCIE_INTSTATL   (RP1_PCIE_CFG_BASE + 0x0050)
#define RP1_PCIE_INTSTATH   (RP1_PCIE_CFG_BASE + 0x0054)
#endif

/* MSI-X CFG bits */
#define RP1_MSIX_CFG_ENABLE  BIT(0)
#define RP1_MSIX_CFG_TEST    BIT(8)   /* Software test bit - ORed with interrupt source */
#define RP1_MSIX_CFG_IACK_EN BIT(16)  /* IACK enable */

/*
 * GPIO Interrupt registers (Section 5.3.4)
 * These are for GPIO bank interrupts to host via PCIe
 */
#define RP1_IO_BANK0_BASE   0xd0000  /* From DT: reg = <0x1f 0xd0000 0x4000> */
#define RP1_IO_BANK1_BASE   0xe0000
#define RP1_IO_BANK2_BASE   0xf0000

#ifndef RP1_GPIO_PCIE_INTE_OFFSET
#define RP1_GPIO_PCIE_INTE_OFFSET 0x128U
#endif
#ifndef RP1_GPIO_PCIE_INTF_OFFSET
#define RP1_GPIO_PCIE_INTF_OFFSET 0x12cU
#endif
#ifndef RP1_GPIO_PCIE_INTS_OFFSET
#define RP1_GPIO_PCIE_INTS_OFFSET 0x130U
#endif

/* Per-bank interrupt registers for PCIe (host) direction */
#define RP1_GPIO_PCIE_INTE(bank, n)  ((bank) + RP1_GPIO_PCIE_INTE_OFFSET + ((n) * 0x30))
#define RP1_GPIO_PCIE_INTF(bank, n)  ((bank) + RP1_GPIO_PCIE_INTF_OFFSET + ((n) * 0x30))
#define RP1_GPIO_PCIE_INTS(bank, n)  ((bank) + RP1_GPIO_PCIE_INTS_OFFSET + ((n) * 0x30))

struct rp1_trigger {
	uintptr_t rp1_base;  /* RP1 MMIO base (from BAR, mapped) */
	pcie_bdf_t bdf;
	bool use_cfg;
	bool initialized;
};

/**
 * @brief Initialize RP1 trigger (map base address)
 */
static inline bool rp1_trigger_init(struct rp1_trigger *trig, uintptr_t rp1_base)
{
	if (!rp1_base) {
		printk("ERROR: Invalid RP1 base address\n");
		return false;
	}
	
	trig->rp1_base = rp1_base;
	trig->use_cfg = false;
	trig->initialized = true;
	
	printk("RP1 trigger initialized with base 0x%llx\n",
	       (unsigned long long)rp1_base);
	return true;
}

/**
 * @brief Initialize RP1 trigger using PCIe config space
 */
static inline bool rp1_trigger_init_cfg(struct rp1_trigger *trig, pcie_bdf_t bdf)
{
	trig->bdf = bdf;
	trig->use_cfg = true;
	trig->initialized = true;

	printk("RP1 trigger initialized with PCIe config access (BDF 0x%08x)\n", bdf);
	return true;
}

static inline uint32_t rp1_cfg_read(const struct rp1_trigger *trig, uint32_t off)
{
	if (trig->use_cfg) {
		return pcie_conf_read(trig->bdf, off / 4U);
	}

	return sys_read32(trig->rp1_base + off);
}

static inline void rp1_cfg_write(const struct rp1_trigger *trig,
				 uint32_t off, uint32_t val)
{
	if (trig->use_cfg) {
		pcie_conf_write(trig->bdf, off / 4U, val);
		return;
	}

	sys_write32(val, trig->rp1_base + off);
}

static inline void rp1_cfg_set(const struct rp1_trigger *trig,
			       uint32_t off, uint32_t mask)
{
	if (trig->use_cfg) {
		uint32_t val = rp1_cfg_read(trig, off);
		rp1_cfg_write(trig, off, val | mask);
		return;
	}

#if RP1_USE_APBS_SETCLR
	sys_write32(mask, trig->rp1_base + off + RP1_PCIE_REG_SET_OFFSET);
#else
	uint32_t val = rp1_cfg_read(trig, off);
	sys_write32(val | mask, trig->rp1_base + off);
#endif
}

static inline void rp1_cfg_clear(const struct rp1_trigger *trig,
				 uint32_t off, uint32_t mask)
{
	if (trig->use_cfg) {
		uint32_t val = rp1_cfg_read(trig, off);
		rp1_cfg_write(trig, off, val & ~mask);
		return;
	}

#if RP1_USE_APBS_SETCLR
	sys_write32(mask, trig->rp1_base + off + RP1_PCIE_REG_CLR_OFFSET);
#else
	uint32_t val = rp1_cfg_read(trig, off);
	sys_write32(val & ~mask, trig->rp1_base + off);
#endif
}

/**
 * @brief Trigger interrupt via MSI-X TEST bit
 * 
 * This uses the MSI-X CFG TEST bit which ORs with the actual interrupt source.
 * Most reliable way to generate a test interrupt.
 */
static inline void rp1_trigger_msix_test(struct rp1_trigger *trig, uint32_t vector)
{
	if (!trig->initialized) {
		printk("ERROR: Trigger not initialized\n");
		return;
	}
	
	printk("\n>>> Triggering RP1 interrupt via MSI-X TEST (vector %u) <<<\n", vector);
	
	/* Read current config */
	uint32_t cfg = rp1_cfg_read(trig, RP1_MSIX_CFG(vector));
	printk("  Current MSIX_CFG[%u]: 0x%08x\n", vector, cfg);
	
	/* Set ENABLE, TEST, and IACK_EN for clean edge delivery */
	rp1_cfg_set(trig, RP1_MSIX_CFG(vector),
		    RP1_MSIX_CFG_ENABLE | RP1_MSIX_CFG_TEST | RP1_MSIX_CFG_IACK_EN);
	
	printk("  New MSIX_CFG[%u]: 0x%08x\n", vector,
	       rp1_cfg_read(trig, RP1_MSIX_CFG(vector)));
	printk(">>> Interrupt should now be triggered <<<\n\n");
}

/**
 * @brief Trigger MSI-X TEST bit without console output (for sweeps)
 */
static inline void rp1_trigger_msix_test_quiet(struct rp1_trigger *trig, uint32_t vector)
{
	if (!trig->initialized) {
		return;
	}

	rp1_cfg_set(trig, RP1_MSIX_CFG(vector),
		    RP1_MSIX_CFG_ENABLE | RP1_MSIX_CFG_TEST | RP1_MSIX_CFG_IACK_EN);
}

/**
 * @brief Clear MSI-X TEST bit
 */
static inline void rp1_clear_msix_test(struct rp1_trigger *trig, uint32_t vector)
{
	if (!trig->initialized) {
		return;
	}
	
	rp1_cfg_clear(trig, RP1_MSIX_CFG(vector), RP1_MSIX_CFG_TEST);
	
	printk("Cleared MSI-X TEST bit for vector %u\n", vector);
}

/**
 * @brief Clear MSI-X TEST bit without console output (for sweeps)
 */
static inline void rp1_clear_msix_test_quiet(struct rp1_trigger *trig, uint32_t vector)
{
	if (!trig->initialized) {
		return;
	}

	rp1_cfg_clear(trig, RP1_MSIX_CFG(vector), RP1_MSIX_CFG_TEST);
}

/**
 * @brief Read RP1 PCIe interrupt status
 */
static inline void rp1_read_intstatus(struct rp1_trigger *trig)
{
	if (!trig->initialized) {
		return;
	}
	
	uint32_t sl = rp1_cfg_read(trig, RP1_PCIE_INTSTATL);
	uint32_t sh = rp1_cfg_read(trig, RP1_PCIE_INTSTATH);
	
	printk("\n=== RP1 PCIe INTSTAT ===\n");
	printk("  INTSTATL [31:0]:  0x%08x\n", sl);
	printk("  INTSTATH [63:32]: 0x%08x\n", sh);
	
	if (sl || sh) {
		printk("  Active interrupt sources:\n");
		for (int i = 0; i < 32; i++) {
			if (sl & BIT(i)) {
				printk("    - Bit %d\n", i);
			}
		}
		for (int i = 0; i < 32; i++) {
			if (sh & BIT(i)) {
				printk("    - Bit %d\n", i + 32);
			}
		}
	}
	printk("=== End INTSTAT ===\n\n");
}

/**
 * @brief Trigger GPIO interrupt via FORCE (alternative method)
 * 
 * Note: Requires GPIO to be configured and enabled.
 * Less reliable for initial testing than MSI-X TEST.
 */
static inline void rp1_trigger_gpio_force(struct rp1_trigger *trig,
					  uint32_t bank,
					  uint32_t gpio)
{
	if (!trig->initialized) {
		return;
	}

	if (trig->use_cfg) {
		printk("GPIO force requires MMIO base, not PCIe config access\n");
		return;
	}
	
	uint64_t bank_base;
	switch (bank) {
	case 0:
		bank_base = RP1_IO_BANK0_BASE;
		break;
	case 1:
		bank_base = RP1_IO_BANK1_BASE;
		break;
	case 2:
		bank_base = RP1_IO_BANK2_BASE;
		break;
	default:
		printk("Invalid GPIO bank %u\n", bank);
		return;
	}
	
	uint32_t reg = gpio / 8;  /* 4 interrupts per register */
	uint32_t shift = (gpio % 8) * 4;
	uint32_t mask = (0xfU << shift);
	
	uintptr_t inte = trig->rp1_base +
		RP1_GPIO_PCIE_INTE(bank_base, reg);
	uintptr_t intf = trig->rp1_base +
		RP1_GPIO_PCIE_INTF(bank_base, reg);
	uintptr_t ints = trig->rp1_base +
		RP1_GPIO_PCIE_INTS(bank_base, reg);
	
	printk("Forcing GPIO interrupt: bank %u, gpio %u\n", bank, gpio);
	uint32_t before = sys_read32(ints);
	sys_write32(sys_read32(inte) | mask, inte);
	sys_write32(mask, intf);  /* Force all edge types */
	uint32_t after = sys_read32(ints);

	printk("  PCIE_INTS before: 0x%08x\n", before);
	printk("  PCIE_INTS after:  0x%08x\n", after);
}

#endif /* RP1_IRQ_TRIGGER_H */
