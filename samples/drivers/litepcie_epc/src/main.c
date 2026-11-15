/*
 * Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cache.h>
#include <zephyr/drivers/pcie/epc/pcie_epc.h>
#include <zephyr/sys/sys_io.h>

#define BAR_SIZE DT_PROP(DT_NODELABEL(pcie_epc), bar0_size)

static uint32_t bar_memory[BAR_SIZE / sizeof(uint32_t)] __aligned(64);

static int shared_bar_bind(struct pcie_epf_device_s *epf)
{
	struct pcie_epf_bar_s *bar = &epf->bar[0];

	bar->phys_addr = (uintptr_t)bar_memory;
	bar->addr = bar_memory;
	bar->size = sizeof(bar_memory);
	bar->barno = 0;
	bar->flags = PCI_BASE_ADDRESS_MEM_TYPE_32;
	return pcie_epc_set_bar(epf->epc, epf->funcno, bar);
}

static void shared_bar_unbind(struct pcie_epf_device_s *epf)
{
	pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[0]);
}

int main(void)
{
	static const struct pcie_epf_ops_s ops = {
		.bind = shared_bar_bind,
		.unbind = shared_bar_unbind,
	};
	static struct pcie_epf_device_s epf = {.ops = &ops};
	const struct device *epc = DEVICE_DT_GET(DT_NODELABEL(pcie_epc));
	uintptr_t base = (uintptr_t)bar_memory;
	uint32_t heartbeat = 0;
	int ret;

	if (!device_is_ready(epc)) {
		printk("LitePCIe EPC is not ready\n");
		return -ENODEV;
	}

	/* The fabric PCIe master is not coherent with the AE350 data cache. */
	ret = sys_cache_data_flush_all();
	if (ret != 0) {
		printk("Cannot flush the data cache: %d\n", ret);
		return ret;
	}
	sys_cache_data_disable();
	sys_write32(0x45504331U, base);
	ret = pcie_epc_add_epf(epc, &epf);
	if (ret != 0) {
		printk("Cannot bind BAR0: %d\n", ret);
		return ret;
	}
	ret = pcie_epc_start(epc);
	if (ret != 0) {
		pcie_epc_remove_epf(epc, &epf);
		printk("Cannot start the endpoint: %d\n", ret);
		return ret;
	}

	printk("LitePCIe BAR0: %u bytes at CPU address 0x%lx\n", BAR_SIZE,
	       (unsigned long)base);
	while (true) {
		sys_write32(sys_read32(base + 4U), base + 8U);
		sys_write32(++heartbeat, base + 12U);
		k_sleep(K_MSEC(10));
	}
	return 0;
}
