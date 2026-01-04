/*
 * RP1 IRQ test configuration.
 *
 * Fill in these values from a known-good DTB (Linux firmware DTB or
 * a Zephyr overlay). Use 0 to skip steps safely.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP1_IRQ_TEST_CONFIG_H
#define RP1_IRQ_TEST_CONFIG_H

/* MIP (MSI-X Interrupt Peripheral) register window */
#define RP1_MIP_BASE_ADDR      0x1000130000ULL
#define RP1_MIP_REG_SIZE       0x00c0U

/* MSI-X message (doorbell) address expected by MIP */
#define RP1_MIP_MSG_ADDR       0x000000fffffff000ULL

/* MIP output SPI range (GIC INTID base + count) */
#define RP1_MIP_MSI_BASE_INTID 0x80U
#define RP1_MIP_MSI_NUM_SPIS   0x40U
#define RP1_MIP_MSI_OFFSET     0x00U

/* MSI-X vector used for the test */
#define RP1_MSIX_TEST_VECTOR   0U

/*
 * Sweep step for locating the RP1 config block within mapped BARs.
 * Smaller values increase coverage at the cost of more test writes.
 */
#define RP1_CFG_SWEEP_STEP     0x100U

/*
 * Optional CPU-generated MSI doorbell test to validate MIP -> GIC path.
 * Disable if the doorbell address is not CPU-accessible on your setup.
 */
#define RP1_ENABLE_CPU_DOORBELL_TEST 0U

/*
 * Optional MIP self-raise test (writes MIP_INT_RAISED).
 * This is a best-effort sanity check; some revisions may treat it as read-only.
 */
#define RP1_ENABLE_MIP_RAISE_TEST 1U

/*
 * BAR index that exposes the RP1 PCIe config block (MSIX_CFG/INTSTAT).
 * Use 0xFF to reuse the MSI-X table BAR.
 */
#define RP1_CFG_BAR_INDEX      0x02U

/*
 * Optional forced BAR mapping for the RP1 PCIe config block.
 * Use when pcie_get_mbar() fails due to BAR sizing writes being rejected.
 * Set to 0 to disable.
 */
#define RP1_CFG_BAR_PHYS_ADDR  0x0000001f00400000ULL
#define RP1_CFG_BAR_SIZE       0x00010000U

/*
 * Use PCIe config space for MSIX_CFG/INTSTAT access instead of MMIO BAR.
 * This is recommended for RP1 MSI-X test registers.
 */
#define RP1_CFG_USE_PCIE_CFG   0U

/*
 * Base offset within the RP1 PCIe BAR for the endpoint config block.
 * MSI-X table occupies BAR0+0x0, so start past it (e.g. 0x400).
 */
#define RP1_PCIE_CFG_BASE_OFFSET 0x000U

#endif /* RP1_IRQ_TEST_CONFIG_H */
