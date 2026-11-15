# Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(
  pcie_epc.c
  pcie_epc_mem.c
  pcie_epf.c
)

zephyr_library_sources_ifdef(CONFIG_PCIE_EPC_QEMU pcie_qemu_epc.c)
zephyr_library_sources_ifdef(CONFIG_PCIE_EPF_TEST pcie_epf_test.c)

zephyr_library_sources_ifdef(CONFIG_PCIE_EPC_LITEPCIE pcie_litepcie_epc.c)
