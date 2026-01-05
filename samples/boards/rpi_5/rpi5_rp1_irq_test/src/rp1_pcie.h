/*
 * RP1 PCIe Device and MSI-X Control
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef RP1_PCIE_H
#define RP1_PCIE_H

#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/drivers/pcie/pcie.h>
#include <zephyr/sys/device_mmio.h>

/* RP1 PCIe identifiers */
#define RP1_VENDOR_ID  0x1de4
#define RP1_DEVICE_ID  0x0001

/* PCIe capability ID */
#define PCI_CAP_ID_MSIX    0x11

/* MSI-X Capability registers */
#define PCI_MSIX_CTRL      0x02  /* Message Control */
#define PCI_MSIX_TABLE     0x04  /* Table offset/BAR */
#define PCI_MSIX_PBA       0x08  /* Pending Bit Array offset/BAR */

#define PCI_MSIX_ENABLE    0x8000
#define PCI_MSIX_MASK      0x4000

/* MSI-X Table Entry structure (16 bytes) */
struct msix_entry {
	uint32_t msg_addr_lo;
	uint32_t msg_addr_hi;
	uint32_t msg_data;
	uint32_t vector_control;
};

struct rp1_bar_map {
	struct pcie_bar bar;
	mm_reg_t vaddr;
	bool mapped;
};

struct rp1_device {
	pcie_bdf_t bdf;
	bool found;
	uint32_t msix_cap_offset;
	uint32_t msix_table_bar;
	uint32_t msix_table_offset;
	mm_reg_t msix_table_addr;
	uint16_t msix_count;
	struct rp1_bar_map msix_bar;
	struct rp1_bar_map cfg_bar;
};

struct rp1_find_ctx {
	pcie_bdf_t bdf;
	bool found;
};

static inline bool rp1_scan_cb(pcie_bdf_t bdf, pcie_id_t id, void *cb_data)
{
	struct rp1_find_ctx *ctx = cb_data;

	if ((PCIE_ID_TO_VEND(id) == RP1_VENDOR_ID) &&
	    (PCIE_ID_TO_DEV(id) == RP1_DEVICE_ID)) {
		ctx->bdf = bdf;
		ctx->found = true;
		return false;
	}

	return true;
}

/**
 * @brief Find RP1 PCIe device
 */
static inline bool rp1_find_device(struct rp1_device *dev)
{
	struct rp1_find_ctx ctx = {
		.bdf = PCIE_BDF_NONE,
		.found = false,
	};
	struct pcie_scan_opt opt = {
		.bus = 0,
		.cb = rp1_scan_cb,
		.cb_data = &ctx,
		.flags = PCIE_SCAN_RECURSIVE,
	};
	
	printk("Searching for RP1 device (VID:0x%04x, DID:0x%04x)...\n",
	       RP1_VENDOR_ID, RP1_DEVICE_ID);
	
	if (pcie_scan(&opt) < 0 || !ctx.found) {
		printk("RP1 device not found\n");
		dev->found = false;
		return false;
	}
	
	dev->bdf = ctx.bdf;
	dev->found = true;
	
	uint32_t id = pcie_conf_read(dev->bdf, PCIE_CONF_ID);
	printk("Found RP1: BDF 0x%08x, VID/DID: 0x%08x\n", dev->bdf, id);
	
	return true;
}

/**
 * @brief Find MSI-X capability
 */
static inline bool rp1_find_msix_cap(struct rp1_device *dev)
{
	if (!dev->found) {
		return false;
	}
	
	printk("Searching for MSI-X capability...\n");
	
	uint32_t cap_ptr = PCIE_CONF_CAPPTR_FIRST(
		pcie_conf_read(dev->bdf, PCIE_CONF_CAPPTR));
	
	while (cap_ptr) {
		uint32_t cap = pcie_conf_read(dev->bdf, cap_ptr);
		uint8_t cap_id = PCIE_CONF_CAP_ID(cap);
		
		if (cap_id == PCI_CAP_ID_MSIX) {
			dev->msix_cap_offset = cap_ptr;
			
			uint16_t ctrl = (cap >> 16) & 0xffff;
			dev->msix_count = (ctrl & 0x7ff) + 1;
			
			uint32_t table_reg = pcie_conf_read(
				dev->bdf, cap_ptr + (PCI_MSIX_TABLE / 4));
			dev->msix_table_bar = table_reg & 0x7;
			dev->msix_table_offset = table_reg & ~0x7;
			
			printk("  MSI-X cap at cfg word %u\n", cap_ptr);
			printk("  MSI-X vectors: %u\n", dev->msix_count);
			printk("  MSI-X table: BAR%u + 0x%x\n",
			       dev->msix_table_bar, dev->msix_table_offset);
			
			return true;
		}
		
		cap_ptr = PCIE_CONF_CAP_NEXT(cap);
	}
	
	printk("MSI-X capability not found\n");
	return false;
}

/**
 * @brief Enable MSI-X
 */
static inline void rp1_enable_msix(struct rp1_device *dev)
{
	if (!dev->found || !dev->msix_cap_offset) {
		return;
	}
	
	printk("Enabling MSI-X...\n");
	
	uint32_t cmd = pcie_conf_read(dev->bdf, PCIE_CONF_CMDSTAT);
	cmd |= (PCIE_CONF_CMDSTAT_MEM | PCIE_CONF_CMDSTAT_MASTER);
	pcie_conf_write(dev->bdf, PCIE_CONF_CMDSTAT, cmd);
	
	uint32_t cap = pcie_conf_read(dev->bdf, dev->msix_cap_offset);
	uint16_t ctrl = (cap >> 16) & 0xffff;
	ctrl |= PCI_MSIX_ENABLE;
	ctrl &= ~PCI_MSIX_MASK;
	cap = (cap & 0xffff) | ((uint32_t)ctrl << 16);
	pcie_conf_write(dev->bdf, dev->msix_cap_offset, cap);
	
	printk("MSI-X enabled\n");
}

/**
 * @brief Map a PCIe BAR to a CPU-accessible virtual address
 */
static inline bool rp1_map_bar(pcie_bdf_t bdf, uint32_t bar_idx,
			       struct rp1_bar_map *map)
{
	if (!pcie_get_mbar(bdf, bar_idx, &map->bar)) {
		printk("ERROR: BAR%u not available\n", bar_idx);
		map->mapped = false;
		return false;
	}
	
	device_map(&map->vaddr, map->bar.phys_addr, map->bar.size,
		   K_MEM_CACHE_NONE);
	map->mapped = true;
	
	printk("Mapped BAR%u: phys 0x%llx, size 0x%zx\n",
	       bar_idx,
	       (unsigned long long)map->bar.phys_addr,
	       map->bar.size);
	return true;
}

/**
 * @brief Map the MSI-X table BAR
 */
static inline bool rp1_map_msix_table(struct rp1_device *dev)
{
	if (!dev->found) {
		return false;
	}
	
	if (!rp1_map_bar(dev->bdf, dev->msix_table_bar, &dev->msix_bar)) {
		return false;
	}
	
	dev->msix_table_addr = dev->msix_bar.vaddr;
	return true;
}

/**
 * @brief Map RP1 config BAR (MSIX_CFG/INTSTAT)
 */
static inline bool rp1_map_cfg_bar(struct rp1_device *dev, uint32_t bar_idx)
{
	if (!dev->found) {
		return false;
	}
	
	return rp1_map_bar(dev->bdf, bar_idx, &dev->cfg_bar);
}

/**
 * @brief Setup MSI-X table entry (requires BAR mapping)
 */
static inline bool rp1_setup_msix_entry(struct rp1_device *dev,
					uint32_t vector,
					uint64_t msg_addr,
					uint32_t msg_data)
{
	if (!dev->msix_bar.mapped || !dev->msix_table_addr) {
		printk("ERROR: MSI-X table not mapped\n");
		return false;
	}
	
	if (vector >= dev->msix_count) {
		printk("ERROR: MSI-X vector %u out of range (max %u)\n",
		       vector, dev->msix_count - 1);
		return false;
	}
	
	volatile struct msix_entry *table =
		(volatile struct msix_entry *)(dev->msix_table_addr +
					       dev->msix_table_offset);
	
	printk("Setting MSI-X vector %u:\n", vector);
	printk("  Address: 0x%llx\n", (unsigned long long)msg_addr);
	printk("  Data: 0x%x\n", msg_data);
	
	table[vector].msg_addr_lo = (uint32_t)msg_addr;
	table[vector].msg_addr_hi = (uint32_t)(msg_addr >> 32);
	table[vector].msg_data = msg_data;
	table[vector].vector_control = 0;  /* Unmask */

	return true;
}

#endif /* RP1_PCIE_H */
