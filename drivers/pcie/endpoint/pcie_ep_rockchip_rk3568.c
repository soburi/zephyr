/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT rockchip_rk3568_pcie_ep

#include <zephyr/device.h>
#include <zephyr/drivers/pcie/endpoint/pcie_ep.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(pcie_ep_rk3568, CONFIG_PCIE_EP_LOG_LEVEL);

/* RK3568 TRM Part 2, chapter 18: PCIe client registers. */
#define RK3568_PCIE_CLIENT_GENERAL_CON		0x0000
#define RK3568_PCIE_CLIENT_MSG_GEN_CON		0x0034
#define RK3568_PCIE_CLIENT_MSI_GEN_CON		0x0038
#define RK3568_PCIE_CLIENT_HOT_RESET_CTRL	0x0180
#define RK3568_PCIE_CLIENT_LTSSM_STATUS		0x0300

#define RK3568_PCIE_CLIENT_DEVICE_TYPE		GENMASK(7, 4)
#define RK3568_PCIE_CLIENT_DEVICE_TYPE_EP	0
#define RK3568_PCIE_CLIENT_LINK_REQ_RST_GRT	BIT(3)
#define RK3568_PCIE_CLIENT_LTSSM_ENABLE		BIT(2)
#define RK3568_PCIE_CLIENT_LEGACY_INT_REQ	BIT(1)

#define RK3568_PCIE_HOT_RESET_LTSSM_ENHANCE	BIT(4)
#define RK3568_PCIE_HOT_RESET_DELAY_LINK_EN	BIT(1)

#define RK3568_PCIE_LTSSM_SMLH_LINK_UP		BIT(16)
#define RK3568_PCIE_LTSSM_RDLH_LINK_UP		BIT(17)

/* Core configuration and port logic registers. */
#define RK3568_PCIE_CFG_VENDOR_DEVICE_ID		0x0000
#define RK3568_PCIE_CFG_CLASS_REVISION		0x0008
#define RK3568_PCIE_CFG_SUBSYSTEM_ID		0x002c
#define RK3568_PCIE_MISC_CONTROL_1		0x08bc
#define RK3568_PCIE_DBI_RO_WR_EN			BIT(0)
#define RK3568_PCIE_MSIX_DOORBELL		0x0948

/* Unrolled iATU register space starts at core offset 0x300000. */
#define RK3568_PCIE_ATU_BASE			0x300000
#define RK3568_PCIE_ATU_REGION_STRIDE		0x200
#define RK3568_PCIE_ATU_REGION_CTRL1		0x00
#define RK3568_PCIE_ATU_REGION_CTRL2		0x04
#define RK3568_PCIE_ATU_LOWER_BASE		0x08
#define RK3568_PCIE_ATU_UPPER_BASE		0x0c
#define RK3568_PCIE_ATU_LIMIT			0x10
#define RK3568_PCIE_ATU_LOWER_TARGET		0x14
#define RK3568_PCIE_ATU_UPPER_TARGET		0x18
#define RK3568_PCIE_ATU_UPPER_LIMIT		0x20

#define RK3568_PCIE_ATU_TYPE_MEM			0
#define RK3568_PCIE_ATU_ENABLE			BIT(31)
#define RK3568_PCIE_ATU_MIN_SIZE			KB(64)
#define RK3568_PCIE_ATU_MAX_REGIONS		16

#define RK3568_PCIE_MAX_MSI			32
#define RK3568_PCIE_MAX_MSIX			64

struct rk3568_pcie_ep_config {
	uintptr_t dbi_addr;
	size_t dbi_size;
	uintptr_t client_addr;
	size_t client_size;
	uint64_t map_addr;
	size_t map_size;
	uint8_t num_ob_windows;
	uint16_t vendor_id;
	uint16_t device_id;
	uint32_t class_revision;
	uint16_t subsystem_vendor_id;
	uint16_t subsystem_id;
};

struct rk3568_pcie_ep_data {
	struct k_spinlock lock;
	mm_reg_t dbi_addr;
	mm_reg_t client_addr;
	mm_reg_t map_addr;
	uint16_t ob_in_use;
};

static inline void rk3568_pcie_hiword_update(uintptr_t addr, uint32_t mask, uint32_t value)
{
	sys_write32((mask << 16) | (value & mask), addr);
}

static inline uintptr_t rk3568_pcie_atu_region(const struct rk3568_pcie_ep_data *data,
					       uint8_t index)
{
	return data->dbi_addr + RK3568_PCIE_ATU_BASE +
	       (index * RK3568_PCIE_ATU_REGION_STRIDE);
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

static int rk3568_pcie_map_addr(const struct device *dev, uint64_t pcie_addr,
				uint64_t *mapped_addr, uint32_t size,
				enum pcie_ob_mem_type ob_mem_type)
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

	region_size = ROUND_UP(MIN((uint64_t)size + offset, window_size),
			       RK3568_PCIE_ATU_MIN_SIZE);
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
	sys_write32((uint32_t)((local + region_size - 1) >> 32),
		    atu + RK3568_PCIE_ATU_UPPER_LIMIT);
	sys_write32((uint32_t)target, atu + RK3568_PCIE_ATU_LOWER_TARGET);
	sys_write32((uint32_t)(target >> 32), atu + RK3568_PCIE_ATU_UPPER_TARGET);
	sys_write32(RK3568_PCIE_ATU_ENABLE, atu + RK3568_PCIE_ATU_REGION_CTRL2);

	if ((sys_read32(atu + RK3568_PCIE_ATU_REGION_CTRL2) & RK3568_PCIE_ATU_ENABLE) == 0U) {
		key = k_spin_lock(&data->lock);
		data->ob_in_use &= ~BIT(index);
		k_spin_unlock(&data->lock, key);
		return -EIO;
	}

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
	sys_write32(0, rk3568_pcie_atu_region(data, index) +
		       RK3568_PCIE_ATU_REGION_CTRL2);

	key = k_spin_lock(&data->lock);
	data->ob_in_use &= ~BIT(index);
	k_spin_unlock(&data->lock, key);
}

static int rk3568_pcie_raise_irq(const struct device *dev,
				 enum pci_ep_irq_type irq_type, uint32_t irq_num)
{
	struct rk3568_pcie_ep_data *data = dev->data;
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
		sys_write32(BIT(irq_num),
			    data->client_addr + RK3568_PCIE_CLIENT_MSI_GEN_CON);
		break;
	case PCIE_EP_IRQ_MSIX:
		if (irq_num >= RK3568_PCIE_MAX_MSIX) {
			ret = -EINVAL;
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

static int rk3568_pcie_ep_init(const struct device *dev)
{
	const struct rk3568_pcie_ep_config *cfg = dev->config;
	struct rk3568_pcie_ep_data *data = dev->data;
	uint32_t value;

	if (cfg->num_ob_windows == 0U ||
	    cfg->num_ob_windows > RK3568_PCIE_ATU_MAX_REGIONS ||
	    cfg->map_size < RK3568_PCIE_ATU_MIN_SIZE ||
	    !IS_POWER_OF_TWO(cfg->num_ob_windows) ||
	    !IS_ALIGNED(cfg->map_addr, RK3568_PCIE_ATU_MIN_SIZE) ||
	    !IS_ALIGNED(cfg->map_size / cfg->num_ob_windows,
			RK3568_PCIE_ATU_MIN_SIZE)) {
		LOG_ERR("invalid outbound window configuration");
		return -EINVAL;
	}

	device_map(&data->dbi_addr, cfg->dbi_addr, cfg->dbi_size, K_MEM_CACHE_NONE);
	device_map(&data->client_addr, cfg->client_addr, cfg->client_size, K_MEM_CACHE_NONE);
	device_map(&data->map_addr, cfg->map_addr, cfg->map_size, K_MEM_CACHE_NONE);

	data->ob_in_use = 0;
	for (uint8_t i = 0; i < cfg->num_ob_windows; i++) {
		sys_write32(0, rk3568_pcie_atu_region(data, i) +
			       RK3568_PCIE_ATU_REGION_CTRL2);
	}

	/* Hold LTSSM while the endpoint configuration space is initialized. */
	rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_GENERAL_CON,
				 RK3568_PCIE_CLIENT_DEVICE_TYPE |
				 RK3568_PCIE_CLIENT_LINK_REQ_RST_GRT |
				 RK3568_PCIE_CLIENT_LTSSM_ENABLE,
				 FIELD_PREP(RK3568_PCIE_CLIENT_DEVICE_TYPE,
					    RK3568_PCIE_CLIENT_DEVICE_TYPE_EP) |
				 RK3568_PCIE_CLIENT_LINK_REQ_RST_GRT);

	if (cfg->vendor_id != 0U || cfg->device_id != 0U) {
		value = ((uint32_t)cfg->device_id << 16) | cfg->vendor_id;
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_VENDOR_DEVICE_ID, value);
	}
	if (cfg->class_revision != 0U) {
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_CLASS_REVISION,
				      cfg->class_revision);
	}
	if (cfg->subsystem_vendor_id != 0U || cfg->subsystem_id != 0U) {
		value = ((uint32_t)cfg->subsystem_id << 16) | cfg->subsystem_vendor_id;
		rk3568_pcie_conf_write(dev, RK3568_PCIE_CFG_SUBSYSTEM_ID, value);
	}

	/* Delay link retraining after a hot reset until software is ready. */
	sys_write32(RK3568_PCIE_HOT_RESET_LTSSM_ENHANCE |
		    RK3568_PCIE_HOT_RESET_DELAY_LINK_EN,
		    data->client_addr + RK3568_PCIE_CLIENT_HOT_RESET_CTRL);

	rk3568_pcie_hiword_update(data->client_addr + RK3568_PCIE_CLIENT_GENERAL_CON,
				 RK3568_PCIE_CLIENT_LTSSM_ENABLE,
				 RK3568_PCIE_CLIENT_LTSSM_ENABLE);

	value = sys_read32(data->client_addr + RK3568_PCIE_CLIENT_LTSSM_STATUS);
	LOG_INF("RK3568 PCIe EP initialized, link %s",
		(value & (RK3568_PCIE_LTSSM_SMLH_LINK_UP |
			  RK3568_PCIE_LTSSM_RDLH_LINK_UP)) ==
			 (RK3568_PCIE_LTSSM_SMLH_LINK_UP |
			  RK3568_PCIE_LTSSM_RDLH_LINK_UP) ? "up" : "training");

	return 0;
}

static DEVICE_API(pcie_ep, rk3568_pcie_ep_api) = {
	.conf_read = rk3568_pcie_conf_read,
	.conf_write = rk3568_pcie_conf_write,
	.map_addr = rk3568_pcie_map_addr,
	.unmap_addr = rk3568_pcie_unmap_addr,
	.raise_irq = rk3568_pcie_raise_irq,
};

#define RK3568_PCIE_EP_DEFINE(inst)							\
	static struct rk3568_pcie_ep_data rk3568_pcie_ep_data_##inst;			\
	static const struct rk3568_pcie_ep_config rk3568_pcie_ep_config_##inst = {	\
		.dbi_addr = DT_INST_REG_ADDR_BY_NAME(inst, dbi),				\
		.dbi_size = DT_INST_REG_SIZE_BY_NAME(inst, dbi),				\
		.client_addr = DT_INST_REG_ADDR_BY_NAME(inst, client),			\
		.client_size = DT_INST_REG_SIZE_BY_NAME(inst, client),			\
		.map_addr = DT_INST_REG_ADDR_BY_NAME(inst, map),				\
		.map_size = DT_INST_REG_SIZE_BY_NAME(inst, map),				\
		.num_ob_windows = DT_INST_PROP(inst, num_ob_windows),			\
		.vendor_id = DT_INST_PROP_OR(inst, vendor_id, 0),				\
		.device_id = DT_INST_PROP_OR(inst, device_id, 0),				\
		.class_revision = DT_INST_PROP_OR(inst, class_revision, 0),		\
		.subsystem_vendor_id = DT_INST_PROP_OR(inst, subsystem_vendor_id, 0),	\
		.subsystem_id = DT_INST_PROP_OR(inst, subsystem_id, 0),			\
	};										\
	DEVICE_DT_INST_DEFINE(inst, rk3568_pcie_ep_init, NULL,				\
			      &rk3568_pcie_ep_data_##inst,				\
			      &rk3568_pcie_ep_config_##inst, POST_KERNEL,		\
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE,			\
			      &rk3568_pcie_ep_api);

DT_INST_FOREACH_STATUS_OKAY(RK3568_PCIE_EP_DEFINE)
