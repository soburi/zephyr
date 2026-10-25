/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>

#include <xenstore_cli.h>

#include <xen/public/xen.h>
#include <xen/public/hvm/ioreq.h>
#include <xen/public/hvm/dm_op.h>
#include <xen/public/hvm/hvm_op.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/byteorder.h>
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
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/sys/util_macro.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(xen_vhost_mmio);

#define DT_DRV_COMPAT xen_vhost_mmio

#define XEN_GRANT_ADDR_OFF (1ULL << 63)

#define VIRTIO_MMIO_MAGIC             0x74726976
#define VIRTIO_MMIO_SUPPORTED_VERSION 2

/* The maximum retry period is 12.8 seconds */
#define RETRY_DELAY_BASE_MS   50
#define RETRY_BACKOFF_EXP_MAX 8

#define HEX_64BIT_DIGITS 16

#define LOG_LVL_Q(lvl, str, ...)                                                                   \
	UTIL_CAT(LOG_, lvl)("%s[%u]: " str, __func__, queue_id, ##__VA_ARGS__)
#define LOG_ERR_Q(str, ...) LOG_LVL_Q(ERR, str, ##__VA_ARGS__)
#define LOG_WRN_Q(str, ...) LOG_LVL_Q(WRN, str, ##__VA_ARGS__)
#define LOG_INF_Q(str, ...) LOG_LVL_Q(INF, str, ##__VA_ARGS__)
#define LOG_DBG_Q(str, ...) LOG_LVL_Q(DBG, str, ##__VA_ARGS__)

#define META_PAGES_INDEX(cfg) (cfg->queue_size_max)

enum virtq_parts {
	VIRTQ_DESC = 0,
	VIRTQ_AVAIL,
	VIRTQ_USED,
	NUM_OF_VIRTQ_PARTS,
};

struct vhost_xen_mmio_config {
	k_thread_stack_t *workq_stack;
	size_t workq_stack_size;
	int workq_priority;

	uint16_t num_queues;
	uint16_t queue_size_max;
	size_t buffer_size;
	uint8_t device_id;
	uint32_t vendor_id;
	uintptr_t base;
	size_t reg_size;
	uint8_t *config_data;
	size_t config_data_len;

	uint64_t device_features;
};

struct mapped_pages_chunk {
	uint8_t *buf;
	uint8_t **map;
	struct gnttab_map_grant_ref *map_ops;
	struct gnttab_unmap_grant_ref *ops;
	size_t capacity;
	size_t count;
	size_t map_count;
	bool releasing;
	bool reset_pending;
};

struct virtq_callback {
	void (*cb)(const struct device *dev, uint16_t queue_id, void *user_data);
	void *data;
};

struct virtq_context {
	/**
	 * Store pages mapped to descriptors.
	 * Initializer allocate (queue_size_max + 1) statically,
	 * The last one is used to hold the desc, avail, and used
	 * of virtq itself.
	 */
	struct mapped_pages_chunk *pages_chunks;
	struct virtq_callback queue_notify_cb;
	atomic_t queue_size;
	atomic_t queue_ready_notified;
	uint64_t virtq_parts_gpa[NUM_OF_VIRTQ_PARTS];
	struct k_spinlock lock;
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
	evtchn_port_t ioserv_port;
	struct shared_iopage *shared_iopage;
	uint32_t vcpus;

	struct {
		ioservid_t servid;
		domid_t domid;
		uint32_t deviceid;
		uint32_t irq;
		uintptr_t base;
	} fe;

	struct {
		uint64_t driver_features;
		uint8_t device_features_sel;
		uint8_t driver_features_sel;
		atomic_t irq_status;
		atomic_t status;
		atomic_t queue_sel;
	} be;

	atomic_t notify_queue_id; /**< Temporary variable to pass to workq */
	struct virtq_callback queue_ready_cb;
	struct virtq_context *vq_ctx;
};

int query_virtio_backend(const char *key, const char *expected, domid_t *domid, int *deviceid);
uintptr_t query_irq(domid_t domid, int deviceid);

static int unmap_chunk(struct mapped_pages_chunk *chunk)
{
	int ret = 0;

	for (size_t i = 0; i < chunk->map_count; i++) {
		if (chunk->ops[i].status != GNTST_okay) {
			continue;
		}

		int rc = gnttab_unmap_refs(&chunk->ops[i], 1);

		if (rc != 0 || chunk->ops[i].status != GNTST_okay) {
			ret = rc != 0 ? rc : -EIO;
			/* Retain the handle if unmapping failed. */
			chunk->ops[i].status = GNTST_okay;
		} else {
			chunk->ops[i].status = GNTST_general_error;
		}
	}

	if (ret == 0) {
		chunk->map_count = 0;
		chunk->count = 0;
	}

	return ret;
}

static int release_chunk(struct virtq_context *ctx, struct mapped_pages_chunk *chunk, bool reset)
{
	k_spinlock_key_t key = k_spin_lock(&ctx->lock);

	if (chunk->releasing) {
		chunk->reset_pending |= reset;
		k_spin_unlock(&ctx->lock, key);
		return reset ? 0 : -EBUSY;
	}
	chunk->releasing = true;
	k_spin_unlock(&ctx->lock, key);

	int ret = unmap_chunk(chunk);

	key = k_spin_lock(&ctx->lock);
	chunk->reset_pending = false;
	chunk->releasing = false;
	k_spin_unlock(&ctx->lock, key);

	return ret;
}

static void reset_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		return;
	}

	struct virtq_context *ctx = &data->vq_ctx[queue_id];

	atomic_set(&ctx->queue_size, 0);
	atomic_set(&ctx->queue_ready_notified, 0);
	for (size_t i = 0; i <= config->queue_size_max; i++) {
		int ret = release_chunk(ctx, &ctx->pages_chunks[i], true);

		if (ret != 0) {
			LOG_ERR_Q("Failed to reset chunk %zu: %d", i, ret);
		}
	}

	k_spinlock_key_t key = k_spin_lock(&ctx->lock);

	ctx->queue_notify_cb = (struct virtq_callback){0};
	k_spin_unlock(&ctx->lock, key);
}

static size_t buffer_pages(const struct vhost_buf *buf)
{
	size_t offset = buf->gpa & (XEN_PAGE_SIZE - 1);

	return buf->len / XEN_PAGE_SIZE +
	       DIV_ROUND_UP(offset + buf->len % XEN_PAGE_SIZE, XEN_PAGE_SIZE);
}

static int map_chunk(struct virtq_context *ctx, struct mapped_pages_chunk *chunk,
		     domid_t domid, const struct vhost_buf *bufs, size_t count)
{
	size_t total = 0;
	int ret;

	if (count > chunk->capacity) {
		LOG_ERR("Buffer count exceeds capacity: count=%zu capacity=%zu", count,
			chunk->capacity);
		return -E2BIG;
	}
	for (size_t i = 0; i < count; i++) {
		if (bufs[i].len == 0 || (bufs[i].gpa & XEN_GRANT_ADDR_OFF) == 0 ||
		    bufs[i].len - 1 > UINT64_MAX - bufs[i].gpa) {
			return -EINVAL;
		}
		size_t pages = buffer_pages(&bufs[i]);
		uint64_t ref = (bufs[i].gpa & ~XEN_GRANT_ADDR_OFF) >> XEN_PAGE_SHIFT;

		if (ref > UINT32_MAX || pages - 1 > UINT32_MAX - ref) {
			return -EINVAL;
		}
		if (pages > chunk->capacity - total) {
			LOG_ERR("Mapping page capacity exceeded: buf=%zu len=%zu offset=%zu "
				"pages=%zu used=%zu capacity=%zu",
				i, bufs[i].len, (size_t)(bufs[i].gpa & (XEN_PAGE_SIZE - 1)),
				pages, total, chunk->capacity);
			return -E2BIG;
		}
		total += pages;
	}

	k_spinlock_key_t key = k_spin_lock(&ctx->lock);

	if (chunk->releasing) {
		k_spin_unlock(&ctx->lock, key);
		return -EBUSY;
	}
	chunk->releasing = true;
	k_spin_unlock(&ctx->lock, key);

	ret = unmap_chunk(chunk);
	if (ret != 0 || total == 0) {
		goto out;
	}

	size_t index = 0;

	for (size_t i = 0; i < count; i++) {
		chunk->map[i] = chunk->buf + index * XEN_PAGE_SIZE;
		for (size_t j = 0; j < buffer_pages(&bufs[i]); j++, index++) {
			chunk->map_ops[index] = (struct gnttab_map_grant_ref){
				.host_addr = (uintptr_t)(chunk->buf + index * XEN_PAGE_SIZE),
				.flags = GNTMAP_host_map |
					 (bufs[i].is_write ? 0 : GNTMAP_readonly),
				.ref = ((bufs[i].gpa & ~XEN_GRANT_ADDR_OFF) >>
					XEN_PAGE_SHIFT) + j,
				.dom = domid,
			};
		}
	}

	ret = gnttab_map_refs(chunk->map_ops, total);
	if (ret != 0) {
		goto out;
	}

	chunk->map_count = total;
	for (size_t i = 0; i < total; i++) {
		const struct gnttab_map_grant_ref *op = &chunk->map_ops[i];

		chunk->ops[i] = (struct gnttab_unmap_grant_ref){
			.host_addr = op->host_addr,
			.dev_bus_addr = op->dev_bus_addr,
			.handle = op->handle,
			.status = op->status,
		};
		if (op->status != GNTST_okay) {
			ret = -EIO;
		}
	}
	if (ret == 0) {
		chunk->count = count;
	}

out:
	key = k_spin_lock(&ctx->lock);
	if (chunk->reset_pending) {
		k_spin_unlock(&ctx->lock, key);
		int rc = unmap_chunk(chunk);

		ret = rc != 0 ? rc : -ECANCELED;
		key = k_spin_lock(&ctx->lock);
		chunk->reset_pending = false;
	}
	chunk->releasing = false;
	k_spin_unlock(&ctx->lock, key);
	return ret;
}

static int setup_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *ctx = &data->vq_ctx[queue_id];
	size_t size = atomic_get(&ctx->queue_size);
	struct vhost_buf bufs[NUM_OF_VIRTQ_PARTS] = {
		{.gpa = ctx->virtq_parts_gpa[VIRTQ_DESC], .len = 16 * size, .is_write = true},
		{.gpa = ctx->virtq_parts_gpa[VIRTQ_AVAIL], .len = 2 * size + 6, .is_write = true},
		{.gpa = ctx->virtq_parts_gpa[VIRTQ_USED], .len = 8 * size + 6, .is_write = true},
	};

	if (size == 0) {
		return -EINVAL;
	}
	return map_chunk(ctx, &ctx->pages_chunks[META_PAGES_INDEX(config)], data->fe.domid,
			 bufs, ARRAY_SIZE(bufs));
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

static void ioreq_server_read_req(const struct device *dev, struct ioreq *r)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	const size_t addr_offset = r->addr - data->fe.base;

	LOG_DBG("count %u: size: %u vp_eport: %u state: %d df: %d type: %d", r->count, r->size,
		r->vp_eport, r->state, r->df, r->type);

	switch (addr_offset) {
	case VIRTIO_MMIO_MAGIC_VALUE: {
		r->data = VIRTIO_MMIO_MAGIC;
	} break;
	case VIRTIO_MMIO_VERSION: {
		r->data = VIRTIO_MMIO_SUPPORTED_VERSION;
	} break;
	case VIRTIO_MMIO_DEVICE_ID: {
		r->data = config->device_id;
	} break;
	case VIRTIO_MMIO_VENDOR_ID: {
		r->data = config->vendor_id;
	} break;
	case VIRTIO_MMIO_DEVICE_FEATURES: {
		if (data->be.device_features_sel == 0) {
			r->data = (config->device_features & UINT32_MAX);
		} else if (data->be.device_features_sel == 1) {
			r->data = (config->device_features >> 32);
		} else {
			r->data = 0;
		}
	} break;
	case VIRTIO_MMIO_DRIVER_FEATURES: {
		if (data->be.driver_features_sel == 0) {
			r->data = (data->be.driver_features & UINT32_MAX);
		} else if (data->be.driver_features_sel == 1) {
			r->data = (data->be.driver_features >> 32);
		} else {
			r->data = 0;
		}
	} break;
	case VIRTIO_MMIO_QUEUE_SIZE_MAX: {
		r->data = config->queue_size_max;
	} break;
	case VIRTIO_MMIO_STATUS: {
		r->data = atomic_get(&data->be.status);
	} break;
	case VIRTIO_MMIO_INTERRUPT_STATUS: {
		r->data = atomic_clear(&data->be.irq_status);
	} break;
	case VIRTIO_MMIO_QUEUE_READY: {
		r->data = vhost_queue_ready(dev, atomic_get(&data->be.queue_sel));
	} break;
	default: {
		const size_t config_offset = addr_offset - VIRTIO_MMIO_CONFIG;

		if ((config_offset % 4) == 0 &&
		    (config_offset < ROUND_DOWN(config->config_data_len, 4))) {
			r->data = sys_read32((mem_addr_t)(config->config_data + config_offset));
		} else {
			r->data = -1;
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

	LOG_DBG("w/%zx %" PRIx64, addr_offset, r->data);

	switch (addr_offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL: {
		if (r->data == 0 || r->data == 1) {
			data->be.device_features_sel = (uint8_t)r->data;
		}
	} break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL: {
		if (r->data == 0 || r->data == 1) {
			data->be.driver_features_sel = (uint8_t)r->data;
		}
	} break;
	case VIRTIO_MMIO_DRIVER_FEATURES: {
		if (data->be.driver_features_sel == 0) {
			uint64_t *drvfeats = &data->be.driver_features;

			*drvfeats = (r->data | (*drvfeats & 0xFFFFFFFF00000000));
		} else {
			uint64_t *drvfeats = &data->be.driver_features;

			*drvfeats = ((r->data << 32) | (*drvfeats & UINT32_MAX));
		}
	} break;
	case VIRTIO_MMIO_INTERRUPT_ACK: {
		if (r->data) {
			atomic_and(&data->be.irq_status, ~r->data);
		}
	} break;
	case VIRTIO_MMIO_STATUS: {
		if (r->data & BIT(DEVICE_STATUS_FEATURES_OK)) {
			const bool ok = !(data->be.driver_features & ~config->device_features);

			if (ok) {
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FEATURES_OK));
			} else {
				LOG_ERR("%" PRIx64 " d driver_feats=%" PRIx64
					" device_feats=%" PRIx64,
					r->data, data->be.driver_features, config->device_features);
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FAILED));
				atomic_and(&data->be.status, ~BIT(DEVICE_STATUS_FEATURES_OK));
			}
		} else if (r->data == 0) {
			reset_device(dev);
		} else {
			atomic_or(&data->be.status, r->data);
		}
	} break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
	case VIRTIO_MMIO_QUEUE_USED_LOW:
	case VIRTIO_MMIO_QUEUE_USED_HIGH: {
		const uint16_t queue_id = atomic_get(&data->be.queue_sel);

		if (queue_id < config->num_queues) {
			const size_t part = (addr_offset - VIRTIO_MMIO_QUEUE_DESC_LOW) / 0x10;
			const bool hi = !!((addr_offset - VIRTIO_MMIO_QUEUE_DESC_LOW) % 0x10);
			uint64_t *p_gpa = &data->vq_ctx[queue_id].virtq_parts_gpa[part];

			*p_gpa = hi ? ((r->data << 32) | (*p_gpa & UINT32_MAX))
				    : (r->data | (*p_gpa & 0xFFFFFFFF00000000));
		}
	} break;
	case VIRTIO_MMIO_QUEUE_NOTIFY: {
		if (r->data < config->num_queues) {
			atomic_set(&data->notify_queue_id, r->data);
			k_work_schedule_for_queue(&data->workq, &data->isr_work, K_NO_WAIT);
		}
	} break;
	case VIRTIO_MMIO_QUEUE_SIZE: {
		const uint16_t queue_sel = atomic_get(&data->be.queue_sel);

		if (queue_sel < config->num_queues) {
			const bool is_pow2 = (POPCOUNT((int)r->data) == 1);

			if (r->data > 0 && r->data <= config->queue_size_max && is_pow2) {
				atomic_set(&data->vq_ctx[queue_sel].queue_size, r->data);
			} else {
				LOG_ERR("queue_size should be 2^n and <%u: size=%" PRIu64,
					config->queue_size_max, r->data);
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FAILED));
			}
		}
	} break;
	case VIRTIO_MMIO_QUEUE_READY: {
		const uint16_t queue_sel = atomic_get(&data->be.queue_sel);
		const uint16_t queue_id = queue_sel;

		if (r->data == 0) {
			reset_queue(dev, queue_id);
		} else if (r->data && (queue_sel < config->num_queues)) {
			int err = setup_queue(dev, queue_id);

			if (err < 0) {
				atomic_or(&data->be.status, BIT(DEVICE_STATUS_FAILED));
				LOG_ERR("queue%u setup failed: %d", queue_sel, err);
			} else if (data->queue_ready_cb.cb) {
				data->queue_ready_cb.cb(dev, queue_id, data->queue_ready_cb.data);
			}
		}
	} break;
	case VIRTIO_MMIO_QUEUE_SEL: {
		atomic_set(&data->be.queue_sel, r->data);
	} break;
	default:
		break;
	}
}

static void ioreq_server_cb(void *ptr)
{
	const struct device *dev = ptr;
	struct vhost_xen_mmio_data *data = dev->data;
	struct ioreq *r = &data->shared_iopage->vcpu_ioreq[0];

	if (r->state == STATE_IOREQ_READY) {
		if (r->dir == IOREQ_WRITE) {
			ioreq_server_write_req(dev, r);
		} else {
			ioreq_server_read_req(dev, r);
		}

		r->state = STATE_IORESP_READY;

		barrier_dmem_fence_full();
		notify_evtchn(data->ioserv_port);
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
	char baseaddr[HEX_64BIT_DIGITS + 3]; /* add rooms for "0x" and '\0' */
	uint32_t n_frms = 1;
	xen_pfn_t gfn = 0;
	mm_reg_t va;
	int ret;

	if (atomic_get(&data->initialized)) {
		return;
	}

	/*
	 * Using the settings obtained from xenstore can only be checked for
	 * matching the base value.
	 * This means that if multiple FEs try to connect to a BE using the same base address,
	 * they cannot be matched correctly.
	 */

	snprintf(baseaddr, sizeof(baseaddr), "0x%lx", config->base);

	ret = query_virtio_backend("base", baseaddr, &data->fe.domid, &data->fe.deviceid);
	if (ret < 0) {
		LOG_INF("%s: failed %d", __func__, ret);
		goto retry;
	}

	data->fe.base = config->base;
	data->fe.irq = query_irq(data->fe.domid, data->fe.deviceid);
	if (data->fe.irq == -1) {
		ret = -EINVAL;
		goto retry;
	}

	LOG_DBG("%u %u %lu", data->fe.domid, data->fe.irq, data->fe.base);

	ret = dmop_nr_vcpus(data->fe.domid);
	if (ret < 0) {
		LOG_ERR("dmop_nr_vcpus err=%d", ret);
		goto retry;
	}
	data->vcpus = ret;

	ret = dmop_create_ioreq_server(data->fe.domid, 0, &data->fe.servid);
	if (ret < 0) {
		LOG_ERR("dmop_create_ioreq_server err=%d", ret);
		goto retry;
	}

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

	LOG_DBG("bind_interdomain dom=%d remote_port=%d", data->fe.domid,
		data->shared_iopage->vcpu_ioreq[0].vp_eport);

	/* Assume that all interrupts are accepted by cpu0. */
	ret = bind_interdomain_event_channel(data->fe.domid,
					     data->shared_iopage->vcpu_ioreq[0].vp_eport,
					     bind_interdomain_nop, NULL);
	if (ret < 0) {
		LOG_ERR("EVTCHNOP_bind_interdomain[0] err=%d", ret);
		goto retry;
	}
	data->ioserv_port = ret;

	bind_event_channel(data->ioserv_port, ioreq_server_cb, (void *)dev);
	unmask_event_channel(data->ioserv_port);

	LOG_INF("%s: backend ready base=%zx fe.domid=%d irq=%d vcpus=%d shared_iopage=%p "
		"ioserv_port=%d",
		dev->name, data->fe.base, data->fe.domid, data->fe.irq, data->vcpus,
		data->shared_iopage, data->ioserv_port);

	atomic_set(&data->initialized, 1);

	ret = 0;

retry:
	if (ret < 0) {
		const uint32_t retry_count = MIN(RETRY_BACKOFF_EXP_MAX, atomic_inc(&data->retry));

		reset_device(dev);
		k_work_schedule_for_queue(&data->workq, &data->init_work,
					  K_MSEC(RETRY_DELAY_BASE_MS * (1 << retry_count)));
	}

	LOG_INF("exit work_inithandler: %d", ret);
}

static bool vhost_xen_mmio_virtq_is_ready(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	const struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID");
		return false;
	}

	const struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	const size_t queue_size = atomic_get(&vq_ctx->queue_size);

	if (queue_size == 0) {
		return false;
	}

	struct mapped_pages_chunk *meta = &vq_ctx->pages_chunks[META_PAGES_INDEX(config)];

	if (!meta->map || meta->count != NUM_OF_VIRTQ_PARTS) {
		return false;
	}

	if (meta->releasing || meta->map_count == 0) {
		return false;
	}
	for (size_t i = 0; i < meta->map_count; i++) {
		if (meta->ops[i].status != GNTST_okay) {
			return false;
		}
	}

	return vq_ctx->virtq_parts_gpa[VIRTQ_DESC] != 0 &&
	       vq_ctx->virtq_parts_gpa[VIRTQ_AVAIL] != 0 &&
	       vq_ctx->virtq_parts_gpa[VIRTQ_USED] != 0;
}

static int vhost_xen_mmio_get_virtq(const struct device *dev, uint16_t queue_id, void **parts,
				    size_t *queue_size)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	const struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID");
		return -EINVAL;
	}

	const struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	if (!vhost_xen_mmio_virtq_is_ready(dev, queue_id)) {
		LOG_ERR_Q("not ready");
		return -ENODEV;
	}

	struct mapped_pages_chunk *meta = &vq_ctx->pages_chunks[META_PAGES_INDEX(config)];

	if (!meta->map || meta->count != NUM_OF_VIRTQ_PARTS) {
		return -EINVAL;
	}

	for (size_t i = 0; i < meta->count; i++) {
		parts[i] = meta->map[i] + (vq_ctx->virtq_parts_gpa[i] & (XEN_PAGE_SIZE - 1));
	}

	if (!parts[VIRTQ_DESC] || !parts[VIRTQ_AVAIL] || !parts[VIRTQ_USED]) {
		LOG_ERR_Q("failed to get ring base addresses");
		return -EINVAL;
	}

	*queue_size = atomic_get(&vq_ctx->queue_size);

	LOG_DBG_Q("rings desc=%p, avail=%p, used=%p, size=%zu", parts[VIRTQ_DESC],
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
		LOG_ERR_Q("Invalid queue ID");
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

	if (!data) {
		return -EINVAL;
	}

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

	if (queue_id >= config->num_queues || head >= config->queue_size_max) {
		return -EINVAL;
	}

	struct virtq_context *ctx = &data->vq_ctx[queue_id];

	return release_chunk(ctx, &ctx->pages_chunks[head], false);
}

static int vhost_xen_mmio_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
					const struct vhost_buf *bufs, size_t bufs_count,
					struct vhost_iovec *r_iovecs, size_t r_iovecs_count,
					struct vhost_iovec *w_iovecs, size_t w_iovecs_count,
					size_t *read_count, size_t *write_count)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	size_t reads = 0, writes = 0;
	size_t length = 0;

	*read_count = 0;
	*write_count = 0;
	if (queue_id >= config->num_queues) {
		return -EINVAL;
	}

	struct virtq_context *ctx = &data->vq_ctx[queue_id];

	if (head >= atomic_get(&ctx->queue_size)) {
		return -EINVAL;
	}
	for (size_t i = 0; i < bufs_count; i++) {
		if (bufs[i].len > config->buffer_size - length) {
			LOG_ERR_Q("head=%u byte capacity exceeded: buf=%zu len=%zu "
				  "used=%zu capacity=%zu",
				  head, i, bufs[i].len, length, config->buffer_size);
			return -E2BIG;
		}
		length += bufs[i].len;
		if (bufs[i].is_write) {
			writes++;
		} else {
			reads++;
		}
	}
	if (reads > r_iovecs_count || writes > w_iovecs_count) {
		LOG_ERR_Q("head=%u iovec capacity exceeded: read=%zu capacity=%zu "
			  "write=%zu capacity=%zu",
			  head, reads, r_iovecs_count, writes, w_iovecs_count);
		return -E2BIG;
	}

	struct mapped_pages_chunk *chunk = &ctx->pages_chunks[head];
	int ret = map_chunk(ctx, chunk, data->fe.domid, bufs, bufs_count);

	if (ret != 0) {
		LOG_ERR_Q("head=%u mapping failed: %d", head, ret);
		return ret;
	}
	for (size_t i = 0; i < bufs_count; i++) {
		struct vhost_iovec *iov = bufs[i].is_write ?
			&w_iovecs[(*write_count)++] : &r_iovecs[(*read_count)++];

		iov->iov_base = chunk->map[i] + (bufs[i].gpa & (XEN_PAGE_SIZE - 1));
		iov->iov_len = bufs[i].len;
	}

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
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID");
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&vq_ctx->lock);

	vq_ctx->queue_notify_cb.cb = callback;
	vq_ctx->queue_notify_cb.data = user_data;

	k_spin_unlock(&vq_ctx->lock, key);

	return 0;
}

static const struct vhost_driver_api vhost_driver_xen_mmio_api = {
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

static void free_chunks(const struct device *dev)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	for (size_t q = 0; q < config->num_queues; q++) {
		for (size_t h = 0; h <= config->queue_size_max; h++) {
			struct mapped_pages_chunk *chunk = &data->vq_ctx[q].pages_chunks[h];

			if (chunk->buf != NULL) {
				int ret = gnttab_put_pages(chunk->buf, chunk->capacity);

				if (ret != 0) {
					LOG_ERR("Failed to free mapping pages: %d", ret);
				}
			}
			k_free(chunk->map);
			k_free(chunk->map_ops);
			k_free(chunk->ops);
			*chunk = (struct mapped_pages_chunk){0};
		}
	}
}

static int allocate_chunks(const struct device *dev)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	size_t size = config->queue_size_max;
	size_t meta_pages = DIV_ROUND_UP(16 * size + XEN_PAGE_SIZE - 1, XEN_PAGE_SIZE) +
		DIV_ROUND_UP(2 * size + 6 + XEN_PAGE_SIZE - 1, XEN_PAGE_SIZE) +
		DIV_ROUND_UP(8 * size + 6 + XEN_PAGE_SIZE - 1, XEN_PAGE_SIZE);

	for (size_t q = 0; q < config->num_queues; q++) {
		for (size_t h = 0; h <= size; h++) {
			struct mapped_pages_chunk *chunk = &data->vq_ctx[q].pages_chunks[h];

			chunk->capacity = h == size ? meta_pages :
				DIV_ROUND_UP(config->buffer_size, XEN_PAGE_SIZE);
			chunk->buf = gnttab_get_pages(chunk->capacity);
			chunk->map = k_calloc(chunk->capacity, sizeof(*chunk->map));
			chunk->map_ops = k_calloc(chunk->capacity, sizeof(*chunk->map_ops));
			chunk->ops = k_calloc(chunk->capacity, sizeof(*chunk->ops));
			if (chunk->buf == NULL || chunk->map == NULL || chunk->map_ops == NULL ||
			    chunk->ops == NULL) {
				free_chunks(dev);
				return -ENOMEM;
			}
		}
	}
	return 0;
}

static int xen_event_initialized;

static int vhost_xen_mmio_init(const struct device *dev)
{
	const struct k_work_queue_config qcfg = {.name = "vhost-mmio-wq"};
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	char buf[65] = {0};
	int ret;

	data->dev = dev;

	atomic_set(&data->initialized, 0);
	atomic_set(&data->retry, 0);
	atomic_set(&data->be.status, 0);
	atomic_set(&data->be.queue_sel, 0);
	atomic_set(&data->be.irq_status, 0);
	atomic_set(&data->notify_queue_id, 0);

	if (!xen_event_initialized) {
		xen_event_initialized = 1;
		xen_events_init();
	}

	ret = xs_init();

	if (ret < 0) {
		LOG_INF("xs_init_xenstore failed: %d", ret);
		return ret;
	}

	ret = allocate_chunks(dev);
	if (ret != 0) {
		return ret;
	}

	ret = xs_watch("backend/virtio", dev->name, buf, ARRAY_SIZE(buf) - 1, 0);
	if (ret < 0) {
		LOG_INF("xs_watch failed: %d", ret);
		free_chunks(dev);
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

	/* Probe once even if the initial watch event arrived before registration. */
	k_work_schedule_for_queue(&data->workq, &data->init_work, K_NO_WAIT);

	return 0;
}

#define VQCTX_INIT(n, idx)                                                                         \
	{                                                                                          \
		.pages_chunks = vhost_xen_mmio_pages_chunks_##idx[n],                              \
	}

#define Q_NUM(idx)    DT_INST_PROP_OR(idx, num_queues, 1)
#define Q_SZ_MAX(idx) DT_INST_PROP_OR(idx, queue_size_max, 1)

#define VHOST_XEN_MMIO_INST(idx)                                                                   \
	BUILD_ASSERT(DT_INST_PROP(idx, buffer_size) > 0, "buffer-size must be positive");          \
	static K_THREAD_STACK_DEFINE(workq_stack_##idx, DT_INST_PROP_OR(idx, stack_size, 4096));   \
	uint8_t config_data##idx[] = {                                                             \
		COND_CODE_1(DT_INST_NODE_HAS_PROP(idx, config_data),                               \
		(DT_INST_FOREACH_PROP_ELEM_SEP(idx, config_data, DT_PROP_BY_IDX, (,))), ())};      \
	static const struct vhost_xen_mmio_config vhost_xen_mmio_config_##idx = {                  \
		.buffer_size = DT_INST_PROP(idx, buffer_size),                               \
		.queue_size_max = DT_INST_PROP_OR(idx, queue_size_max, 1),                         \
		.num_queues = DT_INST_PROP_OR(idx, num_queues, 1),                                 \
		.device_id = DT_INST_PROP(idx, device_id),                                         \
		.vendor_id = DT_INST_PROP_OR(idx, vendor_id, 0),                                   \
		.base = DT_INST_PROP(idx, base),                                                   \
		.reg_size = XEN_PAGE_SIZE,                                                         \
		.config_data = config_data##idx,                                                   \
		.config_data_len = DT_INST_PROP_LEN_OR(idx, config_data, 0),                       \
		.workq_stack = (k_thread_stack_t *)&workq_stack_##idx,                             \
		.workq_stack_size = K_THREAD_STACK_SIZEOF(workq_stack_##idx),                      \
		.workq_priority = DT_INST_PROP_OR(idx, priority, 0),                               \
		.device_features = BIT(VIRTIO_F_VERSION_1) | BIT(VIRTIO_F_ACCESS_PLATFORM) |      \
				   DT_INST_PROP_OR(idx, device_features, 0),                         \
	};                                                                                         \
	struct mapped_pages_chunk vhost_xen_mmio_pages_chunks_##idx[Q_NUM(idx)]                    \
								   [Q_SZ_MAX(idx) + 1];            \
	static struct virtq_context vhost_xen_mmio_vq_ctx_##idx[Q_NUM(idx)] = {                    \
		LISTIFY(Q_NUM(idx), VQCTX_INIT, (,), idx),                                         \
	};                                                                                         \
	static struct vhost_xen_mmio_data vhost_xen_mmio_data_##idx = {                            \
		.vq_ctx = vhost_xen_mmio_vq_ctx_##idx,                                             \
		.fe.base = -1,                                                                     \
		.ioserv_port = -1,                                                                 \
		.fe.servid = -1,                                                                   \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(idx, vhost_xen_mmio_init, NULL, &vhost_xen_mmio_data_##idx,          \
			      &vhost_xen_mmio_config_##idx, POST_KERNEL, 100,                      \
			      &vhost_driver_xen_mmio_api);

DT_INST_FOREACH_STATUS_OKAY(VHOST_XEN_MMIO_INST)
