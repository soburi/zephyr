/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT zephyr_vhost_pci_epf

#include <errno.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epf.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost_pci_epf, CONFIG_VHOST_LOG_LEVEL);

/*
 * Modern VIRTIO PCI interface ("Virtio Over PCI Bus" in the VIRTIO
 * specification), without MSI-X. The structures the capabilities point at
 * lie in BAR 0.
 */
#define VIRTIO_PCI_BAR                   0
#define VIRTIO_PCI_VENDOR_ID             0x1af4
#define VIRTIO_PCI_BASECLASS_OTHERS      0xff
#define VIRTIO_PCI_MODERN_DEVICE_ID_BASE 0x1040
#define VIRTIO_PCI_MODERN_REVISION       1

#define VIRTIO_PCI_CAP_COMMON_CFG 1
#define VIRTIO_PCI_CAP_NOTIFY_CFG 2
#define VIRTIO_PCI_CAP_ISR_CFG    3
#define VIRTIO_PCI_CAP_DEVICE_CFG 4

#define COMMON_CFG_OFF 0x000U
#define COMMON_CFG_LEN 0x038U
#define ISR_CFG_OFF    0x040U
#define ISR_CFG_LEN    0x001U
#define NOTIFY_CFG_OFF 0x080U
#define NOTIFY_CFG_LEN 0x002U
#define DEVICE_CFG_OFF 0x100U

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

/* Transport features the controller does not implement */
#define VIRTIO_F_SR_IOV            37
#define VIRTIO_F_NOTIFICATION_DATA 38
#define VIRTIO_F_NOTIF_CONFIG_DATA 39
#define VIRTIO_F_RING_RESET        40
#define VIRTIO_F_ADMIN_VQ          41

#define SUPPORTED_FEATURES                                                                  \
	(UINT64_MAX & ~(BIT64(VIRTIO_F_SR_IOV) | BIT64(VIRTIO_F_NOTIFICATION_DATA) |               \
			BIT64(VIRTIO_F_NOTIF_CONFIG_DATA) | BIT64(VIRTIO_F_RING_RESET) |           \
			BIT64(VIRTIO_F_ADMIN_VQ)))
#define MANDATORY_FEATURES BIT64(VIRTIO_F_VERSION_1)

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
	/* Period of polling for BAR accesses the EPC does not notify */
	uint32_t poll_period_ms;
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
	/* Protects the fields below and the queues */
	struct k_mutex lock;
	/* Memory behind BAR 0, whose accesses the function serves */
	void *bar_mem;
	/* The EPC notifies the host accesses to BAR 0 */
	bool access_notified;
	uint16_t queue_select;
	uint32_t device_feature_select;
	uint32_t driver_feature_select;
	/* Feature bits offered to the driver, set by the backend */
	uint64_t offered_features;
	uint64_t driver_features;
	uint8_t status;
	uint8_t isr;
	struct virtq_callback ready_cb;
	struct virtq_callback reset_cb;
};

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

static int raise_irq(const struct device *dev, uint8_t isr)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);
	data->isr |= isr;
	/* The level stays asserted until the driver reads the ISR status */
	ret = pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, true);
	k_mutex_unlock(&data->lock);

	if (ret < 0) {
		LOG_ERR("%s: failed to raise interrupt: %d", dev->name, ret);
		return -EIO;
	}

	return 0;
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

/* Access a 64-bit field of the common configuration, as a whole or by halves */
static int access64(uint64_t *field, uint32_t pos, struct pcie_epc_bar_access *access)
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

static void write_status(const struct device *dev, uint8_t status)
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
	     ((data->driver_features & MANDATORY_FEATURES) !=
	      MANDATORY_FEATURES))) {
		LOG_WRN("%s: driver features 0x%08x%08x refused", dev->name,
			(uint32_t)(data->driver_features >> 32),
			(uint32_t)data->driver_features);
		status &= ~BIT(DEVICE_STATUS_FEATURES_OK);
	}

	data->status = status;
}

static void enable_queue(const struct device *dev, uint16_t queue_id)
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

static int common_write(const struct device *dev, struct pcie_epc_bar_access *access)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, data->queue_select);
	const uint32_t off = access->offset - COMMON_CFG_OFF;
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
		write_status(dev, value);
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
			enable_queue(dev, data->queue_select);
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

		return access64(field, off % 8U, access);
	}

	/* The remaining fields are read-only for the driver */
	return 0;
}

static int common_read(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct vhost_pci_epf_queue *queue = get_queue(dev, data->queue_select);
	const uint32_t off = access->offset - COMMON_CFG_OFF;
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

			return access64((queue != NULL) ? queue_field(queue, off) : &zero,
					       off % 8U, access);
		}
		return -EINVAL;
	}

	return access->size == size ? 0 : -EINVAL;
}

/* Called under the lock; each host access is completed exactly once. */
static int serve_access(const struct device *dev, struct pcie_epc_bar_access *access)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const uint32_t off = access->offset;

	if ((access->bar != VIRTIO_PCI_BAR) ||
	    ((access->size != 1U) && (access->size != 2U) && (access->size != 4U) &&
	     (access->size != 8U)) ||
	    ((off % access->size) != 0U)) {
		return -EINVAL;
	}

	if (off < COMMON_CFG_OFF + COMMON_CFG_LEN) {
		if (off + access->size > COMMON_CFG_OFF + COMMON_CFG_LEN) {
			return -EINVAL;
		}
		return access->write ? common_write(dev, access)
				     : common_read(dev, access);
	}

	if (off == ISR_CFG_OFF) {
		if ((access->size != 1U) || access->write) {
			return -EINVAL;
		}
		/* Reading the ISR status acknowledges the interrupt */
		access->value = data->isr;
		data->isr = 0U;
		return pcie_epc_set_legacy_irq(config->epc, data->epf.funcno, false);
	}

	if (off == NOTIFY_CFG_OFF) {
		if (!access->write || (access->size != 2U)) {
			return -EINVAL;
		}
		/* Without VIRTIO_F_NOTIFICATION_DATA the value is the queue index */
		if (access->value < config->num_queues) {
			(void)atomic_set_bit(&data->kicked, (int)access->value);
		}
		return 0;
	}

	if (off >= DEVICE_CFG_OFF) {
		return access_device_config(dev, off - DEVICE_CFG_OFF, access);
	}

	return -EINVAL;
}

static void poll_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct vhost_pci_epf_data *data = CONTAINER_OF(dwork, struct vhost_pci_epf_data, poll_work);
	const struct device *dev = data->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct pcie_epc_bar_access access;
	struct virtq_callback ready_cb;
	uint32_t ready = 0U;
	uint32_t notified = 0U;
	bool more = false;
	int ret;

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = pcie_epc_bar_access_get(config->epc, data->epf.funcno, &access);
	if (ret == 0) {
		ret = serve_access(dev, &access);
		ret = pcie_epc_bar_access_complete(config->epc, data->epf.funcno, &access, ret);
		if ((ret != 0) && (ret != -ESTALE)) {
			LOG_ERR("BAR response failed: %d", ret);
		}

		/* Look for the next access right away */
		more = true;
	}

	if ((data->status & BIT(DEVICE_STATUS_DRIVER_OK)) != 0U) {
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

	/*
	 * Without notifications from the EPC, poll for the next access. With
	 * them, the next one queues the work. Either is a no-op when a
	 * notification has already queued the next run.
	 */
	if (more) {
		k_work_schedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);
	} else if (!data->access_notified) {
		k_work_schedule_for_queue(&data->workq, &data->poll_work,
					  K_MSEC(config->poll_period_ms));
	}
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

	*supported = SUPPORTED_FEATURES;

	return 0;
}

static int vhost_pci_epf_set_device_features(const struct device *dev, uint64_t wanted,
					     uint64_t *offered)
{
	struct vhost_pci_epf_data *data = dev->data;
	int ret = 0;

	k_mutex_lock(&data->lock, K_FOREVER);

	if (data->bar_mem == NULL) {
		ret = -ENODEV;
	} else if (data->status != 0U) {
		/* The driver may have read the features already */
		ret = -EBUSY;
	} else {
		data->offered_features =
			(wanted & SUPPORTED_FEATURES) | MANDATORY_FEATURES;

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
	struct vhost_pci_epf_data *data = dev->data;

	if (status > UINT8_MAX) {
		return -EINVAL;
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	data->status |= (uint8_t)status;
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

static int add_caps(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	int ret;

	ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_COMMON_CFG, COMMON_CFG_OFF, COMMON_CFG_LEN,
			     false);
	if (ret == 0) {
		ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_NOTIFY_CFG, NOTIFY_CFG_OFF,
				     NOTIFY_CFG_LEN, true);
	}

	if (ret == 0) {
		ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_ISR_CFG, ISR_CFG_OFF, ISR_CFG_LEN,
				     false);
	}

	if ((ret == 0) && (config->config_data_len > 0U)) {
		ret = add_virtio_cap(epf, VIRTIO_PCI_CAP_DEVICE_CFG, DEVICE_CFG_OFF,
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
	int ret;

	features = pcie_epc_get_features(epf->epc, epf->funcno);
	if (features == NULL) {
		return -ENOTSUP;
	}

	if ((features->bar_reserved & BIT(VIRTIO_PCI_BAR)) != 0U) {
		LOG_ERR("%s: BAR%d is not available", dev->name, VIRTIO_PCI_BAR);
		return -ENOTSUP;
	}

	/* Polling cannot follow the registers the common configuration banks by selectors */
	if (!features->bar_access || !features->capability_writable) {
		LOG_ERR("%s: the controller needs software BAR access and capabilities",
			dev->name);
		return -ENOTSUP;
	}

	if ((features->bar_fixed_64bit & BIT(VIRTIO_PCI_BAR)) != 0U) {
		epf->bar[VIRTIO_PCI_BAR].flags |= PCI_BASE_ADDRESS_MEM_TYPE_64;
	}

	/* The EPC serves the accesses to BAR 0 to the function, the memory stays unused */
	data->bar_mem = pcie_epf_alloc_space(epf, VIRTIO_PCI_BAR,
					     DEVICE_CFG_OFF + config->config_data_len,
					     features->align);
	if (data->bar_mem == NULL) {
		return -ENOMEM;
	}

	data->access_notified = features->bar_access_notifier;
	LOG_DBG("%s: BAR accesses %s", dev->name, data->access_notified ? "notified" : "polled");

	k_mutex_lock(&data->lock, K_FOREVER);
	reset_queues(dev);
	data->status = 0;
	data->isr = 0;
	k_mutex_unlock(&data->lock);

	ret = add_caps(epf);
	if (ret < 0) {
		goto err_free;
	}

	if (!features->core_init_notifier) {
		ret = vhost_pci_epf_setup(epf);
		if (ret < 0) {
			goto err_free;
		}
	}

	ret = pcie_epc_bar_access_enable(epf->epc, epf->funcno, VIRTIO_PCI_BAR, true);
	if (ret != 0) {
		pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_BAR]);
		goto err_free;
	}

	k_work_schedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);

	return 0;

err_free:
	/* The framework removes the capabilities of a function that fails to bind */
	pcie_epf_free_space(epf, VIRTIO_PCI_BAR, data->bar_mem);
	data->bar_mem = NULL;
	return ret;
}

static void vhost_pci_epf_unbind(struct pcie_epf_device_s *epf)
{
	const struct device *dev = CONTAINER_OF(epf, struct vhost_pci_epf_data, epf)->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct k_work_sync sync;
	int ret;

	k_work_cancel_sync(&data->core_init_work, &sync);
	k_work_cancel_delayable_sync(&data->poll_work, &sync);
	ret = pcie_epc_bar_access_enable(epf->epc, epf->funcno, VIRTIO_PCI_BAR, false);
	if (ret != 0) {
		LOG_ERR("%s: cannot disable BAR servicing: %d", dev->name, ret);
	}

	k_mutex_lock(&data->lock, K_FOREVER);
	invalidate_queues(dev);
	for (uint16_t i = 0; i < config->num_queues; i++) {
		reclaim_queue(dev, i);
	}
	k_mutex_unlock(&data->lock);

	pcie_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[VIRTIO_PCI_BAR]);
	pcie_epf_free_space(epf, VIRTIO_PCI_BAR, data->bar_mem);
	data->bar_mem = NULL;
}

static int vhost_pci_epf_init(const struct device *dev)
{
	static const struct pcie_epf_ops_s epf_ops = {
		.bind = vhost_pci_epf_bind,
		.unbind = vhost_pci_epf_unbind,
	};
	static const struct pcie_epc_event_ops_s epf_event_ops = {
		.core_init = vhost_pci_epf_core_init,
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

	data->header.vendorid = VIRTIO_PCI_VENDOR_ID;
	data->header.deviceid = VIRTIO_PCI_MODERN_DEVICE_ID_BASE + config->device_id;
	data->header.revid = VIRTIO_PCI_MODERN_REVISION;
	data->header.subsys_vendor_id = VIRTIO_PCI_VENDOR_ID;
	data->header.subsys_id = config->device_id;
	data->header.baseclass_code = VIRTIO_PCI_BASECLASS_OTHERS;
	data->header.interrupt_pin = PCI_INTERRUPT_INTA;

	data->dev = dev;
	data->epf.header = &data->header;
	data->epf.ops = &epf_ops;
	data->epf.event_ops = &epf_event_ops;
	/* Until the backend sets them, only the bits the transport requires */
	data->offered_features = MANDATORY_FEATURES;

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
