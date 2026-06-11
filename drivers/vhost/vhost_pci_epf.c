/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * VIRTIO legacy PCI backend (VHost) over the PCI Endpoint Function
 * framework.
 *
 * The driver implements one PCI function that a host (root complex)
 * connected to the endpoint port enumerates as a transitional (legacy)
 * virtio-pci device. The legacy register interface (section 4.1.4.8 of
 * the VIRTIO specification) is used because it lives entirely in BAR0:
 * the modern interface requires vendor-specific capabilities in the
 * configuration space, which endpoint controller hardware generally
 * cannot provide. The Linux pci-epf-vnet proposal takes the same
 * approach.
 *
 * BAR0 is backed by local memory and the host accesses it directly
 * over PCIe, so register accesses do not generate events on the
 * endpoint. A polling work item compares the register block against
 * shadow copies to detect driver writes. Writes to windowed registers
 * (QUEUE_PFN, QUEUE_MSIX_VECTOR) are attributed to the queue currently
 * addressed by QUEUE_SEL; this is reliable because PCIe posted writes
 * arrive in order, so by the time a change of a windowed register is
 * observed, QUEUE_SEL already holds the matching value. QUEUE_NOTIFY
 * is rewritten to a sentinel after each consumed notification so that
 * repeated kicks of the same queue remain observable.
 *
 * Virtqueue rings and buffers reside in host memory and are reached
 * through the endpoint controller's outbound windows
 * (pci_epc_mem_alloc_addr() + pci_epc_map_addr()). Mapping bookkeeping
 * is statically allocated from a per-instance memory slab, like in the
 * Xen MMIO backend.
 *
 * Limitations:
 *  - MSI-X is required for host interrupt delivery: the legacy ISR
 *    register is read-to-clear, which cannot be emulated in a
 *    memory-backed BAR, so the INTx flow of host drivers does not
 *    work.
 *  - The legacy interface uses guest-native endianness; both ends are
 *    assumed little-endian.
 */

#include <inttypes.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pcie/epf/pci_epc.h>
#include <zephyr/drivers/pcie/epf/pci_epf.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/drivers/vhost.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost_pci_epf, CONFIG_VHOST_LOG_LEVEL);

#define DT_DRV_COMPAT zephyr_vhost_pci_epf

#define CHAIN_BUFS_MAX CONFIG_VHOST_CHAIN_BUFS_MAX

/* Granularity of outbound window mappings of host memory */
#define MAP_PAGE_SIZE 4096

/*
 * Legacy virtio-pci register block (VIRTIO spec 4.1.4.8, layout with
 * MSI-X enabled). The device-specific configuration space follows
 * immediately after.
 */
struct virtio_pci_legacy_regs {
	uint32_t host_features;      /* 0x00 RO: device features */
	uint32_t guest_features;     /* 0x04 RW: driver features */
	uint32_t queue_pfn;          /* 0x08 RW: ring PFN, by QUEUE_SEL */
	uint16_t queue_size;         /* 0x0c RO: ring size, by QUEUE_SEL */
	uint16_t queue_sel;          /* 0x0e RW: queue selector */
	uint16_t queue_notify;       /* 0x10 RW: queue doorbell */
	uint8_t status;              /* 0x12 RW: device status */
	uint8_t isr;                 /* 0x13 R-to-clear: interrupt status */
	uint16_t config_msix_vector; /* 0x14 RW: config change vector */
	uint16_t queue_msix_vector;  /* 0x16 RW: queue vector, by QUEUE_SEL */
} __packed;

BUILD_ASSERT(sizeof(struct virtio_pci_legacy_regs) == 0x18,
	     "legacy virtio-pci header must be 0x18 bytes with MSI-X");

#define LEGACY_QUEUE_PFN_SHIFT 12
#define LEGACY_VRING_ALIGN     4096
#define LEGACY_NOTIFY_IDLE     UINT16_MAX
#define LEGACY_MSIX_NO_VECTOR  UINT16_MAX

enum virtq_parts {
	VIRTQ_DESC = 0,
	VIRTQ_AVAIL,
	VIRTQ_USED,
	NUM_OF_VIRTQ_PARTS,
};

/* Legacy contiguous vring layout: desc, avail, page pad, used. */
static size_t legacy_used_offset(size_t qsz)
{
	return ROUND_UP(16 * qsz + 6 + 2 * qsz, LEGACY_VRING_ALIGN);
}

static size_t legacy_vring_size(size_t qsz)
{
	return legacy_used_offset(qsz) + ROUND_UP(6 + 8 * qsz, LEGACY_VRING_ALIGN);
}

/** Outbound window mapping of a contiguous host memory range. */
struct buf_mapping {
	void *win_virt;     /**< Window virtual address of the mapping */
	uintptr_t win_phys;  /**< Window physical address of the mapping */
	size_t win_size;    /**< Mapped size */
	size_t offset;      /**< Offset of the buffer in the mapping */
};

/** Mappings of one descriptor chain, slab-allocated. */
struct chain_mapping {
	uint16_t nr_bufs;
	struct buf_mapping bufs[CHAIN_BUFS_MAX];
};

struct virtq_callback {
	void (*cb)(const struct device *dev, uint16_t queue_id, void *user_data);
	void *data;
};

struct virtq_context {
	/** Chain mappings indexed by descriptor head */
	struct chain_mapping **chains;
	struct virtq_callback queue_notify_cb;
	struct buf_mapping ring; /**< Mapping of the virtqueue rings */
	atomic_t ready;
	atomic_t ready_notified;
	uint16_t msix_vector;
	struct k_spinlock lock;
};

struct vhost_pci_epf_config {
	k_thread_stack_t *workq_stack;
	size_t workq_stack_size;
	int workq_priority;

	struct k_mem_slab *chain_slab;
	struct pci_epf_header *header;
	const char *epc_name;

	uint16_t num_queues;
	uint16_t queue_size;
	const uint8_t *config_data;
	size_t config_data_len;
	uint32_t device_features;
};

struct vhost_pci_epf_data {
	const struct device *dev;
	struct pci_epf_device epf;

	struct k_work_q workq;
	struct k_work_delayable poll_work;

	volatile struct virtio_pci_legacy_regs *regs;
	int reg_bar;
	int msix_bar;

	/* Last observed values of the polled registers */
	struct {
		uint8_t status;
		uint32_t queue_pfn;
		uint16_t queue_msix_vector;
		uint16_t config_msix_vector;
	} shadow;

	uint16_t config_msix_vector;
	struct k_spinlock isr_lock;
	struct virtq_callback queue_ready_cb;
	struct virtq_context *vq_ctx;
};

/*
 * Outbound window mappings of host memory
 */

static int map_buf(const struct device *dev, uint64_t pci_addr, size_t len,
		   struct buf_mapping *buf)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct pci_epc *epc = data->epf.epc;
	const uint64_t pci_base = ROUND_DOWN(pci_addr, MAP_PAGE_SIZE);
	const size_t offset = pci_addr - pci_base;
	const size_t map_size = ROUND_UP(offset + len, MAP_PAGE_SIZE);
	int ret;

	buf->win_virt = pci_epc_mem_alloc_addr(epc, map_size, &buf->win_phys);
	if (buf->win_virt == NULL) {
		LOG_ERR("no window space for %zu bytes at 0x%" PRIx64, map_size, pci_addr);
		return -ENOMEM;
	}

	ret = pci_epc_map_addr(epc, data->epf.funcno, buf->win_phys, pci_base, map_size);
	if (ret < 0) {
		LOG_ERR("mapping 0x%" PRIx64 " (%zu bytes) failed: %d", pci_base, map_size, ret);
		pci_epc_mem_free_addr(epc, buf->win_virt, map_size);
		buf->win_virt = NULL;
		return ret;
	}

	buf->win_size = map_size;
	buf->offset = offset;

	return 0;
}

static void unmap_buf(const struct device *dev, struct buf_mapping *buf)
{
	struct vhost_pci_epf_data *data = dev->data;
	struct pci_epc *epc = data->epf.epc;

	if (buf->win_virt == NULL) {
		return;
	}

	pci_epc_unmap_addr(epc, data->epf.funcno, buf->win_phys);
	pci_epc_mem_free_addr(epc, buf->win_virt, buf->win_size);
	buf->win_virt = NULL;
	buf->win_size = 0;
}

static void free_chain(const struct device *dev, struct chain_mapping *chain)
{
	const struct vhost_pci_epf_config *config = dev->config;

	for (size_t i = 0; i < chain->nr_bufs; i++) {
		unmap_buf(dev, &chain->bufs[i]);
	}

	k_mem_slab_free(config->chain_slab, chain);
}

static int map_chain(const struct device *dev, const struct vhost_buf *bufs, size_t bufs_len,
		     struct chain_mapping **chain_out)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct chain_mapping *chain;
	int ret;

	if (bufs_len > CHAIN_BUFS_MAX) {
		LOG_ERR("chain has %zu buffers > VHOST_CHAIN_BUFS_MAX (%d)", bufs_len,
			CHAIN_BUFS_MAX);
		return -E2BIG;
	}

	ret = k_mem_slab_alloc(config->chain_slab, (void **)&chain, K_NO_WAIT);
	if (ret < 0) {
		LOG_WRN("chain slab exhausted, too many in-flight chains");
		return -ENOMEM;
	}

	memset(chain, 0, sizeof(*chain));

	for (size_t i = 0; i < bufs_len; i++) {
		chain->nr_bufs = i + 1;

		ret = map_buf(dev, bufs[i].gpa, bufs[i].len, &chain->bufs[i]);
		if (ret < 0) {
			free_chain(dev, chain);
			return ret;
		}
	}

	*chain_out = chain;

	return 0;
}

static int fill_iovecs(const struct chain_mapping *chain, const struct vhost_buf *bufs,
		       size_t bufs_len, struct vhost_iovec *r_iovecs, size_t r_iovecs_max,
		       struct vhost_iovec *w_iovecs, size_t w_iovecs_max, size_t *r_count,
		       size_t *w_count)
{
	size_t nr_read = 0, nr_write = 0;

	for (size_t i = 0; i < bufs_len; i++) {
		struct vhost_iovec *iovec;

		if (bufs[i].is_write) {
			if (nr_write >= w_iovecs_max) {
				LOG_ERR("no more write iovecs: %zu", w_iovecs_max);
				return -E2BIG;
			}
			iovec = &w_iovecs[nr_write++];
		} else {
			if (nr_read >= r_iovecs_max) {
				LOG_ERR("no more read iovecs: %zu", r_iovecs_max);
				return -E2BIG;
			}
			iovec = &r_iovecs[nr_read++];
		}

		iovec->iov_base = (uint8_t *)chain->bufs[i].win_virt + chain->bufs[i].offset;
		iovec->iov_len = bufs[i].len;
	}

	*r_count = nr_read;
	*w_count = nr_write;

	return 0;
}

/** Detach and return the chain at @a slot, or NULL when the slot is empty. */
static struct chain_mapping *detach_chain(struct virtq_context *vq_ctx, size_t slot)
{
	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);
	struct chain_mapping *chain = vq_ctx->chains[slot];

	vq_ctx->chains[slot] = NULL;
	k_spin_unlock(&vq_ctx->lock, key);

	return chain;
}

static void publish_chain(struct virtq_context *vq_ctx, size_t slot, struct chain_mapping *chain)
{
	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);

	__ASSERT_NO_MSG(vq_ctx->chains[slot] == NULL);
	vq_ctx->chains[slot] = chain;
	k_spin_unlock(&vq_ctx->lock, key);
}

/*
 * Queue and device lifecycle (poll work context)
 */

static void reset_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	atomic_set(&vq_ctx->ready, 0);
	atomic_set(&vq_ctx->ready_notified, 0);

	for (size_t slot = 0; slot < config->queue_size; slot++) {
		struct chain_mapping *chain = detach_chain(vq_ctx, slot);

		if (chain != NULL) {
			free_chain(dev, chain);
		}
	}

	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);

	vq_ctx->queue_notify_cb.cb = NULL;
	vq_ctx->queue_notify_cb.data = NULL;
	k_spin_unlock(&vq_ctx->lock, key);

	unmap_buf(dev, &vq_ctx->ring);
}

static int setup_queue(const struct device *dev, uint16_t queue_id, uint32_t pfn)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	const uint64_t pci_addr = (uint64_t)pfn << LEGACY_QUEUE_PFN_SHIFT;
	int ret;

	if (vq_ctx->ring.win_virt != NULL) {
		reset_queue(dev, queue_id);
	}

	ret = map_buf(dev, pci_addr, legacy_vring_size(config->queue_size), &vq_ctx->ring);
	if (ret < 0) {
		LOG_ERR("queue%u: mapping rings at 0x%" PRIx64 " failed: %d", queue_id, pci_addr,
			ret);
		return ret;
	}

	atomic_set(&vq_ctx->ready_notified, 0);
	atomic_set(&vq_ctx->ready, 1);

	LOG_DBG("queue%u: rings at 0x%" PRIx64 " mapped to %p", queue_id, pci_addr,
		vq_ctx->ring.win_virt);

	return 0;
}

static void reset_device(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	volatile struct virtio_pci_legacy_regs *regs = data->regs;

	for (size_t i = 0; i < config->num_queues; i++) {
		reset_queue(dev, i);
		data->vq_ctx[i].msix_vector = LEGACY_MSIX_NO_VECTOR;
	}

	data->config_msix_vector = LEGACY_MSIX_NO_VECTOR;

	/*
	 * Reinitialize the device-owned register values. A register is
	 * only rewritten when it still holds the last value this driver
	 * observed: a differing value is a host write that arrived after
	 * the reset (PCIe writes are ordered) and belongs to the next
	 * driver initialization, so it must survive and is picked up by
	 * the regular change detection.
	 */
	if (regs->queue_pfn == data->shadow.queue_pfn) {
		regs->queue_pfn = 0;
	}
	data->shadow.queue_pfn = 0;

	if (regs->queue_msix_vector == data->shadow.queue_msix_vector) {
		regs->queue_msix_vector = LEGACY_MSIX_NO_VECTOR;
	}
	data->shadow.queue_msix_vector = LEGACY_MSIX_NO_VECTOR;

	if (regs->config_msix_vector == data->shadow.config_msix_vector) {
		regs->config_msix_vector = LEGACY_MSIX_NO_VECTOR;
	}
	data->shadow.config_msix_vector = LEGACY_MSIX_NO_VECTOR;

	regs->queue_notify = LEGACY_NOTIFY_IDLE;
	regs->isr = 0;
}

/*
 * Register polling
 */

static void poll_workhandler(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct vhost_pci_epf_data *data =
		CONTAINER_OF(delayable, struct vhost_pci_epf_data, poll_work);
	const struct device *dev = data->dev;
	const struct vhost_pci_epf_config *config = dev->config;
	volatile struct virtio_pci_legacy_regs *regs = data->regs;

	if (regs == NULL) {
		return;
	}

	/* Device status */
	const uint8_t status = regs->status;

	if (status != data->shadow.status) {
		LOG_DBG("status 0x%02x -> 0x%02x", data->shadow.status, status);

		if (status == 0) {
			reset_device(dev);
		}
		data->shadow.status = status;
	}

	/* Writes to windowed registers, attributed to the current QUEUE_SEL */
	const uint16_t queue_sel = regs->queue_sel;
	const uint32_t pfn = regs->queue_pfn;

	if (pfn != data->shadow.queue_pfn) {
		data->shadow.queue_pfn = pfn;

		if (queue_sel >= config->num_queues) {
			LOG_WRN("QUEUE_PFN write with invalid QUEUE_SEL %u", queue_sel);
		} else if (pfn == 0) {
			reset_queue(dev, queue_sel);
		} else if (setup_queue(dev, queue_sel, pfn) < 0) {
			LOG_ERR("queue%u setup failed", queue_sel);
		}
	}

	const uint16_t queue_vector = regs->queue_msix_vector;

	if (queue_vector != data->shadow.queue_msix_vector) {
		data->shadow.queue_msix_vector = queue_vector;

		if (queue_sel < config->num_queues) {
			data->vq_ctx[queue_sel].msix_vector = queue_vector;
		}
	}

	const uint16_t config_vector = regs->config_msix_vector;

	if (config_vector != data->shadow.config_msix_vector) {
		data->shadow.config_msix_vector = config_vector;
		data->config_msix_vector = config_vector;
	}

	/* Queue-ready notification towards the backend application */
	if (data->queue_ready_cb.cb != NULL) {
		for (size_t i = 0; i < config->num_queues; i++) {
			struct virtq_context *vq_ctx = &data->vq_ctx[i];

			if (atomic_get(&vq_ctx->ready) &&
			    atomic_cas(&vq_ctx->ready_notified, 0, 1)) {
				data->queue_ready_cb.cb(dev, i, data->queue_ready_cb.data);
			}
		}
	}

	/* Queue doorbell: consumed by rewriting the sentinel value */
	const uint16_t notify = regs->queue_notify;

	if (notify != LEGACY_NOTIFY_IDLE) {
		regs->queue_notify = LEGACY_NOTIFY_IDLE;
		barrier_dmem_fence_full();

		if (notify < config->num_queues) {
			struct virtq_context *vq_ctx = &data->vq_ctx[notify];
			k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);
			struct virtq_callback cb = vq_ctx->queue_notify_cb;

			k_spin_unlock(&vq_ctx->lock, key);

			if (cb.cb != NULL) {
				cb.cb(dev, notify, cb.data);
			}
		} else {
			LOG_WRN("notify for invalid queue %u", notify);
		}
	}

	k_work_schedule_for_queue(&data->workq, &data->poll_work,
				  K_USEC(CONFIG_VHOST_PCI_EPF_POLL_PERIOD_US));
}

/*
 * VHost API implementation
 */

static bool vhost_pci_epf_virtq_is_ready(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		return false;
	}

	return atomic_get(&data->vq_ctx[queue_id].ready) != 0;
}

static int vhost_pci_epf_get_virtq(const struct device *dev, uint16_t queue_id, void **parts,
				   size_t *queue_size)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	if (!vhost_pci_epf_virtq_is_ready(dev, queue_id)) {
		LOG_ERR("queue%u not ready", queue_id);
		return -ENODEV;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	uint8_t *base = (uint8_t *)vq_ctx->ring.win_virt + vq_ctx->ring.offset;

	parts[VIRTQ_DESC] = base;
	parts[VIRTQ_AVAIL] = base + 16 * config->queue_size;
	parts[VIRTQ_USED] = base + legacy_used_offset(config->queue_size);
	*queue_size = config->queue_size;

	LOG_DBG("queue%u rings desc=%p, avail=%p, used=%p, size=%u", queue_id, parts[VIRTQ_DESC],
		parts[VIRTQ_AVAIL], parts[VIRTQ_USED], config->queue_size);

	return 0;
}

static int vhost_pci_epf_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	struct vhost_pci_epf_data *data = dev->data;

	if (data->regs == NULL) {
		return -ENODEV;
	}

	*drv_feats = data->regs->guest_features;

	return 0;
}

static int vhost_pci_epf_raise_msix(const struct device *dev, uint16_t vector)
{
	struct vhost_pci_epf_data *data = dev->data;

	if (vector == LEGACY_MSIX_NO_VECTOR) {
		return 0;
	}

	/* The EPC API counts MSI-X interrupts from 1 */
	return pci_epc_raise_irq(data->epf.epc, data->epf.funcno, PCI_EPC_IRQ_MSIX, vector + 1);
}

static int vhost_pci_epf_notify_virtq(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	if (data->regs == NULL) {
		return -ENODEV;
	}

	k_spinlock_key_t key = k_spin_lock(&data->isr_lock);

	data->regs->isr |= VIRTIO_QUEUE_INTERRUPT;
	k_spin_unlock(&data->isr_lock, key);

	return vhost_pci_epf_raise_msix(dev, data->vq_ctx[queue_id].msix_vector);
}

static int vhost_pci_epf_set_device_status(const struct device *dev, uint32_t status)
{
	struct vhost_pci_epf_data *data = dev->data;

	if (data->regs == NULL) {
		return -ENODEV;
	}

	k_spinlock_key_t key = k_spin_lock(&data->isr_lock);

	data->regs->status |= status;
	data->shadow.status |= status;
	data->regs->isr |= VIRTIO_DEVICE_CONFIGURATION_INTERRUPT;
	k_spin_unlock(&data->isr_lock, key);

	return vhost_pci_epf_raise_msix(dev, data->config_msix_vector);
}

static int vhost_pci_epf_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	if (head >= config->queue_size) {
		LOG_ERR("queue%u: invalid head: %u >= queue_size %u", queue_id, head,
			config->queue_size);
		return -EINVAL;
	}

	struct chain_mapping *chain = detach_chain(&data->vq_ctx[queue_id], head);

	if (chain == NULL) {
		LOG_ERR("queue%u: head %u not in use", queue_id, head);
		return -EINVAL;
	}

	free_chain(dev, chain);

	return 0;
}

static int vhost_pci_epf_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
				       const struct vhost_buf *bufs, size_t bufs_count,
				       struct vhost_iovec *r_iovecs, size_t r_iovecs_max,
				       struct vhost_iovec *w_iovecs, size_t w_iovecs_max,
				       size_t *read_count, size_t *write_count)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct chain_mapping *chain;
	int ret;

	*read_count = 0;
	*write_count = 0;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	if (head >= config->queue_size) {
		LOG_ERR("queue%u: invalid head: %u >= queue_size %u", queue_id, head,
			config->queue_size);
		return -EINVAL;
	}

	if (bufs_count == 0) {
		return 0;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	struct chain_mapping *stale = detach_chain(vq_ctx, head);

	if (stale != NULL) {
		LOG_WRN("queue%u: head %u was not released", queue_id, head);
		free_chain(dev, stale);
	}

	ret = map_chain(dev, bufs, bufs_count, &chain);
	if (ret < 0) {
		return ret;
	}

	ret = fill_iovecs(chain, bufs, bufs_count, r_iovecs, r_iovecs_max, w_iovecs, w_iovecs_max,
			  read_count, write_count);
	if (ret < 0) {
		free_chain(dev, chain);
		*read_count = 0;
		*write_count = 0;
		return ret;
	}

	publish_chain(vq_ctx, head, chain);

	return 0;
}

static int vhost_pci_epf_register_virtq_ready_cb(const struct device *dev,
						 void (*callback)(const struct device *dev,
								  uint16_t queue_id,
								  void *user_data),
						 void *user_data)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	data->queue_ready_cb.cb = callback;
	data->queue_ready_cb.data = user_data;

	for (size_t i = 0; i < config->num_queues; i++) {
		atomic_set(&data->vq_ctx[i].ready_notified, 0);
	}

	if (data->regs != NULL) {
		k_work_reschedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);
	}

	return 0;
}

static int vhost_pci_epf_register_virtq_notify_cb(const struct device *dev, uint16_t queue_id,
						  void (*callback)(const struct device *dev,
								   uint16_t queue_id,
								   void *user_data),
						  void *user_data)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);

	vq_ctx->queue_notify_cb.cb = callback;
	vq_ctx->queue_notify_cb.data = user_data;
	k_spin_unlock(&vq_ctx->lock, key);

	return 0;
}

static const struct vhost_controller_api vhost_pci_epf_api = {
	.virtq_is_ready = vhost_pci_epf_virtq_is_ready,
	.get_virtq = vhost_pci_epf_get_virtq,
	.get_driver_features = vhost_pci_epf_get_driver_features,
	.set_device_status = vhost_pci_epf_set_device_status,
	.notify_virtq = vhost_pci_epf_notify_virtq,
	.prepare_iovec = vhost_pci_epf_prepare_iovec,
	.release_iovec = vhost_pci_epf_release_iovec,
	.register_virtq_ready_cb = vhost_pci_epf_register_virtq_ready_cb,
	.register_virtq_notify_cb = vhost_pci_epf_register_virtq_notify_cb,
};

/*
 * EPF function programming
 */

static int vhost_pci_epf_program(const struct device *dev)
{
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct pci_epf_device *epf = &data->epf;
	int ret;

	ret = pci_epc_write_header(epf->epc, epf->funcno, epf->header);
	if (ret < 0) {
		LOG_ERR("writing config header failed: %d", ret);
		return ret;
	}

	ret = pci_epc_set_bar(epf->epc, epf->funcno, &epf->bar[data->reg_bar]);
	if (ret < 0) {
		LOG_ERR("setting register BAR%d failed: %d", data->reg_bar, ret);
		return ret;
	}

	if (data->msix_bar >= 0) {
		ret = pci_epc_set_bar(epf->epc, epf->funcno, &epf->bar[data->msix_bar]);
		if (ret < 0) {
			LOG_ERR("setting MSI-X BAR%d failed: %d", data->msix_bar, ret);
			return ret;
		}

		ret = pci_epc_set_msix(epf->epc, epf->funcno, config->num_queues + 1,
				       data->msix_bar, 0);
		if (ret < 0 && ret != -ENOSYS) {
			LOG_ERR("configuring MSI-X failed: %d", ret);
			return ret;
		}
	}

	return 0;
}

static int vhost_pci_epf_bind(struct pci_epf_device *epf)
{
	const struct device *dev = epf->priv;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	const struct pci_epc_features *features;
	size_t align = 0;
	int ret;

	features = pci_epc_get_features(epf->epc, epf->funcno);
	if (features != NULL) {
		align = features->align;

		data->reg_bar = pci_epc_get_first_free_bar(features);
		if (data->reg_bar < 0) {
			LOG_ERR("no BAR available for the registers");
			return -ENOENT;
		}
	} else {
		data->reg_bar = 0;
	}

	const size_t reg_size = sizeof(struct virtio_pci_legacy_regs) + config->config_data_len;
	volatile struct virtio_pci_legacy_regs *regs =
		pci_epf_alloc_space(epf, data->reg_bar, reg_size, align);

	if (regs == NULL) {
		return -ENOMEM;
	}

	/* The legacy interface requires its registers in an I/O space BAR */
	epf->bar[data->reg_bar].flags |= PCI_EPF_BAR_SPACE_IO;

	data->msix_bar = -1;
	if (features == NULL || features->msix_capable) {
		const uint16_t nvec = config->num_queues + 1;
		/* MSI-X table followed by the pending bit array */
		const size_t msix_size =
			ROUND_UP(nvec * 16, 8) + (ROUND_UP(nvec, 64) / 8);
		int barno = (features != NULL)
				    ? pci_epc_get_next_free_bar(features, data->reg_bar + 1)
				    : data->reg_bar + 1;

		if (barno >= 0 && pci_epf_alloc_space(epf, barno, msix_size, align) != NULL) {
			data->msix_bar = barno;
		}
	}

	if (data->msix_bar < 0) {
		LOG_WRN("MSI-X unavailable; legacy INTx cannot be emulated, host "
			"interrupt delivery will not work");
	}

	regs->host_features = config->device_features;
	regs->queue_size = config->queue_size;
	regs->queue_notify = LEGACY_NOTIFY_IDLE;
	regs->config_msix_vector = LEGACY_MSIX_NO_VECTOR;
	regs->queue_msix_vector = LEGACY_MSIX_NO_VECTOR;
	memcpy((void *)(regs + 1), config->config_data, config->config_data_len);

	data->shadow.status = 0;
	data->shadow.queue_pfn = 0;
	data->shadow.queue_msix_vector = LEGACY_MSIX_NO_VECTOR;
	data->shadow.config_msix_vector = LEGACY_MSIX_NO_VECTOR;
	data->regs = regs;

	ret = vhost_pci_epf_program(dev);
	if (ret < 0) {
		goto fail;
	}

	ret = pci_epc_start(epf->epc);
	if (ret < 0 && ret != -ENOSYS) {
		LOG_ERR("starting the EPC failed: %d", ret);
		goto fail;
	}

	k_work_schedule_for_queue(&data->workq, &data->poll_work, K_NO_WAIT);

	LOG_INF("%s: virtio function ready on %s func%u (device-id %u)", dev->name,
		epf->epc_name, epf->funcno, epf->header->subsys_id);

	return 0;

fail:
	data->regs = NULL;
	pci_epf_free_space(epf, data->reg_bar);
	if (data->msix_bar >= 0) {
		pci_epf_free_space(epf, data->msix_bar);
		data->msix_bar = -1;
	}

	return ret;
}

static void vhost_pci_epf_unbind(struct pci_epf_device *epf)
{
	const struct device *dev = epf->priv;
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;
	struct k_work_sync sync;

	k_work_cancel_delayable_sync(&data->poll_work, &sync);

	for (size_t i = 0; i < config->num_queues; i++) {
		reset_queue(dev, i);
	}

	data->regs = NULL;

	pci_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[data->reg_bar]);
	pci_epf_free_space(epf, data->reg_bar);

	if (data->msix_bar >= 0) {
		pci_epc_clear_bar(epf->epc, epf->funcno, &epf->bar[data->msix_bar]);
		pci_epf_free_space(epf, data->msix_bar);
		data->msix_bar = -1;
	}
}

static int vhost_pci_epf_core_init(struct pci_epf_device *epf)
{
	return vhost_pci_epf_program(epf->priv);
}

static const struct pci_epc_event_ops vhost_pci_epf_event_ops = {
	.core_init = vhost_pci_epf_core_init,
};

static const struct pci_epf_ops vhost_pci_epf_ops = {
	.bind = vhost_pci_epf_bind,
	.unbind = vhost_pci_epf_unbind,
};

static const char *const vhost_pci_epf_names[] = {"vhost", NULL};

static struct pci_epf_driver vhost_pci_epf_driver = {
	.ops = &vhost_pci_epf_ops,
	.names = vhost_pci_epf_names,
};

static int vhost_pci_epf_init(const struct device *dev)
{
	static atomic_t driver_registered;
	const struct k_work_queue_config qcfg = {.name = "vhost-epf-wq"};
	const struct vhost_pci_epf_config *config = dev->config;
	struct vhost_pci_epf_data *data = dev->data;

	data->dev = dev;
	data->msix_bar = -1;
	data->config_msix_vector = LEGACY_MSIX_NO_VECTOR;

	for (size_t i = 0; i < config->num_queues; i++) {
		data->vq_ctx[i].msix_vector = LEGACY_MSIX_NO_VECTOR;
	}

	k_work_init_delayable(&data->poll_work, poll_workhandler);
	k_work_queue_init(&data->workq);
	k_work_queue_start(&data->workq, config->workq_stack, config->workq_stack_size,
			   config->workq_priority, &qcfg);

	data->epf.name = "vhost";
	data->epf.epc_name = config->epc_name;
	data->epf.header = config->header;
	data->epf.msix_interrupts = config->num_queues + 1;
	data->epf.event_ops = &vhost_pci_epf_event_ops;
	data->epf.priv = (void *)dev;

	if (atomic_cas(&driver_registered, 0, 1)) {
		int ret = pci_epf_register_driver(&vhost_pci_epf_driver);

		if (ret < 0) {
			return ret;
		}
	}

	/*
	 * The EPC must already be registered: binding only happens at
	 * device or driver registration time. EPC drivers have to
	 * initialize before this driver (see
	 * VHOST_PCI_EPF_INIT_PRIORITY).
	 */
	return pci_epf_device_register(&data->epf);
}

#define Q_NUM(idx) DT_INST_PROP(idx, num_queues)
#define Q_SZ(idx)  DT_INST_PROP(idx, queue_size_max)

#define VQCTX_INIT(n, idx)                                                                         \
	{                                                                                          \
		.chains = vhost_pci_epf_chains_##idx[n],                                           \
	}

#define VHOST_PCI_EPF_EPC_NAME(idx)                                                                \
	COND_CODE_1(DT_INST_NODE_HAS_PROP(idx, epc),                                               \
		    (DEVICE_DT_NAME(DT_INST_PHANDLE(idx, epc))),                                   \
		    (DT_INST_PROP(idx, epc_name)))

#define VHOST_PCI_EPF_INST(idx)                                                                    \
	BUILD_ASSERT(POPCOUNT(Q_SZ(idx)) == 1, "queue-size-max must be a power of 2");             \
	static K_THREAD_STACK_DEFINE(vhost_pci_epf_stack_##idx,                                    \
				     DT_INST_PROP_OR(idx, stack_size, 4096));                      \
	static const uint8_t vhost_pci_epf_cfgdata_##idx[] = {                                     \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(idx, config_data),                               \
		(DT_INST_FOREACH_PROP_ELEM_SEP(idx, config_data, DT_PROP_BY_IDX, (,))), ())};      \
	K_MEM_SLAB_DEFINE_STATIC(                                                                  \
		vhost_pci_epf_chain_slab_##idx, ROUND_UP(sizeof(struct chain_mapping), 8),         \
		Q_NUM(idx) * CONFIG_VHOST_PCI_EPF_INFLIGHT_CHAINS_MAX, 8);                         \
	static struct chain_mapping *vhost_pci_epf_chains_##idx[Q_NUM(idx)][Q_SZ(idx)];            \
	static struct virtq_context vhost_pci_epf_vq_ctx_##idx[Q_NUM(idx)] = {                     \
		LISTIFY(Q_NUM(idx), VQCTX_INIT, (,), idx),                                         \
	};                                                                                         \
	static struct pci_epf_header vhost_pci_epf_header_##idx = {                                \
		.vendorid = DT_INST_PROP(idx, vendor_id),                                          \
		.deviceid = DT_INST_PROP(idx, pci_device_id),                                      \
		.revid = 0, /* transitional devices must use revision 0 */                         \
		.baseclass_code = 0xff,                                                            \
		.interrupt_pin = 1,                                                                \
		.subsys_vendor_id = DT_INST_PROP(idx, vendor_id),                                  \
		.subsys_id = DT_INST_PROP(idx, device_id),                                         \
	};                                                                                         \
	static const struct vhost_pci_epf_config vhost_pci_epf_config_##idx = {                    \
		.workq_stack = (k_thread_stack_t *)&vhost_pci_epf_stack_##idx,                     \
		.workq_stack_size = K_THREAD_STACK_SIZEOF(vhost_pci_epf_stack_##idx),              \
		.workq_priority = DT_INST_PROP_OR(idx, priority, 0),                               \
		.chain_slab = &vhost_pci_epf_chain_slab_##idx,                                     \
		.header = &vhost_pci_epf_header_##idx,                                             \
		.epc_name = VHOST_PCI_EPF_EPC_NAME(idx),                                           \
		.num_queues = Q_NUM(idx),                                                          \
		.queue_size = Q_SZ(idx),                                                           \
		.config_data = vhost_pci_epf_cfgdata_##idx,                                        \
		.config_data_len = DT_INST_PROP_LEN_OR(idx, config_data, 0),                       \
		.device_features = DT_INST_PROP(idx, device_features),                             \
	};                                                                                         \
	static struct vhost_pci_epf_data vhost_pci_epf_data_##idx = {                              \
		.vq_ctx = vhost_pci_epf_vq_ctx_##idx,                                              \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, vhost_pci_epf_init, NULL, &vhost_pci_epf_data_##idx,            \
			      &vhost_pci_epf_config_##idx, POST_KERNEL,                            \
			      CONFIG_VHOST_PCI_EPF_INIT_PRIORITY, &vhost_pci_epf_api);

DT_INST_FOREACH_STATUS_OKAY(VHOST_PCI_EPF_INST)
