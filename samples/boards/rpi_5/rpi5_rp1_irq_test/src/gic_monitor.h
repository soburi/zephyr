/*
 * GIC State Monitor for RPi5 RP1 interrupt testing
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef GIC_MONITOR_H
#define GIC_MONITOR_H

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

struct gic_monitor {
	uintptr_t dist_base;
};

/* GIC Distributor register offsets */
#define GICD_ISENABLER_OFF  0x100
#define GICD_ISPENDR_OFF    0x200
#define GICD_ICPENDR_OFF    0x280
#define GICD_ISACTIVER_OFF  0x300

static inline void gic_monitor_init(struct gic_monitor *mon, uintptr_t dist_base)
{
	mon->dist_base = dist_base;
}

static inline uint32_t gicd_read32(const struct gic_monitor *mon, uint32_t off)
{
	return sys_read32(mon->dist_base + off);
}

static inline void gicd_write32(const struct gic_monitor *mon, uint32_t off, uint32_t val)
{
	sys_write32(val, mon->dist_base + off);
}

/**
 * @brief Scan GIC pending register for active SPI interrupts
 * 
 * @param start_spi Starting SPI number (offset from 32)
 * @param end_spi Ending SPI number (offset from 32)
 */
static inline void gic_scan_pending(const struct gic_monitor *mon,
				    uint32_t start_intid,
				    uint32_t end_intid)
{
	printk("\n=== GIC Pending Scan (INTID %u-%u) ===\n",
	       start_intid, end_intid);
	
	for (uint32_t intid = start_intid; intid <= end_intid; intid++) {
		uint32_t reg_idx = intid / 32;
		uint32_t bit_idx = intid % 32;
		
		uint32_t pend_val = gicd_read32(mon, GICD_ISPENDR_OFF + (reg_idx * 4));
		uint32_t act_val = gicd_read32(mon, GICD_ISACTIVER_OFF + (reg_idx * 4));
		
		if (pend_val & BIT(bit_idx)) {
			printk("  [PENDING] INTID %u - bit %u in reg %u\n",
			       intid, bit_idx, reg_idx);
		}
		
		if (act_val & BIT(bit_idx)) {
			printk("  [ACTIVE]  INTID %u - bit %u in reg %u\n",
			       intid, bit_idx, reg_idx);
		}
	}
	
	printk("=== End GIC Scan ===\n\n");
}

/**
 * @brief Get pending status for a specific SPI
 */
static inline bool gic_is_intid_pending(const struct gic_monitor *mon, uint32_t intid)
{
	uint32_t reg_idx = intid / 32;
	uint32_t bit_idx = intid % 32;
	
	uint32_t pend_val = gicd_read32(mon, GICD_ISPENDR_OFF + (reg_idx * 4));
	return (pend_val & BIT(bit_idx)) != 0;
}

/**
 * @brief Clear pending for a specific SPI (write ICPENDR)
 */
static inline void gic_clear_intid_pending(const struct gic_monitor *mon, uint32_t intid)
{
	uint32_t reg_idx = intid / 32;
	uint32_t bit_idx = intid % 32;
	
	gicd_write32(mon, GICD_ICPENDR_OFF + (reg_idx * 4), BIT(bit_idx));
}

/**
 * @brief Dump all GIC ISPENDR registers (for wide scan)
 */
static inline void gic_dump_all_pending(const struct gic_monitor *mon)
{
	printk("\n=== GIC All ISPENDR Registers ===\n");
	for (int i = 0; i < 16; i++) {
		uint32_t val = gicd_read32(mon, GICD_ISPENDR_OFF + (i * 4));
		if (val) {
			printk("  ISPENDR[%d] = 0x%08x\n", i, val);
		}
	}
	printk("=== End ISPENDR Dump ===\n\n");
}

#endif /* GIC_MONITOR_H */
