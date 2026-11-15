/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX drivers/pci/pci_epc.c
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pcie/epc/pcie_epc.h>

LOG_MODULE_REGISTER(pcie_epc, CONFIG_PCIE_EPC_LOG_LEVEL);

static K_MUTEX_DEFINE(g_pcie_epc_lock);
static sys_dlist_t g_pcie_epc_device_list =
	SYS_DLIST_STATIC_INIT(&g_pcie_epc_device_list);

const struct device *pcie_get_epc(const char *epc_name)
{
	const struct device *res = NULL;
	struct pcie_epc_ctrl_s *epc;

	__ASSERT_NO_MSG(epc_name != NULL);

	k_mutex_lock(&g_pcie_epc_lock, K_FOREVER);
	SYS_DLIST_FOR_EACH_CONTAINER(&g_pcie_epc_device_list, epc, node) {
		if (strcmp(epc_name, epc->name) == 0) {
			res = epc->dev;
			break;
		}
	}

	k_mutex_unlock(&g_pcie_epc_lock);
	return res;
}

int pcie_epc_get_next_free_bar(const struct pcie_epc_features_s *epc_features,
			       int barno)
{
	unsigned long free_bar;

	if (epc_features == NULL) {
		return -EINVAL;
	}

	/* If 'bar - 1' is a 64-bit BAR, move to the next BAR */
	if (((epc_features->bar_fixed_64bit << 1) & BIT(barno)) != 0U) {
		barno++;
	}

	/* Find if the reserved BAR is also a 64-bit BAR */
	free_bar = epc_features->bar_reserved & epc_features->bar_fixed_64bit;

	/* Set the adjacent bit if the reserved BAR is also a 64-bit BAR */
	free_bar <<= 1;
	free_bar |= epc_features->bar_reserved;

	while (barno < PCI_STD_NUM_BARS && (free_bar & BIT(barno)) != 0U) {
		barno++;
	}

	if (barno >= PCI_STD_NUM_BARS) {
		return -ENOENT;
	}

	return barno;
}

const struct pcie_epc_features_s *
pcie_epc_get_features(const struct device *dev, uint8_t funcno)
{
	const struct pcie_epc_features_s *epc_features;
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return NULL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (epc->ops->get_features == NULL || funcno >= epc->max_functions) {
		return NULL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc_features = epc->ops->get_features(dev, funcno);
	k_mutex_unlock(&epc->lock);
	return epc_features;
}

void pcie_epc_stop(const struct device *dev)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (epc->ops->stop == NULL) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc->ops->stop(dev);
	k_mutex_unlock(&epc->lock);
}

int pcie_epc_start(const struct device *dev)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (epc->ops->start == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->start(dev);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_raise_irq(const struct device *dev, uint8_t funcno,
		       enum pcie_epc_irq_type_e type, uint16_t interrupt_num)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (epc->ops->raise_irq == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->raise_irq(dev, funcno, type, interrupt_num);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_map_msi_irq(const struct device *dev, uint8_t funcno,
			 uintptr_t phys_addr, uint8_t interrupt_num,
			 uint32_t entry_size, uint32_t *msi_data,
			 uint32_t *msi_addr_offset)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || epc->ops->map_msi_irq == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->map_msi_irq(dev, funcno, phys_addr, interrupt_num,
				    entry_size, msi_data, msi_addr_offset);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_get_msi(const struct device *dev, uint8_t funcno)
{
	struct pcie_epc_ctrl_s *epc;
	int interrupt;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || epc->ops->get_msi == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	interrupt = epc->ops->get_msi(dev, funcno);
	k_mutex_unlock(&epc->lock);

	if (interrupt >= 0) {
		interrupt = 1 << interrupt;
	}

	return interrupt;
}

int pcie_epc_set_msi(const struct device *dev, uint8_t funcno,
		     uint8_t interrupts)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || interrupts < 1U ||
	    interrupts > 32U || epc->ops->set_msi == NULL) {
		return -EINVAL;
	}

	/* Encode the number of vectors as log2, as used by the Multiple
	 * Message Capable field of the MSI Message Control register.
	 */
	interrupts = find_msb_set(interrupts - 1U);

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->set_msi(dev, funcno, interrupts);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_get_msix(const struct device *dev, uint8_t funcno)
{
	struct pcie_epc_ctrl_s *epc;
	int interrupt;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || epc->ops->get_msix == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	interrupt = epc->ops->get_msix(dev, funcno);
	k_mutex_unlock(&epc->lock);

	if (interrupt >= 0) {
		interrupt += 1;
	}

	return interrupt;
}

int pcie_epc_set_msix(const struct device *dev, uint8_t funcno,
		      uint16_t interrupts, int barno, uint32_t offset)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || interrupts < 1U ||
	    interrupts > 2048U || epc->ops->set_msix == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->set_msix(dev, funcno, interrupts - 1U, barno, offset);
	k_mutex_unlock(&epc->lock);
	return ret;
}

void pcie_epc_unmap_addr(const struct device *dev, uint8_t funcno,
			 uintptr_t phys_addr)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || epc->ops->unmap_addr == NULL) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc->ops->unmap_addr(dev, funcno, phys_addr);
	k_mutex_unlock(&epc->lock);
}

int pcie_epc_map_addr(const struct device *dev, uint8_t funcno,
		      uintptr_t phys_addr, uint64_t pci_addr, size_t size)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || epc->ops->map_addr == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->map_addr(dev, funcno, phys_addr, pci_addr, size);
	k_mutex_unlock(&epc->lock);
	return ret;
}

void pcie_epc_clear_bar(const struct device *dev, uint8_t funcno,
			struct pcie_epf_bar_s *bar)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions || epc->ops->clear_bar == NULL ||
	    (bar->barno == PCI_STD_NUM_BARS - 1 &&
	     (bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0)) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc->ops->clear_bar(dev, funcno, bar);
	k_mutex_unlock(&epc->lock);
}

int pcie_epc_set_bar(const struct device *dev, uint8_t funcno,
		     struct pcie_epf_bar_s *bar)
{
	struct pcie_epc_ctrl_s *epc;
	int flags = bar->flags;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (funcno >= epc->max_functions ||
	    (bar->barno == PCI_STD_NUM_BARS - 1 &&
	     (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0) ||
	    (bar->size > UINT32_MAX &&
	     (flags & PCI_BASE_ADDRESS_MEM_TYPE_64) == 0) ||
	    epc->ops->set_bar == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->set_bar(dev, funcno, bar);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_write_header(const struct device *dev, uint8_t funcno,
			  struct pcie_epf_header_s *header)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	if (epc->ops->write_header == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc->ops->write_header(dev, funcno, header);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_add_epf(const struct device *dev, struct pcie_epf_device_s *epf)
{
	struct pcie_epc_ctrl_s *epc;
	uint32_t funcno;
	int ret = 0;

	__ASSERT_NO_MSG(dev != NULL && epf != NULL);

	if (epf->epc != NULL) {
		return -EBUSY;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&epc->lock, K_FOREVER);

	for (funcno = 0; funcno < epc->max_functions; funcno++) {
		if ((epc->funcno_map & BIT(funcno)) == 0U) {
			break;
		}
	}

	if (funcno >= epc->max_functions) {
		LOG_ERR("Exceeding max supported Function Number");
		ret = -ENOENT;
		goto out;
	}

	epc->funcno_map |= BIT(funcno);
	epf->funcno = funcno;
	epf->epc = dev;
	sys_dlist_append(&epc->epf, &epf->epc_node);

out:
	k_mutex_unlock(&epc->lock);
	return ret;
}

void pcie_epc_remove_epf(const struct device *dev,
			 struct pcie_epf_device_s *epf)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL || epf == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&epc->lock, K_FOREVER);
	epc->funcno_map &= ~BIT(epf->funcno);
	if (sys_dnode_is_linked(&epf->epc_node)) {
		sys_dlist_remove(&epf->epc_node);
	}

	epf->epc = NULL;
	k_mutex_unlock(&epc->lock);
}

void pcie_epc_linkup(const struct device *dev)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&epc->lock, K_FOREVER);
	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		k_mutex_lock(&epf->lock, K_FOREVER);
		if (epf->event_ops != NULL && epf->event_ops->link_up != NULL) {
			epf->event_ops->link_up(epf);
		}

		k_mutex_unlock(&epf->lock);
	}

	k_mutex_unlock(&epc->lock);
}

void pcie_epc_linkdown(const struct device *dev)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&epc->lock, K_FOREVER);
	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		k_mutex_lock(&epf->lock, K_FOREVER);
		if (epf->event_ops != NULL &&
		    epf->event_ops->link_down != NULL) {
			epf->event_ops->link_down(epf);
		}

		k_mutex_unlock(&epf->lock);
	}

	k_mutex_unlock(&epc->lock);
}

void pcie_epc_init_notify(const struct device *dev)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&epc->lock, K_FOREVER);
	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		k_mutex_lock(&epf->lock, K_FOREVER);
		if (epf->event_ops != NULL &&
		    epf->event_ops->core_init != NULL) {
			epf->event_ops->core_init(epf);
		}

		k_mutex_unlock(&epf->lock);
	}

	k_mutex_unlock(&epc->lock);
}

void pcie_epc_bme_notify(const struct device *dev)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&epc->lock, K_FOREVER);
	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		k_mutex_lock(&epf->lock, K_FOREVER);
		if (epf->event_ops != NULL && epf->event_ops->bme != NULL) {
			epf->event_ops->bme(epf);
		}

		k_mutex_unlock(&epf->lock);
	}

	k_mutex_unlock(&epc->lock);
}

int pcie_epc_create(const struct device *dev, const char *name,
		    struct k_heap *dmaheap, const struct pcie_epc_ops_s *ops)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL || name == NULL || ops == NULL) {
		return -EINVAL;
	}

	epc = pcie_epc_get_ctrl(dev);
	epc->dev = dev;
	epc->name = name;
	epc->dmaheap = dmaheap;
	k_mutex_init(&epc->lock);
	sys_dlist_init(&epc->epf);
	epc->ops = ops;

	k_mutex_lock(&g_pcie_epc_lock, K_FOREVER);
	sys_dlist_append(&g_pcie_epc_device_list, &epc->node);
	k_mutex_unlock(&g_pcie_epc_lock);

	return 0;
}

void pcie_epc_destroy(const struct device *dev)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return;
	}

	epc = pcie_epc_get_ctrl(dev);
	k_mutex_lock(&g_pcie_epc_lock, K_FOREVER);
	sys_dlist_remove(&epc->node);
	k_mutex_unlock(&g_pcie_epc_lock);
}

void *pcie_epc_dma_alloc(const struct device *dev, size_t size)
{
	struct pcie_epc_ctrl_s *epc = pcie_epc_get_ctrl(dev);

	if (epc->dmaheap == NULL) {
		return NULL;
	}

	return k_heap_alloc(epc->dmaheap, size, K_NO_WAIT);
}

void *pcie_epc_dma_memalign(const struct device *dev, size_t alignment,
			    size_t size)
{
	struct pcie_epc_ctrl_s *epc = pcie_epc_get_ctrl(dev);

	if (epc->dmaheap == NULL) {
		return NULL;
	}

	if (alignment == 0U) {
		alignment = sizeof(void *);
	}

	return k_heap_aligned_alloc(epc->dmaheap, alignment, size, K_NO_WAIT);
}

void pcie_epc_dma_free(const struct device *dev, void *mem)
{
	struct pcie_epc_ctrl_s *epc = pcie_epc_get_ctrl(dev);

	if (epc->dmaheap == NULL) {
		return;
	}

	k_heap_free(epc->dmaheap, mem);
}
