/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT litex_pcie_epc

#include <zephyr/drivers/pcie/epc/pcie_epc.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <errno.h>

#define EPC_CONTROL 0x00U
#define EPC_BAR_BASE 0x04U
#define EPC_BAR_SIZE 0x08U
#define EPC_LINK_ENABLE BIT(0)
#define EPC_BAR_ENABLE BIT(1)

struct litepcie_config {
	uintptr_t base;
	struct pcie_epc_features_s features;
};

struct litepcie_data {
	/* Must stay first, the device data starts with the EPC control */
	struct pcie_epc_ctrl_s epc;
	bool bar_set;
};

BUILD_ASSERT(offsetof(struct litepcie_data, epc) == 0,
	     "the EPC control structure must be the first member");

static int litepcie_set_bar(const struct device *dev, uint8_t funcno,
			  struct pcie_epf_bar_s *bar)
{
	const struct litepcie_config *cfg = dev->config;
	struct litepcie_data *data = dev->data;
	size_t size = cfg->features.bar_fixed_size[0];

	if (funcno != 0U || bar->barno != 0 || bar->size != size ||
	    bar->flags != PCI_BASE_ADDRESS_MEM_TYPE_32 ||
	    (bar->phys_addr & 3U) != 0U || bar->phys_addr > UINT32_MAX - (size - 1U)) {
		return -EINVAL;
	}
	if (data->epc.started) {
		return -EBUSY;
	}
	sys_write32(bar->phys_addr, cfg->base + EPC_BAR_BASE);
	data->bar_set = true;
	return 0;
}

static void litepcie_clear_bar(const struct device *dev, uint8_t funcno,
			      struct pcie_epf_bar_s *bar)
{
	const struct litepcie_config *cfg = dev->config;
	struct litepcie_data *data = dev->data;

	if (funcno == 0U && bar->barno == 0) {
		sys_write32(0U, cfg->base + EPC_CONTROL);
		data->bar_set = false;
	}
}

static int litepcie_write_header(const struct device *dev, uint8_t funcno,
				struct pcie_epf_header_s *header)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(funcno);
	ARG_UNUSED(header);
	/* The Gowin hard IP configuration is fixed in the generated IP. */
	return -ENOTSUP;
}

static int litepcie_start(const struct device *dev)
{
	const struct litepcie_config *cfg = dev->config;
	struct litepcie_data *data = dev->data;

	if (!data->bar_set) {
		return -EINVAL;
	}
	sys_write32(EPC_LINK_ENABLE | EPC_BAR_ENABLE, cfg->base + EPC_CONTROL);
	return 0;
}

static void litepcie_stop(const struct device *dev)
{
	const struct litepcie_config *cfg = dev->config;

	sys_write32(0U, cfg->base + EPC_CONTROL);
}

static const struct pcie_epc_features_s *litepcie_features(const struct device *dev,
						       uint8_t funcno)
{
	const struct litepcie_config *cfg = dev->config;

	return funcno == 0U ? &cfg->features : NULL;
}

static DEVICE_API(pcie_epc, litepcie_api) = {
	.write_header = litepcie_write_header,
	.set_bar = litepcie_set_bar,
	.clear_bar = litepcie_clear_bar,
	.start = litepcie_start,
	.stop = litepcie_stop,
	.get_features = litepcie_features,
};

static int litepcie_init(const struct device *dev)
{
	const struct litepcie_config *cfg = dev->config;
	struct litepcie_data *data = dev->data;
	int ret;

	litepcie_stop(dev);
	if (sys_read32(cfg->base + EPC_BAR_SIZE) != cfg->features.bar_fixed_size[0]) {
		return -EINVAL;
	}
	ret = pcie_epc_init(dev, NULL);
	if (ret == 0) {
		data->epc.max_functions = 1U;
	}
	return ret;
}

#define LITEPCIE_DEFINE(n)                                                   \
	static struct litepcie_data litepcie_data_##n;                        \
	static const struct litepcie_config litepcie_cfg_##n = {              \
		.base = DT_INST_REG_ADDR(n),                                 \
		.features = {                                               \
			.bar_reserved = 0x3eU,                               \
			.bar_fixed_size = {DT_INST_PROP(n, bar0_size)},       \
			.align = 4U,                                        \
		},                                                          \
	};                                                                  \
	DEVICE_DT_INST_DEFINE(n, litepcie_init, NULL, &litepcie_data_##n,      \
			      &litepcie_cfg_##n, POST_KERNEL,                 \
			      CONFIG_PCIE_EPC_INIT_PRIORITY, &litepcie_api);

DT_INST_FOREACH_STATUS_OKAY(LITEPCIE_DEFINE)
