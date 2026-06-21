/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3568_pcie_ep

#include <zephyr/device.h>
#include <zephyr/drivers/pcie/endpoint/pcie_ep.h>
#include <zephyr/irq.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pcie_ep_rk3568, CONFIG_PCIE_EP_LOG_LEVEL);

/* RK3568 TRM Part 2, chapter 18: PCIe client registers. */
#define RK3568_PCIE_CLIENT_GENERAL_CON      0x0000
#define RK3568_PCIE_CLIENT_INTR_STATUS_MISC 0x0010
#define RK3568_PCIE_CLIENT_INTR_MASK_MISC   0x0024
#define RK3568_PCIE_CLIENT_MSG_GEN_CON      0x0034
#define RK3568_PCIE_CLIENT_MSI_GEN_CON      0x0038
#define RK3568_PCIE_CLIENT_HOT_RESET_CTRL   0x0180
#define RK3568_PCIE_CLIENT_LTSSM_STATUS     0x0300

#define RK3568_PCIE_CLIENT_DEVICE_TYPE      GENMASK(7, 4)
#define RK3568_PCIE_CLIENT_DEVICE_TYPE_EP   0
#define RK3568_PCIE_CLIENT_LINK_REQ_RST_GRT BIT(3)
#define RK3568_PCIE_CLIENT_LTSSM_ENABLE     BIT(2)
#define RK3568_PCIE_CLIENT_LEGACY_INT_REQ   BIT(1)
#define RK3568_PCIE_CLIENT_LINK_REQ_RST_INT BIT(2)
#define RK3568_PCIE_CLIENT_DLL_LINK_INT     BIT(1)
#define RK3568_PCIE_CLIENT_PHY_LINK_INT     BIT(0)
#define RK3568_PCIE_CLIENT_LINK_INTS        GENMASK(2, 0)

#define RK3568_PCIE_HOT_RESET_LTSSM_ENHANCE BIT(4)

#define RK3568_PCIE_LTSSM_SMLH_LINK_UP BIT(16)
#define RK3568_PCIE_LTSSM_RDLH_LINK_UP BIT(17)

/* Core configuration and port logic registers. */
#define RK3568_PCIE_CFG_VENDOR_DEVICE_ID 0x0000
#define RK3568_PCIE_CFG_CLASS_REVISION   0x0008
#define RK3568_PCIE_CFG_BAR0             0x0010
#define RK3568_PCIE_CFG_SUBSYSTEM_ID     0x002c
#define RK3568_PCIE_MSI_CAP              0x0050
#define RK3568_PCIE_MSIX_CAP             0x00b0
#define RK3568_PCIE_MSIX_TABLE           0x00b4
#define RK3568_PCIE_MSIX_PBA             0x00b8
#define RK3568_PCIE_MISC_CONTROL_1       0x08bc
#define RK3568_PCIE_DBI_RO_WR_EN         BIT(0)
#define RK3568_PCIE_MSIX_DOORBELL        0x0948
#define RK3568_PCIE_DBI2_OFFSET          0x100000

#define RK3568_PCIE_BAR_IO_SPACE    BIT(0)
#define RK3568_PCIE_BAR_MEM_TYPE_64 BIT(2)
#define RK3568_PCIE_BAR_PREFETCH    BIT(3)

#define RK3568_PCIE_MSI_ENABLE        BIT(16)
#define RK3568_PCIE_MSI_MMC           GENMASK(19, 17)
#define RK3568_PCIE_MSI_MME           GENMASK(22, 20)
#define RK3568_PCIE_MSI_PVM_CAPABLE   BIT(24)
#define RK3568_PCIE_MSIX_TABLE_SIZE   GENMASK(26, 16)
#define RK3568_PCIE_MSIX_ENABLE       BIT(31)
#define RK3568_PCIE_MSIX_BIR          GENMASK(2, 0)
#define RK3568_PCIE_MSIX_TABLE_OFFSET 0x0000
#define RK3568_PCIE_MSIX_PBA_OFFSET   0x0400

/* Unrolled iATU register space starts at core offset 0x300000. */
#define RK3568_PCIE_ATU_BASE          0x300000
#define RK3568_PCIE_ATU_REGION_STRIDE 0x200
#define RK3568_PCIE_ATU_REGION_CTRL1  0x00
#define RK3568_PCIE_ATU_REGION_CTRL2  0x04
#define RK3568_PCIE_ATU_LOWER_BASE    0x08
#define RK3568_PCIE_ATU_UPPER_BASE    0x0c
#define RK3568_PCIE_ATU_LIMIT         0x10
#define RK3568_PCIE_ATU_LOWER_TARGET  0x14
#define RK3568_PCIE_ATU_UPPER_TARGET  0x18
#define RK3568_PCIE_ATU_UPPER_LIMIT   0x20

#define RK3568_PCIE_ATU_TYPE_MEM       0
#define RK3568_PCIE_ATU_ENABLE         BIT(31)
#define RK3568_PCIE_ATU_BAR_MATCH      BIT(30)
#define RK3568_PCIE_ATU_BAR_NUMBER     GENMASK(10, 8)
#define RK3568_PCIE_ATU_INBOUND_OFFSET 0x100
#define RK3568_PCIE_ATU_MIN_SIZE       KB(64)
#define RK3568_PCIE_ATU_MAX_SIZE       BIT64(32)
#define RK3568_PCIE_ATU_MAX_REGIONS    16

#define RK3568_PCIE_MAX_MSI  32
#define RK3568_PCIE_MAX_MSIX 64
#define RK3568_PCIE_MAX_BARS 6

/* RK3568 TRM Part 1: CRU, PIPE_GRF and PCIe30 PHY GRF. */
#define RK3568_CRU_GATE_CON13       0x0334
#define RK3568_CRU_GATE_CON33       0x0384
#define RK3568_CRU_SOFTRST_CON12    0x0430
#define RK3568_CRU_SOFTRST_CON27    0x046c
#define RK3568_CRU_PCIE30X2_CLOCKS  GENMASK(5, 0)
#define RK3568_CRU_PCIE30PHY_PCLK   BIT(8)
#define RK3568_CRU_PCIE30X2_RESETS  GENMASK(10, 0)
#define RK3568_CRU_PCIE30PHY_RESETS GENMASK(15, 13)

#define RK3568_PMU_GRF_GPIO0A_IOMUX_H 0x0004
#define RK3568_PMU_GRF_GPIO0C_IOMUX_H 0x0014
#define RK3568_PMU_GRF_PCIE_CLKREQ_M0 GENMASK(11, 8)
#define RK3568_PMU_GRF_PCIE_WAKE_M0   GENMASK(7, 4)
#define RK3568_PMU_GRF_PCIE_PERST_M0  GENMASK(11, 8)
#define RK3568_PMU_GRF_MUX_PCIE       3

#define RK3568_SYS_GRF_IOFUNC_SEL5        0x0314
#define RK3568_SYS_GRF_PCIE30X2_IOMUX_SEL GENMASK(7, 6)
#define RK3568_SYS_GRF_PCIE30X2_M0        0

#define RK3568_PIPE_GRF_PIPE_CON0             0x0000
#define RK3568_PIPE_GRF_PCIE30X2_LINK_RST_GRT BIT(2)

#define RK3568_PHY_GRF_CON3           0x000c
#define RK3568_PHY_GRF_CON4           0x0010
#define RK3568_PHY_GRF_STATUS0        0x0080
#define RK3568_PHY_GRF_USE_PAD_REFCLK BIT(15)
#define RK3568_PHY_GRF_MPLLA_FORCE_EN BIT(7)
#define RK3568_PHY_GRF_EXT_CTRL_SEL   BIT(3)
#define RK3568_PHY_GRF_MPLLA_STATE    BIT(27)

#define RK3568_PCIE_PLL_LOCK_TIMEOUT_US 100000
#define RK3568_PCIE_DMA_TIMEOUT_US      1000000

/* RK3568 TRM Part 2, chapter 18: PCIe embedded DMA registers. */
#define RK3568_PCIE_DMA_BASE          0x380000
#define RK3568_PCIE_DMA_WR_ENGINE_EN  0x00c
#define RK3568_PCIE_DMA_WR_DOORBELL   0x010
#define RK3568_PCIE_DMA_RD_ENGINE_EN  0x02c
#define RK3568_PCIE_DMA_RD_DOORBELL   0x030
#define RK3568_PCIE_DMA_CH_CONTROL1   0x200
#define RK3568_PCIE_DMA_TRANSFER_SIZE 0x208
#define RK3568_PCIE_DMA_SAR_LOW       0x20c
#define RK3568_PCIE_DMA_SAR_HIGH      0x210
#define RK3568_PCIE_DMA_DAR_LOW       0x214
#define RK3568_PCIE_DMA_DAR_HIGH      0x218
#define RK3568_PCIE_DMA_ENGINE_ENABLE BIT(0)
#define RK3568_PCIE_DMA_DOORBELL_STOP BIT(31)

struct rk3568_pcie_ep_config {
	uintptr_t dbi_addr;
	size_t dbi_size;
	uintptr_t client_addr;
	size_t client_size;
	uint64_t map_addr;
	size_t map_size;
	uintptr_t cru_addr;
	size_t cru_size;
	uintptr_t pmu_grf_addr;
	size_t pmu_grf_size;
	uintptr_t sys_grf_addr;
	size_t sys_grf_size;
	uintptr_t pipe_grf_addr;
	size_t pipe_grf_size;
	uintptr_t phy_grf_addr;
	size_t phy_grf_size;
	uint8_t num_ob_windows;
	bool external_refclk;
	bool configure_m0_pins;
	uint16_t vendor_id;
	uint16_t device_id;
	uint32_t class_revision;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;
	void (*irq_config_func)(const struct device *dev);
};

struct rk3568_pcie_ep_data {
	struct k_spinlock lock;
	const struct device *dev;
	mm_reg_t dbi_addr;
	mm_reg_t client_addr;
	mm_reg_t map_addr;
	mm_reg_t cru_addr;
	mm_reg_t pmu_grf_addr;
	mm_reg_t sys_grf_addr;
	mm_reg_t pipe_grf_addr;
	mm_reg_t phy_grf_addr;
	uint16_t ob_in_use;
	uint64_t ob_target[RK3568_PCIE_ATU_MAX_REGIONS];
	uint64_t ob_region_size[RK3568_PCIE_ATU_MAX_REGIONS];
	uint8_t ib_in_use;
	uint8_t bar_64;
	pcie_ep_reset_callback_t reset_cb[PCIE_RESET_MAX];
	void *reset_cb_arg[PCIE_RESET_MAX];
	struct k_work hot_reset_work;
	struct k_mutex dma_lock;
};

static inline void rk3568_pcie_hiword_update(uintptr_t addr, uint32_t mask, uint32_t value)
{
	sys_write32((mask << 16) | (value & mask), addr);
}

static inline uintptr_t rk3568_pcie_atu_region(const struct rk3568_pcie_ep_data *data,
					       uint8_t index)
{
	return data->dbi_addr + RK3568_PCIE_ATU_BASE + (index * RK3568_PCIE_ATU_REGION_STRIDE);
}

static inline uintptr_t rk3568_pcie_atu_inbound_region(const struct rk3568_pcie_ep_data *data,
						       uint8_t index)
{
	return rk3568_pcie_atu_region(data, index) + RK3568_PCIE_ATU_INBOUND_OFFSET;
}

static bool rk3568_pcie_link_up(const struct device *dev)
{
	const struct rk3568_pcie_ep_data *data = dev->data;
	uint32_t status = sys_read32(data->client_addr + RK3568_PCIE_CLIENT_LTSSM_STATUS);

	return (status & (RK3568_PCIE_LTSSM_SMLH_LINK_UP | RK3568_PCIE_LTSSM_RDLH_LINK_UP)) ==
	       (RK3568_PCIE_LTSSM_SMLH_LINK_UP | RK3568_PCIE_LTSSM_RDLH_LINK_UP);
}

static int rk3568_pcie_wait_for_pll(const struct rk3568_pcie_ep_data *data)
{
	for (uint32_t timeout = 0; timeout < RK3568_PCIE_PLL_LOCK_TIMEOUT_US; timeout++) {
		if ((sys_read32(data->phy_grf_addr + RK3568_PHY_GRF_STATUS0) &
		     RK3568_PHY_GRF_MPLLA_STATE) != 0U) {
			return 0;
		}
		k_busy_wait(1);
	}

	return -ETIMEDOUT;
}

static int rk3568_pcie_conf_read(const struct device *dev, uint32_t offset, uint32_t *data)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	const struct rk3568_pcie_ep_data *ctx = dev->data;

	if (data == NULL || offset > cfg->dbi_size - sizeof(uint32_t) || !IS_ALIGNED(offset, 4)) {
		return -EINVAL;
	}

	*data = sys_read32(ctx->dbi_addr + offset);
	return 0;
}

static void rk3568_pcie_conf_write(const struct device *dev, uint32_t offset, uint32_t value)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	struct rk3568_pcie_ep_data *data = dev->data;
	uintptr_t misc;
	uint32_t reg;
	k_spinlock_key_t key;

	if (offset > cfg->dbi_size - sizeof(uint32_t) || !IS_ALIGNED(offset, 4)) {
		return;
	}

	/*
	 * The TRM specifies that DBI_RO_WR_EN temporarily makes selected
	 * read-only configuration fields writable from DBI.
	 */
	key = k_spin_lock(&data->lock);
	misc = data->dbi_addr + RK3568_PCIE_MISC_CONTROL_1;
	reg = sys_read32(misc);
	sys_write32(reg | RK3568_PCIE_DBI_RO_WR_EN, misc);
	sys_write32(value, data->dbi_addr + offset);
	sys_write32(reg & ~RK3568_PCIE_DBI_RO_WR_EN, misc);
	k_spin_unlock(&data->lock, key);
}

static int rk3568_pcie_map_addr(const struct device *dev, uint64_t pcie_addr, uint64_t *mapped_addr,
				uint32_t size, enum pcie_ob_mem_type ob_mem_type)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	struct rk3568_pcie_ep_data *data = dev->data;
	const uint64_t window_size = cfg->map_size / cfg->num_ob_windows;
	const uint64_t target = ROUND_DOWN(pcie_addr, RK3568_PCIE_ATU_MIN_SIZE);
	const uint64_t offset = pcie_addr - target;
	uint64_t region_size;
	uint64_t local;
	uintptr_t atu;
	k_spinlock_key_t key;
	uint8_t index;
	int ret = -EBUSY;

	if (mapped_addr == NULL || size == 0U) {
		return -EINVAL;
	}

	if (ob_mem_type == PCIE_OB_HIGHMEM && cfg->map_addr <= UINT32_MAX) {
		return -ENOTSUP;
	}
	if (ob_mem_type == PCIE_OB_LOWMEM && cfg->map_addr > UINT32_MAX) {
		return -ENOTSUP;
	}

	region_size = ROUND_UP(MIN((uint64_t)size + offset, window_size), RK3568_PCIE_ATU_MIN_SIZE);
	if (region_size > window_size) {
		region_size = window_size;
	}
	if (offset >= region_size) {
		return -ENOTSUP;
	}

	key = k_spin_lock(&data->lock);
	for (index = 0; index < cfg->num_ob_windows; index++) {
		if ((data->ob_in_use & BIT(index)) == 0U) {
			data->ob_in_use |= BIT(index);
			ret = 0;
			break;
		}
	}
	k_spin_unlock(&data->lock, key);
	if (ret != 0) {
		return ret;
	}

	local = cfg->map_addr + (index * window_size);
	atu = rk3568_pcie_atu_region(data, index);

	/* Disable before updating, as required by the iATU programming model. */
	sys_write32(0, atu + RK3568_PCIE_ATU_REGION_CTRL2);
	sys_write32(RK3568_PCIE_ATU_TYPE_MEM, atu + RK3568_PCIE_ATU_REGION_CTRL1);
	sys_write32((uint32_t)local, atu + RK3568_PCIE_ATU_LOWER_BASE);
	sys_write32((uint32_t)(local >> 32), atu + RK3568_PCIE_ATU_UPPER_BASE);
	sys_write32((uint32_t)(local + region_size - 1), atu + RK3568_PCIE_ATU_LIMIT);
	sys_write32((uint32_t)((local + region_size - 1) >> 32), atu + RK3568_PCIE_ATU_UPPER_LIMIT);
	sys_write32((uint32_t)target, atu + RK3568_PCIE_ATU_LOWER_TARGET);
	sys_write32((uint32_t)(target >> 32), atu + RK3568_PCIE_ATU_UPPER_TARGET);
	sys_write32(RK3568_PCIE_ATU_ENABLE, atu + RK3568_PCIE_ATU_REGION_CTRL2);

	if ((sys_read32(atu + RK3568_PCIE_ATU_REGION_CTRL2) & RK3568_PCIE_ATU_ENABLE) == 0U) {
		key = k_spin_lock(&data->lock);
		data->ob_in_use &= ~BIT(index);
		k_spin_unlock(&data->lock, key);
		return -EIO;
	}

	data->ob_target[index] = target;
	data->ob_region_size[index] = region_size;
	*mapped_addr = data->map_addr + (index * window_size) + offset;
	return (int)MIN((uint64_t)size, region_size - offset);
}

static void rk3568_pcie_unmap_addr(const struct device *dev, uint64_t mapped_addr)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	struct rk3568_pcie_ep_data *data = dev->data;
	const uint64_t window_size = cfg->map_size / cfg->num_ob_windows;
	uint64_t offset;
	uint8_t index;
	k_spinlock_key_t key;

	if (mapped_addr < data->map_addr || mapped_addr >= data->map_addr + cfg->map_size) {
		return;
	}

	offset = mapped_addr - data->map_addr;
	index = offset / window_size;
	sys_write32(0, rk3568_pcie_atu_region(data, index) + RK3568_PCIE_ATU_REGION_CTRL2);

	key = k_spin_lock(&data->lock);
	data->ob_in_use &= ~BIT(index);
	data->ob_target[index] = 0;
	data->ob_region_size[index] = 0;
	k_spin_unlock(&data->lock, key);
}

static int rk3568_pcie_raise_irq(const struct device *dev, enum pci_ep_irq_type irq_type,
				 uint32_t irq_num)
{
	struct rk3568_pcie_ep_data *data = dev->data;
	uint32_t enabled_vectors;
	uint32_t msi_cap;
	k_spinlock_key_t key;
	int ret = 0;

	key = k_spin_lock(&data->lock);
	switch (irq_type) {
	case PCIE_EP_IRQ_LEGACY:
		if (irq_num != 0U) {
			ret = -EINVAL;
			break;
		}
		rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_MSG_GEN_CON,
					  RK3568_PCIE_CLIENT_LEGACY_INT_REQ,
					  RK3568_PCIE_CLIENT_LEGACY_INT_REQ);
		rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_MSG_GEN_CON,
					  RK3568_PCIE_CLIENT_LEGACY_INT_REQ, 0);
		break;
	case PCIE_EP_IRQ_MSI:
		if (irq_num >= RK3568_PCIE_MAX_MSI) {
			ret = -EINVAL;
			break;
		}
		msi_cap = sys_read32(data->dbi_addr + RK3568_PCIE_MSI_CAP);
		enabled_vectors = BIT(FIELD_GET(RK3568_PCIE_MSI_MME, msi_cap));
		if ((msi_cap & RK3568_PCIE_MSI_ENABLE) == 0U || irq_num >= enabled_vectors) {
			ret = -EACCES;
			break;
		}
		sys_write32(BIT(irq_num), data->client_addr + RK3568_PCIE_CLIENT_MSI_GEN_CON);
		break;
	case PCIE_EP_IRQ_MSIX:
		if (irq_num >= RK3568_PCIE_MAX_MSIX) {
			ret = -EINVAL;
			break;
		}
		if ((sys_read32(data->dbi_addr + RK3568_PCIE_MSIX_CAP) & RK3568_PCIE_MSIX_ENABLE) ==
		    0U) {
			ret = -EACCES;
			break;
		}
		sys_write32(irq_num, data->dbi_addr + RK3568_PCIE_MSIX_DOORBELL);
		break;
	default:
		ret = -EINVAL;
		break;
	}
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int rk3568_pcie_set_bar(const struct device *dev, const struct pcie_ep_bar *bar)
{
	struct rk3568_pcie_ep_data *data = dev->data;
	uintptr_t atu;
	uint32_t flags = 0;
	uint32_t mask;

	if (bar == NULL || bar->bar >= RK3568_PCIE_MAX_BARS ||
	    bar->size < RK3568_PCIE_ATU_MIN_SIZE || bar->size > RK3568_PCIE_ATU_MAX_SIZE ||
	    !IS_POWER_OF_TWO(bar->size) || !IS_ALIGNED(bar->phys_addr, bar->size)) {
		return -EINVAL;
	}
	if ((bar->flags & PCIE_EP_BAR_IO) != 0U &&
	    (bar->flags & (PCIE_EP_BAR_64 | PCIE_EP_BAR_PREFETCH)) != 0U) {
		return -EINVAL;
	}
	if ((bar->flags & ~(PCIE_EP_BAR_IO | PCIE_EP_BAR_64 | PCIE_EP_BAR_PREFETCH)) != 0U) {
		return -EINVAL;
	}
	if ((bar->flags & PCIE_EP_BAR_64) != 0U &&
	    (bar->bar == RK3568_PCIE_MAX_BARS - 1U || (bar->bar & 1U) != 0U)) {
		return -EINVAL;
	}
	if (bar->bar >= RK3568_PCIE_ATU_MAX_REGIONS) {
		return -ENOTSUP;
	}
	if ((data->ib_in_use & BIT(bar->bar)) != 0U ||
	    ((bar->flags & PCIE_EP_BAR_64) != 0U && (data->ib_in_use & BIT(bar->bar + 1U)) != 0U)) {
		return -EBUSY;
	}

	atu = rk3568_pcie_atu_inbound_region(data, bar->bar);
	sys_write32(0, atu + RK3568_PCIE_ATU_REGION_CTRL2);

	/*
	 * DBI2 holds the BAR sizing mask returned during enumeration. The
	 * normal DBI BAR register holds only the PCI BAR type attributes.
	 */
	mask = (uint32_t)(bar->size - 1U);
	sys_write32(mask, data->dbi_addr + RK3568_PCIE_DBI2_OFFSET + RK3568_PCIE_CFG_BAR0 +
				  (bar->bar * sizeof(uint32_t)));

	if ((bar->flags & PCIE_EP_BAR_IO) != 0U) {
		flags |= RK3568_PCIE_BAR_IO_SPACE;
	} else {
		if ((bar->flags & PCIE_EP_BAR_64) != 0U) {
			flags |= RK3568_PCIE_BAR_MEM_TYPE_64;
		}
		if ((bar->flags & PCIE_EP_BAR_PREFETCH) != 0U) {
			flags |= RK3568_PCIE_BAR_PREFETCH;
		}
	}
	rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_BAR0 + (bar->bar * sizeof(uint32_t)), flags);

	sys_write32((uint32_t)bar->phys_addr, atu + RK3568_PCIE_ATU_LOWER_TARGET);
	sys_write32((uint32_t)(bar->phys_addr >> 32), atu + RK3568_PCIE_ATU_UPPER_TARGET);
	sys_write32((bar->flags & PCIE_EP_BAR_IO) != 0U ? 2U : RK3568_PCIE_ATU_TYPE_MEM,
		    atu + RK3568_PCIE_ATU_REGION_CTRL1);
	sys_write32(RK3568_PCIE_ATU_ENABLE | RK3568_PCIE_ATU_BAR_MATCH |
			    FIELD_PREP(RK3568_PCIE_ATU_BAR_NUMBER, bar->bar),
		    atu + RK3568_PCIE_ATU_REGION_CTRL2);

	if ((sys_read32(atu + RK3568_PCIE_ATU_REGION_CTRL2) & RK3568_PCIE_ATU_ENABLE) == 0U) {
		return -EIO;
	}

	data->ib_in_use |= BIT(bar->bar);
	if ((bar->flags & PCIE_EP_BAR_64) != 0U) {
		rk3568_pcie_conf_write(
			dev, RK3568_PCIE_CFG_BAR0 + ((bar->bar + 1U) * sizeof(uint32_t)), 0);
		data->ib_in_use |= BIT(bar->bar + 1U);
		data->bar_64 |= BIT(bar->bar);
	}

	return 0;
}

static int rk3568_pcie_clear_bar(const struct device *dev, uint8_t bar)
{
	struct rk3568_pcie_ep_data *data = dev->data;
	uint32_t value;

	if (bar >= RK3568_PCIE_MAX_BARS) {
		return -EINVAL;
	}
	if (bar > 0U && (data->bar_64 & BIT(bar - 1U)) != 0U) {
		return -EINVAL;
	}

	sys_write32(0, rk3568_pcie_atu_inbound_region(data, bar) + RK3568_PCIE_ATU_REGION_CTRL2);
	sys_write32(0, data->dbi_addr + RK3568_PCIE_DBI2_OFFSET + RK3568_PCIE_CFG_BAR0 +
			       (bar * sizeof(uint32_t)));
	rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_BAR0 + (bar * sizeof(uint32_t)), 0);

	value = data->bar_64;
	data->ib_in_use &= ~BIT(bar);
	if (bar < RK3568_PCIE_MAX_BARS - 1U && (value & BIT(bar)) != 0U) {
		sys_write32(0, data->dbi_addr + RK3568_PCIE_DBI2_OFFSET + RK3568_PCIE_CFG_BAR0 +
				       ((bar + 1U) * sizeof(uint32_t)));
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_BAR0 + ((bar + 1U) * sizeof(uint32_t)),
				       0);
		data->ib_in_use &= ~BIT(bar + 1U);
		data->bar_64 &= ~BIT(bar);
	}

	return 0;
}

static int rk3568_pcie_start(const struct device *dev)
{
	struct rk3568_pcie_ep_data *data = dev->data;

	rk3568_pcie_hiword_update(data->pipe_grf_addr + RK3568_PIPE_GRF_PIPE_CON0,
				  RK3568_PIPE_GRF_PCIE30X2_LINK_RST_GRT,
				  RK3568_PIPE_GRF_PCIE30X2_LINK_RST_GRT);
	rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_GENERAL_CON,
				  RK3568_PCIE_CLIENT_LTSSM_ENABLE, RK3568_PCIE_CLIENT_LTSSM_ENABLE);
	return 0;
}

static int rk3568_pcie_stop(const struct device *dev)
{
	struct rk3568_pcie_ep_data *data = dev->data;

	rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_GENERAL_CON,
				  RK3568_PCIE_CLIENT_LTSSM_ENABLE, 0);
	return 0;
}

static int rk3568_pcie_register_reset_cb(const struct device *dev, enum pcie_reset reset,
					 pcie_ep_reset_callback_t cb, void *arg)
{
	struct rk3568_pcie_ep_data *data = dev->data;
	k_spinlock_key_t key;

	if (reset >= PCIE_RESET_MAX) {
		return -EINVAL;
	}

	key = k_spin_lock(&data->lock);
	data->reset_cb[reset] = cb;
	data->reset_cb_arg[reset] = arg;
	k_spin_unlock(&data->lock, key);
	return 0;
}

static int rk3568_pcie_dma_xfer(const struct device *dev, uint64_t mapped_addr,
				uintptr_t local_addr, uint32_t size, enum xfer_direction dir)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	struct rk3568_pcie_ep_data *data = dev->data;
	const uint64_t window_size = cfg->map_size / cfg->num_ob_windows;
	uint64_t aperture_offset;
	uint64_t region_offset;
	uint64_t remote_addr;
	uint64_t source;
	uint64_t destination;
	uintptr_t dma = data->dbi_addr + RK3568_PCIE_DMA_BASE;
	uint32_t engine;
	uint32_t doorbell;
	uint8_t index;
	int ret = -ETIMEDOUT;

	if (size == 0U || (dir != HOST_TO_DEVICE && dir != DEVICE_TO_HOST) ||
	    mapped_addr < data->map_addr || mapped_addr >= data->map_addr + cfg->map_size) {
		return -EINVAL;
	}

	aperture_offset = mapped_addr - data->map_addr;
	index = aperture_offset / window_size;
	region_offset = aperture_offset % window_size;
	if ((data->ob_in_use & BIT(index)) == 0U ||
	    region_offset + size > data->ob_region_size[index]) {
		return -EINVAL;
	}

	remote_addr = data->ob_target[index] + region_offset;
	if (dir == DEVICE_TO_HOST) {
		source = local_addr;
		destination = remote_addr;
		engine = RK3568_PCIE_DMA_WR_ENGINE_EN;
		doorbell = RK3568_PCIE_DMA_WR_DOORBELL;
	} else {
		source = remote_addr;
		destination = local_addr;
		engine = RK3568_PCIE_DMA_RD_ENGINE_EN;
		doorbell = RK3568_PCIE_DMA_RD_DOORBELL;
	}

	k_mutex_lock(&data->dma_lock, K_FOREVER);

	/*
	 * Non-linked-list channel zero transfer from the TRM programming
	 * examples. Writing zero to an engine-enable register resets DMA,
	 * therefore it is only ever written with the enable bit set.
	 */
	sys_write32(RK3568_PCIE_DMA_ENGINE_ENABLE, dma + engine);
	sys_write32(0, dma + RK3568_PCIE_DMA_CH_CONTROL1);
	sys_write32(size, dma + RK3568_PCIE_DMA_TRANSFER_SIZE);
	sys_write32((uint32_t)source, dma + RK3568_PCIE_DMA_SAR_LOW);
	sys_write32((uint32_t)(source >> 32), dma + RK3568_PCIE_DMA_SAR_HIGH);
	sys_write32((uint32_t)destination, dma + RK3568_PCIE_DMA_DAR_LOW);
	sys_write32((uint32_t)(destination >> 32), dma + RK3568_PCIE_DMA_DAR_HIGH);
	sys_write32(0, dma + doorbell);

	for (uint32_t timeout = 0; timeout < RK3568_PCIE_DMA_TIMEOUT_US; timeout++) {
		if (sys_read32(dma + RK3568_PCIE_DMA_TRANSFER_SIZE) == 0U) {
			ret = 0;
			break;
		}
		k_busy_wait(1);
	}
	if (ret != 0) {
		sys_write32(RK3568_PCIE_DMA_DOORBELL_STOP, dma + doorbell);
	}

	k_mutex_unlock(&data->dma_lock);
	return ret;
}

static void rk3568_pcie_hot_reset_work(struct k_work *work)
{
	struct rk3568_pcie_ep_data *data =
		CONTAINER_OF(work, struct rk3568_pcie_ep_data, hot_reset_work);
	const struct device *dev = data->dev;

	/*
	 * The TRM asks software to stop LTSSM before granting the delayed
	 * reset, then restart training. The documented PCIe_USB_GRF NIU-idle
	 * register is absent from the Part 1 USB_GRF register map, so no
	 * undocumented write is made here.
	 */
	(void)rk3568_pcie_stop(dev);
	rk3568_pcie_hiword_update(data->pipe_grf_addr + RK3568_PIPE_GRF_PIPE_CON0,
				  RK3568_PIPE_GRF_PCIE30X2_LINK_RST_GRT,
				  RK3568_PIPE_GRF_PCIE30X2_LINK_RST_GRT);
	(void)rk3568_pcie_start(dev);
}

static void rk3568_pcie_isr(const struct device *dev)
{
	struct rk3568_pcie_ep_data *data = dev->data;
	uint32_t status;

	status = sys_read32(data->client_addr + RK3568_PCIE_CLIENT_INTR_STATUS_MISC) &
		 RK3568_PCIE_CLIENT_LINK_INTS;
	if (status == 0U) {
		return;
	}

	/* The three miscellaneous link interrupt status bits are W1C. */
	sys_write32(status, data->client_addr + RK3568_PCIE_CLIENT_INTR_STATUS_MISC);

	if ((status & RK3568_PCIE_CLIENT_LINK_REQ_RST_INT) != 0U) {
		if (data->reset_cb[PCIE_PERST_INB] != NULL) {
			data->reset_cb[PCIE_PERST_INB](data->reset_cb_arg[PCIE_PERST_INB]);
		}
		k_work_submit(&data->hot_reset_work);
	} else if ((status & RK3568_PCIE_CLIENT_DLL_LINK_INT) != 0U && rk3568_pcie_link_up(dev)) {
		rk3568_pcie_hiword_update(data->pipe_grf_addr + RK3568_PIPE_GRF_PIPE_CON0,
					  RK3568_PIPE_GRF_PCIE30X2_LINK_RST_GRT, 0);
	}
}

static int rk3568_pcie_ep_init(const struct device *dev)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	struct rk3568_pcie_ep_data *data = dev->data;
	uint32_t value;
	int ret;

	if (cfg->num_ob_windows == 0U || cfg->num_ob_windows > RK3568_PCIE_ATU_MAX_REGIONS ||
	    cfg->map_size < RK3568_PCIE_ATU_MIN_SIZE || !IS_POWER_OF_TWO(cfg->num_ob_windows) ||
	    !IS_ALIGNED(cfg->map_addr, RK3568_PCIE_ATU_MIN_SIZE) ||
	    !IS_ALIGNED(cfg->map_size / cfg->num_ob_windows, RK3568_PCIE_ATU_MIN_SIZE)) {
		LOG_ERR("invalid outbound window configuration");
		return -EINVAL;
	}

	device_map(&data->dbi_addr, cfg->dbi_addr, cfg->dbi_size, K_MEM_CACHE_NONE);
	device_map(&data->client_addr, cfg->client_addr, cfg->client_size, K_MEM_CACHE_NONE);
	device_map(&data->map_addr, cfg->map_addr, cfg->map_size, K_MEM_CACHE_NONE);
	device_map(&data->cru_addr, cfg->cru_addr, cfg->cru_size, K_MEM_CACHE_NONE);
	device_map(&data->pmu_grf_addr, cfg->pmu_grf_addr, cfg->pmu_grf_size, K_MEM_CACHE_NONE);
	device_map(&data->sys_grf_addr, cfg->sys_grf_addr, cfg->sys_grf_size, K_MEM_CACHE_NONE);
	device_map(&data->pipe_grf_addr, cfg->pipe_grf_addr, cfg->pipe_grf_size, K_MEM_CACHE_NONE);
	device_map(&data->phy_grf_addr, cfg->phy_grf_addr, cfg->phy_grf_size, K_MEM_CACHE_NONE);

	data->dev = dev;
	data->ob_in_use = 0;
	data->ib_in_use = 0;
	data->bar_64 = 0;
	k_work_init(&data->hot_reset_work, rk3568_pcie_hot_reset_work);
	k_mutex_init(&data->dma_lock);

	if (cfg->configure_m0_pins) {
		rk3568_pcie_hiword_update(
			data->sys_grf_addr + RK3568_SYS_GRF_IOFUNC_SEL5,
			RK3568_SYS_GRF_PCIE30X2_IOMUX_SEL,
			FIELD_PREP(RK3568_SYS_GRF_PCIE30X2_IOMUX_SEL, RK3568_SYS_GRF_PCIE30X2_M0));
		rk3568_pcie_hiword_update(
			data->pmu_grf_addr + RK3568_PMU_GRF_GPIO0A_IOMUX_H,
			RK3568_PMU_GRF_PCIE_CLKREQ_M0,
			FIELD_PREP(RK3568_PMU_GRF_PCIE_CLKREQ_M0, RK3568_PMU_GRF_MUX_PCIE));
		rk3568_pcie_hiword_update(
			data->pmu_grf_addr + RK3568_PMU_GRF_GPIO0C_IOMUX_H,
			RK3568_PMU_GRF_PCIE_WAKE_M0 | RK3568_PMU_GRF_PCIE_PERST_M0,
			FIELD_PREP(RK3568_PMU_GRF_PCIE_WAKE_M0, RK3568_PMU_GRF_MUX_PCIE) |
				FIELD_PREP(RK3568_PMU_GRF_PCIE_PERST_M0, RK3568_PMU_GRF_MUX_PCIE));
	}

	/* Enable the PCIe3x2 bus, auxiliary, PIPE, and PHY APB clocks. */
	rk3568_pcie_hiword_update(data->cru_addr + RK3568_CRU_GATE_CON13,
				  RK3568_CRU_PCIE30X2_CLOCKS, 0);
	rk3568_pcie_hiword_update(data->cru_addr + RK3568_CRU_GATE_CON33, RK3568_CRU_PCIE30PHY_PCLK,
				  0);

	/* Hold both controller and PHY while selecting the reference clock. */
	rk3568_pcie_hiword_update(data->cru_addr + RK3568_CRU_SOFTRST_CON12,
				  RK3568_CRU_PCIE30X2_RESETS, RK3568_CRU_PCIE30X2_RESETS);
	rk3568_pcie_hiword_update(data->cru_addr + RK3568_CRU_SOFTRST_CON27,
				  RK3568_CRU_PCIE30PHY_RESETS, RK3568_CRU_PCIE30PHY_RESETS);
	k_busy_wait(10);

	rk3568_pcie_hiword_update(data->phy_grf_addr + RK3568_PHY_GRF_CON3,
				  RK3568_PHY_GRF_USE_PAD_REFCLK | RK3568_PHY_GRF_MPLLA_FORCE_EN,
				  (cfg->external_refclk ? RK3568_PHY_GRF_USE_PAD_REFCLK : 0) |
					  RK3568_PHY_GRF_MPLLA_FORCE_EN);
	/*
	 * Keep external protocol overrides disabled. The PHY databook fields
	 * state that the PCS selects its hard-coded per-protocol PLL and
	 * analog settings when EXT_CTRL_SEL is clear.
	 */
	rk3568_pcie_hiword_update(data->phy_grf_addr + RK3568_PHY_GRF_CON4,
				  RK3568_PHY_GRF_EXT_CTRL_SEL, 0);

	/* Release PHY resets first and wait for its PCIe TX PLL. */
	rk3568_pcie_hiword_update(data->cru_addr + RK3568_CRU_SOFTRST_CON27,
				  RK3568_CRU_PCIE30PHY_RESETS, 0);
	ret = rk3568_pcie_wait_for_pll(data);
	if (ret != 0) {
		LOG_ERR("PCIe30 PHY MPLLA did not lock");
		return ret;
	}

	/* Release the controller only after the PHY clock is available. */
	rk3568_pcie_hiword_update(data->cru_addr + RK3568_CRU_SOFTRST_CON12,
				  RK3568_CRU_PCIE30X2_RESETS, 0);
	k_busy_wait(10);

	for (uint8_t i = 0; i < cfg->num_ob_windows; i++) {
		sys_write32(0, rk3568_pcie_atu_region(data, i) + RK3568_PCIE_ATU_REGION_CTRL2);
	}
	for (uint8_t i = 0; i < RK3568_PCIE_MAX_BARS; i++) {
		sys_write32(0,
			    rk3568_pcie_atu_inbound_region(data, i) + RK3568_PCIE_ATU_REGION_CTRL2);
	}

	/* Hold LTSSM while the endpoint configuration space is initialized. */
	rk3568_pcie_hiword_update(
		data->client_addr + RK3568_PCIE_CLIENT_GENERAL_CON,
		RK3568_PCIE_CLIENT_DEVICE_TYPE | RK3568_PCIE_CLIENT_LINK_REQ_RST_GRT |
			RK3568_PCIE_CLIENT_LTSSM_ENABLE,
		FIELD_PREP(RK3568_PCIE_CLIENT_DEVICE_TYPE, RK3568_PCIE_CLIENT_DEVICE_TYPE_EP) |
			RK3568_PCIE_CLIENT_LINK_REQ_RST_GRT);

	if (cfg->vendor_id != 0U || cfg->device_id != 0U) {
		value = ((uint32_t)cfg->device_id << 16) | cfg->vendor_id;
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_VENDOR_DEVICE_ID, value);
	}
	if (cfg->class_revision != 0U) {
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_CLASS_REVISION, cfg->class_revision);
	}
	if (cfg->subsystem_vendor_id != 0U || cfg->subsystem_id != 0U) {
		value = ((uint32_t)cfg->subsystem_id << 16) | cfg->subsystem_vendor_id;
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_SUBSYSTEM_ID, value);
	}

	/* Advertise all interrupt vectors implemented by the client logic. */
	value = sys_read32(data->dbi_addr + RK3568_PCIE_MSI_CAP);
	value &= ~RK3568_PCIE_MSI_MMC;
	value |= FIELD_PREP(RK3568_PCIE_MSI_MMC, 5) | RK3568_PCIE_MSI_PVM_CAPABLE;
	rk3568_pcie_conf_write(dev, RK3568_PCIE_MSI_CAP, value);

	value = sys_read32(data->dbi_addr + RK3568_PCIE_MSIX_CAP);
	value &= ~RK3568_PCIE_MSIX_TABLE_SIZE;
	value |= FIELD_PREP(RK3568_PCIE_MSIX_TABLE_SIZE, RK3568_PCIE_MAX_MSIX - 1);
	rk3568_pcie_conf_write(dev, RK3568_PCIE_MSIX_CAP, value);
	rk3568_pcie_conf_write(dev, RK3568_PCIE_MSIX_TABLE,
			       RK3568_PCIE_MSIX_TABLE_OFFSET | FIELD_PREP(RK3568_PCIE_MSIX_BIR, 0));
	rk3568_pcie_conf_write(dev, RK3568_PCIE_MSIX_PBA,
			       RK3568_PCIE_MSIX_PBA_OFFSET | FIELD_PREP(RK3568_PCIE_MSIX_BIR, 0));

	/* Use the PIPE_GRF grant path recommended by the TRM for hot reset. */
	sys_write32(RK3568_PCIE_HOT_RESET_LTSSM_ENHANCE,
		    data->client_addr + RK3568_PCIE_CLIENT_HOT_RESET_CTRL);

	/* Unmask PHY/DLL state changes and delayed hot/link-down reset request. */
	rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_INTR_MASK_MISC,
				  RK3568_PCIE_CLIENT_LINK_INTS, 0);
	sys_write32(RK3568_PCIE_CLIENT_LINK_INTS,
		    data->client_addr + RK3568_PCIE_CLIENT_INTR_STATUS_MISC);

	cfg->irq_config_func(dev);

	value = sys_read32(data->client_addr + RK3568_PCIE_CLIENT_LTSSM_STATUS);
	LOG_INF("RK3568 PCIe EP ready, link %s",
		(value & (RK3568_PCIE_LTSSM_SMLH_LINK_UP | RK3568_PCIE_LTSSM_RDLH_LINK_UP)) ==
				(RK3568_PCIE_LTSSM_SMLH_LINK_UP | RK3568_PCIE_LTSSM_RDLH_LINK_UP)
			? "up"
			: "stopped");

	return 0;
}

static DEVICE_API(pcie_ep, rk3568_pcie_ep_api) = {
	.conf_read = rk3568_pcie_conf_read,
	.conf_write = rk3568_pcie_conf_write,
	.map_addr = rk3568_pcie_map_addr,
	.unmap_addr = rk3568_pcie_unmap_addr,
	.raise_irq = rk3568_pcie_raise_irq,
	.register_reset_cb = rk3568_pcie_register_reset_cb,
	.dma_xfer = rk3568_pcie_dma_xfer,
	.set_bar = rk3568_pcie_set_bar,
	.clear_bar = rk3568_pcie_clear_bar,
	.start = rk3568_pcie_start,
	.stop = rk3568_pcie_stop,
	.is_link_up = rk3568_pcie_link_up,
};

#define RK3568_PCIE_EP_DEFINE(inst)                                                                \
	static void rk3568_pcie_ep_irq_config_##inst(const struct device *dev)                     \
	{                                                                                          \
		IRQ_CONNECT(DT_INST_IRQN(inst), DT_INST_IRQ(inst, priority), rk3568_pcie_isr,      \
			    DEVICE_DT_INST_GET(inst), DT_INST_IRQ(inst, flags));                   \
		irq_enable(DT_INST_IRQN(inst));                                                    \
	}                                                                                          \
	static struct rk3568_pcie_ep_data rk3568_pcie_ep_data_##inst;                              \
	static const struct rk3568_pcie_ep_config rk3568_pcie_ep_config_##inst = {                 \
		.dbi_addr = DT_INST_REG_ADDR_BY_NAME(inst, dbi),                                   \
		.dbi_size = DT_INST_REG_SIZE_BY_NAME(inst, dbi),                                   \
		.client_addr = DT_INST_REG_ADDR_BY_NAME(inst, client),                             \
		.client_size = DT_INST_REG_SIZE_BY_NAME(inst, client),                             \
		.map_addr = DT_INST_REG_ADDR_BY_NAME(inst, map),                                   \
		.map_size = DT_INST_REG_SIZE_BY_NAME(inst, map),                                   \
		.cru_addr = DT_INST_REG_ADDR_BY_NAME(inst, cru),                                   \
		.cru_size = DT_INST_REG_SIZE_BY_NAME(inst, cru),                                   \
		.pmu_grf_addr = DT_INST_REG_ADDR_BY_NAME(inst, pmu_grf),                           \
		.pmu_grf_size = DT_INST_REG_SIZE_BY_NAME(inst, pmu_grf),                           \
		.sys_grf_addr = DT_INST_REG_ADDR_BY_NAME(inst, sys_grf),                           \
		.sys_grf_size = DT_INST_REG_SIZE_BY_NAME(inst, sys_grf),                           \
		.pipe_grf_addr = DT_INST_REG_ADDR_BY_NAME(inst, pipe_grf),                         \
		.pipe_grf_size = DT_INST_REG_SIZE_BY_NAME(inst, pipe_grf),                         \
		.phy_grf_addr = DT_INST_REG_ADDR_BY_NAME(inst, phy_grf),                           \
		.phy_grf_size = DT_INST_REG_SIZE_BY_NAME(inst, phy_grf),                           \
		.num_ob_windows = DT_INST_PROP(inst, num_ob_windows),                              \
		.external_refclk = DT_INST_PROP(inst, rockchip_external_refclk),                   \
		.configure_m0_pins = DT_INST_PROP(inst, rockchip_configure_m0_pins),               \
		.vendor_id = DT_INST_PROP_OR(inst, vendor_id, 0),                                  \
		.device_id = DT_INST_PROP_OR(inst, device_id, 0),                                  \
		.class_revision = DT_INST_PROP_OR(inst, class_revision, 0),                        \
		.subsystem_vendor_id = DT_INST_PROP_OR(inst, subsystem_vendor_id, 0),              \
		.subsystem_id = DT_INST_PROP_OR(inst, subsystem_id, 0),                            \
		.irq_config_func = rk3568_pcie_ep_irq_config_##inst,                               \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, rk3568_pcie_ep_init, NULL, &rk3568_pcie_ep_data_##inst,        \
			      &rk3568_pcie_ep_config_##inst, POST_KERNEL,                          \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &rk3568_pcie_ep_api);

DT_INST_FOREACH_STATUS_OKAY(RK3568_PCIE_EP_DEFINE)
