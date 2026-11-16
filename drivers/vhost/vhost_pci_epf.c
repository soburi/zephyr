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

/* Both interfaces place their registers in BAR 0 */
#define VIRTIO_PCI_BAR              0
#define VIRTIO_PCI_VENDOR_ID        0x1af4
#define VIRTIO_PCI_BASECLASS_OTHERS 0xff

/*
 * Legacy VIRTIO PCI register layout ("Legacy Interfaces: A Note on PCI
 * Device Layout" in the VIRTIO specification), without MSI-X.
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

/*
 * Modern VIRTIO PCI interface ("Virtio Over PCI Bus" in the VIRTIO
 * specification), without MSI-X. The structures the capabilities point at
 * lie in BAR 0.
 */
#define VIRTIO_PCI_MODERN_DEVICE_ID_BASE 0x1040
#define VIRTIO_PCI_MODERN_REVISION       1

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define MODERN_COMMON_OFF 0x000U
#define MODERN_COMMON_LEN 0x038U
#define MODERN_ISR_OFF    0x040U
#define MODERN_ISR_LEN    0x001U
#define MODERN_NOTIFY_OFF 0x080U
#define MODERN_NOTIFY_LEN 0x002U
#define MODERN_DEVICE_OFF 0x100U

/* Fields of the common configuration structure */
#define COMMON_DEVICE_FEATURE_SELECT 0x00U
#define COMMON_DEVICE_FEATURE        0x04U
#define COMMON_DRIVER_FEATURE_SELECT 0x08U
#define COMMON_DRIVER_FEATURE        0x0cU
#define COMMON_CONFIG_MSIX_VECTOR    0x10U
#define COMMON_NUM_QUEUES            0x12U
#define COMMON_DEVICE_STATUS         0x14U
#define COMMON_CONFIG_GENERATION     0x15U
#define COMMON_QUEUE_SELECT          0x16U
#define COMMON_QUEUE_SIZE            0x18U
#define COMMON_QUEUE_MSIX_VECTOR     0x1aU
#define COMMON_QUEUE_ENABLE          0x1cU
#define COMMON_QUEUE_NOTIFY_OFF      0x1eU
#define COMMON_QUEUE_DESC            0x20U
#define COMMON_QUEUE_DRIVER          0x28U
#define COMMON_QUEUE_DEVICE          0x30U

/* The function has no MSI-X capability */
#define VIRTIO_MSI_NO_VECTOR 0xffffU

/*
 * For controllers without software BAR access, a value the driver never
 * writes makes the next register write visible to the polling fallback.
 */
#define DRIVER_FEATURES_IDLE UINT32_MAX
#define QUEUE_NOTIFY_IDLE    UINT16_MAX

/*
 * The legacy device features register has 32 bits. Bit 31 stays clear, so
 * that the idle pattern of the driver features register is never a valid
 * selection.
 */
#define LEGACY_SUPPORTED_FEATURES ((uint64_t)UINT32_MAX & ~BIT64(31))

/* Transport features of the modern interface the controller does not implement */
#define VIRTIO_F_SR_IOV            37
#define VIRTIO_F_NOTIFICATION_DATA 38
#define VIRTIO_F_NOTIF_CONFIG_DATA 39
#define VIRTIO_F_RING_RESET        40
#define VIRTIO_F_ADMIN_VQ          41

#define MODERN_SUPPORTED_FEATURES                                                                  \
	(UINT64_MAX & ~(BIT64(VIRTIO_F_SR_IOV) | BIT64(VIRTIO_F_NOTIFICATION_DATA) |               \
			BIT64(VIRTIO_F_NOTIF_CONFIG_DATA) | BIT64(VIRTIO_F_RING_RESET) |           \
			BIT64(VIRTIO_F_ADMIN_VQ)))
#define MODERN_MANDATORY_FEATURES BIT64(VIRTIO_F_VERSION_1)

/* Granularity of the outbound address windows */
#define MAP_ALIGN 4096U

/* A range of PCI bus addresses mapped through an outbound window */
struct vhost_pci_epf_map {
	void *virt;
	uintptr_t phys;
	size_t size;
	uint16_t queue_id;
	uint16_t head;
	bool used;
};

struct virtq_callback {
	void (*cb)(const struct device *dev, uint16_t queue_id, void *user_data);
	void *data;
};

struct vhost_pci_epf_queue {
	/* Rings set up by the driver, in PCI bus addresses */
	uint64_t desc;
	uint64_t driver;
	uint64_t device;
	uint16_t size;
	/* The driver has enabled the queue */
	bool enabled;
	/* The rings are mapped and the backend may use them */
	bool valid;
	bool ready_notified;
	struct vhost_pci_epf_map desc_map;
	struct vhost_pci_epf_map driver_map;
	struct vhost_pci_epf_map device_map;
	struct virtq_callback notify_cb;
};

struct vhost_pci_epf_config {
	const struct device *epc;
	k_thread_stack_t *workq_stack;
	size_t workq_stack_size;
	int workq_priority;
	uint32_t poll_period_ms;
	uint32_t doorbell_poll_period_ms;
	/* The EPC doorbell wired to the legacy queue notify register, if any */
	bool has_doorbell;
	uint8_t doorbell_index;
	bool legacy;
	uint16_t device_id;
	uint16_t num_queues;
	uint16_t queue_size;
	const uint8_t *config_data;
	size_t config_data_len;
	struct vhost_pci_epf_queue *queues;
	struct vhost_pci_epf_map *maps;
	size_t maps_count;
};

struct vhost_pci_epf_data {
	const struct device *dev;
	struct pcie_epf_device_s epf;
	struct pcie_epf_header_s header;
	struct k_work_q workq;
	struct k_work_delayable poll_work;
	/* Programs the function when the EPC reports its core initialization */
	struct k_work core_init_work;
	/* One bit per queue the driver notified */
	atomic_t kicked;
	/* Protects the fields below, the queues and the register window */
	struct k_mutex lock;
	uint8_t *regs;
	bool trapped;
	/* The EPC notifies the host accesses to the trapped register window */
	bool access_notified;
	uint16_t queue_select;
	uint32_t device_feature_select;
	uint32_t driver_feature_select;
	/* Feature bits offered to the driver, set by the backend */
	uint64_t offered_features;
	uint64_t driver_features;
	/* Page frame number of the legacy queue */
	uint32_t pfn;
	/*
	 * Device status: the register of the modern interface, the value last
	 * seen by polling the legacy one
	 */
	uint8_t status;
	/* Interrupt status of the modern interface */
	uint8_t isr;
	struct virtq_callback ready_cb;
	struct virtq_callback reset_cb;
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

static struct vhost_pci_epf_queue *get_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_pci_epf_config *config = dev->config;

	return (queue_id < config->num_queues) ? &config->queues[queue_id] : NULL;
}

/* Sizes of the parts of a split virtqueue, without the event index fields */
static size_t vring_desc_size(uint16_t num)
{
	return sizeof(struct virtq_desc) * num;
}

static size_t vring_driver_size(uint16_t num)
{
	return sizeof(uint16_t) * (2U + num);
}

static size_t vring_device_size(uint16_t num)
{
	return sizeof(uint16_t) * 2U + sizeof(struct virtq_used_elem) * num;
}

/* Offset of the used ring in the legacy split virtqueue layout */
static size_t vring_legacy_used_offset(uint16_t num)
{
	return ROUND_UP(vring_desc_size(num) + vring_driver_size(num) + sizeof(uint16_t),
			VIRTIO_PCI_LEGACY_VRING_ALIGN);
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

static void release_head(const struct device *dev, uint16_t queue_id, uint16_t head)
{
	const struct vhost_pci_epf_config *config = dev->config;

	for (size_t i = 0; i < config->maps_count; i++) {
		struct vhost_pci_epf_map *map = &config->maps[i];

		if (map->used && (map->queue_id == queue_id) && (map->head == head)) {
			unmap_bus_addr(dev, map);
		}
	}
}

/* Map the rings the driver has set up; the caller holds the lock */
static int setup_queue(const struct device *dev, struct vhost_pci_epf_queue *queue)
{
	int ret;

	ret = map_bus_addr(dev, queue->desc, vring_desc_size(queue->size), &queue->desc_map);
	if (ret == 0) {
		ret = map_bus_addr(dev, queue->driver, vring_driver_size(queue->size),
				   &queue->driver_map);
	}

	if (ret == 0) {
		ret = map_bus_addr(dev, queue->device, vring_device_size(queue->size),
				   &queue->device_map);
	}

	if (ret < 0) {
		unmap_bus_addr(dev, &queue->desc_map);
		unmap_bus_addr(dev, &queue->driver_map);
		unmap_bus_addr(dev, &queue->device_map);
		return ret;
	}

	queue->valid = true;

	return 0;
}

/*
 * The virtqueue the driver has set up is no longer valid. The backend is told
 * at once and the controller does not wait for it. The guest memory stays
 * mapped, so that a backend that has not stopped yet accesses stale but
 * accessible memory. reclaim_queue() lets it go.
 *
 * The caller holds the lock once; it is released across the reset callback.
 */
static void invalidate_queue(const struct device *dev, uint16_t queue_id)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);
	const struct virtq_callback reset_cb = data->reset_cb;
	const bool was_ready = queue->ready_notified;

	queue->valid = false;
	queue->ready_notified = false;
	queue->notify_cb.cb = NULL;
	queue->notify_cb.data = NULL;
	(void)atomic_clear_bit(&data->kicked, queue_id);

	if (was_ready && (reset_cb.cb != NULL)) {
		k_mutex_unlock(&data->lock);
		reset_cb.cb(dev, queue_id, reset_cb.data);
		k_mutex_lock(&data->lock, K_FOREVER);
	}
}

static void invalidate_queues(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;

	for (uint16_t i = 0; i < config->num_queues; i++) {
		invalidate_queue(dev, i);
	}
}

/* Unmap the guest memory of a queue that was invalidated */
static void reclaim_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);

	for (size_t i = 0; i < config->maps_count; i++) {
		if (config->maps[i].queue_id == queue_id) {
			unmap_bus_addr(dev, &config->maps[i]);
		}
	}

	unmap_bus_addr(dev, &queue->desc_map);
	unmap_bus_addr(dev, &queue->driver_map);
	unmap_bus_addr(dev, &queue->device_map);
}

/* Return the queues to their state after a device reset; the caller holds the lock */
static void reset_queues(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;

	invalidate_queues(dev);

	for (uint16_t i = 0; i < config->num_queues; i++) {
		struct vhost_pci_epf_queue *queue = &config->queues[i];

		queue->enabled = false;
		queue->desc = 0;
		queue->driver = 0;
		queue->device = 0;
		queue->size = config->queue_size;
	}
}

static uint64_t supported_features(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;

	return config->legacy ? LEGACY_SUPPORTED_FEATURES : MODERN_SUPPORTED_FEATURES;
}

static uint64_t mandatory_features(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;

	return config->legacy ? 0U : MODERN_MANDATORY_FEATURES;
}

static uint8_t device_status(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	return config->legacy ? reg_read8(data, VIRTIO_PCI_LEGACY_STATUS) : data->status;
}

static void legacy_init_regs(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	reg_write32(data, VIRTIO_PCI_LEGACY_DEVICE_FEATURES, (uint32_t)data->offered_features);
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
	if (config->legacy) {
		reg_write8(data, VIRTIO_PCI_LEGACY_ISR,
			   reg_read8(data, VIRTIO_PCI_LEGACY_ISR) | isr);
	} else {
		data->isr |= isr;
	}

	/* The level stays asserted until the driver reads the ISR status */
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

/* The legacy driver reset the device: forget the state it had set up */
static void legacy_reset(const struct device *dev)
{
	struct vhost_pci_epf_data *data = dev->data;

	reset_queues(dev);
	data->pfn = 0;
	data->driver_features = 0;
	data->queue_select = 0;
}

/* Map the queue at a new legacy page frame number, 0 to tear it down */
static void legacy_set_pfn(const struct device *dev, uint32_t pfn)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, 0);
	int ret;

	invalidate_queue(dev, 0);
	reclaim_queue(dev, 0);
	queue->enabled = false;
	data->pfn = pfn;
	if (pfn == 0U) {
		return;
	}

	queue->size = config->queue_size;
	queue->desc = (uint64_t)pfn << VIRTIO_PCI_LEGACY_QUEUE_PFN_SHIFT;
	queue->driver = queue->desc + vring_desc_size(queue->size);
	queue->device = queue->desc + vring_legacy_used_offset(queue->size);
	queue->enabled = true;

	ret = setup_queue(dev, queue);
	if (ret < 0) {
		LOG_ERR("%s: failed to map the virtqueue: %d", dev->name, ret);
		reg_write8(data, VIRTIO_PCI_LEGACY_STATUS,
			   reg_read8(data, VIRTIO_PCI_LEGACY_STATUS) | BIT(DEVICE_STATUS_FAILED));
	}
}

/* Detect a driver that (re)starts the device initialization */
static void legacy_poll_features(const struct device *dev)
{
	struct vhost_pci_epf_data *data = dev->data;
	const uint32_t features = reg_read32(data, VIRTIO_PCI_LEGACY_DRIVER_FEATURES);

	if (features == DRIVER_FEATURES_IDLE) {
		return;
	}

	reg_write32(data, VIRTIO_PCI_LEGACY_DRIVER_FEATURES, DRIVER_FEATURES_IDLE);

	/* A reset shorter than the poll period goes unnoticed otherwise */
	invalidate_queues(dev);
	data->pfn = 0;
	/* The legacy interface cannot reject a selection, mask it */
	data->driver_features = features & data->offered_features;

	LOG_DBG("%s: driver features 0x%08x, accepted 0x%08x", dev->name, features,
		(uint32_t)data->driver_features);
}

static void legacy_poll_reset(const struct device *dev, uint8_t status)
{
	struct vhost_pci_epf_data *data = dev->data;

	if ((status != 0U) || (data->status == 0U)) {
		return;
	}

	LOG_DBG("%s: device reset", dev->name);

	legacy_reset(dev);
	reg_write32(data, VIRTIO_PCI_LEGACY_QUEUE_PFN, 0);
	reg_write8(data, VIRTIO_PCI_LEGACY_ISR, 0);
}

static void legacy_poll_queue(const struct device *dev)
{
	struct vhost_pci_epf_data *data = dev->data;
	const uint32_t pfn = reg_read32(data, VIRTIO_PCI_LEGACY_QUEUE_PFN);

	if (pfn != data->pfn) {
		legacy_set_pfn(dev, pfn);
	}
}

static bool legacy_poll_notify(const struct device *dev)
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

	return queue_id == 0U;
}

/* Access the static device configuration */
static int access_device_config(const struct device *dev, size_t pos,
				struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;

	if ((pos > config->config_data_len) || (access->size > config->config_data_len - pos)) {
		return -EINVAL;
	}

	/* The driver cannot change the static configuration */
	if (access->write) {
		return 0;
	}

	access->value = 0U;
	for (uint8_t i = 0U; i < access->size; i++) {
		access->value |= (uint64_t)config->config_data[pos + i] << (8U * i);
	}

	return 0;
}

/* Called under the register lock; each host access is completed exactly once. */
static int legacy_service_access(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	uint32_t off = access->offset;
	uint32_t value = access->value;

	if (off >= VIRTIO_PCI_LEGACY_CONFIG) {
		return access_device_config(dev, off - VIRTIO_PCI_LEGACY_CONFIG, access);
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
			if (data->queue_select == 0U) {
				legacy_set_pfn(dev, value);
			}
			break;
		case VIRTIO_PCI_LEGACY_QUEUE_NOTIFY:
			if (access->size != 2U) {
				return -EINVAL;
			}
			if (value == 0U) {
				(void)atomic_set_bit(&data->kicked, 0);
			}
			break;
		case VIRTIO_PCI_LEGACY_STATUS:
			if (access->size != 1U) {
				return -EINVAL;
			}
			if (value == 0U) {
				int ret;

				legacy_reset(dev);
				legacy_init_regs(dev);
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
		access->value = (uint32_t)data->offered_features;
		return access->size == 4U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_DRIVER_FEATURES:
		access->value = (uint32_t)data->driver_features;
		return access->size == 4U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_QUEUE_PFN:
		access->value = data->queue_select == 0U ? data->pfn : 0U;
		return access->size == 4U ? 0 : -EINVAL;
	case VIRTIO_PCI_LEGACY_QUEUE_SIZE:
		access->value = data->queue_select == 0U ? config->queue_size : 0U;
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

/* Access a 64-bit field of the common configuration, as a whole or by halves */
static int modern_access64(uint64_t *field, uint32_t pos, struct pcie_epc_bar_access *access)
{
	const uint32_t shift = 8U * pos;
	uint64_t mask;

	if (!(((access->size == 4U) && ((pos == 0U) || (pos == 4U))) ||
	      ((access->size == 8U) && (pos == 0U)))) {
		return -EINVAL;
	}

	if (!access->write) {
		access->value = (access->size == 8U) ? *field : (uint32_t)(*field >> shift);
		return 0;
	}

	mask = (access->size == 8U) ? UINT64_MAX : ((uint64_t)UINT32_MAX << shift);
	*field = (*field & ~mask) | ((access->value << shift) & mask);

	return 0;
}

/* The 64-bit queue field at an offset of the common configuration */
static uint64_t *queue_field(struct vhost_pci_epf_queue *queue, uint32_t off)
{
	if (off < COMMON_QUEUE_DRIVER) {
		return &queue->desc;
	}

	return (off < COMMON_QUEUE_DEVICE) ? &queue->driver : &queue->device;
}

static void modern_write_status(const struct device *dev, uint8_t status)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (status == 0U) {
		LOG_DBG("%s: device reset", dev->name);

		reset_queues(dev);
		data->driver_features = 0;
		data->device_feature_select = 0;
		data->driver_feature_select = 0;
		data->queue_select = 0;
		data->status = 0;
		data->isr = 0;
		(void)pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, false);
		return;
	}

	/* Refuse a selection outside the offered bits; the driver checks the bit */
	if (((status & BIT(DEVICE_STATUS_FEATURES_OK)) != 0U) &&
	    ((data->status & BIT(DEVICE_STATUS_FEATURES_OK)) == 0U) &&
	    (((data->driver_features & ~data->offered_features) != 0U) ||
	     ((data->driver_features & MODERN_MANDATORY_FEATURES) !=
	      MODERN_MANDATORY_FEATURES))) {
		LOG_WRN("%s: driver features 0x%08x%08x refused", dev->name,
			(uint32_t)(data->driver_features >> 32),
			(uint32_t)data->driver_features);
		status &= ~BIT(DEVICE_STATUS_FEATURES_OK);
	}

	data->status = status;
}

static void modern_enable_queue(const struct device *dev, uint16_t queue_id)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);
	int ret;

	/* Let the memory of an earlier setup go */
	reclaim_queue(dev, queue_id);

	ret = setup_queue(dev, queue);
	if (ret < 0) {
		LOG_ERR("%s: failed to map virtqueue %u: %d", dev->name, queue_id, ret);
		data->status |= BIT(DEVICE_STATUS_NEEDS_RESET);
		return;
	}

	queue->enabled = true;
}

static int modern_common_write(const struct device *dev, struct pcie_epc_bar_access *access)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, data->queue_select);
	const uint32_t off = access->offset - MODERN_COMMON_OFF;
	const uint32_t value = access->value;

	switch (off) {
	case COMMON_DEVICE_FEATURE_SELECT:
		data->device_feature_select = value;
		return access->size == 4U ? 0 : -EINVAL;
	case COMMON_DRIVER_FEATURE_SELECT:
		data->driver_feature_select = value;
		return access->size == 4U ? 0 : -EINVAL;
	case COMMON_DRIVER_FEATURE:
		if (access->size != 4U) {
			return -EINVAL;
		}
		if (data->driver_feature_select < 2U) {
			const uint32_t shift = 32U * data->driver_feature_select;

			data->driver_features &= ~((uint64_t)UINT32_MAX << shift);
			data->driver_features |= (uint64_t)value << shift;
		}
		return 0;
	case COMMON_DEVICE_STATUS:
		if (access->size != 1U) {
			return -EINVAL;
		}
		modern_write_status(dev, value);
		return 0;
	case COMMON_QUEUE_SELECT:
		data->queue_select = value;
		return access->size == 2U ? 0 : -EINVAL;
	case COMMON_QUEUE_SIZE:
		if (access->size != 2U) {
			return -EINVAL;
		}
		/* A split virtqueue has a power of two entries, at most the maximum */
		if ((queue != NULL) && !queue->enabled && (value != 0U) &&
		    IS_POWER_OF_TWO(value) && (value <= queue->size)) {
			queue->size = value;
		}
		return 0;
	case COMMON_QUEUE_ENABLE:
		if (access->size != 2U) {
			return -EINVAL;
		}
		/* Only a device reset disables a queue */
		if ((queue != NULL) && !queue->enabled && (value == 1U)) {
			modern_enable_queue(dev, data->queue_select);
		}
		return 0;
	case COMMON_CONFIG_MSIX_VECTOR:
	case COMMON_QUEUE_MSIX_VECTOR:
		/* Without MSI-X the vectors stay VIRTIO_MSI_NO_VECTOR */
		return access->size == 2U ? 0 : -EINVAL;
	default:
		break;
	}

	if (off >= COMMON_QUEUE_DESC) {
		uint64_t ignored = 0U;
		uint64_t *field = &ignored;

		/* The rings of an enabled queue cannot move */
		if ((queue != NULL) && !queue->enabled) {
			field = queue_field(queue, off);
		}

		return modern_access64(field, off % 8U, access);
	}

	/* The remaining fields are read-only for the driver */
	return 0;
}

static int modern_common_read(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, data->queue_select);
	const uint32_t off = access->offset - MODERN_COMMON_OFF;
	uint32_t select;
	uint8_t size;

	switch (off) {
	case COMMON_DEVICE_FEATURE_SELECT:
		access->value = data->device_feature_select;
		size = 4U;
		break;
	case COMMON_DEVICE_FEATURE:
		select = data->device_feature_select;
		access->value =
			(select < 2U) ? (uint32_t)(data->offered_features >> (32U * select)) : 0U;
		size = 4U;
		break;
	case COMMON_DRIVER_FEATURE_SELECT:
		access->value = data->driver_feature_select;
		size = 4U;
		break;
	case COMMON_DRIVER_FEATURE:
		select = data->driver_feature_select;
		access->value =
			(select < 2U) ? (uint32_t)(data->driver_features >> (32U * select)) : 0U;
		size = 4U;
		break;
	case COMMON_CONFIG_MSIX_VECTOR:
	case COMMON_QUEUE_MSIX_VECTOR:
		access->value = VIRTIO_MSI_NO_VECTOR;
		size = 2U;
		break;
	case COMMON_NUM_QUEUES:
		access->value = config->num_queues;
		size = 2U;
		break;
	case COMMON_DEVICE_STATUS:
		access->value = data->status;
		size = 1U;
		break;
	case COMMON_CONFIG_GENERATION:
		/* The device configuration is static */
		access->value = 0U;
		size = 1U;
		break;
	case COMMON_QUEUE_SELECT:
		access->value = data->queue_select;
		size = 2U;
		break;
	case COMMON_QUEUE_SIZE:
		/* A size of 0 tells the driver the queue does not exist */
		access->value = (queue != NULL) ? queue->size : 0U;
		size = 2U;
		break;
	case COMMON_QUEUE_ENABLE:
		access->value = ((queue != NULL) && queue->enabled) ? 1U : 0U;
		size = 2U;
		break;
	case COMMON_QUEUE_NOTIFY_OFF:
		/* All queues share the notification register */
		access->value = 0U;
		size = 2U;
		break;
	default:
		if (off >= COMMON_QUEUE_DESC) {
			uint64_t zero = 0U;

			return modern_access64((queue != NULL) ? queue_field(queue, off) : &zero,
					       off % 8U, access);
		}
		return -EINVAL;
	}

	return access->size == size ? 0 : -EINVAL;
}

/* Called under the register lock; each host access is completed exactly once. */
static int modern_service_access(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const uint32_t off = access->offset;

	if (off < MODERN_COMMON_OFF + MODERN_COMMON_LEN) {
		if (off + access->size > MODERN_COMMON_OFF + MODERN_COMMON_LEN) {
			return -EINVAL;
		}
		return access->write ? modern_common_write(dev, access)
				     : modern_common_read(dev, access);
	}

	if (off == MODERN_ISR_OFF) {
		if ((access->size != 1U) || access->write) {
			return -EINVAL;
		}
		/* Reading the ISR status acknowledges the interrupt */
		access->value = data->isr;
		data->isr = 0U;
		return pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, false);
	}

	if (off == MODERN_NOTIFY_OFF) {
		if (!access->write || (access->size != 2U)) {
			return -EINVAL;
		}
		/* Without VIRTIO_F_NOTIFICATION_DATA the value is the queue index */
		if (access->value < config->num_queues) {
			(void)atomic_set_bit(&data->kicked, (int)access->value);
		}
		return 0;
	}

	if (off >= MODERN_DEVICE_OFF) {
		return access_device_config(dev, off - MODERN_DEVICE_OFF, access);
	}

	return -EINVAL;
}

static int service_access(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;

	if ((access->bar != VIRTIO_PCI_BAR) ||
	    ((access->size != 1U) && (access->size != 2U) && (access->size != 4U) &&
	     (access->size != 8U)) ||
	    ((access->offset % access->size) != 0U)) {
		return -EINVAL;
	}

	return config->legacy ? legacy_service_access(dev, access)
			      : modern_service_access(dev, access);
}

static void poll_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct vhost_pci_epf_data *data = CONTAINER_OF(dwork, struct vhost_pci_epf_data, poll_work);
	const struct device *dev = data->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct virtq_callback ready_cb;
	uint32_t period_ms = config->poll_period_ms;
	uint32_t ready = 0U;
	uint32_t notified = 0U;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->trapped) {
		struct pcie_epc_bar_access access;
		int ret = pcie_epc_bar_access_get(config->epc, data->epf.funcno, &access);

		if (ret == 0) {
			ret = service_access(dev, &access);
			ret = pcie_epc_bar_access_complete(config->epc, data->epf.funcno,
							   &access, ret);
			if ((ret != 0) && (ret != -ESTALE)) {
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
		legacy_poll_reset(dev, reg_read8(data, VIRTIO_PCI_LEGACY_STATUS));
		legacy_poll_features(dev);
		legacy_poll_queue(dev);
		data->status = reg_read8(data, VIRTIO_PCI_LEGACY_STATUS);
	}

	if ((device_status(dev) & BIT(DEVICE_STATUS_DRIVER_OK)) != 0U) {
		/* A doorbell write may or may not reach the register as well */
		if (!data->trapped && legacy_poll_notify(dev)) {
			(void)atomic_set_bit(&data->kicked, 0);
		}

		for (uint16_t i = 0; i < config->num_queues; i++) {
			struct vhost_pci_epf_queue *queue = &config->queues[i];

			if (!queue->valid) {
				continue;
			}

			if (!queue->ready_notified && (data->ready_cb.cb != NULL)) {
				queue->ready_notified = true;
				ready |= BIT(i);
			}

			if (atomic_test_and_clear_bit(&data->kicked, i)) {
				notified |= BIT(i);
			}
		}

		/* Only a device reset is left to be found by polling */
		if (config->has_doorbell && !data->trapped) {
			period_ms = config->doorbell_poll_period_ms;
		}
	}

	ready_cb = data->ready_cb;
	k_mutex_unlock(&data->lock);

	/* The callbacks call back into the controller, run them unlocked */
	for (uint16_t i = 0; i < config->num_queues; i++) {
		if ((ready & BIT(i)) != 0U) {
			ready_cb.cb(dev, i, ready_cb.data);
		}
	}

	/*
	 * A notification that preceded the registration of the notify callback
	 * is lost, so process a queue that became ready once in any case.
	 */
	for (uint16_t i = 0; i < config->num_queues; i++) {
		struct virtq_callback notify_cb;

		if (((ready | notified) & BIT(i)) == 0U) {
			continue;
		}

		k_mutex_lock(&data->lock, K_FOREVER);
		notify_cb = config->queues[i].notify_cb;
		k_mutex_unlock(&data->lock);

		if (notify_cb.cb != NULL) {
			notify_cb.cb(dev, i, notify_cb.data);
		}
	}

	/* No-op when a notification has already queued the next run */
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
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);
	size_t read_num = 0;
	size_t write_num = 0;
	int ret = 0;

	*read_count = 0;
	*write_count = 0;

	if (queue == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (!queue->valid) {
		ret = -ENODEV;
	} else if (head >= queue->size) {
		ret = -EINVAL;
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

		map->queue_id = queue_id;
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
	struct vhost_pci_epf_data *data = dev->data;

	/* The buffers are mapped, not copied; no write-back is needed. */
	ARG_UNUSED(written);

	if (get_queue(dev, queue_id) == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	release_head(dev, queue_id, head);
	k_mutex_unlock(&data->lock);

	return 0;
}

static bool vhost_pci_epf_virtq_is_ready(const struct device *dev, uint16_t queue_id)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);
	bool ready;

	if (queue == NULL) {
		return false;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	ready = queue->valid;
	k_mutex_unlock(&data->lock);

	return ready;
}

static int vhost_pci_epf_get_virtq(const struct device *dev, uint16_t queue_id,
				   struct vhost_vring *vring)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);
	int ret = 0;

	if (queue == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	if (queue->valid) {
		vring->num = queue->size;
		vring->desc = queue->desc_map.virt;
		vring->avail = queue->driver_map.virt;
		vring->used = queue->device_map.virt;
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
	if (supported == NULL) {
		return -EINVAL;
	}

	*supported = supported_features(dev);

	return 0;
}

static int vhost_pci_epf_set_device_features(const struct device *dev, uint64_t wanted,
					     uint64_t *offered)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->regs == NULL) {
		ret = -ENODEV;
	} else if (device_status(dev) != 0U) {
		/* The driver may have read the features already */
		ret = -EBUSY;
	} else {
		data->offered_features =
			(wanted & supported_features(dev)) | mandatory_features(dev);
		if (config->legacy) {
			reg_write32(data, VIRTIO_PCI_LEGACY_DEVICE_FEATURES,
				    (uint32_t)data->offered_features);
		}

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
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->ready_cb.cb = callback;
	data->ready_cb.data = user_data;
	for (uint16_t i = 0; i < config->num_queues; i++) {
		config->queues[i].ready_notified = false;
	}
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
	struct vhost_pci_epf_queue *queue = get_queue(dev, queue_id);

	if (queue == NULL) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	queue->notify_cb.cb = callback;
	queue->notify_cb.data = user_data;
	k_mutex_unlock(&data->lock);

	return 0;
}

static int vhost_pci_epf_notify_virtq(const struct device *dev, uint16_t queue_id)
{
	if (get_queue(dev, queue_id) == NULL) {
		return -EINVAL;
	}

	if (!vhost_pci_epf_virtq_is_ready(dev, queue_id)) {
		return -ENODEV;
	}

	return raise_irq(dev, VIRTIO_QUEUE_INTERRUPT);
}

static int vhost_pci_epf_set_device_status(const struct device *dev, uint32_t status)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (status > UINT8_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	if (config->legacy) {
		reg_write8(data, VIRTIO_PCI_LEGACY_STATUS,
			   reg_read8(data, VIRTIO_PCI_LEGACY_STATUS) | (uint8_t)status);
	} else {
		data->status |= (uint8_t)status;
	}
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

	(void)atomic_set_bit(&data->kicked, 0);
	(void)k_work_reschedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);
}

static void vhost_pci_epf_bar_access(struct pcie_epf_device_s *epf)
{
	struct vhost_pci_epf_data *data = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf);

	(void)k_work_reschedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);
}

/* Program the configuration header and the register BAR of the function */
static int vhost_pci_epf_setup(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	int ret;

	ret = pcie_epc_write_header(epf->epc, epf->funcno, epf->header);
	if (ret < 0) {
		LOG_ERR("%s: configuration header write failed: %d", dev->name, ret);
		return ret;
	}

	ret = pcie_epc_set_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_BAR]);
	if (ret < 0) {
		LOG_ERR("%s: failed to set BAR%d: %d", dev->name, VIRTIO_PCI_BAR, ret);
	}

	return ret;
}

static void core_init_handler(struct k_work *work)
{
	struct vhost_pci_epf_data *data =
		CONTAINER_OF(work, struct vhost_pci_epf_data, core_init_work);

	(void)vhost_pci_epf_setup(&data->epf);
}

static void vhost_pci_epf_core_init(struct pcie_epf_device_s *epf)
{
	struct vhost_pci_epf_data *data = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf);

	(void)k_work_submit_to_queue(&data->workq, &data->core_init_work);
}

/* Add a VIRTIO vendor-specific capability pointing at a range of BAR 0 */
static int add_virtio_cap(struct pcie_epf_device_s *epf, uint8_t cfg_type, uint32_t offset,
			  uint32_t length, bool notify)
{
	/*
	 * struct virtio_pci_cap, or struct virtio_pci_notify_cap, without the
	 * capability ID and next pointer, which the EPC fills in
	 */
	uint8_t body[18] = {0};
	const uint8_t cap_len = notify ? 20U : 16U;

	body[0] = cap_len;
	body[1] = cfg_type;
	body[2] = VIRTIO_PCI_BAR;
	sys_put_le32(offset, &body[6]);
	sys_put_le32(length, &body[10]);
	/* notify_off_multiplier stays 0: every queue uses the same register */

	return pcie_epc_add_capability(epf->epc, epf->funcno, PCI_CAP_ID_VNDR, body,
				       cap_len - 2U);
}

static int modern_add_caps(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	int ret;

	ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_COMMON_CFG, MODERN_COMMON_OFF, MODERN_COMMON_LEN,
			     false);
	if (ret == 0) {
		ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_NOTIFY_CFG, MODERN_NOTIFY_OFF,
				     MODERN_NOTIFY_LEN, true);
	}

	if (ret == 0) {
		ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_ISR_CFG, MODERN_ISR_OFF, MODERN_ISR_LEN,
				     false);
	}

	if ((ret == 0) && (config->config_data_len > 0U)) {
		ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_DEVICE_CFG, MODERN_DEVICE_OFF,
				     config->config_data_len, false);
	}

	if (ret < 0) {
		LOG_ERR("%s: failed to add the VIRTIO capabilities: %d", dev->name, ret);
	}

	return ret;
}

static int vhost_pci_epf_bind(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const struct pcie_epc_features_s *features;
	size_t size;
	int ret;

	features = pcie_epc_get_features(epf->epc, epf->funcno);
	if (features == NULL) {
		return -ENOTSUP;
	}

	if ((features->bar_reserved & BIT(VIRTIO_PCI_BAR)) != 0U) {
		LOG_ERR("%s: BAR%d is not available", dev->name, VIRTIO_PCI_BAR);
		return -ENOTSUP;
	}

	/* Polling cannot follow the registers the modern interface banks by selectors */
	if (!config->legacy && (!features->bar_access || !features->capability_writable)) {
		LOG_ERR("%s: the modern interface needs software BAR access and capabilities",
			dev->name);
		return -ENOTSUP;
	}

	if ((features->bar_fixed_64bit & BIT(VIRTIO_PCI_BAR)) != 0U) {
		epf->bar[VIRTIO_PCI_BAR].flags |= PCI_BASE_ADDRESS_MEM_TYPE_64;
	}

	size = (config->legacy ? VIRTIO_PCI_LEGACY_CONFIG : MODERN_DEVICE_OFF) +
	       config->config_data_len;
	data->regs = pcie_epf_alloc_space(epf, VIRTIO_PCI_BAR, size, features->align);
	if (data->regs == NULL) {
		return -ENOMEM;
	}

	data->trapped = features->bar_access;
	data->access_notified = data->trapped && features->bar_access_notifier;
	LOG_DBG("%s: %s register window %s", dev->name, config->legacy ? "legacy" : "modern",
		data->access_notified ? "trapped, notified" :
		data->trapped ? "trapped, polled" : "polled");

	/* The legacy driver expects an I/O BAR when the registers are emulated */
	if (config->legacy && data->trapped) {
		epf->bar[VIRTIO_PCI_BAR].flags = PCI_BASE_ADDRESS_SPACE_IO;
	}

	memset(data->regs, 0, epf->bar[VIRTIO_PCI_BAR].size);

	k_mutex_lock(&data->lock, K_FOREVER);
	reset_queues(dev);
	data->status = 0;
	data->isr = 0;
	data->pfn = 0;
	k_mutex_unlock(&data->lock);

	if (config->legacy) {
		legacy_init_regs(dev);
	} else {
		ret = modern_add_caps(epf);
		if (ret < 0) {
			goto err_free;
		}
	}

	if (!features->core_init_notifier) {
		ret = vhost_pci_epf_setup(epf);
		if (ret < 0) {
			goto err_free;
		}
	}

	if (data->trapped) {
		ret = pcie_epc_bar_access_enable(epf->epc, epf->funcno, VIRTIO_PCI_BAR, true);
		if (ret != 0) {
			pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_BAR]);
			goto err_free;
		}
	}

	k_work_schedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);

	return 0;

err_free:
	/* The framework removes the capabilities of a function that fails to bind */
	pcie_epf_free_space(epf, VIRTIO_PCI_BAR, data->regs);
	data->regs = NULL;
	return ret;
}

static void vhost_pci_epf_unbind(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct k_work_sync sync;

	k_work_cancel_sync(&data->core_init_work, &sync);
	k_work_cancel_delayable_sync(&data->poll_work, &sync);
	if (data->trapped) {
		int ret = pcie_epc_bar_access_enable(epf->epc, epf->funcno, VIRTIO_PCI_BAR, false);

		if (ret != 0) {
			LOG_ERR("Cannot disable BAR servicing: %d", ret);
		}
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	invalidate_queues(dev);
	for (uint16_t i = 0; i < config->num_queues; i++) {
		reclaim_queue(dev, i);
	}
	k_mutex_unlock(&data->lock);

	pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_BAR]);
	pcie_epf_free_space(epf, VIRTIO_PCI_BAR, data->regs);
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

	if (config->legacy) {
		data->header.deviceid = transitional_pci_device_id(config->device_id);
		if (data->header.deviceid == 0U) {
			LOG_ERR("%s: VIRTIO device ID %u has no transitional PCI device ID",
				dev->name, config->device_id);
			return -ENOTSUP;
		}
	} else {
		data->header.deviceid = VIRTIO_PCI_MODERN_DEVICE_ID_BASE + config->device_id;
		data->header.revid = VIRTIO_PCI_MODERN_REVISION;
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
	/* Until the backend sets them, only the bits the transport requires */
	data->offered_features = mandatory_features(dev);

	k_mutex_init(&data->lock);
	k_work_init_delayable(&data->poll_work, poll_handler);
	k_work_init(&data->core_init_work, core_init_handler);
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
	BUILD_ASSERT(DT_INST_PROP(idx, num_queues) <= 32, "num-queues must not exceed 32");        \
	BUILD_ASSERT(!DT_INST_PROP(idx, legacy) || (DT_INST_PROP(idx, num_queues) == 1),           \
		     "the legacy interface has a single virtqueue");                               \
	static K_THREAD_STACK_DEFINE(vhost_pci_epf_stack_##idx, DT_INST_PROP(idx, stack_size));    \
	static const uint8_t vhost_pci_epf_config_data_##idx[] =                                   \
		DT_INST_PROP_OR(idx, config_data, {});                                             \
	static struct vhost_pci_epf_queue                                                          \
		vhost_pci_epf_queues_##idx[DT_INST_PROP(idx, num_queues)];                         \
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
		.legacy = DT_INST_PROP(idx, legacy),                                               \
		.device_id = DT_INST_PROP(idx, device_id),                                         \
		.num_queues = DT_INST_PROP(idx, num_queues),                                       \
		.queue_size = DT_INST_PROP(idx, queue_size_max),                                   \
		.config_data = vhost_pci_epf_config_data_##idx,                                    \
		.config_data_len = DT_INST_PROP_LEN_OR(idx, config_data, 0),                       \
		.queues = vhost_pci_epf_queues_##idx,                                              \
		.maps = vhost_pci_epf_maps_##idx,                                                  \
		.maps_count = ARRAY_SIZE(vhost_pci_epf_maps_##idx),                                \
	};                                                                                         \
	static struct vhost_pci_epf_data vhost_pci_epf_data_##idx;                                 \
	DEVICE_DT_INST_DEFINE(idx, vhost_pci_epf_init, NULL, &vhost_pci_epf_data_##idx,            \
			      &vhost_pci_epf_config_##idx, POST_KERNEL,                            \
			      CONFIG_VHOST_PCI_EPF_INIT_PRIORITY, &vhost_pci_epf_api);

DT_INST_FOREACH_STATUS_OKAY(VHOST_PCI_EPF_INST)
