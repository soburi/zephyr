/*
 * MIP (MSI-X Interrupt Peripheral) Register Access for BCM2712
 *
 * Based on Linux kernel driver:
 * https://raw.githubusercontent.com/raspberrypi/linux/rpi-6.6.y/drivers/irqchip/irq-bcm2712-mip.c
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef MIP_REGS_H
#define MIP_REGS_H

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>

#include "rp1_irq_test_config.h"

/*
 * MIP register offsets (from Linux driver)
 */
#define MIP_INT_RAISE        0x00
//#define MIP_INT_SETL          0x08
//#define MIP_INT_SETH          0x0c
#define MIP_INT_CLEARED      0x10
//#define MIP_INT_CLEAREDH      0x14
#define MIP_INT_CFGL_HOST     0x20
#define MIP_INT_CFGH_HOST     0x30
#define MIP_INT_MASKL_HOST    0x40
#define MIP_INT_MASKH_HOST    0x50
#define MIP_INT_MASKL_VPU     0x60
#define MIP_INT_MASKH_VPU     0x70
#define MIP_INT_STATUSL_HOST  0x80
#define MIP_INT_STATUSH_HOST  0x90
#define MIP_INT_STATUSL_VPU   0xa0
#define MIP_INT_STATUSH_VPU   0xb0

#define MIP_MSI_BASE_INTID  RP1_MIP_MSI_BASE_INTID
#define MIP_MSI_NUM_SPIS    RP1_MIP_MSI_NUM_SPIS
#define MIP_MSI_OFFSET      RP1_MIP_MSI_OFFSET
#define MIP_MSG_ADDR        RP1_MIP_MSG_ADDR
#define MIP_REG_SIZE        RP1_MIP_REG_SIZE

struct mip_state {
	uintptr_t base;
	uint32_t statusl;
	uint32_t statush;
	uint32_t maskl;
	uint32_t maskh;
	uint32_t raise;
};

static inline void mip_dump_raw(uintptr_t mip_base, size_t bytes)
{
	if (bytes == 0U) {
		return;
	}

	printk("\n=== MIP Raw Dump (0x00..0x%02x) ===\n",
	       (unsigned int)(bytes - sizeof(uint32_t)));

	for (size_t off = 0U; off < bytes; off += 16U) {
		uint32_t v0 = sys_read32(mip_base + off + 0U);
		uint32_t v1 = sys_read32(mip_base + off + 4U);
		uint32_t v2 = sys_read32(mip_base + off + 8U);
		uint32_t v3 = sys_read32(mip_base + off + 12U);

		printk("  0x%02x: %08x %08x %08x %08x\n",
		       (unsigned int)off, v0, v1, v2, v3);
	}

	printk("=== End MIP Raw Dump ===\n\n");
}

/**
 * @brief Read MIP status registers
 */
static inline void mip_read_status(uintptr_t mip_base, struct mip_state *state)
{
	volatile uint32_t *regs = (volatile uint32_t *)mip_base;
	
	state->base = mip_base;
	state->statusl = regs[MIP_INT_STATUSL_HOST / 4];
	state->statush = regs[MIP_INT_STATUSH_HOST / 4];
	state->maskl = regs[MIP_INT_MASKL_HOST / 4];
	state->maskh = regs[MIP_INT_MASKH_HOST / 4];
	state->raise = regs[MIP_INT_RAISE / 4];
}

/**
 * @brief Dump MIP state to console
 */
static inline void mip_dump_state(const struct mip_state *state)
{
	printk("\n=== MIP State (base: 0x%llx) ===\n",
	       (unsigned long long)state->base);
	printk("  STATUS_L: 0x%08x\n", state->statusl);
	printk("  STATUS_H: 0x%08x\n", state->statush);
	printk("  MASK_L:   0x%08x\n", state->maskl);
	printk("  MASK_H:   0x%08x\n", state->maskh);
	printk("  RAISED: 0x%08x\n", state->raise);
	
	/* Show which bits are set in status */
	if (state->statusl || state->statush) {
		printk("  Active vectors:\n");
		for (int i = 0; i < 32; i++) {
			if (state->statusl & BIT(i)) {
				printk("    - Vector %d (INTID %d)\n",
				       i, MIP_MSI_BASE_INTID + i);
			}
		}
		for (int i = 0; i < 32; i++) {
			if (state->statush & BIT(i)) {
				printk("    - Vector %d (INTID %d)\n",
				       i + 32, MIP_MSI_BASE_INTID + i + 32);
			}
		}
	}
	printk("=== End MIP State ===\n\n");
}

/**
 * @brief Initialize MIP (unmask all, clear status)
 */
static inline void mip_init(uintptr_t mip_base)
{
	volatile uint32_t *regs = (volatile uint32_t *)mip_base;
	
	printk("Initializing MIP at 0x%llx\n",
	       (unsigned long long)mip_base);
	
	/* Clear all status */
	regs[MIP_INT_CLEARED / 4] = 0xffffffff;
	//regs[MIP_INT_CLEAREDH / 4] = 0xffffffff;
	
	/* Host unmasked, VPU masked */
	regs[MIP_INT_MASKL_HOST / 4] = 0x00000000;
	regs[MIP_INT_MASKH_HOST / 4] = 0x00000000;
	regs[MIP_INT_MASKL_VPU / 4] = 0xffffffff;
	regs[MIP_INT_MASKH_VPU / 4] = 0xffffffff;
	
	/* Configure all as edge-triggered (all 1s in Linux) */
	regs[MIP_INT_CFGL_HOST / 4] = 0xffffffff;
	regs[MIP_INT_CFGH_HOST / 4] = 0xffffffff;
	
	printk("MIP initialized\n");
}

/**
 * @brief Raise a MIP vector (best-effort; may be read-only on some revisions)
 */
static inline void mip_raise_vector(uintptr_t mip_base, uint32_t vector)
{
	volatile uint32_t *regs = (volatile uint32_t *)mip_base;

	if (vector < 32U) {
		regs[MIP_INT_RAISE / 4] = BIT(vector);
	}
}
/*
static inline void mip_set_vector_undoc(uintptr_t mip_base, uint32_t vector)
{
	volatile uint32_t *regs = (volatile uint32_t *)mip_base;

	if (vector < 32U) {
		regs[MIP_INT_SETL / 4] = BIT(vector);
	} else if (vector < 64U) {
		regs[MIP_INT_SETH / 4] = BIT(vector - 32U);
	}
}
*/
/**
 * @brief Clear specific MIP vector
 */
static inline void mip_clear_vector(uintptr_t mip_base, uint32_t vector)
{
	volatile uint32_t *regs = (volatile uint32_t *)mip_base;
	
	if (vector < 32) {
		regs[MIP_INT_CLEARED / 4] = BIT(vector);
	}
}

#endif /* MIP_REGS_H */
