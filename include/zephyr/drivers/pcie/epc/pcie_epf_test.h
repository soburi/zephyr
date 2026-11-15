/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief PCI endpoint test function driver API
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PCIE_EPC_PCIE_EPF_TEST_H_
#define ZEPHYR_INCLUDE_DRIVERS_PCIE_EPC_PCIE_EPF_TEST_H_

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register the PCI endpoint test function devices
 *
 * Registers CONFIG_PCIE_EPF_TEST_FUNC_COUNT endpoint test function devices
 * on the EPC device named @p epc_name.
 *
 * @param epc_name Name of the EPC device the functions bind to
 *
 * @retval 0 Success
 * @retval -ENODEV No EPC device with the given name is registered
 */
int pcie_register_epf_test_device(const char *epc_name);

/**
 * @brief Register the PCI endpoint test function driver
 *
 * @retval 0 Success
 */
int pcie_register_epf_test_driver(void);

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_PCIE_EPC_PCIE_EPF_TEST_H_ */
