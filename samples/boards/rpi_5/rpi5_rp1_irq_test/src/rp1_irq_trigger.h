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
#include <zephyr/sys/sys_io.h>

/*
 * RP1 PCIe Configuration registers (from RP1 Peripherals doc)
 * Base offset from RP1 BAR0 or function base
 */
#define RP1_PCIE_CFG_BASE   0x00000000  /* Function-specific base */

/* MSI-X Configuration registers (Section 3.1.5) */
#define RP1_MSIX_CFG(n)     (RP1_PCIE_CFG_BASE + 0x8c + ((n) * 4))

/* MSI-X CFG bits */
#define RP1_MSIX_CFG_ENABLE  BIT(0)
#define RP1_MSIX_CFG_TEST    BIT(8)   /* Software test bit - ORed with interrupt source */
#define RP1_MSIX_CFG_IACK_EN BIT(16)  /* IACK enable */

/* PCIe Interrupt Status/Control (Section 3.1.4) */
#define RP1_PCIE_INTSTATL   0x0050  /* Interrupt status [31:0] */
#define RP1_PCIE_INTSTATH   0x0054  /* Interrupt status [63:32] */

/*
 * GPIO Interrupt registers (Section 5.3.4)
 * These are for GPIO bank interrupts to host via PCIe
 */
#define RP1_IO_BANK0_BASE   0xd0000  /* From DT: reg = <0x1f 0xd0000 0x4000> */
#define RP1_IO_BANK1_BASE   0xe0000
#define RP1_IO_BANK2_BASE   0xf0000

/* Per-bank interrupt registers for PCIe (host) direction */
#define RP1_GPIO_PROC1_INTE(bank, n)  ((bank) + 0x128 + ((n) * 0x30))  /* Enable */
#define RP1_GPIO_PROC1_INTF(bank, n)  ((bank) + 0x12c + ((n) * 0x30))  /* Force */
#define RP1_GPIO_PROC1_INTS(bank, n)  ((bank) + 0x130 + ((n) * 0x30))  /* Status */

struct rp1_trigger {
	uintptr_t rp1_base;  /* RP1 MMIO base (from BAR, mapped) */
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
	trig->initialized = true;
	
	printk("RP1 trigger initialized with base 0x%llx\n",
	       (unsigned long long)rp1_base);
	return true;
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
	
	uintptr_t msix_cfg = trig->rp1_base + RP1_MSIX_CFG(vector);
	
	printk("\n>>> Triggering RP1 interrupt via MSI-X TEST (vector %u) <<<\n", vector);
	
	/* Read current config */
	uint32_t cfg = sys_read32(msix_cfg);
	printk("  Current MSIX_CFG[%u]: 0x%08x\n", vector, cfg);
	
	/* Set ENABLE, TEST, and IACK_EN for clean edge delivery */
	cfg |= (RP1_MSIX_CFG_ENABLE | RP1_MSIX_CFG_TEST | RP1_MSIX_CFG_IACK_EN);
	sys_write32(cfg, msix_cfg);
	
	printk("  New MSIX_CFG[%u]: 0x%08x\n", vector,
	       sys_read32(msix_cfg));
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

	uintptr_t msix_cfg = trig->rp1_base + RP1_MSIX_CFG(vector);
	uint32_t cfg = sys_read32(msix_cfg);

	cfg |= (RP1_MSIX_CFG_ENABLE | RP1_MSIX_CFG_TEST | RP1_MSIX_CFG_IACK_EN);
	sys_write32(cfg, msix_cfg);
}

/**
 * @brief Clear MSI-X TEST bit
 */
static inline void rp1_clear_msix_test(struct rp1_trigger *trig, uint32_t vector)
{
	if (!trig->initialized) {
		return;
	}
	
	uintptr_t msix_cfg = trig->rp1_base + RP1_MSIX_CFG(vector);
	uint32_t cfg = sys_read32(msix_cfg);
	cfg &= ~RP1_MSIX_CFG_TEST;
	sys_write32(cfg, msix_cfg);
	
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

	uintptr_t msix_cfg = trig->rp1_base + RP1_MSIX_CFG(vector);
	uint32_t cfg = sys_read32(msix_cfg);

	cfg &= ~RP1_MSIX_CFG_TEST;
	sys_write32(cfg, msix_cfg);
}

/**
 * @brief Read RP1 PCIe interrupt status
 */
static inline void rp1_read_intstatus(struct rp1_trigger *trig)
{
	if (!trig->initialized) {
		return;
	}
	
	uint32_t sl = sys_read32(trig->rp1_base + RP1_PCIE_INTSTATL);
	uint32_t sh = sys_read32(trig->rp1_base + RP1_PCIE_INTSTATH);
	
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
	
	uintptr_t intf = trig->rp1_base +
		RP1_GPIO_PROC1_INTF(bank_base, reg);
	
	printk("Forcing GPIO interrupt: bank %u, gpio %u\n", bank, gpio);
	sys_write32((0xf << shift), intf);  /* Force all edge types */
}

#endif /* RP1_IRQ_TRIGGER_H */
