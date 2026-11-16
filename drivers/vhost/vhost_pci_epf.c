/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_vhost_pci_epf

#include <errno.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epf.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost_pci_epf, CONFIG_VHOST_LOG_LEVEL);

/*
 * Legacy VIRTIO PCI register layout ("Legacy Interfaces: A Note on PCI
 * Device Layout" in the VIRTIO specification), without MSI-X. The layout
 * needs no vendor-specific PCI capability.
 */
#define VIRTIO_PCI_LEGACY_DEVICE_FEATURES 0x00
#define VIRTIO_PCI_LEGACY_DRIVER_FEATURES 0x04
#define VIRTIO_PCI_LEGACY_QUEUE_PFN       0x08
#define VIRTIO_PCI_LEGACY_QUEUE_SIZE      0x0c
#define VIRTIO_PCI_LEGACY_QUEUE_SELECT    0x0e
#define VIRTIO_PCI_LEGACY_QUEUE_NOTIFY    0x10
#define VIRTIO_PCI_LEGACY_STATUS          0x12
#define VIRTIO_PCI_LEGACY_ISR             0x13
#define VIRTIO_PCI_LEGACY_CONFIG          0x14

#define VIRTIO_PCI_LEGACY_QUEUE_PFN_SHIFT 12
#define VIRTIO_PCI_LEGACY_VRING_ALIGN     4096U

#define VIRTIO_PCI_VENDOR_ID        0x1af4
#define VIRTIO_PCI_BASECLASS_OTHERS 0xff
#define VIRTIO_PCI_LEGACY_BAR       0

/*
 * For controllers without software BAR access, a value the driver never
 * writes makes the next register write visible to the polling fallback.
 */
#define DRIVER_FEATURES_IDLE UINT32_MAX

/*
 * The device features register has 32 bits. Bit 31 stays clear, so that the
 * idle pattern of the driver features register is never a valid selection.
 */
#define SUPPORTED_FEATURES (UINT32_MAX & ~BIT(31))
#define QUEUE_NOTIFY_IDLE    UINT16_MAX

/* Granularity of the outbound address windows */
#define MAP_ALIGN 4096U

/* The single virtqueue of the device */
#define QUEUE_ID 0U

struct vhost_pci_epf_config {
	const struct device *epc;
	k_thread_stack_t *workq_stack;
	size_t workq_stack_size;
	int workq_priority;
	uint32_t poll_period_ms;
	uint32_t doorbell_poll_period_ms;
	/* The EPC doorbell wired to the queue notify register, if any */
	bool has_doorbell;
	uint8_t doorbell_index;
	uint16_t device_id;
	uint16_t queue_size;
	const uint8_t *config_data;
	size_t config_data_len;
	struct vhost_pci_epf_map *maps;
	size_t maps_count;
};

/* A range of PCI bus addresses mapped through an outbound window */
struct vhost_pci_epf_map {
	void *virt;
	uintptr_t phys;
	size_t size;
	uint16_t head;
	bool used;
};

struct virtq_callback {
	void (*cb)(const struct device *dev, uint16_t queue_id, void *user_data);
	void *data;
};

struct vhost_pci_epf_data {
	const struct device *dev;
	struct pcie_epf_device_s epf;
	struct pcie_epf_header_s header;
	struct k_work_q workq;
	struct k_work_delayable poll_work;
	/* Set by the doorbell of the queue notify register */
	atomic_t kicked;
	/* Protects the fields below and the register window */
	struct k_mutex lock;
	uint8_t *regs;
	bool trapped;
	/* The EPC notifies the host accesses to the trapped register window */
	bool access_notified;
	uint16_t queue_select;
	/* Feature bits offered to the driver, set by the backend */
	uint32_t offered_features;
	uint32_t driver_features;
	uint32_t pfn;
	uint8_t status;
	/* The driver has set up the virtqueue and the backend may use it */
	bool queue_valid;
	bool ready_notified;
	struct vhost_pci_epf_map ring;
	struct virtq_callback ready_cb;
	struct virtq_callback reset_cb;
	struct virtq_callback notify_cb;
};

static uint8_t reg_read8(const struct vhost_pci_epf_data *data, size_t offset)
{
	return sys_read8((mem_addr_t)&data->regs[offset]);
}

static uint16_t reg_read16(const struct vhost_pci_epf_data *data, size_t offset)
{
	return sys_le16_to_cpu(sys_read16((mem_addr_t)&data->regs[offset]));
}

static uint32_t reg_read32(const struct vhost_pci_epf_data *data, size_t offset)
{
	return sys_le32_to_cpu(sys_read32((mem_addr_t)&data->regs[offset]));
}

static void reg_write8(const struct vhost_pci_epf_data *data, size_t offset, uint8_t value)
{
	sys_write8(value, (mem_addr_t)&data->regs[offset]);
}

static void reg_write16(const struct vhost_pci_epf_data *data, size_t offset, uint16_t value)
{
	sys_write16(sys_cpu_to_le16(value), (mem_addr_t)&data->regs[offset]);
}

static void reg_write32(const struct vhost_pci_epf_data *data, size_t offset, uint32_t value)
{
	sys_write32(sys_cpu_to_le32(value), (mem_addr_t)&data->regs[offset]);
}

/* Offset of the used ring in the legacy split virtqueue layout */
static size_t vring_used_offset(uint16_t num)
{
	return ROUND_UP(sizeof(struct virtq_desc) * num + sizeof(uint16_t) * (3U + num),
			VIRTIO_PCI_LEGACY_VRING_ALIGN);
}

static size_t vring_size(uint16_t num)
{
	return vring_used_offset(num) + sizeof(uint16_t) * 3U +
	       sizeof(struct virtq_used_elem) * num;
}

static int map_bus_addr(const struct device *dev, uint64_t bus_addr, size_t len,
			struct vhost_pci_epf_map *map)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const uint64_t base = ROUND_DOWN(bus_addr, MAP_ALIGN);
	uint8_t *virt;
	size_t size;
	int ret;

	if ((len == 0U) || (len > SIZE_MAX - 2U * MAP_ALIGN) ||
	    (bus_addr > UINT64_MAX - len - MAP_ALIGN)) {
		return -EINVAL;
	}

	size = (size_t)(ROUND_UP(bus_addr + len, MAP_ALIGN) - base);

	virt = pcie_epc_mem_alloc_addr(config->epc, &map->phys, size);
	if (virt == NULL) {
		return -ENOMEM;
	}

	ret = pcie_epc_map_addr(config->epc, data->epf.funcno, map->phys, base, size);
	if (ret < 0) {
		pcie_epc_mem_free_addr(config->epc, map->phys, size);
		return ret;
	}

	map->virt = virt + (size_t)(bus_addr - base);
	map->size = size;
	map->used = true;

	return 0;
}

static void unmap_bus_addr(const struct device *dev, struct vhost_pci_epf_map *map)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (!map->used) {
		return;
	}

	pcie_epc_unmap_addr(config->epc, data->epf.funcno, map->phys);
	pcie_epc_mem_free_addr(config->epc, map->phys, map->size);
	map->used = false;
	map->virt = NULL;
}

static void release_head(const struct device *dev, uint16_t head)
{
	const struct vhost_pci_epf_config *config = dev->config;

	for (size_t i = 0; i < config->maps_count; i++) {
		if (config->maps[i].used && (config->maps[i].head == head)) {
			unmap_bus_addr(dev, &config->maps[i]);
		}
	}
}

/*
 * The virtqueue the driver has set up is no longer valid. The backend is told
 * at once and the controller does not wait for it. The guest memory stays
 * mapped, so that a backend that has not stopped yet accesses stale but
 * accessible memory. reclaim_queue() lets it go.
 */
static void invalidate_queue(const struct device *dev)
{
	struct vhost_pci_epf_data *data = dev->data;
	const struct virtq_callback reset_cb = data->reset_cb;
	const bool was_ready = data->ready_notified;

	data->queue_valid = false;
	data->ready_notified = false;
	data->notify_cb.cb = NULL;
	data->notify_cb.data = NULL;
	data->pfn = 0;
	(void)atomic_clear(&data->kicked);

	if (was_ready && (reset_cb.cb != NULL)) {
		/* The caller holds the lock once. Do not hold it across the callback */
		k_mutex_unlock(&data->lock);
		reset_cb.cb(dev, QUEUE_ID, reset_cb.data);
		k_mutex_lock(&data->lock, K_FOREVER);
	}
}

/* Unmap the guest memory of the queue that was invalidated */
static void reclaim_queue(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	for (size_t i = 0; i < config->maps_count; i++) {
		unmap_bus_addr(dev, &config->maps[i]);
	}

	unmap_bus_addr(dev, &data->ring);
}

static void init_regs(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	reg_write32(data, VIRTIO_PCI_LEGACY_DEVICE_FEATURES, data->offered_features);
	reg_write32(data, VIRTIO_PCI_LEGACY_DRIVER_FEATURES, DRIVER_FEATURES_IDLE);
	reg_write32(data, VIRTIO_PCI_LEGACY_QUEUE_PFN, 0);
	reg_write16(data, VIRTIO_PCI_LEGACY_QUEUE_SIZE, config->queue_size);
	reg_write16(data, VIRTIO_PCI_LEGACY_QUEUE_SELECT, 0);
	reg_write16(data, VIRTIO_PCI_LEGACY_QUEUE_NOTIFY, QUEUE_NOTIFY_IDLE);
	reg_write8(data, VIRTIO_PCI_LEGACY_STATUS, 0);
	reg_write8(data, VIRTIO_PCI_LEGACY_ISR, 0);

	if (config->config_data_len > 0U) {
		memcpy(&data->regs[VIRTIO_PCI_LEGACY_CONFIG], config->config_data,
		       config->config_data_len);
	}
}

static int raise_irq(const struct device *dev, uint8_t isr)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	reg_write8(data, VIRTIO_PCI_LEGACY_ISR, reg_read8(data, VIRTIO_PCI_LEGACY_ISR) | isr);
	if (data->trapped) {
		ret = pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, true);
	} else {
		ret = pcie_epc_raise_irq(config->epc, data->epf.funcno, PCIE_EPC_IRQ_LEGACY, 0);
	}
	k_mutex_unlock(&data->lock);
	if (ret < 0) {
		LOG_ERR("%s: failed to raise interrupt: %d", dev->name, ret);
		return -EIO;
	}

	return 0;
}

/* Detect a driver that (re)starts the device initialization */
static void poll_features(const struct device *dev)
{
	struct vhost_pci_epf_data *data = dev->data;
	const uint32_t features = reg_read32(data, VIRTIO_PCI_LEGACY_DRIVER_FEATURES);

	if (features == DRIVER_FEATURES_IDLE) {
		return;
	}

	reg_write32(data, VIRTIO_PCI_LEGACY_DRIVER_FEATURES, DRIVER_FEATURES_IDLE);

	/* A reset shorter than the poll period goes unnoticed otherwise */
	invalidate_queue(dev);
	/* The legacy interface cannot reject a selection, mask it */
	data->driver_features = features & data->offered_features;

	LOG_DBG("%s: driver features 0x%08x, accepted 0x%08x", dev->name, features,
		data->driver_features);
}

static void poll_reset(const struct device *dev, uint8_t status)
{
	struct vhost_pci_epf_data *data = dev->data;

	if ((status != 0U) || (data->status == 0U)) {
		return;
	}

	LOG_DBG("%s: device reset", dev->name);

	invalidate_queue(dev);
	data->driver_features = 0;
	reg_write32(data, VIRTIO_PCI_LEGACY_QUEUE_PFN, 0);
	reg_write8(data, VIRTIO_PCI_LEGACY_ISR, 0);
}

static void poll_queue(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const uint32_t pfn = reg_read32(data, VIRTIO_PCI_LEGACY_QUEUE_PFN);
	int ret;

	if (pfn == data->pfn) {
		return;
	}

	invalidate_queue(dev);
	reclaim_queue(dev);
	data->pfn = pfn;
	if (pfn == 0U) {
		return;
	}

	ret = map_bus_addr(dev, (uint64_t)pfn << VIRTIO_PCI_LEGACY_QUEUE_PFN_SHIFT,
			   vring_size(config->queue_size), &data->ring);
	if (ret < 0) {
		LOG_ERR("%s: failed to map the virtqueue: %d", dev->name, ret);
		reg_write8(data, VIRTIO_PCI_LEGACY_STATUS,
			   reg_read8(data, VIRTIO_PCI_LEGACY_STATUS) | BIT(DEVICE_STATUS_FAILED));
		return;
	}

	data->queue_valid = true;
}

static bool poll_notify(const struct device *dev)
{
	struct vhost_pci_epf_data *data = dev->data;
	const uint16_t queue_id = reg_read16(data, VIRTIO_PCI_LEGACY_QUEUE_NOTIFY);

	if (queue_id == QUEUE_NOTIFY_IDLE) {
		return false;
	}

	/*
	 * Rearm before the queue is processed: a notification written from
	 * now on is seen by the next poll, an earlier one is covered by the
	 * processing this one triggers.
	 */
	reg_write16(data, VIRTIO_PCI_LEGACY_QUEUE_NOTIFY, QUEUE_NOTIFY_IDLE);

	return queue_id == QUEUE_ID;
}

/* Called under the register lock; each host access is completed exactly once. */
static int service_access(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	uint32_t off = access->offset;
	uint32_t value = access->value;

	if (access->bar != VIRTIO_PCI_LEGACY_BAR ||
	    (access->size != 1U && access->size != 2U && access->size != 4U) ||
	    off % access->size != 0U) {
		return -EINVAL;
	}
	if (off >= VIRTIO_PCI_LEGACY_CONFIG) {
		size_t pos = off - VIRTIO_PCI_LEGACY_CONFIG;

		if (pos > config->config_data_len || access->size > config->config_data_len - pos) {
			return -EINVAL;
		}
		if (!access->write) {
			access->value = 0U;
			for (uint8_t i = 0U; i < access->size; i++) {
				access->value |= (uint64_t)data->regs[off + i] << (8U * i);
			}
		}
		return 0;
	}

	if (access->write) {
		switch (off) {
		case VIRTIO_PCI_LEGACY_DRIVER_FEATURES:
			if (access->size != 4U) {
				return -EINVAL;
			}
			data->driver_features = value & data->offered_features;
			break;
		case VIRTIO_PCI_LEGACY_QUEUE_SELECT:
			if (access->size != 2U) {
				return -EINVAL;
			}
			data->queue_select = value;
			break;
		case VIRTIO_PCI_LEGACY_QUEUE_PFN:
			if (access->size != 4U) {
				return -EINVAL;
			}
			if (data->queue_select == QUEUE_ID) {
				reg_write32(data, off, value);
				poll_queue(dev);
			}
			break;
		case VIRTIO_PCI_LEGACY_QUEUE_NOTIFY:
			if (access->size != 2U) {
				return -EINVAL;
			}
			if (value == QUEUE_ID) {
				atomic_set(&data->kicked, 1);
			}
			break;
		case VIRTIO_PCI_LEGACY_STATUS:
			if (access->size != 1U) {
				return -EINVAL;
			}
			if (value == 0U) {
				int ret;

				invalidate_queue(dev);
				data->driver_features = 0U;
				data->queue_select = 0U;
				init_regs(dev);
				ret = pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, false);
				if (ret != 0) {
					return ret;
				}
			}
			reg_write8(data, off, value);
			break;
		default:
			return -EINVAL;
		}
		return 0;
	}

	switch (off) {
	case VIRTIO_PCI_LEGACY_DEVICE_FEATURES:
		access->value = data->offered_features;
		return access->size == 4U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_DRIVER_FEATURES:
		access->value = data->driver_features;
		return access->size == 4U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_QUEUE_PFN:
		access->value = data->queue_select == QUEUE_ID ? data->pfn : 0U;
		return access->size == 4U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_QUEUE_SIZE:
		access->value = data->queue_select == QUEUE_ID ? config->queue_size : 0U;
		return access->size == 2U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_QUEUE_SELECT:
		access->value = data->queue_select;
		return access->size == 2U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_STATUS:
		access->value = reg_read8(data, off);
		return access->size == 1U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_ISR:
		if (access->size != 1U) {
			return -EINVAL;
		}
		access->value = reg_read8(data, off);
		reg_write8(data, off, 0U);
		return pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, false);
	default:
		return -EINVAL;
	}
}

static void poll_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct vhost_pci_epf_data *data = CONTAINER_OF(dwork, struct vhost_pci_epf_data, poll_work);
	const struct device *dev = data->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct virtq_callback ready_cb = {0};
	struct virtq_callback notify_cb = {0};
	uint32_t period_ms = config->poll_period_ms;
	bool notified;
	uint8_t status;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->trapped) {
		struct pcie_epc_bar_access access;
		int ret = pcie_epc_bar_access_get(config->epc, data->epf.funcno, &access);

		if (ret == 0) {
			ret = service_access(dev, &access);
			ret = pcie_epc_bar_access_complete(config->epc, data->epf.funcno,
						   &access, ret);
			if (ret != 0 && ret != -ESTALE) {
				LOG_ERR("BAR response failed: %d", ret);
			}

			/* Look for the next access right away */
			period_ms = 0U;
		} else if (data->access_notified) {
			/* The EPC reschedules the work on the next access */
			period_ms = config->doorbell_poll_period_ms;
		} else {
			period_ms = 1U;
		}
	} else {
		status = reg_read8(data, VIRTIO_PCI_LEGACY_STATUS);
		poll_reset(dev, status);
		poll_features(dev);
		poll_queue(dev);
	}
	status = reg_read8(data, VIRTIO_PCI_LEGACY_STATUS);
	data->status = status;

	if (data->queue_valid && ((status & BIT(DEVICE_STATUS_DRIVER_OK)) != 0U)) {
		if (!data->ready_notified && (data->ready_cb.cb != NULL)) {
			data->ready_notified = true;
			ready_cb = data->ready_cb;
		}

		/* A doorbell write may or may not reach the register as well */
		notified = data->trapped ? false : poll_notify(dev);
		if (atomic_clear(&data->kicked) != 0) {
			notified = true;
		}

		if (notified) {
			notify_cb = data->notify_cb;
		}

		/* Only a device reset is left to be found by polling */
		if (config->has_doorbell && !data->trapped) {
			period_ms = config->doorbell_poll_period_ms;
		}
	}

	k_mutex_unlock(&data->lock);

	/* The callbacks call back into the controller, run them unlocked */
	if (ready_cb.cb != NULL) {
		ready_cb.cb(dev, QUEUE_ID, ready_cb.data);

		/*
		 * A notification that preceded the registration of the
		 * callback is lost, so process the queue once in any case.
		 */
		k_mutex_lock(&data->lock, K_FOREVER);
		notify_cb = data->notify_cb;
		k_mutex_unlock(&data->lock);
	}

	if (notify_cb.cb != NULL) {
		notify_cb.cb(dev, QUEUE_ID, notify_cb.data);
	}

	/* No-op when a doorbell has already queued the next run */
	k_work_schedule_for_queue(&data->workq, &data->poll_work, K_MSEC(period_ms));
}

static int vhost_pci_epf_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
				       const struct vhost_buf *bufs, size_t bufs_count,
				       struct vhost_iovec *read_iovec, size_t max_read_iovecs,
				       struct vhost_iovec *write_iovec, size_t max_write_iovecs,
				       size_t *read_count, size_t *write_count)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	size_t read_num = 0;
	size_t write_num = 0;
	int ret = 0;

	*read_count = 0;
	*write_count = 0;

	if ((queue_id != QUEUE_ID) || (head >= config->queue_size)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!data->queue_valid) {
		ret = -ENODEV;
	}

	for (size_t i = 0; (i < bufs_count) && (ret == 0); i++) {
		struct vhost_pci_epf_map *map = NULL;
		struct vhost_iovec *iovec;

		if (bufs[i].is_write) {
			iovec = (write_num < max_write_iovecs) ? &write_iovec[write_num] : NULL;
		} else {
			iovec = (read_num < max_read_iovecs) ? &read_iovec[read_num] : NULL;
		}

		if (iovec == NULL) {
			ret = -E2BIG;
			break;
		}

		for (size_t j = 0; j < config->maps_count; j++) {
			if (!config->maps[j].used) {
				map = &config->maps[j];
				break;
			}
		}

		if (map == NULL) {
			ret = -ENOMEM;
			break;
		}

		/* The descriptor address is a PCI bus address of the host */
		ret = map_bus_addr(dev, bufs[i].gpa, bufs[i].len, map);
		if (ret < 0) {
			break;
		}

		map->head = head;
		iovec->iov_base = map->virt;
		iovec->iov_len = bufs[i].len;

		if (bufs[i].is_write) {
			write_num++;
		} else {
			read_num++;
		}
	}

	k_mutex_unlock(&data->lock);

	if (ret < 0) {
		LOG_ERR("%s: failed to map the buffers of head %u: %d", dev->name, head, ret);
		return ret;
	}

	*read_count = read_num;
	*write_count = write_num;

	return 0;
}

static int vhost_pci_epf_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
				       uint32_t written)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	/* The buffers are mapped, not copied; no write-back is needed. */
	ARG_UNUSED(written);

	if ((queue_id != QUEUE_ID) || (head >= config->queue_size)) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	release_head(dev, head);
	k_mutex_unlock(&data->lock);

	return 0;
}

static bool vhost_pci_epf_virtq_is_ready(const struct device *dev, uint16_t queue_id)
{
	struct vhost_pci_epf_data *data = dev->data;
	bool ready;

	if (queue_id != QUEUE_ID) {
		return false;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ready = data->queue_valid;
	k_mutex_unlock(&data->lock);

	return ready;
}

static int vhost_pci_epf_get_virtq(const struct device *dev, uint16_t queue_id,
				   struct vhost_vring *vring)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	int ret = 0;

	if (queue_id != QUEUE_ID) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->queue_valid) {
		uint8_t *base = data->ring.virt;

		vring->num = config->queue_size;
		vring->desc = (struct virtq_desc *)base;
		vring->avail = (struct virtq_avail *)(base + sizeof(struct virtq_desc) *
								     config->queue_size);
		vring->used = (struct virtq_used *)(base + vring_used_offset(config->queue_size));
	} else {
		ret = -ENODEV;
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int vhost_pci_epf_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	struct vhost_pci_epf_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	*drv_feats = data->driver_features;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int vhost_pci_epf_get_supported_features(const struct device *dev, uint64_t *supported)
{
	ARG_UNUSED(dev);

	if (supported == NULL) {
		return -EINVAL;
	}

	*supported = SUPPORTED_FEATURES;

	return 0;
}

static int vhost_pci_epf_set_device_features(const struct device *dev, uint64_t wanted,
					     uint64_t *offered)
{
	struct vhost_pci_epf_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->regs == NULL) {
		ret = -ENODEV;
	} else if (reg_read8(data, VIRTIO_PCI_LEGACY_STATUS) != 0U) {
		/* The driver may have read the features already */
		ret = -EBUSY;
	} else {
		data->offered_features = (uint32_t)(wanted & SUPPORTED_FEATURES);
		reg_write32(data, VIRTIO_PCI_LEGACY_DEVICE_FEATURES, data->offered_features);

		if (offered != NULL) {
			*offered = data->offered_features;
		}
	}

	k_mutex_unlock(&data->lock);

	return ret;
}

static int vhost_pci_epf_register_virtq_ready_cb(const struct device *dev,
						 void (*callback)(const struct device *dev,
								  uint16_t queue_id,
								  void *user_data),
						 void *user_data)
{
	struct vhost_pci_epf_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->ready_cb.cb = callback;
	data->ready_cb.data = user_data;
	data->ready_notified = false;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int vhost_pci_epf_register_virtq_reset_cb(const struct device *dev,
						 void (*callback)(const struct device *dev,
								  uint16_t queue_id,
								  void *user_data),
						 void *user_data)
{
	struct vhost_pci_epf_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->reset_cb.cb = callback;
	data->reset_cb.data = user_data;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int vhost_pci_epf_register_virtq_notify_cb(const struct device *dev, uint16_t queue_id,
						  void (*callback)(const struct device *dev,
								   uint16_t queue_id,
								   void *user_data),
						  void *user_data)
{
	struct vhost_pci_epf_data *data = dev->data;

	if (queue_id != QUEUE_ID) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->notify_cb.cb = callback;
	data->notify_cb.data = user_data;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int vhost_pci_epf_notify_virtq(const struct device *dev, uint16_t queue_id)
{
	if (queue_id != QUEUE_ID) {
		return -EINVAL;
	}

	if (!vhost_pci_epf_virtq_is_ready(dev, queue_id)) {
		return -ENODEV;
	}

	return raise_irq(dev, VIRTIO_QUEUE_INTERRUPT);
}

static int vhost_pci_epf_set_device_status(const struct device *dev, uint32_t status)
{
	struct vhost_pci_epf_data *data = dev->data;

	if (status > UINT8_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	reg_write8(data, VIRTIO_PCI_LEGACY_STATUS,
		   reg_read8(data, VIRTIO_PCI_LEGACY_STATUS) | (uint8_t)status);
	k_mutex_unlock(&data->lock);

	return raise_irq(dev, VIRTIO_DEVICE_CONFIGURATION_INTERRUPT);
}

static DEVICE_API(vhost, vhost_pci_epf_api) = {
	.prepare_iovec = vhost_pci_epf_prepare_iovec,
	.release_iovec = vhost_pci_epf_release_iovec,
	.get_virtq = vhost_pci_epf_get_virtq,
	.get_driver_features = vhost_pci_epf_get_driver_features,
	.get_supported_features = vhost_pci_epf_get_supported_features,
	.set_device_features = vhost_pci_epf_set_device_features,
	.virtq_is_ready = vhost_pci_epf_virtq_is_ready,
	.register_virtq_ready_cb = vhost_pci_epf_register_virtq_ready_cb,
	.register_virtq_reset_cb = vhost_pci_epf_register_virtq_reset_cb,
	.register_virtq_notify_cb = vhost_pci_epf_register_virtq_notify_cb,
	.notify_virtq = vhost_pci_epf_notify_virtq,
	.set_device_status = vhost_pci_epf_set_device_status,
};

static void vhost_pci_epf_doorbell(struct pcie_epf_device_s *epf, uint8_t index)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (!config->has_doorbell || (index != config->doorbell_index)) {
		return;
	}

	(void)atomic_set(&data->kicked, 1);
	(void)k_work_reschedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);
}

static void vhost_pci_epf_bar_access(struct pcie_epf_device_s *epf)
{
	struct vhost_pci_epf_data *data = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf);

	(void)k_work_reschedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);
}

static int vhost_pci_epf_core_init(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	int ret;

	ret = pcie_epc_write_header(epf->epc, epf->funcno, epf->header);
	if (ret < 0) {
		LOG_ERR("%s: configuration header write failed: %d", dev->name, ret);
		return ret;
	}

	ret = pcie_epc_set_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_LEGACY_BAR]);
	if (ret < 0) {
		LOG_ERR("%s: failed to set BAR%d: %d", dev->name, VIRTIO_PCI_LEGACY_BAR, ret);
	}

	return ret;
}

static int vhost_pci_epf_bind(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const struct pcie_epc_features_s *features;
	int ret;

	features = pcie_epc_get_features(epf->epc, epf->funcno);
	if (features == NULL) {
		return -ENOTSUP;
	}

	/* The legacy driver looks for the registers in BAR 0 only */
	if ((features->bar_reserved & BIT(VIRTIO_PCI_LEGACY_BAR)) != 0U) {
		LOG_ERR("%s: BAR%d is not available", dev->name, VIRTIO_PCI_LEGACY_BAR);
		return -ENOTSUP;
	}

	if ((features->bar_fixed_64bit & BIT(VIRTIO_PCI_LEGACY_BAR)) != 0U) {
		epf->bar[VIRTIO_PCI_LEGACY_BAR].flags |= PCI_BASE_ADDRESS_MEM_TYPE_64;
	}

	data->regs = pcie_epf_alloc_space(epf, VIRTIO_PCI_LEGACY_BAR,
					  VIRTIO_PCI_LEGACY_CONFIG + config->config_data_len,
					  features->align);
	if (data->regs == NULL) {
		return -ENOMEM;
	}

	data->trapped = features->bar_access;
	data->access_notified = data->trapped && features->bar_access_notifier;
	LOG_DBG("%s: register window %s", dev->name,
		data->access_notified ? "trapped, notified" :
		data->trapped ? "trapped, polled" : "polled");
	if (data->trapped) {
		epf->bar[VIRTIO_PCI_LEGACY_BAR].flags = PCI_BASE_ADDRESS_SPACE_IO;
	}
	memset(data->regs, 0, epf->bar[VIRTIO_PCI_LEGACY_BAR].size);
	init_regs(dev);

	if (!features->core_init_notifier) {
		ret = vhost_pci_epf_core_init(epf);
		if (ret < 0) {
			pcie_epf_free_space(epf, VIRTIO_PCI_LEGACY_BAR, data->regs);
			data->regs = NULL;
			return ret;
		}
	}

	if (data->trapped) {
		ret = pcie_epc_bar_access_enable(epf->epc, epf->funcno,
						VIRTIO_PCI_LEGACY_BAR, true);
		if (ret != 0) {
			pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_LEGACY_BAR]);
			pcie_epf_free_space(epf, VIRTIO_PCI_LEGACY_BAR, data->regs);
			data->regs = NULL;
			return ret;
		}
	}
	k_work_schedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);

	return 0;
}

static void vhost_pci_epf_unbind(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	struct vhost_pci_epf_data *data = dev->data;
	struct k_work_sync sync;

	k_work_cancel_delayable_sync(&data->poll_work, &sync);
	if (data->trapped) {
		int ret = pcie_epc_bar_access_enable(epf->epc, epf->funcno,
						   VIRTIO_PCI_LEGACY_BAR, false);

		if (ret != 0) {
			LOG_ERR("Cannot disable BAR servicing: %d", ret);
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	invalidate_queue(dev);
	reclaim_queue(dev);
	k_mutex_unlock(&data->lock);

	pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_LEGACY_BAR]);
	pcie_epf_free_space(epf, VIRTIO_PCI_LEGACY_BAR, data->regs);
	data->regs = NULL;
}

/* Transitional PCI device ID of a VIRTIO device type, 0 when it has none */
static uint16_t transitional_pci_device_id(uint16_t device_id)
{
	static const struct {
		uint16_t device_id;
		uint16_t pci_device_id;
	} ids[] = {
		{1, 0x1000}, /* network device */
		{2, 0x1001}, /* block device */
		{5, 0x1002}, /* memory balloon */
		{3, 0x1003}, /* console */
		{8, 0x1004}, /* SCSI host */
		{4, 0x1005}, /* entropy source */
		{9, 0x1009}, /* 9P transport */
	};

	for (size_t i = 0; i < ARRAY_SIZE(ids); i++) {
		if (ids[i].device_id == device_id) {
			return ids[i].pci_device_id;
		}
	}

	return 0;
}

static int vhost_pci_epf_init(const struct device *dev)
{
	static const struct pcie_epf_ops_s epf_ops = {
		.bind = vhost_pci_epf_bind,
		.unbind = vhost_pci_epf_unbind,
	};
	static const struct pcie_epc_event_ops_s epf_event_ops = {
		.core_init = vhost_pci_epf_core_init,
		.doorbell = vhost_pci_epf_doorbell,
		.bar_access = vhost_pci_epf_bar_access,
	};
	const struct k_work_queue_config qcfg = {.name = "vhost-pci-epf"};
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	int ret;

	if (!device_is_ready(config->epc)) {
		LOG_ERR("%s: endpoint controller %s is not ready", dev->name, config->epc->name);
		return -ENODEV;
	}

	data->header.deviceid = transitional_pci_device_id(config->device_id);
	if (data->header.deviceid == 0U) {
		LOG_ERR("%s: VIRTIO device ID %u has no transitional PCI device ID", dev->name,
			config->device_id);
		return -ENOTSUP;
	}

	/* The legacy driver takes the device type from the subsystem ID */
	data->header.vendorid = VIRTIO_PCI_VENDOR_ID;
	data->header.subsys_vendor_id = VIRTIO_PCI_VENDOR_ID;
	data->header.subsys_id = config->device_id;
	data->header.baseclass_code = VIRTIO_PCI_BASECLASS_OTHERS;
	data->header.interrupt_pin = PCI_INTERRUPT_INTA;

	data->dev = dev;
	data->epf.header = &data->header;
	data->epf.ops = &epf_ops;
	data->epf.event_ops = &epf_event_ops;

	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->poll_work, poll_handler);
	k_work_queue_init(&data->workq);
	k_work_queue_start(&data->workq, config->workq_stack, config->workq_stack_size,
			   config->workq_priority, &qcfg);

	ret = pcie_epc_add_epf(config->epc, &data->epf);
	if (ret < 0) {
		LOG_ERR("%s: failed to add the endpoint function: %d", dev->name, ret);
		return ret;
	}

	return 0;
}

#define VHOST_PCI_EPF_INST(idx)                                                                    \
	BUILD_ASSERT(IS_POWER_OF_TWO(DT_INST_PROP(idx, queue_size_max)),                           \
		     "queue-size-max must be a power of two");                                     \
	static K_THREAD_STACK_DEFINE(vhost_pci_epf_stack_##idx, DT_INST_PROP(idx, stack_size));    \
	static const uint8_t vhost_pci_epf_config_data_##idx[] =                                   \
		DT_INST_PROP_OR(idx, config_data, {});                                             \
	static struct vhost_pci_epf_map vhost_pci_epf_maps_##idx[DT_INST_PROP(idx, max_buffers)];  \
	static const struct vhost_pci_epf_config vhost_pci_epf_config_##idx = {                    \
		.epc = DEVICE_DT_GET(DT_INST_PHANDLE(idx, epc)),                                   \
		.workq_stack = vhost_pci_epf_stack_##idx,                                          \
		.workq_stack_size = K_THREAD_STACK_SIZEOF(vhost_pci_epf_stack_##idx),              \
		.workq_priority = DT_INST_PROP(idx, priority),                                     \
		.poll_period_ms = DT_INST_PROP(idx, poll_period_ms),                               \
		.doorbell_poll_period_ms = DT_INST_PROP(idx, doorbell_poll_period_ms),             \
		.has_doorbell = DT_INST_NODE_HAS_PROP(idx, queue_notify_doorbell),                 \
		.doorbell_index = DT_INST_PROP_OR(idx, queue_notify_doorbell, 0),                  \
		.device_id = DT_INST_PROP(idx, device_id),                                         \
		.queue_size = DT_INST_PROP(idx, queue_size_max),                                   \
		.config_data = vhost_pci_epf_config_data_##idx,                                    \
		.config_data_len = DT_INST_PROP_LEN_OR(idx, config_data, 0),                       \
		.maps = vhost_pci_epf_maps_##idx,                                                  \
		.maps_count = ARRAY_SIZE(vhost_pci_epf_maps_##idx),                                \
	};                                                                                         \
	static struct vhost_pci_epf_data vhost_pci_epf_data_##idx;                                 \
	DEVICE_DT_INST_DEFINE(idx, vhost_pci_epf_init, NULL, &vhost_pci_epf_data_##idx,            \
			      &vhost_pci_epf_config_##idx, POST_KERNEL,                            \
			      CONFIG_VHOST_PCI_EPF_INIT_PRIORITY, &vhost_pci_epf_api);

DT_INST_FOREACH_STATUS_OKAY(VHOST_PCI_EPF_INST)
