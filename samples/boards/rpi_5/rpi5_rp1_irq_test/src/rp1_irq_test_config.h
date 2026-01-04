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
 * BAR index that exposes the RP1 PCIe config block (MSIX_CFG/INTSTAT).
 * Use 0xFF to reuse the MSI-X table BAR.
 */
#define RP1_CFG_BAR_INDEX      0xFFU

#endif /* RP1_IRQ_TEST_CONFIG_H */
