/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * VIRTIO-MMIO backend (VHost) driver for the Xen hypervisor.
 *
 * The driver registers an IOREQ server for the VIRTIO-MMIO register
 * window of a frontend domain and emulates the register interface
 * defined in section 4.2.2 of the VIRTIO specification. Guest buffers
 * are accessed through the grant table: every guest physical address
 * carries the Xen grant marker (bit 63) and is mapped page by page
 * into local pages obtained from gnttab_get_pages().
 *
 * Mapping bookkeeping is fully static: one `struct chain_mapping` per
 * in-flight descriptor chain, allocated from a per-instance memory
 * slab. The local pages backing the mappings come from the Xen
 * extended regions allocator when CONFIG_XEN_REGIONS is enabled.
 */

#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdlib.h>

#include <xenstore_cli.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/util_macro.h>
#include <zephyr/xen/generic.h>
#include <zephyr/xen/events.h>
#include <zephyr/xen/hvm.h>
#include <zephyr/xen/gnttab.h>
#include <zephyr/xen/memory.h>
#include <zephyr/xen/dmop.h>
#include <zephyr/drivers/virtio.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/drivers/vhost.h>
#include <xen/public/xen.h>
#include <xen/public/hvm/ioreq.h>
#include <xen/public/hvm/dm_op.h>
#include <xen/public/hvm/hvm_op.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost_xen_mmio, CONFIG_VHOST_LOG_LEVEL);

#define DT_DRV_COMPAT xen_vhost_mmio

/* Guest physical addresses with this bit set encode grant references. */
#define XEN_GRANT_ADDR_OFF BIT64(63)

#define VIRTIO_MMIO_MAGIC             0x74726976
#define VIRTIO_MMIO_SUPPORTED_VERSION 2

/* The maximum retry period is 12.8 seconds */
#define RETRY_DELAY_BASE_MS   50
#define RETRY_BACKOFF_EXP_MAX 8

#define XS_VALUE_LEN 64

#define CHAIN_BUFS_MAX CONFIG_VHOST_CHAIN_BUFS_MAX
#define BUF_PAGES_MAX  CONFIG_VHOST_XEN_MMIO_BUF_PAGES_MAX
#define VCPUS_MAX      CONFIG_VHOST_XEN_MMIO_VCPUS_MAX

/* Slot in the chain table that holds the virtqueue ring mappings. */
#define META_CHAIN_SLOT(cfg) ((cfg)->queue_size_max)

enum virtq_parts {
	VIRTQ_DESC = 0,
	VIRTQ_AVAIL,
	VIRTQ_USED,
	NUM_OF_VIRTQ_PARTS,
};

BUILD_ASSERT(NUM_OF_VIRTQ_PARTS <= CHAIN_BUFS_MAX,
	     "VHOST_CHAIN_BUFS_MAX must cover the three virtqueue parts");

/** Grant mapping of a single guest buffer (one descriptor). */
struct buf_mapping {
	uint8_t *va;        /**< Local pages from gnttab_get_pages() */
	uint16_t offset;    /**< Offset of the buffer in the first page */
	uint16_t nr_pages;  /**< Number of pages backing @a va */
	uint16_t nr_mapped; /**< Number of successfully mapped pages */
	struct gnttab_unmap_grant_ref unmap[BUF_PAGES_MAX];
};

/** Grant mappings of one descriptor chain, slab-allocated. */
struct chain_mapping {
	uint16_t nr_bufs;
	struct buf_mapping bufs[CHAIN_BUFS_MAX];
};

struct virtq_callback {
	void (*cb)(const struct device *dev, uint16_t queue_id, void *user_data);
	void *data;
};

struct virtq_context {
	/**
	 * Chain mappings indexed by descriptor head. The extra trailing
	 * slot (META_CHAIN_SLOT) holds the mappings of the virtqueue
	 * rings (desc/avail/used) themselves.
	 */
	struct chain_mapping **chains;
	struct virtq_callback queue_notify_cb;
	atomic_t queue_size;
	atomic_t queue_ready_notified;
	uint64_t virtq_parts_gpa[NUM_OF_VIRTQ_PARTS];
	struct k_spinlock lock;
};

struct vhost_xen_mmio_config {
	k_thread_stack_t *workq_stack;
	size_t workq_stack_size;
	int workq_priority;

	struct k_mem_slab *chain_slab;

	uint16_t num_queues;
	uint16_t queue_size_max;
	uint8_t device_id;
	uint32_t vendor_id;
	uintptr_t base;
	size_t reg_size;
	const uint8_t *config_data;
	size_t config_data_len;

	uint64_t device_features;
};

struct vhost_xen_mmio_data {
	struct k_work_delayable init_work;
	struct k_work_delayable isr_work;
	struct k_work_delayable ready_work;
	struct k_work_q workq;
	const struct device *dev;
	atomic_t initialized;
	atomic_t retry;

	struct xs_watcher watcher;
	evtchn_port_t ioserv_ports[VCPUS_MAX];
	uint32_t nr_ioserv_ports;
	struct shared_iopage *shared_iopage;
	uint32_t vcpus;

	/* Frontend parameters discovered via XenStore */
	struct {
		ioservid_t servid;
		bool servid_valid;
		domid_t domid;
		uint32_t deviceid;
		uint32_t irq;
		uintptr_t base;
	} fe;

	/* Backend-side VIRTIO-MMIO register state */
	struct {
		uint64_t driver_features;
		uint8_t device_features_sel;
		uint8_t driver_features_sel;
		atomic_t irq_status;
		atomic_t status;
		atomic_t queue_sel;
	} be;

	atomic_t notify_queue_id; /**< Queue ID handed over to the workqueue */
	struct virtq_callback queue_ready_cb;
	struct virtq_context *vq_ctx;
};

/*
 * Grant mapping management
 *
 * A descriptor chain is mapped into a slab-allocated `struct
 * chain_mapping`. The chain is built and mapped without holding any
 * lock, and is then atomically published to the per-queue chain table
 * under the queue spinlock. Releasing detaches the chain from the
 * table under the lock and performs the unmap hypercalls outside of
 * it, so no flag/spin dance is needed to serialize against readers.
 */

static void unmap_buf(struct buf_mapping *buf)
{
	if (buf->nr_mapped > 0) {
		int ret = gnttab_unmap_refs(buf->unmap, buf->nr_mapped);

		if (ret < 0) {
			LOG_ERR("gnttab_unmap_refs failed: %d", ret);
		}
		buf->nr_mapped = 0;
	}

	if (buf->va != NULL) {
		int ret = gnttab_put_pages(buf->va, buf->nr_pages);

		if (ret < 0) {
			LOG_ERR("gnttab_put_pages failed: %d", ret);
		}
		buf->va = NULL;
		buf->nr_pages = 0;
	}
}

static void free_chain(struct k_mem_slab *slab, struct chain_mapping *chain)
{
	for (size_t i = 0; i < chain->nr_bufs; i++) {
		unmap_buf(&chain->bufs[i]);
	}

	k_mem_slab_free(slab, chain);
}

static int map_buf(domid_t domid, const struct vhost_buf *vbuf, struct buf_mapping *buf)
{
	const size_t offset = vbuf->gpa & (XEN_PAGE_SIZE - 1);
	const size_t nr_pages = DIV_ROUND_UP(offset + vbuf->len, XEN_PAGE_SIZE);
	const grant_ref_t first_ref = (vbuf->gpa & ~XEN_GRANT_ADDR_OFF) >> XEN_PAGE_SHIFT;
	struct gnttab_map_grant_ref ops[BUF_PAGES_MAX];
	size_t mapped = 0;
	int ret;

	if (!(vbuf->gpa & XEN_GRANT_ADDR_OFF)) {
		LOG_ERR("addr missing grant marker: 0x%" PRIx64, vbuf->gpa);
		return -EINVAL;
	}

	if (nr_pages > BUF_PAGES_MAX) {
		LOG_ERR("buffer spans %zu pages > VHOST_XEN_MMIO_BUF_PAGES_MAX (%d)", nr_pages,
			BUF_PAGES_MAX);
		return -E2BIG;
	}

	buf->va = gnttab_get_pages(nr_pages);
	if (buf->va == NULL) {
		LOG_ERR("failed to allocate %zu pages for grant mapping", nr_pages);
		return -ENOMEM;
	}
	buf->offset = offset;
	buf->nr_pages = nr_pages;
	buf->nr_mapped = 0;

	for (size_t i = 0; i < nr_pages; i++) {
		ops[i] = (struct gnttab_map_grant_ref){
			.host_addr = (uintptr_t)buf->va + (i * XEN_PAGE_SIZE),
			.flags = GNTMAP_host_map | (vbuf->is_write ? 0 : GNTMAP_readonly),
			.ref = first_ref + i,
			.dom = domid,
		};
	}

	ret = gnttab_map_refs(ops, nr_pages);
	if (ret < 0) {
		LOG_ERR("gnttab_map_refs failed: %d", ret);
		goto fail;
	}

	for (size_t i = 0; i < nr_pages; i++) {
		if (ops[i].status != GNTST_okay) {
			LOG_ERR("mapping page %zu of gpa 0x%" PRIx64 " failed: status=%d", i,
				vbuf->gpa, ops[i].status);
			ret = -EIO;
			goto fail;
		}

		buf->unmap[i] = (struct gnttab_unmap_grant_ref){
			.host_addr = ops[i].host_addr,
			.dev_bus_addr = ops[i].dev_bus_addr,
			.handle = ops[i].handle,
		};
		mapped++;
	}

	buf->nr_mapped = mapped;

	return 0;

fail:
	buf->nr_mapped = mapped;
	unmap_buf(buf);

	return ret;
}

static int map_chain(const struct device *dev, const struct vhost_buf *bufs, size_t bufs_len,
		     struct chain_mapping **chain_out)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
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

		ret = map_buf(data->fe.domid, &bufs[i], &chain->bufs[i]);
		if (ret < 0) {
			free_chain(config->chain_slab, chain);
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

		iovec->iov_base = chain->bufs[i].va + chain->bufs[i].offset;
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

static void reset_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	for (size_t slot = 0; slot <= config->queue_size_max; slot++) {
		struct chain_mapping *chain = detach_chain(vq_ctx, slot);

		if (chain != NULL) {
			free_chain(config->chain_slab, chain);
		}
	}

	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);

	vq_ctx->queue_notify_cb.cb = NULL;
	vq_ctx->queue_notify_cb.data = NULL;

	for (size_t i = 0; i < NUM_OF_VIRTQ_PARTS; i++) {
		vq_ctx->virtq_parts_gpa[i] = 0;
	}

	k_spin_unlock(&vq_ctx->lock, key);

	atomic_set(&vq_ctx->queue_size, 0);
	atomic_set(&vq_ctx->queue_ready_notified, 0);
}

/**
 * @brief Map the virtqueue rings of a queue
 *
 * Maps the grant pages of the VirtIO ring structures (descriptor
 * table, available ring, used ring) based on the current queue size
 * and stores them in the meta chain slot (META_CHAIN_SLOT).
 *
 * @param dev VHost device instance
 * @param queue_id ID of the queue to set up
 * @return 0 on success, negative error code on failure
 */
static int setup_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	const size_t queue_size = atomic_get(&vq_ctx->queue_size);
	const size_t part_sizes[NUM_OF_VIRTQ_PARTS] = {
		[VIRTQ_DESC] = 16 * queue_size,
		[VIRTQ_AVAIL] = 2 * queue_size + 6,
		[VIRTQ_USED] = 8 * queue_size + 6,
	};
	struct vhost_buf meta_bufs[NUM_OF_VIRTQ_PARTS];
	struct chain_mapping *chain, *stale;
	int ret;

	if (queue_size == 0) {
		LOG_ERR("queue%u: size not configured", queue_id);
		return -EINVAL;
	}

	for (size_t i = 0; i < NUM_OF_VIRTQ_PARTS; i++) {
		meta_bufs[i].gpa = vq_ctx->virtq_parts_gpa[i];
		meta_bufs[i].len = part_sizes[i];
		meta_bufs[i].is_write = true;

		if (meta_bufs[i].gpa == 0) {
			LOG_ERR("queue%u: ring part %zu address not configured", queue_id, i);
			return -EINVAL;
		}

		LOG_DBG("queue%u: ring part %zu gpa=0x%" PRIx64 " len=%zu", queue_id, i,
			meta_bufs[i].gpa, meta_bufs[i].len);
	}

	ret = map_chain(dev, meta_bufs, NUM_OF_VIRTQ_PARTS, &chain);
	if (ret < 0) {
		LOG_ERR("queue%u: mapping rings failed: %d", queue_id, ret);
		return ret;
	}

	/* Release stale mappings of a previous activation, if any. */
	for (size_t slot = 0; slot <= config->queue_size_max; slot++) {
		stale = detach_chain(vq_ctx, slot);
		if (stale != NULL) {
			free_chain(config->chain_slab, stale);
		}
	}

	publish_chain(vq_ctx, META_CHAIN_SLOT(config), chain);

	return 0;
}

static void reset_device(const struct device *dev)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	data->be.driver_features = 0;
	data->be.device_features_sel = 0;
	data->be.driver_features_sel = 0;
	atomic_set(&data->be.irq_status, 0);
	atomic_set(&data->be.status, 0);
	atomic_set(&data->be.queue_sel, 0);

	for (size_t i = 0; i < config->num_queues; i++) {
		reset_queue(dev, i);
	}
}

/*
 * VIRTIO-MMIO register emulation (IOREQ server)
 */

static void ioreq_server_read_req(const struct device *dev, struct ioreq *r)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	const size_t addr_offset = r->addr - data->fe.base;
	const uint16_t queue_sel = atomic_get(&data->be.queue_sel);

	switch (addr_offset) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		r->data = VIRTIO_MMIO_MAGIC;
		break;
	case VIRTIO_MMIO_VERSION:
		r->data = VIRTIO_MMIO_SUPPORTED_VERSION;
		break;
	case VIRTIO_MMIO_DEVICE_ID:
		r->data = config->device_id;
		break;
	case VIRTIO_MMIO_VENDOR_ID:
		r->data = config->vendor_id;
		break;
	case VIRTIO_MMIO_DEVICE_FEATURES:
		if (data->be.device_features_sel == 0) {
			r->data = (config->device_features & UINT32_MAX);
		} else if (data->be.device_features_sel == 1) {
			r->data = (config->device_features >> 32);
		} else {
			r->data = 0;
		}
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES:
		if (data->be.driver_features_sel == 0) {
			r->data = (data->be.driver_features & UINT32_MAX);
		} else if (data->be.driver_features_sel == 1) {
			r->data = (data->be.driver_features >> 32);
		} else {
			r->data = 0;
		}
		break;
	case VIRTIO_MMIO_QUEUE_SIZE_MAX:
		r->data = (queue_sel < config->num_queues) ? config->queue_size_max : 0;
		break;
	case VIRTIO_MMIO_STATUS:
		r->data = atomic_get(&data->be.status);
		break;
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		r->data = atomic_clear(&data->be.irq_status);
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		r->data = (queue_sel < config->num_queues) ? vhost_queue_ready(dev, queue_sel) : 0;
		break;
	default: {
		const size_t config_offset = addr_offset - VIRTIO_MMIO_CONFIG;

		r->data = 0;

		if (addr_offset < VIRTIO_MMIO_CONFIG ||
		    config_offset + r->size > config->config_data_len) {
			LOG_WRN("read of unhandled register 0x%zx", addr_offset);
			r->data = -1;
			break;
		}

		switch (r->size) {
		case sizeof(uint8_t):
			r->data = config->config_data[config_offset];
			break;
		case sizeof(uint16_t):
			r->data = sys_get_le16(&config->config_data[config_offset]);
			break;
		case sizeof(uint32_t):
			r->data = sys_get_le32(&config->config_data[config_offset]);
			break;
		default:
			r->data = -1;
			break;
		}
	} break;
	}

	LOG_DBG("r/%zx %" PRIx64, addr_offset, r->data);
}

static void ioreq_server_write_req(const struct device *dev, struct ioreq *r)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	const size_t addr_offset = r->addr - data->fe.base;
	const uint16_t queue_sel = atomic_get(&data->be.queue_sel);

	LOG_DBG("w/%zx %" PRIx64, addr_offset, r->data);

	switch (addr_offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		if (r->data == 0 || r->data == 1) {
			data->be.device_features_sel = (uint8_t)r->data;
		}
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		if (r->data == 0 || r->data == 1) {
			data->be.driver_features_sel = (uint8_t)r->data;
		}
		break;
	case VIRTIO_MMIO_DRIVER_FEATURES: {
		uint64_t *drvfeats = &data->be.driver_features;

		if (data->be.driver_features_sel == 0) {
			*drvfeats = (r->data | (*drvfeats & GENMASK64(63, 32)));
		} else if (data->be.driver_features_sel == 1) {
			*drvfeats = ((r->data << 32) | (*drvfeats & UINT32_MAX));
		}
	} break;
	case VIRTIO_MMIO_INTERRUPT_ACK:
		if (r->data) {
			atomic_and(&data->be.irq_status, ~r->data);
		}
		break;
	case VIRTIO_MMIO_STATUS:
		if (r->data & BIT(DEVICE_STATUS_FEATURES_OK)) {
			const bool ok = !(data->be.driver_features & ~config->device_features);

			if (ok) {
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FEATURES_OK));
			} else {
				LOG_ERR("unsupported features requested: driver=%" PRIx64
					" device=%" PRIx64,
					data->be.driver_features, config->device_features);
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FAILED));
				atomic_and(&data->be.status, ~BIT(DEVICE_STATUS_FEATURES_OK));
			}
		} else if (r->data == 0) {
			reset_device(dev);
		} else {
			atomic_or(&data->be.status, r->data);
		}
		break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
	case VIRTIO_MMIO_QUEUE_USED_LOW:
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		if (queue_sel < config->num_queues) {
			const size_t part = (addr_offset - VIRTIO_MMIO_QUEUE_DESC_LOW) / 0x10;
			const bool hi = !!((addr_offset - VIRTIO_MMIO_QUEUE_DESC_LOW) % 0x10);
			uint64_t *p_gpa = &data->vq_ctx[queue_sel].virtq_parts_gpa[part];

			*p_gpa = hi ? ((r->data << 32) | (*p_gpa & UINT32_MAX))
				    : (r->data | (*p_gpa & GENMASK64(63, 32)));
		}
		break;
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		if (r->data < config->num_queues) {
			atomic_set(&data->notify_queue_id, r->data);
			k_work_schedule_for_queue(&data->workq, &data->isr_work, K_NO_WAIT);
		}
		break;
	case VIRTIO_MMIO_QUEUE_SIZE:
		if (queue_sel < config->num_queues) {
			const bool is_pow2 = (POPCOUNT((unsigned int)r->data) == 1);

			if (r->data > 0 && r->data <= config->queue_size_max && is_pow2) {
				atomic_set(&data->vq_ctx[queue_sel].queue_size, r->data);
			} else {
				LOG_ERR("queue_size must be a power of 2 and <= %u: size=%" PRIu64,
					config->queue_size_max, r->data);
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FAILED));
			}
		}
		break;
	case VIRTIO_MMIO_QUEUE_READY:
		if (queue_sel >= config->num_queues) {
			break;
		}

		if (r->data == 0) {
			reset_queue(dev, queue_sel);
		} else {
			int err = setup_queue(dev, queue_sel);

			if (err < 0) {
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FAILED));
				LOG_ERR("queue%u setup failed: %d", queue_sel, err);
			} else if (data->queue_ready_cb.cb) {
				data->queue_ready_cb.cb(dev, queue_sel, data->queue_ready_cb.data);
			}
		}
		break;
	case VIRTIO_MMIO_QUEUE_SEL:
		atomic_set(&data->be.queue_sel, r->data);
		break;
	default:
		LOG_WRN("write to unhandled register 0x%zx", addr_offset);
		break;
	}
}

static void ioreq_server_cb(void *ptr)
{
	const struct device *dev = ptr;
	struct vhost_xen_mmio_data *data = dev->data;

	for (uint32_t v = 0; v < data->nr_ioserv_ports; v++) {
		struct ioreq *r = &data->shared_iopage->vcpu_ioreq[v];

		if (r->state != STATE_IOREQ_READY) {
			continue;
		}

		barrier_dmem_fence_full();

		if (r->dir == IOREQ_WRITE) {
			ioreq_server_write_req(dev, r);
		} else {
			ioreq_server_read_req(dev, r);
		}

		barrier_dmem_fence_full();
		r->state = STATE_IORESP_READY;

		barrier_dmem_fence_full();
		notify_evtchn(data->ioserv_ports[v]);
	}
}

/*
 * Frontend discovery via XenStore
 */

struct query_param {
	const char *key;
	const char *expected;
};

/**
 * Get the nth string from a NUL-separated string buffer, or NULL.
 */
static const char *nth_str(const char *buf, size_t len, size_t n)
{
	size_t pos = 0;

	for (size_t i = 0; i < n; i++) {
		while (pos < len && buf[pos] != '\0') {
			pos++;
		}
		pos++; /* skip the NUL separator */

		if (pos >= len) {
			return NULL;
		}
	}

	return &buf[pos];
}

static int parse_id(const char *str, uint32_t *id)
{
	char *endptr;
	unsigned long val = strtoul(str, &endptr, 10);

	if (*endptr != '\0' || endptr == str) {
		return -EINVAL;
	}

	*id = val;

	return 0;
}

static bool match_backend_params(domid_t domid, uint32_t deviceid, const struct query_param *params,
				 size_t param_num)
{
	char path[XS_VALUE_LEN];
	char value[XS_VALUE_LEN];

	for (size_t k = 0; k < param_num; k++) {
		ssize_t len;

		snprintf(path, sizeof(path), "backend/virtio/%u/%u/%s", domid, deviceid,
			 params[k].key);

		len = xs_read(path, value, sizeof(value) - 1, XS_TRANSACTION_NONE);
		if (len < 0) {
			return false;
		}
		value[len] = '\0';

		if (strcmp(params[k].expected, value) != 0) {
			return false;
		}
	}

	return true;
}

/**
 * Find a VIRTIO frontend matching @a params by scanning
 * backend/virtio/<domid>/<deviceid> in XenStore.
 */
static int query_virtio_backend(const struct query_param *params, size_t param_num, domid_t *domid,
				uint32_t *deviceid)
{
	char doms[XS_VALUE_LEN];
	char devs[XS_VALUE_LEN];

	const ssize_t doms_len = xs_directory("backend/virtio", doms, sizeof(doms) - 1, 0);

	if (doms_len < 0) {
		return doms_len;
	}

	for (size_t i = 0; nth_str(doms, doms_len, i) != NULL; i++) {
		char path[XS_VALUE_LEN];
		uint32_t dom;

		if (parse_id(nth_str(doms, doms_len, i), &dom) < 0) {
			continue;
		}

		snprintf(path, sizeof(path), "backend/virtio/%u", dom);

		const ssize_t devs_len = xs_directory(path, devs, sizeof(devs) - 1, 0);

		if (devs_len < 0) {
			continue;
		}

		for (size_t j = 0; nth_str(devs, devs_len, j) != NULL; j++) {
			uint32_t devid;

			if (parse_id(nth_str(devs, devs_len, j), &devid) < 0) {
				continue;
			}

			if (match_backend_params(dom, devid, params, param_num)) {
				*domid = dom;
				*deviceid = devid;
				return 0;
			}
		}
	}

	return -ENOENT;
}

static int query_irq(domid_t domid, uint32_t deviceid, uint32_t *irq)
{
	char path[XS_VALUE_LEN];
	char value[XS_VALUE_LEN];
	ssize_t len;

	snprintf(path, sizeof(path), "backend/virtio/%u/%u/irq", domid, deviceid);

	len = xs_read(path, value, sizeof(value) - 1, XS_TRANSACTION_NONE);
	if (len < 0) {
		return len;
	}
	value[len] = '\0';

	return parse_id(value, irq);
}

/*
 * Backend lifecycle
 */

static void teardown_backend(const struct device *dev)
{
	struct vhost_xen_mmio_data *data = dev->data;

	for (uint32_t v = 0; v < data->nr_ioserv_ports; v++) {
		unbind_event_channel(data->ioserv_ports[v]);
		evtchn_close(data->ioserv_ports[v]);
	}
	data->nr_ioserv_ports = 0;

	if (data->fe.servid_valid) {
		dmop_destroy_ioreq_server(data->fe.domid, data->fe.servid);
		data->fe.servid_valid = false;
	}
}

static void bind_interdomain_nop(void *priv)
{
}

static void xs_notify_handler(const char *path, const char *token, void *param)
{
	const struct device *dev = param;
	struct vhost_xen_mmio_data *data = dev->data;

	if (!atomic_get(&data->initialized) && !k_work_delayable_is_pending(&data->init_work)) {
		k_work_schedule_for_queue(&data->workq, &data->init_work, K_NO_WAIT);
	}
}

static void isr_workhandler(struct k_work *work)
{
	const struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct vhost_xen_mmio_data *data =
		CONTAINER_OF(delayable, struct vhost_xen_mmio_data, isr_work);
	const struct device *dev = data->dev;
	const struct vhost_xen_mmio_config *config = dev->config;

	const uint16_t queue_id = atomic_get(&data->notify_queue_id);
	const struct virtq_context *vq_ctx =
		(queue_id < config->num_queues) ? &data->vq_ctx[queue_id] : NULL;

	if (vq_ctx && vq_ctx->queue_notify_cb.cb) {
		vq_ctx->queue_notify_cb.cb(dev, queue_id, vq_ctx->queue_notify_cb.data);
	}
}

static void ready_workhandler(struct k_work *work)
{
	const struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct vhost_xen_mmio_data *data =
		CONTAINER_OF(delayable, struct vhost_xen_mmio_data, ready_work);
	const struct device *dev = data->dev;
	const struct vhost_xen_mmio_config *config = dev->config;

	for (size_t i = 0; i < config->num_queues; i++) {
		bool queue_ready_notified = atomic_get(&data->vq_ctx[i].queue_ready_notified);

		if (vhost_queue_ready(dev, i) && data->queue_ready_cb.cb && !queue_ready_notified) {
			data->queue_ready_cb.cb(dev, i, data->queue_ready_cb.data);
			atomic_set(&data->vq_ctx[i].queue_ready_notified, 1);
		}
	}
}

static void init_workhandler(struct k_work *work)
{
	struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct vhost_xen_mmio_data *data =
		CONTAINER_OF(delayable, struct vhost_xen_mmio_data, init_work);
	const struct device *dev = data->dev;
	const struct vhost_xen_mmio_config *config = dev->config;
	char baseaddr[2 + 16 + 1]; /* "0x" + 64-bit hex + NUL */
	uint32_t n_frms = 1;
	xen_pfn_t gfn = 0;
	mm_reg_t va;
	int ret;

	if (atomic_get(&data->initialized)) {
		return;
	}

	/*
	 * The frontend is matched by comparing the "base" property in
	 * XenStore against this instance's base address. If multiple
	 * frontends use the same base address, they cannot be told apart.
	 */
	snprintf(baseaddr, sizeof(baseaddr), "0x%lx", config->base);

	const struct query_param params[] = {{
		.key = "base",
		.expected = baseaddr,
	}};

	ret = query_virtio_backend(params, ARRAY_SIZE(params), &data->fe.domid, &data->fe.deviceid);
	if (ret < 0) {
		LOG_DBG("no matching frontend yet: %d", ret);
		goto retry;
	}

	data->fe.base = config->base;

	ret = query_irq(data->fe.domid, data->fe.deviceid, &data->fe.irq);
	if (ret < 0) {
		LOG_ERR("failed to read irq from XenStore: %d", ret);
		goto retry;
	}

	ret = dmop_nr_vcpus(data->fe.domid);
	if (ret < 0) {
		LOG_ERR("dmop_nr_vcpus err=%d", ret);
		goto retry;
	}
	data->vcpus = ret;

	if (data->vcpus > VCPUS_MAX) {
		LOG_ERR("frontend has %u vCPUs > VHOST_XEN_MMIO_VCPUS_MAX (%d)", data->vcpus,
			VCPUS_MAX);
		return; /* configuration error: retrying will not help */
	}

	ret = dmop_create_ioreq_server(data->fe.domid, HVM_IOREQSRV_BUFIOREQ_OFF, &data->fe.servid);
	if (ret < 0) {
		LOG_ERR("dmop_create_ioreq_server err=%d", ret);
		goto retry;
	}
	data->fe.servid_valid = true;

	ret = dmop_map_io_range_to_ioreq_server(data->fe.domid, data->fe.servid, 1, data->fe.base,
						data->fe.base + config->reg_size - 1);
	if (ret < 0) {
		LOG_ERR("dmop_map_io_range_to_ioreq_server err=%d", ret);
		goto retry;
	}

	ret = xendom_acquire_resource(data->fe.domid, XENMEM_resource_ioreq_server, data->fe.servid,
				      XENMEM_resource_ioreq_server_frame_ioreq(0), &n_frms, &gfn);
	if (ret < 0) {
		LOG_ERR("xendom_acquire_resource err=%d", ret);
		goto retry;
	}

	device_map(&va, (gfn << XEN_PAGE_SHIFT), (n_frms << XEN_PAGE_SHIFT), K_MEM_CACHE_NONE);
	data->shared_iopage = (void *)va;

	ret = dmop_set_ioreq_server_state(data->fe.domid, data->fe.servid, 1);
	if (ret) {
		LOG_ERR("dmop_set_ioreq_server_state err=%d", ret);
		goto retry;
	}

	for (uint32_t v = 0; v < data->vcpus; v++) {
		const evtchn_port_t vp_eport = data->shared_iopage->vcpu_ioreq[v].vp_eport;

		ret = bind_interdomain_event_channel(data->fe.domid, vp_eport,
						     bind_interdomain_nop, NULL);
		if (ret < 0) {
			LOG_ERR("EVTCHNOP_bind_interdomain[%u] err=%d", v, ret);
			goto retry;
		}

		data->ioserv_ports[v] = ret;
		data->nr_ioserv_ports = v + 1;

		bind_event_channel(data->ioserv_ports[v], ioreq_server_cb, (void *)dev);
		unmask_event_channel(data->ioserv_ports[v]);
	}

	LOG_INF("%s: backend ready base=0x%zx fe.domid=%d irq=%u vcpus=%u", dev->name,
		data->fe.base, data->fe.domid, data->fe.irq, data->vcpus);

	atomic_set(&data->initialized, 1);

	return;

retry:
	teardown_backend(dev);
	reset_device(dev);

	const uint32_t retry_count = MIN(RETRY_BACKOFF_EXP_MAX, atomic_inc(&data->retry));

	k_work_schedule_for_queue(&data->workq, &data->init_work,
				  K_MSEC(RETRY_DELAY_BASE_MS * BIT(retry_count)));
}

/*
 * VHost API implementation
 */

static bool vhost_xen_mmio_virtq_is_ready(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		return false;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	if (atomic_get(&vq_ctx->queue_size) == 0) {
		return false;
	}

	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);
	const struct chain_mapping *meta = vq_ctx->chains[META_CHAIN_SLOT(config)];
	bool ready = (meta != NULL) && (meta->nr_bufs == NUM_OF_VIRTQ_PARTS);

	for (size_t i = 0; ready && i < NUM_OF_VIRTQ_PARTS; i++) {
		ready = (meta->bufs[i].nr_mapped == meta->bufs[i].nr_pages) &&
			(vq_ctx->virtq_parts_gpa[i] != 0);
	}
	k_spin_unlock(&vq_ctx->lock, key);

	return ready;
}

static int vhost_xen_mmio_get_virtq(const struct device *dev, uint16_t queue_id, void **parts,
				    size_t *queue_size)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	if (!vhost_xen_mmio_virtq_is_ready(dev, queue_id)) {
		LOG_ERR("queue%u not ready", queue_id);
		return -ENODEV;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);
	const struct chain_mapping *meta = vq_ctx->chains[META_CHAIN_SLOT(config)];

	for (size_t i = 0; i < NUM_OF_VIRTQ_PARTS; i++) {
		parts[i] = meta->bufs[i].va + meta->bufs[i].offset;
	}
	k_spin_unlock(&vq_ctx->lock, key);

	*queue_size = atomic_get(&vq_ctx->queue_size);

	LOG_DBG("queue%u rings desc=%p, avail=%p, used=%p, size=%zu", queue_id, parts[VIRTQ_DESC],
		parts[VIRTQ_AVAIL], parts[VIRTQ_USED], *queue_size);

	return 0;
}

static int vhost_xen_mmio_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	const struct vhost_xen_mmio_data *data = dev->data;

	*drv_feats = data->be.driver_features;

	return 0;
}

static int vhost_xen_mmio_notify_virtq(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	atomic_or(&data->be.irq_status, VIRTIO_QUEUE_INTERRUPT);

	dmop_set_irq_level(data->fe.domid, data->fe.irq, 1);
	dmop_set_irq_level(data->fe.domid, data->fe.irq, 0);

	return 0;
}

static int vhost_xen_mmio_set_device_status(const struct device *dev, uint32_t status)
{
	struct vhost_xen_mmio_data *data = dev->data;

	atomic_or(&data->be.status, status);
	atomic_or(&data->be.irq_status, VIRTIO_DEVICE_CONFIGURATION_INTERRUPT);

	dmop_set_irq_level(data->fe.domid, data->fe.irq, 1);
	dmop_set_irq_level(data->fe.domid, data->fe.irq, 0);

	return 0;
}

static int vhost_xen_mmio_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	const size_t queue_size = atomic_get(&vq_ctx->queue_size);

	if (head >= queue_size) {
		LOG_ERR("queue%u: invalid head: %u >= queue_size %zu", queue_id, head, queue_size);
		return -EINVAL;
	}

	struct chain_mapping *chain = detach_chain(vq_ctx, head);

	if (chain == NULL) {
		LOG_ERR("queue%u: head %u not in use", queue_id, head);
		return -EINVAL;
	}

	free_chain(config->chain_slab, chain);

	return 0;
}

static int vhost_xen_mmio_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
					const struct vhost_buf *bufs, size_t bufs_count,
					struct vhost_iovec *r_iovecs, size_t r_iovecs_max,
					struct vhost_iovec *w_iovecs, size_t w_iovecs_max,
					size_t *read_count, size_t *write_count)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct chain_mapping *chain;
	int ret;

	*read_count = 0;
	*write_count = 0;

	if (queue_id >= config->num_queues) {
		LOG_ERR("invalid queue ID %u", queue_id);
		return -EINVAL;
	}

	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	const size_t queue_size = atomic_get(&vq_ctx->queue_size);

	if (head >= queue_size) {
		LOG_ERR("queue%u: invalid head: %u >= queue_size %zu", queue_id, head, queue_size);
		return -EINVAL;
	}

	if (bufs_count == 0) {
		return 0;
	}

	struct chain_mapping *stale = detach_chain(vq_ctx, head);

	if (stale != NULL) {
		LOG_WRN("queue%u: head %u was not released", queue_id, head);
		free_chain(config->chain_slab, stale);
	}

	ret = map_chain(dev, bufs, bufs_count, &chain);
	if (ret < 0) {
		return ret;
	}

	ret = fill_iovecs(chain, bufs, bufs_count, r_iovecs, r_iovecs_max, w_iovecs, w_iovecs_max,
			  read_count, write_count);
	if (ret < 0) {
		free_chain(config->chain_slab, chain);
		*read_count = 0;
		*write_count = 0;
		return ret;
	}

	publish_chain(vq_ctx, head, chain);

	return 0;
}

static int vhost_xen_mmio_register_virtq_ready_cb(const struct device *dev,
						  void (*callback)(const struct device *dev,
								   uint16_t queue_id,
								   void *user_data),
						  void *user_data)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	data->queue_ready_cb.cb = callback;
	data->queue_ready_cb.data = user_data;

	for (size_t i = 0; i < config->num_queues; i++) {
		atomic_set(&data->vq_ctx[i].queue_ready_notified, 0);
	}

	k_work_schedule_for_queue(&data->workq, &data->ready_work, K_NO_WAIT);

	return 0;
}

static int vhost_xen_mmio_register_virtq_notify_cb(const struct device *dev, uint16_t queue_id,
						   void (*callback)(const struct device *dev,
								    uint16_t queue_id,
								    void *user_data),
						   void *user_data)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

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

static const struct vhost_controller_api vhost_driver_xen_mmio_api = {
	.virtq_is_ready = vhost_xen_mmio_virtq_is_ready,
	.get_virtq = vhost_xen_mmio_get_virtq,
	.get_driver_features = vhost_xen_mmio_get_driver_features,
	.set_device_status = vhost_xen_mmio_set_device_status,
	.notify_virtq = vhost_xen_mmio_notify_virtq,
	.prepare_iovec = vhost_xen_mmio_prepare_iovec,
	.release_iovec = vhost_xen_mmio_release_iovec,
	.register_virtq_ready_cb = vhost_xen_mmio_register_virtq_ready_cb,
	.register_virtq_notify_cb = vhost_xen_mmio_register_virtq_notify_cb,
};

static int vhost_xen_mmio_init(const struct device *dev)
{
	const struct k_work_queue_config qcfg = {.name = "vhost-mmio-wq"};
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	char buf[XS_VALUE_LEN];
	static bool xen_event_initialized;
	int ret;

	data->dev = dev;

	if (!xen_event_initialized) {
		xen_event_initialized = true;
		xen_events_init();
	}

	ret = xs_init();
	if (ret < 0) {
		LOG_ERR("xs_init failed: %d", ret);
		return ret;
	}

	k_work_init_delayable(&data->init_work, init_workhandler);
	k_work_init_delayable(&data->isr_work, isr_workhandler);
	k_work_init_delayable(&data->ready_work, ready_workhandler);
	k_work_queue_init(&data->workq);
	k_work_queue_start(&data->workq, config->workq_stack, config->workq_stack_size,
			   config->workq_priority, &qcfg);

	xs_watcher_init(&data->watcher, xs_notify_handler, (void *)dev);
	xs_watcher_register(&data->watcher);

	ret = xs_watch("backend/virtio", dev->name, buf, sizeof(buf) - 1, 0);
	if (ret < 0) {
		LOG_ERR("xs_watch failed: %d", ret);
		return ret;
	}

	return 0;
}

#define Q_NUM(idx)    DT_INST_PROP(idx, num_queues)
#define Q_SZ_MAX(idx) DT_INST_PROP(idx, queue_size_max)

#define VQCTX_INIT(n, idx)                                                                         \
	{                                                                                          \
		.chains = vhost_xen_mmio_chains_##idx[n],                                          \
	}

#define VHOST_XEN_MMIO_INST(idx)                                                                   \
	static K_THREAD_STACK_DEFINE(workq_stack_##idx, DT_INST_PROP_OR(idx, stack_size, 4096));   \
	static const uint8_t config_data##idx[] = {                                                \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(idx, config_data),                               \
		(DT_INST_FOREACH_PROP_ELEM_SEP(idx, config_data, DT_PROP_BY_IDX, (,))), ())};      \
	K_MEM_SLAB_DEFINE_STATIC(                                                                  \
		vhost_xen_mmio_chain_slab_##idx, ROUND_UP(sizeof(struct chain_mapping), 8),        \
		Q_NUM(idx) * (CONFIG_VHOST_XEN_MMIO_INFLIGHT_CHAINS_MAX + 1), 8);                  \
	static struct chain_mapping *vhost_xen_mmio_chains_##idx[Q_NUM(idx)][Q_SZ_MAX(idx) + 1];   \
	static struct virtq_context vhost_xen_mmio_vq_ctx_##idx[Q_NUM(idx)] = {                    \
		LISTIFY(Q_NUM(idx), VQCTX_INIT, (,), idx),                                         \
	};                                                                                         \
	static const struct vhost_xen_mmio_config vhost_xen_mmio_config_##idx = {                  \
		.queue_size_max = Q_SZ_MAX(idx),                                                   \
		.num_queues = Q_NUM(idx),                                                          \
		.device_id = DT_INST_PROP(idx, device_id),                                         \
		.vendor_id = DT_INST_PROP_OR(idx, vendor_id, 0),                                   \
		.base = DT_INST_PROP(idx, base),                                                   \
		.reg_size = XEN_PAGE_SIZE,                                                         \
		.config_data = config_data##idx,                                                   \
		.config_data_len = DT_INST_PROP_LEN_OR(idx, config_data, 0),                       \
		.workq_stack = (k_thread_stack_t *)&workq_stack_##idx,                             \
		.workq_stack_size = K_THREAD_STACK_SIZEOF(workq_stack_##idx),                      \
		.workq_priority = DT_INST_PROP_OR(idx, priority, 0),                               \
		.chain_slab = &vhost_xen_mmio_chain_slab_##idx,                                    \
		.device_features = BIT64(VIRTIO_F_VERSION_1) | BIT64(VIRTIO_F_ACCESS_PLATFORM),    \
	};                                                                                         \
	static struct vhost_xen_mmio_data vhost_xen_mmio_data_##idx = {                            \
		.vq_ctx = vhost_xen_mmio_vq_ctx_##idx,                                             \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, vhost_xen_mmio_init, NULL, &vhost_xen_mmio_data_##idx,          \
			      &vhost_xen_mmio_config_##idx, POST_KERNEL, 100,                      \
			      &vhost_driver_xen_mmio_api);

DT_INST_FOREACH_STATUS_OKAY(VHOST_XEN_MMIO_INST)
