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

/*
 * CPU-accessible alias of the MSI doorbell window.
 *
 * Firmware DT commonly describes MIP with two reg entries:
 *   - MIP registers:    0x1000130000 (size 0xc0)
 *   - PCIe MSI window:  0xff_ffff_f000 (size 0x1000)
 *
 * RP1 MSI-X must use the PCIe MSI window address (RP1_MIP_MSG_ADDR).
 * But a CPU-side devmem-style "doorbell" write should target the CPU-visible
 * alias (usually MIP base + 0), not the PCIe address, otherwise the write can
 * hit nothing (or the wrong thing) and you will not see MIP/GIC activity.
 */
#ifndef RP1_MIP_MSG_ADDR_CPU
#define RP1_MIP_MSG_ADDR_CPU   0x1000130000ULL
#endif

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

/* Dump raw MIP registers for offset verification */
#define RP1_ENABLE_MIP_RAW_DUMP 1U
#define RP1_MIP_RAW_DUMP_BYTES  0x80U

/*
 * Optional RP1 GPIO PCIe interrupt force test.
 * This uses the PCIe host INTE/INTF/INTS registers in IO_BANK.
 */
#define RP1_ENABLE_GPIO_FORCE_TEST 1U
#define RP1_GPIO_FORCE_BANK        0U
#define RP1_GPIO_FORCE_PIN         0U

/*
 * BAR index that exposes the RP1 PCIe config block (MSIX_CFG/INTSTAT).
 * Use 0xFF to reuse the MSI-X table BAR.
 */
#define RP1_CFG_BAR_INDEX      0x01U

/*
 * Optional forced BAR mapping for the RP1 PCIe config block.
 * Use when pcie_get_mbar() fails due to BAR sizing writes being rejected.
 * Set to 0 to disable.
 */
#define RP1_CFG_BAR_PHYS_ADDR  0x0000001f00000000ULL
#define RP1_CFG_BAR_SIZE       0x00400000U

/*
 * Optional BAR programming for RP1 (when firmware has not assigned BAR1/2).
 * Set RP1_ENABLE_BAR_PROGRAM to 0 to skip.
 */
#define RP1_ENABLE_BAR_PROGRAM 0U
#define RP1_BAR1_PHYS_ADDR     0x0000001f00000000ULL
#define RP1_BAR1_SIZE          0x00400000U

/*
 * Use PCIe config space for MSIX_CFG/INTSTAT access instead of MMIO BAR.
 * This is recommended for RP1 MSI-X test registers.
 */
#define RP1_CFG_USE_PCIE_CFG   0U

/*
 * Base offset within the RP1 PCIe BAR for the endpoint config block.
 * This is added to RP1_PCIE_APBS_BASE when using BAR MMIO access.
 */
#define RP1_PCIE_CFG_BASE_OFFSET 0x000U

/*
 * RP1 PCIe APBS base and set/clear window offsets (from Linux rp1 driver).
 * Used when accessing MSIX_CFG/INTSTAT via BAR MMIO.
 */
#define RP1_PCIE_APBS_BASE     0x00108000U
#define RP1_PCIE_REG_SET_OFFSET 0x00000800U
#define RP1_PCIE_REG_CLR_OFFSET 0x00000c00U
#define RP1_USE_APBS_SETCLR     1U

/* GPIO PCIe interrupt offsets (override if your RP1 revision differs) */
#define RP1_GPIO_PCIE_INTE_OFFSET 0x128U
#define RP1_GPIO_PCIE_INTF_OFFSET 0x12cU
#define RP1_GPIO_PCIE_INTS_OFFSET 0x130U

/*
 * Root Complex inbound mapping for MSI doorbell.
 *
 * When enabled, main.c programs RC_BAR1 and UBUS BAR1 remap so that MSI writes
 * to RP1_MIP_MSG_ADDR reach the MIP block at RP1_MIP_BASE_ADDR.
 */
#define RP1_ENABLE_RC_BAR1_PROGRAM 1U

#endif /* RP1_IRQ_TEST_CONFIG_H */
