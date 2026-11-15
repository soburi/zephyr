/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pcie/epc/pcie_epc.h>
#include <zephyr/drivers/pcie/epc/pcie_epf_test.h>

int main(void)
{
	const struct device *epc;
	int ret;

	epc = pcie_get_epc("qemu_epc");
	if (epc == NULL) {
		printk("EPC device not found\n");
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
