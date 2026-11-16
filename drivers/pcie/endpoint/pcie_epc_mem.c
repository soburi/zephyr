/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX drivers/pci/pci_epc_mem.c
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>

LOG_MODULE_DECLARE(pcie_epc, CONFIG_PCIE_EPC_LOG_LEVEL);

static struct pcie_epc_mem_s *pcie_epc_mem_find(const struct device *dev,
						uintptr_t phys_addr)
{
	struct pcie_epc_ctrl_s *epc = dev->data;
	unsigned int i;

	for (i = 0; i < epc->num_windows; i++) {
		if (phys_addr >= epc->mem[i].phys_base &&
		    phys_addr < epc->mem[i].phys_base + epc->mem[i].size) {
			return &epc->mem[i];
		}
	}

	return NULL;
}

int pcie_epc_mem_multi_init(const struct device *dev,
			    struct pcie_epc_mem_s *mem,
			    unsigned int num_windows)
{
	struct pcie_epc_ctrl_s *epc;
	unsigned int i;

	if (dev == NULL || mem == NULL || num_windows == 0U) {
		return -EINVAL;
	}

	for (i = 0; i < num_windows; i++) {
		size_t pages;

		if (mem[i].page_size == 0U || mem[i].bitmap == NULL) {
			return -EINVAL;
		}

		pages = mem[i].size / mem[i].page_size;
		if (pages == 0U || pages > mem[i].bitmap->num_bits) {
			return -EINVAL;
		}

		mem[i].pages = pages;
	}

	epc = dev->data;
	epc->mem = mem;
	epc->num_windows = num_windows;
	return 0;
}

int pcie_epc_mem_init(const struct device *dev, struct pcie_epc_mem_s *mem,
		      void *virt, uintptr_t phys, size_t size,
		      size_t page_size)
{
	if (mem == NULL) {
		return -EINVAL;
	}

	mem->virt_base = virt;
	mem->phys_base = phys;
	mem->size = size;
	mem->page_size = page_size;
	return pcie_epc_mem_multi_init(dev, mem, 1);
}

void pcie_epc_mem_exit(const struct device *dev)
{
	struct pcie_epc_ctrl_s *epc = dev->data;

	epc->mem = NULL;
	epc->num_windows = 0;
}

void *pcie_epc_mem_alloc_addr(const struct device *dev, uintptr_t *phys,
			      size_t size)
{
	struct pcie_epc_ctrl_s *epc = dev->data;
	unsigned int i;

	for (i = 0; i < epc->num_windows; i++) {
		struct pcie_epc_mem_s *mem = &epc->mem[i];
		size_t pages = DIV_ROUND_UP(size, mem->page_size);
		size_t pageno;

		if (sys_bitarray_alloc(mem->bitmap, pages, &pageno) == 0) {
			*phys = mem->phys_base + pageno * mem->page_size;
			return (uint8_t *)mem->virt_base +
			       pageno * mem->page_size;
		}
	}

	return NULL;
}

void pcie_epc_mem_free_addr(const struct device *dev, uintptr_t phys_addr,
			    size_t size)
{
	struct pcie_epc_mem_s *mem;
	size_t pageno;
	size_t pages;

	mem = pcie_epc_mem_find(dev, phys_addr);
	if (mem == NULL) {
		LOG_ERR("Failed to get matching window");
		return;
	}

	pageno = (phys_addr - mem->phys_base) / mem->page_size;
	pages = DIV_ROUND_UP(size, mem->page_size);

	sys_bitarray_free(mem->bitmap, pages, pageno);
}
