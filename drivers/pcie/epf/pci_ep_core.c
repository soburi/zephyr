/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Derived from NuttX drivers/pci/pci_epc.c and pci_epf.c (Apache-2.0),
 * which follow the design of the Linux PCI endpoint framework.
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pcie/epf/pci_epc.h>
#include <zephyr/drivers/pcie/epf/pci_epf.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pci_ep, CONFIG_PCI_EPF_LOG_LEVEL);

static K_MUTEX_DEFINE(pci_ep_lock);
static sys_slist_t pci_epc_list = SYS_SLIST_STATIC_INIT(&pci_epc_list);
static sys_slist_t pci_epf_driver_list = SYS_SLIST_STATIC_INIT(&pci_epf_driver_list);
static sys_slist_t pci_epf_device_list = SYS_SLIST_STATIC_INIT(&pci_epf_device_list);

static inline const struct pci_epc_ops *epc_ops(const struct pci_epc *epc)
{
	return epc->dev->api;
}

/*
 * EPC registration and lookup
 */

int pci_epc_register(struct pci_epc *epc)
{
	if (epc == NULL || epc->dev == NULL || epc->max_functions == 0) {
		return -EINVAL;
	}

	sys_slist_init(&epc->epf);
	k_mutex_init(&epc->lock);
	epc->funcno_map = 0;

	k_mutex_lock(&pci_ep_lock, K_FOREVER);
	sys_slist_append(&pci_epc_list, &epc->node);
	k_mutex_unlock(&pci_ep_lock);

	return 0;
}

struct pci_epc *pci_epc_get(const char *epc_name)
{
	struct pci_epc *epc;

	if (epc_name == NULL) {
		return NULL;
	}

	k_mutex_lock(&pci_ep_lock, K_FOREVER);
	SYS_SLIST_FOR_EACH_CONTAINER(&pci_epc_list, epc, node) {
		if (strcmp(epc->dev->name, epc_name) == 0) {
			k_mutex_unlock(&pci_ep_lock);
			return epc;
		}
	}
	k_mutex_unlock(&pci_ep_lock);

	return NULL;
}

/*
 * EPC operations
 */

const struct pci_epc_features *pci_epc_get_features(struct pci_epc *epc, uint8_t funcno)
{
	const struct pci_epc_features *features;

	if (epc == NULL || funcno >= epc->max_functions || epc_ops(epc)->get_features == NULL) {
		return NULL;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	features = epc_ops(epc)->get_features(epc->dev, funcno);
	k_mutex_unlock(&epc->lock);

	return features;
}

int pci_epc_get_next_free_bar(const struct pci_epc_features *epc_features, int barno)
{
	if (epc_features == NULL || barno < 0 || barno >= PCI_STD_NUM_BARS) {
		return -EINVAL;
	}

	/* A 64-bit BAR claims the following BAR slot as well */
	if (barno > 0 && (epc_features->bar_fixed_64bit & BIT(barno - 1))) {
		barno++;
	}

	for (; barno < PCI_STD_NUM_BARS; barno++) {
		if (!(epc_features->bar_reserved & BIT(barno))) {
			return barno;
		}
	}

	return -ENOENT;
}

int pci_epc_get_first_free_bar(const struct pci_epc_features *epc_features)
{
	return pci_epc_get_next_free_bar(epc_features, 0);
}

int pci_epc_write_header(struct pci_epc *epc, uint8_t funcno, const struct pci_epf_header *hdr)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions || hdr == NULL) {
		return -EINVAL;
	}

	if (epc_ops(epc)->write_header == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->write_header(epc->dev, funcno, hdr);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_set_bar(struct pci_epc *epc, uint8_t funcno, const struct pci_epf_bar *bar)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions || bar == NULL ||
	    bar->barno >= PCI_STD_NUM_BARS ||
	    (bar->barno == PCI_STD_NUM_BARS - 1 && (bar->flags & PCI_EPF_BAR_MEM_TYPE_64))) {
		return -EINVAL;
	}

	if (epc_ops(epc)->set_bar == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->set_bar(epc->dev, funcno, bar);
	k_mutex_unlock(&epc->lock);

	return ret;
}

void pci_epc_clear_bar(struct pci_epc *epc, uint8_t funcno, const struct pci_epf_bar *bar)
{
	if (epc == NULL || funcno >= epc->max_functions || bar == NULL ||
	    epc_ops(epc)->clear_bar == NULL) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc_ops(epc)->clear_bar(epc->dev, funcno, bar);
	k_mutex_unlock(&epc->lock);
}

int pci_epc_map_addr(struct pci_epc *epc, uint8_t funcno, uintptr_t addr, uint64_t pci_addr,
		     size_t size)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->map_addr == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->map_addr(epc->dev, funcno, addr, pci_addr, size);
	k_mutex_unlock(&epc->lock);

	return ret;
}

void pci_epc_unmap_addr(struct pci_epc *epc, uint8_t funcno, uintptr_t addr)
{
	if (epc == NULL || funcno >= epc->max_functions || epc_ops(epc)->unmap_addr == NULL) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc_ops(epc)->unmap_addr(epc->dev, funcno, addr);
	k_mutex_unlock(&epc->lock);
}

int pci_epc_set_msi(struct pci_epc *epc, uint8_t funcno, uint8_t interrupts)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->set_msi == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->set_msi(epc->dev, funcno, interrupts);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_get_msi(struct pci_epc *epc, uint8_t funcno)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->get_msi == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->get_msi(epc->dev, funcno);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_set_msix(struct pci_epc *epc, uint8_t funcno, uint16_t interrupts, int barno,
		     uint32_t offset)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->set_msix == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->set_msix(epc->dev, funcno, interrupts, barno, offset);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_get_msix(struct pci_epc *epc, uint8_t funcno)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->get_msix == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->get_msix(epc->dev, funcno);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_raise_irq(struct pci_epc *epc, uint8_t funcno, enum pci_epc_irq_type type,
		      uint16_t interrupt_num)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->raise_irq == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->raise_irq(epc->dev, funcno, type, interrupt_num);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_map_msi_irq(struct pci_epc *epc, uint8_t funcno, uintptr_t phys_addr,
			uint8_t interrupt_num, uint32_t entry_size, uint32_t *msi_data,
			uint32_t *msi_addr_offset)
{
	int ret;

	if (epc == NULL || funcno >= epc->max_functions) {
		return -EINVAL;
	}

	if (epc_ops(epc)->map_msi_irq == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->map_msi_irq(epc->dev, funcno, phys_addr, interrupt_num, entry_size,
					msi_data, msi_addr_offset);
	k_mutex_unlock(&epc->lock);

	return ret;
}

int pci_epc_start(struct pci_epc *epc)
{
	int ret;

	if (epc == NULL) {
		return -EINVAL;
	}

	if (epc_ops(epc)->start == NULL) {
		return -ENOSYS;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	ret = epc_ops(epc)->start(epc->dev);
	k_mutex_unlock(&epc->lock);

	return ret;
}

void pci_epc_stop(struct pci_epc *epc)
{
	if (epc == NULL || epc_ops(epc)->stop == NULL) {
		return;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);
	epc_ops(epc)->stop(epc->dev);
	k_mutex_unlock(&epc->lock);
}

/*
 * EPC outbound window allocator
 */

void *pci_epc_mem_alloc_addr(struct pci_epc *epc, size_t size, uintptr_t *phys_addr)
{
	if (epc == NULL || size == 0 || phys_addr == NULL) {
		return NULL;
	}

	for (unsigned int i = 0; i < epc->num_windows; i++) {
		const struct pci_epc_mem_window *win = &epc->windows[i];
		const size_t pages = DIV_ROUND_UP(size, win->page_size);
		size_t offset;

		if (sys_bitarray_alloc(epc->window_maps[i], pages, &offset) == 0) {
			*phys_addr = win->phys_base + offset * win->page_size;
			return (uint8_t *)win->virt_base + offset * win->page_size;
		}
	}

	LOG_ERR("no outbound window space for %zu bytes", size);

	return NULL;
}

void pci_epc_mem_free_addr(struct pci_epc *epc, void *virt_addr, size_t size)
{
	if (epc == NULL || virt_addr == NULL) {
		return;
	}

	for (unsigned int i = 0; i < epc->num_windows; i++) {
		const struct pci_epc_mem_window *win = &epc->windows[i];
		const uint8_t *base = win->virt_base;
		const uint8_t *addr = virt_addr;

		if (addr >= base && addr < base + win->size) {
			const size_t pages = DIV_ROUND_UP(size, win->page_size);
			const size_t offset = (addr - base) / win->page_size;

			sys_bitarray_free(epc->window_maps[i], pages, offset);
			return;
		}
	}

	LOG_ERR("address %p not in any outbound window", virt_addr);
}

/*
 * EPC event notification towards bound EPF devices
 */

#define EPC_NOTIFY(epc, op)                                                                        \
	do {                                                                                       \
		struct pci_epf_device *epf;                                                        \
                                                                                                   \
		if ((epc) == NULL) {                                                               \
			return;                                                                    \
		}                                                                                  \
                                                                                                   \
		k_mutex_lock(&(epc)->lock, K_FOREVER);                                             \
		SYS_SLIST_FOR_EACH_CONTAINER(&(epc)->epf, epf, epc_node) {                         \
			if (epf->event_ops && epf->event_ops->op) {                                \
				epf->event_ops->op(epf);                                           \
			}                                                                          \
		}                                                                                  \
		k_mutex_unlock(&(epc)->lock);                                                      \
	} while (0)

void pci_epc_linkup(struct pci_epc *epc)
{
	EPC_NOTIFY(epc, link_up);
}

void pci_epc_linkdown(struct pci_epc *epc)
{
	EPC_NOTIFY(epc, link_down);
}

void pci_epc_init_notify(struct pci_epc *epc)
{
	EPC_NOTIFY(epc, core_init);
}

void pci_epc_bme_notify(struct pci_epc *epc)
{
	EPC_NOTIFY(epc, bme);
}

/*
 * EPF device/driver registration and binding
 */

static bool pci_epf_match_driver(const struct pci_epf_device *epf,
				 const struct pci_epf_driver *drv)
{
	for (size_t i = 0; drv->names != NULL && drv->names[i] != NULL; i++) {
		if (strcmp(epf->name, drv->names[i]) == 0) {
			return true;
		}
	}

	return false;
}

static int pci_epc_add_epf(struct pci_epc *epc, struct pci_epf_device *epf)
{
	int funcno;
	int ret = 0;

	if (epf->epc != NULL) {
		return -EBUSY;
	}

	k_mutex_lock(&epc->lock, K_FOREVER);

	funcno = find_lsb_set(~epc->funcno_map) - 1;
	if (funcno < 0 || funcno >= epc->max_functions) {
		LOG_ERR("exceeding max supported function number");
		ret = -ENOENT;
		goto out;
	}

	epc->funcno_map |= BIT(funcno);
	epf->funcno = funcno;
	epf->epc = epc;
	sys_slist_append(&epc->epf, &epf->epc_node);

out:
	k_mutex_unlock(&epc->lock);

	return ret;
}

static void pci_epc_remove_epf(struct pci_epc *epc, struct pci_epf_device *epf)
{
	k_mutex_lock(&epc->lock, K_FOREVER);
	epc->funcno_map &= ~BIT(epf->funcno);
	sys_slist_find_and_remove(&epc->epf, &epf->epc_node);
	epf->epc = NULL;
	k_mutex_unlock(&epc->lock);
}

static void pci_epf_bind(struct pci_epf_device *epf)
{
	if (epf->driver == NULL || epf->driver->ops == NULL || epf->driver->ops->bind == NULL) {
		return;
	}

	k_mutex_lock(&epf->lock, K_FOREVER);
	if (epf->driver->ops->bind(epf) == 0) {
		epf->is_bound = true;
	}
	k_mutex_unlock(&epf->lock);
}

static void pci_epf_unbind(struct pci_epf_device *epf)
{
	if (epf->driver == NULL || epf->driver->ops == NULL || epf->driver->ops->unbind == NULL) {
		return;
	}

	k_mutex_lock(&epf->lock, K_FOREVER);
	if (epf->is_bound) {
		epf->driver->ops->unbind(epf);
		epf->is_bound = false;
	}
	k_mutex_unlock(&epf->lock);
}

static int pci_epf_try_bind(struct pci_epf_device *epf, struct pci_epf_driver *drv)
{
	struct pci_epc *epc;
	int ret;

	if (!pci_epf_match_driver(epf, drv)) {
		return -ENOTSUP;
	}

	epc = pci_epc_get(epf->epc_name);
	if (epc == NULL) {
		LOG_WRN("EPF %s: EPC %s not found", epf->name,
			epf->epc_name ? epf->epc_name : "(null)");
		return -ENODEV;
	}

	if (drv->probe != NULL) {
		ret = drv->probe(epf);
		if (ret < 0) {
			return ret;
		}
	}

	epf->driver = drv;

	ret = pci_epc_add_epf(epc, epf);
	if (ret < 0) {
		epf->driver = NULL;
		return ret;
	}

	pci_epf_bind(epf);

	return 0;
}

int pci_epf_device_register(struct pci_epf_device *epf)
{
	struct pci_epf_driver *drv;

	if (epf == NULL || epf->name == NULL) {
		return -EINVAL;
	}

	k_mutex_init(&epf->lock);

	k_mutex_lock(&pci_ep_lock, K_FOREVER);
	sys_slist_append(&pci_epf_device_list, &epf->node);

	SYS_SLIST_FOR_EACH_CONTAINER(&pci_epf_driver_list, drv, node) {
		if (pci_epf_try_bind(epf, drv) == 0) {
			break;
		}
	}
	k_mutex_unlock(&pci_ep_lock);

	return 0;
}

int pci_epf_device_unregister(struct pci_epf_device *epf)
{
	if (epf == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&pci_ep_lock, K_FOREVER);

	pci_epf_unbind(epf);

	if (epf->epc != NULL) {
		pci_epc_remove_epf(epf->epc, epf);
	}

	if (epf->driver != NULL) {
		if (epf->driver->remove != NULL) {
			epf->driver->remove(epf);
		}
		epf->driver = NULL;
	}

	sys_slist_find_and_remove(&pci_epf_device_list, &epf->node);
	k_mutex_unlock(&pci_ep_lock);

	return 0;
}

int pci_epf_register_driver(struct pci_epf_driver *drv)
{
	struct pci_epf_device *epf;

	if (drv == NULL || drv->ops == NULL || drv->ops->bind == NULL || drv->names == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&pci_ep_lock, K_FOREVER);
	sys_slist_append(&pci_epf_driver_list, &drv->node);

	SYS_SLIST_FOR_EACH_CONTAINER(&pci_epf_device_list, epf, node) {
		if (epf->driver == NULL) {
			pci_epf_try_bind(epf, drv);
		}
	}
	k_mutex_unlock(&pci_ep_lock);

	return 0;
}

int pci_epf_unregister_driver(struct pci_epf_driver *drv)
{
	struct pci_epf_device *epf;

	if (drv == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&pci_ep_lock, K_FOREVER);

	SYS_SLIST_FOR_EACH_CONTAINER(&pci_epf_device_list, epf, node) {
		if (epf->driver != drv) {
			continue;
		}

		pci_epf_unbind(epf);

		if (epf->epc != NULL) {
			pci_epc_remove_epf(epf->epc, epf);
		}

		if (drv->remove != NULL) {
			drv->remove(epf);
		}
		epf->driver = NULL;
	}

	sys_slist_find_and_remove(&pci_epf_driver_list, &drv->node);
	k_mutex_unlock(&pci_ep_lock);

	return 0;
}

/*
 * EPF BAR backing memory
 */

void *pci_epf_alloc_space(struct pci_epf_device *epf, int barno, size_t size, size_t align)
{
	void *addr;

	if (epf == NULL || barno < 0 || barno >= PCI_STD_NUM_BARS || size == 0) {
		return NULL;
	}

	if (size < 128) {
		size = 128;
	}

	if (align != 0) {
		size = ROUND_UP(size, align);
	} else {
		align = sizeof(void *);
	}

	/* BAR sizes must be powers of two */
	size = (size_t)1 << (sizeof(size) * 8 - __builtin_clzl(size - 1));

	addr = k_aligned_alloc(MAX(align, size), size);
	if (addr == NULL) {
		LOG_ERR("failed to allocate %zu bytes for BAR%d", size, barno);
		return NULL;
	}

	memset(addr, 0, size);

	epf->bar[barno].phys_addr = (uintptr_t)addr;
	epf->bar[barno].addr = addr;
	epf->bar[barno].size = size;
	epf->bar[barno].barno = barno;

	return addr;
}

void pci_epf_free_space(struct pci_epf_device *epf, int barno)
{
	if (epf == NULL || barno < 0 || barno >= PCI_STD_NUM_BARS ||
	    epf->bar[barno].addr == NULL) {
		return;
	}

	k_free(epf->bar[barno].addr);

	epf->bar[barno].phys_addr = 0;
	epf->bar[barno].addr = NULL;
	epf->bar[barno].size = 0;
}
