/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX drivers/pci/pcie_epf.c
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epf.h>

#ifdef CONFIG_MMU
#include <zephyr/kernel/internal/mm.h>
#endif

LOG_MODULE_DECLARE(pcie_epc, CONFIG_PCIE_EPC_LOG_LEVEL);

void pcie_epf_free_space(struct pcie_epf_device_s *epf, int barno, void *addr)
{
	struct pcie_epf_bar_s *bar;

	if (addr == NULL || barno >= PCI_STD_NUM_BARS) {
		return;
	}

	bar = epf->bar;

	pcie_epc_dma_free(epf->epc, addr);

	bar[barno].phys_addr = 0;
	bar[barno].addr = NULL;
	bar[barno].size = 0;
	bar[barno].barno = 0;
	bar[barno].flags = 0;
}

static size_t pcie_epf_roundup_pow_of_two(size_t size)
{
	size_t pow = 1;

	while (pow < size) {
		pow <<= 1;
	}

	return pow;
}

void *pcie_epf_alloc_space(struct pcie_epf_device_s *epf, int barno,
			  size_t size, size_t align)
{
	struct pcie_epf_bar_s *bar;
	uintptr_t phys_addr;
	void *space;

	if (epf == NULL || barno >= PCI_STD_NUM_BARS) {
		return NULL;
	}

	if (size < 128U) {
		size = 128U;
	}

	if (align != 0U) {
		size = ROUND_UP(size, align);
	} else {
		size = pcie_epf_roundup_pow_of_two(size);
	}

	bar = epf->bar;

	space = pcie_epc_dma_memalign(epf->epc, align, size);
	if (space == NULL) {
		LOG_ERR("Failed to allocate mem space");
		return NULL;
	}

#ifdef CONFIG_MMU
	phys_addr = k_mem_phys_addr(space);
#else
	phys_addr = (uintptr_t)space;
#endif

	bar[barno].phys_addr = phys_addr;
	bar[barno].addr = space;
	bar[barno].size = size;
	bar[barno].barno = barno;
	bar[barno].flags |= (size > UINT32_MAX) ?
			    PCI_BASE_ADDRESS_MEM_TYPE_64 :
			    PCI_BASE_ADDRESS_MEM_TYPE_32;

	return space;
}
