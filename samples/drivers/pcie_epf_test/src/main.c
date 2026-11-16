/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epf_test.h>

int main(void)
{
	const struct device *epc = DEVICE_DT_GET(DT_NODELABEL(qemu_epc));
	int ret;

	if (!device_is_ready(epc)) {
		printk("EPC device is not ready\n");
		return 0;
	}

	ret = pcie_epf_test_add(epc);
	if (ret != 0) {
		printk("Failed to add the EPF test functions: %d\n", ret);
		return 0;
	}

	ret = pcie_epc_start(epc);
	if (ret != 0) {
		printk("Failed to start the PCI link: %d\n", ret);
		return 0;
	}

	printk("PCI endpoint test function started\n");
	return 0;
}
