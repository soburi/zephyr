/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/drivers/pcie/epc/pcie_epf_test.h>

int main(void)
{
	int ret;

	ret = pcie_register_epf_test_driver();
	if (ret != 0) {
		printk("Failed to register the EPF test driver: %d\n", ret);
		return 0;
	}

	ret = pcie_register_epf_test_device("qemu_epc");
	if (ret != 0) {
		printk("Failed to register the EPF test device: %d\n", ret);
		return 0;
	}

	printk("PCI endpoint test function registered\n");
	return 0;
}
