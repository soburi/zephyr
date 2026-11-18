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
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>

LOG_MODULE_REGISTER(pcie_epc, CONFIG_PCIE_EPC_LOG_LEVEL);

static inline bool pcie_epc_funcno_valid(const struct device *dev, uint8_t funcno)
{
	const struct pcie_epc_config *cfg = dev->config;

	return funcno < cfg->max_functions;
}

static inline const struct pcie_epc_driver_api *pcie_epc_api(const struct device *dev)
{
	return DEVICE_API_GET(pcie_epc, dev);
}

int pcie_epc_bar_access_enable(const struct device *dev, uint8_t funcno,
			uint8_t bar, bool enable)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL || bar >= PCI_STD_NUM_BARS) {
		return -EINVAL;
	}
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}
	if (pcie_epc_api(dev)->bar_access_enable == NULL) {
		return -ENOTSUP;
	}
	epc = dev->data;
	k_mutex_lock(&epc->lock, K_FOREVER);
	if (epc->started) {
		k_mutex_unlock(&epc->lock);
		return -EBUSY;
	}
	ret = pcie_epc_api(dev)->bar_access_enable(dev, funcno, bar, enable);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_bar_access_get(const struct device *dev, uint8_t funcno,
			struct pcie_epc_bar_access *access)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL || access == NULL) {
		return -EINVAL;
	}
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}
	if (pcie_epc_api(dev)->bar_access_get == NULL) {
		return -ENOTSUP;
	}
	epc = dev->data;
	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->bar_access_get(dev, funcno, access);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_bar_access_complete(const struct device *dev, uint8_t funcno,
			const struct pcie_epc_bar_access *access, int status)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL || access == NULL) {
		return -EINVAL;
	}
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}
	if (pcie_epc_api(dev)->bar_access_complete == NULL) {
		return -ENOTSUP;
	}
	epc = dev->data;
	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->bar_access_complete(dev, funcno, access, status);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_set_legacy_irq(const struct device *dev, uint8_t funcno,
			bool asserted)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}
	if (pcie_epc_api(dev)->set_legacy_irq == NULL) {
		return -ENOTSUP;
	}
	epc = dev->data;
	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->set_legacy_irq(dev, funcno, asserted);
	k_mutex_unlock(&epc->lock);
	return ret;
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

	epc = dev->data;
	if (pcie_epc_api(dev)->get_features == NULL || !pcie_epc_funcno_valid(dev, funcno)) {
		return NULL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc_features = pcie_epc_api(dev)->get_features(dev, funcno);
	k_mutex_unlock(&epc->lock);
	return epc_features;
}

int pcie_epc_stop(const struct device *dev)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = dev->data;
	if (pcie_epc_api(dev)->stop == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->stop(dev);
	if (ret == 0) {
		epc->started = false;
	}
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_start(const struct device *dev)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = dev->data;
	if (pcie_epc_api(dev)->start == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->start(dev);
	if (ret >= 0) {
		epc->started = true;
	}

	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_raise_irq(const struct device *dev, uint8_t funcno,
		       enum pcie_epc_irq_type type, uint16_t interrupt_num)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->raise_irq == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->raise_irq(dev, funcno, type, interrupt_num);
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->map_msi_irq == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->map_msi_irq(dev, funcno, phys_addr, interrupt_num,
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->get_msi == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	interrupt = pcie_epc_api(dev)->get_msi(dev, funcno);
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno) || interrupts < 1U || interrupts > 32U) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->set_msi == NULL) {
		return -ENOTSUP;
	}

	/* Encode the number of vectors as log2, as used by the Multiple
	 * Message Capable field of the MSI Message Control register.
	 */
	interrupts = find_msb_set(interrupts - 1U);

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->set_msi(dev, funcno, interrupts);
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->get_msix == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	interrupt = pcie_epc_api(dev)->get_msix(dev, funcno);
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno) || interrupts < 1U || interrupts > 2048U) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->set_msix == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->set_msix(dev, funcno, interrupts - 1U, barno, offset);
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno) || pcie_epc_api(dev)->unmap_addr == NULL) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	pcie_epc_api(dev)->unmap_addr(dev, funcno, phys_addr);
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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->map_addr == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->map_addr(dev, funcno, phys_addr, pci_addr, size);
	k_mutex_unlock(&epc->lock);
	return ret;
}

void pcie_epc_clear_bar(const struct device *dev, uint8_t funcno,
			struct pcie_epf_bar_s *bar)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL || bar == NULL) {
		return;
	}

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno) || pcie_epc_api(dev)->clear_bar == NULL ||
	    (bar->barno == PCI_STD_NUM_BARS - 1 &&
	     (bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0)) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	pcie_epc_api(dev)->clear_bar(dev, funcno, bar);
	k_mutex_unlock(&epc->lock);
}

int pcie_epc_set_bar(const struct device *dev, uint8_t funcno,
		     struct pcie_epf_bar_s *bar)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL || bar == NULL) {
		return -EINVAL;
	}

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno) ||
	    (bar->barno == PCI_STD_NUM_BARS - 1 &&
	     (bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64) != 0) ||
	    (bar->size > UINT32_MAX &&
	     (bar->flags & PCI_BASE_ADDRESS_MEM_TYPE_64) == 0)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->set_bar == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->set_bar(dev, funcno, bar);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_add_capability(const struct device *dev, uint8_t funcno,
			    uint8_t id, const void *body, size_t len)
{
	struct pcie_epc_ctrl_s *epc;
	int ret;

	if (dev == NULL || body == NULL || len == 0U) {
		return -EINVAL;
	}

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	if (pcie_epc_api(dev)->add_capability == NULL) {
		return -ENOTSUP;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	/* The host reads the capabilities once it enumerates the function */
	if (epc->started) {
		ret = -EBUSY;
	} else {
		ret = pcie_epc_api(dev)->add_capability(dev, funcno, id, body, len);
	}

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

	epc = dev->data;
	if (!pcie_epc_funcno_valid(dev, funcno)) {
		return -EINVAL;
	}

	/* The configuration header is fixed in the hardware */
	if (pcie_epc_api(dev)->write_header == NULL) {
		return 0;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = pcie_epc_api(dev)->write_header(dev, funcno, header);
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_add_epf(const struct device *dev, struct pcie_epf_device_s *epf)
{
	struct pcie_epc_ctrl_s *epc;
	uint32_t funcno;
	k_spinlock_key_t key;
	int ret;

	if (dev == NULL || epf == NULL || epf->ops == NULL ||
	    epf->ops->bind == NULL) {
		return -EINVAL;
	}

	if (epf->epc != NULL) {
		return -EBUSY;
	}

	epc = dev->data;
	k_mutex_lock(&epc->lock, K_FOREVER);

	/* The functions cannot change once the host can enumerate them */
	if (epc->started) {
		ret = -EBUSY;
		goto out;
	}

	for (funcno = 0; pcie_epc_funcno_valid(dev, funcno); funcno++) {
		if ((epc->funcno_map & BIT(funcno)) == 0U) {
			break;
		}
	}

	if (!pcie_epc_funcno_valid(dev, funcno)) {
		LOG_ERR("Exceeding max supported Function Number");
		ret = -ENOENT;
		goto out;
	}

	epc->funcno_map |= BIT(funcno);
	epf->funcno = funcno;
	epf->epc = dev;

	/* Notify the function that it has been bound to the EPC device */
	ret = epf->ops->bind(epf);
	epf->is_bound = (ret >= 0);

	if (ret < 0) {
		if (pcie_epc_api(dev)->clear_capabilities != NULL) {
			pcie_epc_api(dev)->clear_capabilities(dev, funcno);
		}

		epc->funcno_map &= ~BIT(funcno);
		epf->epc = NULL;
	} else {
		key = k_spin_lock(&epc->epf_lock);
		sys_dlist_append(&epc->epf, &epf->epc_node);
		k_spin_unlock(&epc->epf_lock, key);
		ret = 0;
	}

out:
	k_mutex_unlock(&epc->lock);
	return ret;
}

int pcie_epc_remove_epf(const struct device *dev,
			struct pcie_epf_device_s *epf)
{
	struct pcie_epc_ctrl_s *epc;
	k_spinlock_key_t key;

	if (dev == NULL || epf == NULL || epf->epc != dev) {
		return -EINVAL;
	}

	epc = dev->data;
	k_mutex_lock(&epc->lock, K_FOREVER);
	if (epc->started) {
		k_mutex_unlock(&epc->lock);
		return -EBUSY;
	}

	/* Remove the notification target before draining the function's work. */
	key = k_spin_lock(&epc->epf_lock);
	sys_dlist_remove(&epf->epc_node);
	k_spin_unlock(&epc->epf_lock, key);
	k_mutex_unlock(&epc->lock);

	/* Workers may use EPC operations while unbind waits for their exit. */
	if (epf->is_bound) {
		if (epf->ops->unbind != NULL) {
			epf->ops->unbind(epf);
		}

		epf->is_bound = false;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	if (pcie_epc_api(dev)->clear_capabilities != NULL) {
		pcie_epc_api(dev)->clear_capabilities(dev, epf->funcno);
	}

	epc->funcno_map &= ~BIT(epf->funcno);
	epf->epc = NULL;
	k_mutex_unlock(&epc->lock);
	return 0;
}

enum pcie_epc_event {
	PCIE_EPC_EVENT_LINK_UP,
	PCIE_EPC_EVENT_LINK_DOWN,
	PCIE_EPC_EVENT_CORE_INIT,
	PCIE_EPC_EVENT_BME,
};

/* Deliver an event to every function, in the context of the caller */
static void pcie_epc_notify_all(const struct device *dev, enum pcie_epc_event event)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;
	k_spinlock_key_t key;

	if (dev == NULL) {
		return;
	}

	epc = dev->data;

	/* Keep the targets alive until their ISR-safe callbacks return. */
	key = k_spin_lock(&epc->epf_lock);
	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		const struct pcie_epc_event_ops_s *ops = epf->event_ops;
		void (*cb)(struct pcie_epf_device_s *epf) = NULL;

		if (ops == NULL) {
			continue;
		}

		switch (event) {
		case PCIE_EPC_EVENT_LINK_UP:
			cb = ops->link_up;
			break;
		case PCIE_EPC_EVENT_LINK_DOWN:
			cb = ops->link_down;
			break;
		case PCIE_EPC_EVENT_CORE_INIT:
			cb = ops->core_init;
			break;
		case PCIE_EPC_EVENT_BME:
			cb = ops->bme;
			break;
		default:
			break;
		}

		if (cb != NULL) {
			cb(epf);
		}
	}

	k_spin_unlock(&epc->epf_lock, key);
}

void pcie_epc_linkup(const struct device *dev)
{
	pcie_epc_notify_all(dev, PCIE_EPC_EVENT_LINK_UP);
}

void pcie_epc_linkdown(const struct device *dev)
{
	pcie_epc_notify_all(dev, PCIE_EPC_EVENT_LINK_DOWN);
}

void pcie_epc_init_notify(const struct device *dev)
{
	pcie_epc_notify_all(dev, PCIE_EPC_EVENT_CORE_INIT);
}

void pcie_epc_bme_notify(const struct device *dev)
{
	pcie_epc_notify_all(dev, PCIE_EPC_EVENT_BME);
}

/* Called with the EPF list spinlock held */
static struct pcie_epf_device_s *pcie_epc_find_epf(struct pcie_epc_ctrl_s *epc, uint8_t funcno)
{
	struct pcie_epf_device_s *epf;

	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		if (epf->funcno == funcno && epf->event_ops != NULL) {
			return epf;
		}
	}

	return NULL;
}

void pcie_epc_doorbell_notify(const struct device *dev, uint8_t funcno,
			      uint8_t index)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;
	k_spinlock_key_t key;

	if (dev == NULL) {
		return;
	}

	epc = dev->data;

	key = k_spin_lock(&epc->epf_lock);
	epf = pcie_epc_find_epf(epc, funcno);

	/* Keep the target alive until its ISR-safe callback returns. */
	if (epf != NULL && epf->event_ops->doorbell != NULL) {
		epf->event_ops->doorbell(epf, index);
	}

	k_spin_unlock(&epc->epf_lock, key);
}

void pcie_epc_bar_access_notify(const struct device *dev, uint8_t funcno)
{
	struct pcie_epf_device_s *epf;
	struct pcie_epc_ctrl_s *epc;
	k_spinlock_key_t key;

	if (dev == NULL) {
		return;
	}

	epc = dev->data;

	key = k_spin_lock(&epc->epf_lock);
	epf = pcie_epc_find_epf(epc, funcno);

	/* Keep the target alive until its ISR-safe callback returns. */
	if (epf != NULL && epf->event_ops->bar_access != NULL) {
		epf->event_ops->bar_access(epf);
	}

	k_spin_unlock(&epc->epf_lock, key);
}

int pcie_epc_init(const struct device *dev, struct k_heap *dmaheap)
{
	struct pcie_epc_ctrl_s *epc;

	if (dev == NULL) {
		return -EINVAL;
	}

	epc = dev->data;
	epc->dev = dev;
	epc->dmaheap = dmaheap;
	k_mutex_init(&epc->lock);
	sys_dlist_init(&epc->epf);

	return 0;
}

void *pcie_epc_dma_alloc(const struct device *dev, size_t size)
{
	struct pcie_epc_ctrl_s *epc = dev->data;

	if (epc->dmaheap == NULL) {
		return NULL;
	}

	return k_heap_alloc(epc->dmaheap, size, K_NO_WAIT);
}

void *pcie_epc_dma_memalign(const struct device *dev, size_t alignment,
			    size_t size)
{
	struct pcie_epc_ctrl_s *epc = dev->data;

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
	struct pcie_epc_ctrl_s *epc = dev->data;

	if (epc->dmaheap == NULL) {
		return;
	}

	k_heap_free(epc->dmaheap, mem);
}
