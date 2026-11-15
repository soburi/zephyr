/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX drivers/pci/pcie_epf.c
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pcie/epc/pcie_epc.h>
#include <zephyr/drivers/pcie/epc/pcie_epf.h>

#ifdef CONFIG_MMU
#include <zephyr/kernel/internal/mm.h>
#endif

LOG_MODULE_DECLARE(pcie_epc, CONFIG_PCIE_EPC_LOG_LEVEL);

static sys_dlist_t g_pcie_epf_device_list =
	SYS_DLIST_STATIC_INIT(&g_pcie_epf_device_list);
static sys_dlist_t g_pcie_epf_driver_list =
	SYS_DLIST_STATIC_INIT(&g_pcie_epf_driver_list);
static K_MUTEX_DEFINE(g_pcie_epf_lock);

static const struct pcie_epf_device_id_s *
pcie_epf_match_id(const struct pcie_epf_device_s *dev,
		 const struct pcie_epf_device_id_s *id)
{
	while (id->name[0] != '\0') {
		if (strcmp(dev->name, id->name) == 0) {
			return id;
		}

		id++;
	}

	return NULL;
}

static const struct pcie_epf_device_id_s *
pcie_epf_match_device(struct pcie_epf_device_s *dev,
		     struct pcie_epf_driver_s *drv)
{
	if (drv->id_table != NULL) {
		return pcie_epf_match_id(dev, drv->id_table);
	} else {
		return NULL;
	}
}

/*
 * Notify the function driver that the binding between the EPF device and
 * EPC device has been lost.
 */
static void pcie_epf_unbind(struct pcie_epf_device_s *epf)
{
	if (epf->driver == NULL || epf->driver->ops->unbind == NULL) {
		LOG_ERR("Invalid epf parameter, unbind failed!");
		return;
	}

	k_mutex_lock(&epf->lock, K_FOREVER);
	if (epf->is_bound) {
		epf->driver->ops->unbind(epf);
		epf->is_bound = false;
	}

	k_mutex_unlock(&epf->lock);
}

/*
 * Notify the function driver that the EPF device has been bound to a EPC
 * device.
 */
static int pcie_epf_bind(struct pcie_epf_device_s *epf)
{
	int ret;

	if (epf->driver == NULL || epf->driver->ops->bind == NULL) {
		LOG_ERR("Invalid epf parameter, bind failed!");
		return -EINVAL;
	}

	k_mutex_lock(&epf->lock, K_FOREVER);
	ret = epf->driver->ops->bind(epf);
	if (ret >= 0) {
		epf->is_bound = true;
	}

	k_mutex_unlock(&epf->lock);
	return ret;
}

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

int pcie_epf_device_register(struct pcie_epf_device_s *epf)
{
	struct pcie_epf_driver_s *drv;
	const struct device *epc;
	int ret = 0;

	__ASSERT_NO_MSG(epf != NULL && epf->name != NULL);

	k_mutex_init(&epf->lock);

	k_mutex_lock(&g_pcie_epf_lock, K_FOREVER);

	sys_dlist_append(&g_pcie_epf_device_list, &epf->node);

	SYS_DLIST_FOR_EACH_CONTAINER(&g_pcie_epf_driver_list, drv, node) {
		epf->id = pcie_epf_match_device(epf, drv);
		if (epf->id == NULL) {
			continue;
		}

		/* Find the specified EPC by epc_name in epf structure */
		epc = pcie_get_epc(epf->epc_name);
		if (epc == NULL) {
			ret = -ENODEV;
			break;
		}

		ret = drv->probe(epf);
		if (ret < 0) {
			continue;
		}

		epf->driver = drv;

		/* Add the epc to epf */
		pcie_epc_add_epf(epc, epf);

		/* Notify the function driver */
		pcie_epf_bind(epf);
		break;
	}

	k_mutex_unlock(&g_pcie_epf_lock);
	return ret;
}

int pcie_epf_device_unregister(struct pcie_epf_device_s *epf)
{
	const struct device *epc;

	k_mutex_lock(&g_pcie_epf_lock, K_FOREVER);

	pcie_epf_unbind(epf);
	epc = pcie_get_epc(epf->epc_name);
	if (epc != NULL) {
		pcie_epc_remove_epf(epc, epf);
	}

	if (epf->driver != NULL && epf->driver->remove != NULL) {
		epf->driver->remove(epf);
	}

	epf->driver = NULL;
	sys_dlist_remove(&epf->node);

	k_mutex_unlock(&g_pcie_epf_lock);
	return 0;
}

int pcie_epf_register_driver(struct pcie_epf_driver_s *drv)
{
	struct pcie_epf_device_s *epf;
	const struct device *epc;
	int ret = 0;

	if (drv == NULL || drv->ops == NULL || drv->ops->bind == NULL ||
	    drv->id_table == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&g_pcie_epf_lock, K_FOREVER);

	/* Add the driver to the pci epf driver list */
	sys_dlist_append(&g_pcie_epf_driver_list, &drv->node);

	SYS_DLIST_FOR_EACH_CONTAINER(&g_pcie_epf_device_list, epf, node) {
		if (epf->driver != NULL) {
			continue;
		}

		/* 1. Match epf device and epf driver.
		 * 2. Get the epc by name.
		 * 3. Add epc into epf.
		 * 4. Notify the epf driver be bound.
		 */
		epf->id = pcie_epf_match_device(epf, drv);
		if (epf->id == NULL) {
			continue;
		}

		epc = pcie_get_epc(epf->epc_name);
		if (epc == NULL) {
			ret = -ENODEV;
			break;
		}

		ret = drv->probe(epf);
		if (ret < 0) {
			continue;
		}

		epf->driver = drv;

		/* Add the epc to epf */
		pcie_epc_add_epf(epc, epf);

		/* Notify the function driver */
		pcie_epf_bind(epf);
	}

	k_mutex_unlock(&g_pcie_epf_lock);
	return ret;
}

int pcie_epf_unregister_driver(struct pcie_epf_driver_s *drv)
{
	struct pcie_epf_device_s *epf;

	__ASSERT_NO_MSG(drv != NULL);

	k_mutex_lock(&g_pcie_epf_lock, K_FOREVER);

	SYS_DLIST_FOR_EACH_CONTAINER(&g_pcie_epf_device_list, epf, node) {
		if (epf->driver == drv) {
			pcie_epf_unbind(epf);
			if (drv->remove != NULL) {
				drv->remove(epf);
			}

			epf->driver = NULL;
		}
	}

	sys_dlist_remove(&drv->node);

	k_mutex_unlock(&g_pcie_epf_lock);
	return 0;
}
