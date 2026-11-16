/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief PCI endpoint test function driver API
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PCIE_ENDPOINT_PCIE_EPF_TEST_H_
#define ZEPHYR_INCLUDE_DRIVERS_PCIE_ENDPOINT_PCIE_EPF_TEST_H_

#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Add the PCI endpoint test functions to an EPC device
 *
 * Adds CONFIG_PCIE_EPF_TEST_FUNC_COUNT endpoint test functions to the EPC
 * device. The link is not started; invoke pcie_epc_start() afterwards.
 *
 * @param epc The EPC device the functions are added to
 *
 * @retval 0 Success
 * @retval -EBUSY The test functions are already added to an EPC device
 * @retval -EBUSY The link of the EPC device is already started
 * @retval -errno Other negative errno returned by pcie_epc_add_epf()
 */
int pcie_epf_test_add(const struct device *epc);

/**
 * @brief Remove the PCI endpoint test functions from an EPC device
 *
 * @param epc The EPC device the functions were added to
 */
void pcie_epf_test_remove(const struct device *epc);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_PCIE_ENDPOINT_PCIE_EPF_TEST_H_ */
