/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>

#include <xen/public/xen.h>
#include <xen/public/hvm/ioreq.h>
#include <xen/public/hvm/dm_op.h>
#include <xen/public/hvm/hvm_op.h>
#include <xen/public/io/xs_wire.h>

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
#include <zephyr/xen/xenstore.h>
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

/* Grant table resource tracking */
static atomic_t total_allocated_pages = ATOMIC_INIT(0);
static atomic_t allocation_failures = ATOMIC_INIT(0);

#define PAGE_SHIFT         (__builtin_ctz(CONFIG_MMU_PAGE_SIZE))
#define XEN_GRANT_ADDR_OFF (1ULL << 63)

#define VIRTIO_MMIO_MAGIC             0x74726976
#define VIRTIO_MMIO_SUPPORTED_VERSION 2

#define RETRY_DELAY_BASE_MS 50
#define HEX_64BIT_DIGITS    16

#define LOG_ERR_Q(str, ...) LOG_ERR("%s[%u]: " str, __func__, queue_id, __VA_ARGS__)

enum {
	DESC = 0,
	AVAIL,
	USED,
	NUM_OF_VIRTQ_PARTS,
};

struct vhost_xen_mmio_config {
	k_thread_stack_t *workq_stack;
	int workq_priority;

	uint16_t num_queues;
	uint16_t queue_size_max;
	uint8_t device_id;
	uint32_t vendor_id;
	uintptr_t base;
	size_t reg_size;

	uint64_t device_features;
};

struct mapped_pages {
	uint64_t gpa;
	uint8_t *buf;
	size_t len;
	struct gnttab_unmap_grant_ref *unmap;
	size_t unmap_count; /* current number of mapped buffers */
	size_t unmap_pages;
};

struct descriptor_chain {
	struct mapped_pages *pages;
	size_t max_descriptors;
	int32_t chain_head;
};

struct queue_callback {
	void (*cb)(const struct device *dev, uint16_t queue_id, void *user_data);
	void *data;
};

struct virtq_context {
	size_t queue_size; /* Number of descriptors in this queue */
	struct mapped_pages meta[3];
	struct descriptor_chain *chains;
	struct queue_callback notify_callback;
	atomic_t notified;
};

struct vhost_xen_mmio_data {
	struct k_work_delayable init_work;
	struct k_work_delayable isr_work;
	struct k_work_delayable ready_work;
	struct k_work_q workq;
	const struct device *dev;
	struct k_spinlock lock;
	atomic_t initialized;
	atomic_t retry;

	struct xenstore xs;
	evtchn_port_t xs_port;
	evtchn_port_t ioserv_port;
	struct shared_iopage *shared_iopage;

	struct {
		ioservid_t servid;
		domid_t domid;
		uint32_t deviceid;
		uint32_t irq;
		uintptr_t base;
	} fe;

	uint32_t vcpus;

	uint64_t driver_features;
	uint8_t device_features_sel;
	uint8_t driver_features_sel;
	atomic_t irq_status;
	atomic_t status;
	atomic_t queue_sel;

	atomic_t notify_queue_id;
	struct queue_callback ready_callback;
	struct virtq_context *vq_ctx;
};

struct query_param {
	const char *key;
	const char *expected;
};

__maybe_unused static const char *virtio_reg_name(size_t off)
{
	switch (off) {
	case VIRTIO_MMIO_MAGIC_VALUE:
		return "MAGIC_VALUE";
	case VIRTIO_MMIO_VERSION:
		return "VERSION";
	case VIRTIO_MMIO_DEVICE_ID:
		return "DEVICE_ID";
	case VIRTIO_MMIO_VENDOR_ID:
		return "VENDOR_ID";
	case VIRTIO_MMIO_DEVICE_FEATURES:
		return "DEVICE_FEATURES";
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL:
		return "DEVICE_FEATURES_SEL";
	case VIRTIO_MMIO_DRIVER_FEATURES:
		return "DRIVER_FEATURES";
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL:
		return "DRIVER_FEATURES_SEL";
	case VIRTIO_MMIO_QUEUE_SEL:
		return "QUEUE_SEL";
	case VIRTIO_MMIO_QUEUE_SIZE_MAX:
		return "QUEUE_SIZE_MAX";
	case VIRTIO_MMIO_QUEUE_SIZE:
		return "QUEUE_SIZE";
	case VIRTIO_MMIO_QUEUE_READY:
		return "QUEUE_READY";
	case VIRTIO_MMIO_QUEUE_NOTIFY:
		return "QUEUE_NOTIFY";
	case VIRTIO_MMIO_INTERRUPT_STATUS:
		return "INTERRUPT_STATUS";
	case VIRTIO_MMIO_INTERRUPT_ACK:
		return "INTERRUPT_ACK";
	case VIRTIO_MMIO_STATUS:
		return "STATUS";
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
		return "QUEUE_DESC_LOW";
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
		return "QUEUE_DESC_HIGH";
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
		return "QUEUE_AVAIL_LOW";
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
		return "QUEUE_AVAIL_HIGH";
	case VIRTIO_MMIO_QUEUE_USED_LOW:
		return "QUEUE_USED_LOW";
	case VIRTIO_MMIO_QUEUE_USED_HIGH:
		return "QUEUE_USED_HIGH";
	case VIRTIO_MMIO_SHM_SEL:
		return "SHM_SEL";
	case VIRTIO_MMIO_SHM_LEN_LOW:
		return "SHM_LEN_LOW";
	case VIRTIO_MMIO_SHM_LEN_HIGH:
		return "SHM_LEN_HIGH";
	case VIRTIO_MMIO_SHM_BASE_LOW:
		return "SHM_BASE_LOW";
	case VIRTIO_MMIO_SHM_BASE_HIGH:
		return "SHM_BASE_HIGH";
	case VIRTIO_MMIO_QUEUE_RESET:
		return "QUEUE_RESET";
	case VIRTIO_MMIO_CONFIG_GENERATION:
		return "CONFIG_GENERATION";
	case VIRTIO_MMIO_CONFIG:
		return "CONFIG";
	default:
		return "?????";
	}
}

/**
 * Get the nth string from a null-separated string buffer
 */
static const char *nth_str(const char *buf, size_t len, size_t n)
{
	int cnt = 0;

	if (n == 0) {
		return buf;
	}

	for (int i = 0; i < len; i++) {
		if (buf[i] == '\0') {
			cnt++;
		}

		if (cnt == n && (i != (len - 1))) {
			return &buf[i + 1];
		}
	}

	return NULL;
}

/**
 * @brief Query VIRTIO backend's domid/deviceid from XenStore
 *
 * @param[in] xs XenStore connection handle
 * @param[in] params Array of query param to match
 * @param[in] param_num Number of query parameters in the array
 * @param[out] domid frontend domain ID
 * @param[out] deviceid Device ID
 * @return 0 on success, negative error code on failure
 */
static int query_virtio_backend(struct xenstore *xs, const struct query_param *params,
				size_t param_num, domid_t *domid, int *deviceid)
{
	char buf[65] = {0};
	const size_t len = ARRAY_SIZE(buf) - 1;
	const char *ptr_i, *ptr_j;
	int i, j;

	const ssize_t len_i = xs_directory(xs, "backend/virtio", buf, len);

	if (len_i < 0) {
		return -EIO;
	}
	if (len_i == 6 && strncmp(buf, "ENOENT", len) == 0) {
		return -ENOENT;
	}

	for (i = 0, ptr_i = buf; ptr_i; ptr_i = nth_str(buf, len_i, i++)) {
		char *endptr;

		*domid = strtol(ptr_i, &endptr, 10);
		if (*endptr != '\0') {
			continue;
		}

		snprintf(buf, len, "backend/virtio/%d", *domid);

		const ssize_t len_j = xs_directory(xs, buf, buf, ARRAY_SIZE(buf));

		if (len_j < 0 || strncmp(buf, "ENOENT", ARRAY_SIZE(buf)) == 0) {
			continue;
		}

		for (j = 0, ptr_j = buf; ptr_j; ptr_j = nth_str(buf, len_j, j++)) {
			*deviceid = strtol(ptr_j, &endptr, 10);
			if (*endptr != '\0') {
				continue; /* Skip invalid device ID */
			}

			bool match = true;

			for (int k = 0; k < param_num; k++) {
				snprintf(buf, len, "backend/virtio/%d/%d/%s", *domid, *deviceid,
					 params[k].key);
				const ssize_t len_k = xs_read(xs, buf, buf, ARRAY_SIZE(buf));

				if ((len_k < 0) || (strncmp(buf, "ENOENT", ARRAY_SIZE(buf)) == 0) ||
				    (strncmp(params[k].expected, buf, ARRAY_SIZE(buf)) != 0)) {
					match = false;
					break;
				}
			}

			if (match) {
				return 0;
			}
		}
	}

	return -ENOENT;
}

static uintptr_t query_irq(struct xenstore *xs, domid_t domid, int deviceid)
{
	char buf[65] = {0};
	size_t len = ARRAY_SIZE(buf) - 1;
	char *endptr;

	snprintf(buf, len, "backend/virtio/%d/%d/irq", domid, deviceid);

	len = xs_read(xs, buf, buf, ARRAY_SIZE(buf) - 1);
	if ((len < 0) || (strncmp(buf, "ENOENT", ARRAY_SIZE(buf)) == 0)) {
		return (uintptr_t)-1;
	}

	uintptr_t irq_val = strtol(buf, &endptr, 10);

	if (*endptr != '\0') {
		return (uintptr_t)-1;
	}

	return irq_val;
}

static int unmap_pages(struct mapped_pages *pages)
{
	int ret = 0;

	if (!pages->unmap) {
		return 0;
	}

	for (int i = 0; i < pages->unmap_count; i++) {
		if (pages->unmap[i].status == GNTST_okay) {
			int rc = gnttab_unmap_refs(&pages->unmap[i], 1);
			if (rc < 0) {
				LOG_ERR("gnttab_unmap_refs failed: %d", rc);
				ret = rc;
			}
			pages->unmap[i].status = GNTST_general_error;
		}
	}

	return ret;
}

static int free_pages(struct mapped_pages *pages, size_t len)
{
	int ret = 0;

	for (int i = 0; i < len; i++) {
		/* Only unmap if the buffer was actually allocated */
		if (pages[i].buf || pages[i].unmap) {
			int rc = unmap_pages(&pages[i]);
			if (rc < 0) {
				LOG_ERR("unmap_pages failed: %d", rc);
				ret = rc;
			}
		}

		if (pages[i].unmap) {
			k_free(pages[i].unmap);
			pages[i].unmap = NULL;
		}

		if (pages[i].buf) {
			int rc = gnttab_put_pages(pages[i].buf, pages[i].unmap_pages);
			if (rc < 0) {
				LOG_ERR("gnttab_put_pages failed: %d", rc);
				ret = rc;
			} else {
				/* Decrease allocation count on successful release */
				atomic_sub(&total_allocated_pages, pages[i].unmap_pages);
				LOG_DBG("Released %zu pages, total now: %ld", pages[i].unmap_pages,
					atomic_get(&total_allocated_pages));
			}

			pages[i].buf = NULL;
		}

		/* Reset the page structure */
		pages[i].unmap_count = 0;
		pages[i].unmap_pages = 0;
		pages[i].len = 0;
		pages[i].gpa = 0;
	}

	return ret;
}

static int setup_iovec_mappings(struct descriptor_chain *dchain, domid_t domid,
				const struct vhost_gpa_range *ranges, size_t ranges_len,
				struct vhost_iovec *r_iovecs, size_t r_iovecs_len,
				struct vhost_iovec *w_iovecs, size_t w_iovecs_len,
				struct gnttab_map_grant_ref *map_grant, size_t *r_count_out,
				size_t *w_count_out)
{
	//struct vhost_xen_mmio_data *data = dev->data;
	//struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	//struct descriptor_chain *dchain;// = &vq_ctx->chains[head];
	size_t r_count = 0;
	size_t w_count = 0;
	size_t total_iovec_count = 0;

	//k_spinlock_key_t key = k_spin_lock(&data->lock);

	for (size_t i = 0; i < ranges_len; i++) {
		const bool is_write = ranges[i].is_write;
		const size_t iovecs_len = is_write ? w_iovecs_len : r_iovecs_len;
		const size_t current_count = is_write ? w_count : r_count;
		struct vhost_iovec *iovec = is_write ? &w_iovecs[w_count] : &r_iovecs[r_count];

		if (current_count >= iovecs_len) {
			LOG_ERR("%s: no more %s iovecs: %zu >= %zu", __func__,
				is_write ? "write" : "read", current_count, iovecs_len);
			//k_spin_unlock(&data->lock, key);
			return -E2BIG;
		}

		/* Use single buffer with offset for each range */
		const void *host_page = dchain->pages->buf + (total_iovec_count * XEN_PAGE_SIZE);
		const size_t page_offset = ranges[i].gpa & (XEN_PAGE_SIZE - 1);
		const void *va = (void *)(((uintptr_t)host_page) + page_offset);
		struct gnttab_map_grant_ref *map = &map_grant[total_iovec_count];

		map->host_addr = (uintptr_t)host_page;
		map->flags = GNTMAP_host_map;
		map->ref = (ranges[i].gpa & ~XEN_GRANT_ADDR_OFF) >> 12;
		map->dom = domid;

		iovec->iov_base = (void *)va;
		iovec->iov_len = ranges[i].len;

		if (is_write) {
			w_count++;
		} else {
			r_count++;
		}
		total_iovec_count++;
	}

	//k_spin_unlock(&data->lock, key);

	*r_count_out = r_count;
	*w_count_out = w_count;

	return total_iovec_count;
}

static int setup_unmap_info(struct descriptor_chain *dchain, const struct vhost_gpa_range *ranges,
			    size_t ranges_len, struct gnttab_map_grant_ref *map_grant, size_t iovec_count)
{
	bool any_map_failed = false;
	const size_t initial_count = dchain->pages->unmap_count;

	for (size_t i = 0; i < iovec_count; i++) {
		const struct gnttab_map_grant_ref *map = &map_grant[i];
		const size_t page_idx = initial_count + i;
		struct gnttab_unmap_grant_ref *unmap = &dchain->pages->unmap[page_idx];

		/* Set up unmap information */
		unmap->host_addr = map->host_addr;
		unmap->dev_bus_addr = map->dev_bus_addr;
		unmap->handle = map->handle;
		unmap->status = map->status;

		if (map->status != GNTST_okay) {
			LOG_ERR("map[%zu] failed: status=%d", i, map->status);
			any_map_failed = true;
		}
	}

	if (any_map_failed) {
		/* Unmap the ones that were mapped */
		gnttab_unmap_refs(&dchain->pages->unmap[initial_count], iovec_count);
		return -EIO;
	}

	/* Update count only after successful mapping */
	dchain->pages->unmap_count += iovec_count;

	return 0;
}

static int allocate_chain_buffer(const struct device *dev, uint16_t queue_id, uint16_t head,
				 uint16_t total_pages)
{
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	/* Allocate pages structure if not already allocated */
	if (vq_ctx->chains[head].pages == NULL) {
		vq_ctx->chains[head].pages = k_malloc(sizeof(struct mapped_pages));
		if (vq_ctx->chains[head].pages == NULL) {
			LOG_ERR("Failed to allocate pages structure%s", "");
			return -ENOMEM;
		}
		memset(vq_ctx->chains[head].pages, 0, sizeof(struct mapped_pages));
	} else if (vq_ctx->chains[head].pages->unmap_pages != total_pages) {
		/* Only reallocate if size differs - this will also update statistics */
		free_pages(vq_ctx->chains[head].pages, 1);
	} else {
		/* Buffer is already the right size, just reset the count */
		vq_ctx->chains[head].pages->unmap_count = 0;
		LOG_DBG("%s: q=%u: Reusing existing buffer with %u pages", __func__, queue_id,
			total_pages);
		return 0;
	}

	/* Allocate new buffer with the required size */
	atomic_t current_total = atomic_add(&total_allocated_pages, total_pages);
	LOG_DBG("%s: q=%u: Allocating %d pages for chain buffer (total: %ld)", __func__, queue_id,
		total_pages, current_total + total_pages);

	uint8_t *new_buf = gnttab_get_pages(total_pages);
	struct gnttab_unmap_grant_ref *new_unmap =
		k_malloc(sizeof(struct gnttab_unmap_grant_ref) * total_pages);

	if (new_buf == NULL || new_unmap == NULL) {
		/* Adjust allocation count back if failed */
		atomic_sub(&total_allocated_pages, total_pages);
		atomic_inc(&allocation_failures);

		if (new_buf) {
			gnttab_put_pages(new_buf, total_pages);
		}
		if (new_unmap) {
			k_free(new_unmap);
		}
		LOG_ERR("%s: q=%u: failed to allocate pages/unmap array for %u pages (failures: "
			"%ld)",
			__func__, queue_id, total_pages, atomic_get(&allocation_failures));
		return -ENOMEM;
	}

	/* Initialize all new unmap entries */
	for (size_t j = 0; j < total_pages; j++) {
		new_unmap[j].status = GNTST_general_error;
	}

	/* Set new pre-allocated buffer */
	vq_ctx->chains[head].pages->buf = new_buf;
	vq_ctx->chains[head].pages->unmap = new_unmap;
	vq_ctx->chains[head].pages->unmap_pages = total_pages;
	vq_ctx->chains[head].pages->len = total_pages * XEN_PAGE_SIZE;
	vq_ctx->chains[head].pages->unmap_count = 0; /* Reset count for new buffer */

	LOG_DBG("%s: q=%u: Pre-allocated buffer with %u pages", __func__, queue_id, total_pages);

	return 0;
}

static void reset_queue(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	free_pages(vq_ctx->meta, NUM_OF_VIRTQ_PARTS);

	k_spinlock_key_t key = k_spin_lock(&data->lock);

	/* Free dynamically allocated pages in descriptor chains */
	for (int i = 0; i < config->queue_size_max; i++) {
		if (vq_ctx->chains[i].pages) {
			/* First free the buffers inside pages */
			free_pages(vq_ctx->chains[i].pages, 1);
			/* Then free the pages structure itself */
			k_free(vq_ctx->chains[i].pages);
			vq_ctx->chains[i].pages = NULL;
		}
	}

	vq_ctx->queue_size = 0;
	vq_ctx->notify_callback.cb = 0;
	vq_ctx->notify_callback.data = 0;
	atomic_set(&vq_ctx->notified, 0);

	memset(vq_ctx->meta, 0, sizeof(struct mapped_pages) * NUM_OF_VIRTQ_PARTS);
	memset(vq_ctx->chains, 0, sizeof(struct descriptor_chain) * config->queue_size_max);

	k_spin_unlock(&data->lock, key);
}

/**
 * @brief Set up a VirtIO queue for operation
 *
 * Maps the required grant pages for VirtIO ring structures (descriptor,
 * available, and used rings) based on the current queue size. Calculates
 * the number of pages needed dynamically and maps them individually.
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
	const size_t num_pages[] = {DIV_ROUND_UP(16 * (vq_ctx->queue_size), XEN_PAGE_SIZE),
				    DIV_ROUND_UP(2 * (vq_ctx->queue_size) + 6, XEN_PAGE_SIZE),
				    DIV_ROUND_UP(8 * (vq_ctx->queue_size) + 6, XEN_PAGE_SIZE)};
	int ret = 0;

	for (int i = 0; i < NUM_OF_VIRTQ_PARTS; i++) {
		const size_t pages = num_pages[i];

		vq_ctx->meta[i].unmap_pages = pages;
		vq_ctx->meta[i].len = pages * XEN_PAGE_SIZE;
		vq_ctx->meta[i].buf = gnttab_get_pages(pages);
		vq_ctx->meta[i].unmap = k_malloc(sizeof(struct gnttab_unmap_grant_ref) * pages);
		if (vq_ctx->meta[i].buf == NULL || vq_ctx->meta[i].unmap == NULL) {
			ret = -ENOMEM;
			goto end;
		}

		for (int j = 0; j < num_pages[i]; j++) {
			const uint64_t page_gpa = vq_ctx->meta[i].gpa + (j * XEN_PAGE_SIZE);
			struct gnttab_map_grant_ref op = {
				.host_addr = (uintptr_t)vq_ctx->meta[i].buf + (j * XEN_PAGE_SIZE),
				.flags = GNTMAP_host_map,
				.ref = (page_gpa & ~XEN_GRANT_ADDR_OFF) >> 12,
				.dom = data->fe.domid,
			};

			if (!(page_gpa & XEN_GRANT_ADDR_OFF)) {
				LOG_ERR("%s: q=%u: addr missing grant marker: 0x%" PRIx64, __func__,
					queue_id, page_gpa);
				return -EINVAL;
			}

			ret = gnttab_map_refs(&op, 1);
			if (ret < 0 || op.status != GNTST_okay) {
				LOG_ERR("Failed to map %d page %d: %d sts=%d", i, j, ret,
					op.status);
				ret = -EINVAL;
				goto end;
			}

			vq_ctx->meta[i].unmap[j].host_addr = op.host_addr;
			vq_ctx->meta[i].unmap[j].dev_bus_addr = op.dev_bus_addr;
			vq_ctx->meta[i].unmap[j].handle = op.handle;
			vq_ctx->meta[i].unmap[j].status = op.status;
		}
	}

end:
	if (ret < 0) {
		reset_queue(dev, queue_id);
		return ret;
	}

	k_spinlock_key_t key = k_spin_lock(&data->lock);

	vq_ctx->meta[0].unmap_count = num_pages[0];
	vq_ctx->meta[1].unmap_count = num_pages[1];
	vq_ctx->meta[2].unmap_count = num_pages[2];

	for (int i = 0; i < config->queue_size_max; i++) {
		vq_ctx->chains[i].pages = NULL;
		vq_ctx->chains[i].max_descriptors = 1;
		vq_ctx->chains[i].chain_head = -1;
	}

	k_spin_unlock(&data->lock, key);

	return 0;
}

/**
 * @brief Reset the device status and clean up all resources
 *
 * Resets all device registers to their initial state, cleans up
 * all page pools and queue mappings, and prepares the device
 * for reinitialization.
 *
 * @param dev VHost device instance
 */
static void reset_device(const struct device *dev)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	data->driver_features = 0;
	data->device_features_sel = 0;
	data->driver_features_sel = 0;
	atomic_set(&data->irq_status, 0);
	atomic_set(&data->status, 0);
	atomic_set(&data->queue_sel, 0);

	for (int i = 0; i < config->num_queues; i++) {
		reset_queue(dev, i);
	}
}

static void ioreq_server_read_req(const struct device *dev, struct ioreq *r)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	LOG_DBG("count %u: size: %u vp_eport: %u state: %d df: %d type: %d", r->count, r->size,
		r->vp_eport, r->state, r->df, r->type);

	switch (r->addr - data->fe.base) {
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
		if (data->device_features_sel == 0) {
			r->data = (config->device_features & UINT32_MAX);
		} else if (data->device_features_sel == 1) {
			r->data = (config->device_features >> 32);
		} else {
			r->data = 0;
		}
	} break;
	case VIRTIO_MMIO_DRIVER_FEATURES: {
		if (data->driver_features_sel == 0) {
			r->data = (data->driver_features & UINT32_MAX);
		} else if (data->driver_features_sel == 1) {
			r->data = (data->driver_features >> 32);
		} else {
			r->data = 0;
		}
	} break;
	case VIRTIO_MMIO_QUEUE_SIZE_MAX: {
		r->data = config->queue_size_max;
	} break;
	case VIRTIO_MMIO_STATUS: {
		r->data = atomic_get(&data->status);
	} break;
	case VIRTIO_MMIO_INTERRUPT_STATUS: {
		r->data = atomic_clear(&data->irq_status);
	} break;
	case VIRTIO_MMIO_QUEUE_READY: {
		r->data = vhost_queue_ready(dev, atomic_get(&data->queue_sel));
	} break;
	default: {
		r->data = -1;
	} break;
	}

	LOG_DBG("r/%s %" PRIx64, virtio_reg_name(r->addr - data->fe.base), r->data);
}

static void ioreq_server_write_req(const struct device *dev, struct ioreq *r)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	const size_t addr_offset = r->addr - data->fe.base;

	LOG_DBG("w/%s %" PRIx64, virtio_reg_name(addr_offset), r->data);

	switch (addr_offset) {
	case VIRTIO_MMIO_DEVICE_FEATURES_SEL: {
		if (r->data == 0 || r->data == 1) {
			data->device_features_sel = (uint8_t)r->data;
		}
	} break;
	case VIRTIO_MMIO_DRIVER_FEATURES_SEL: {
		if (r->data == 0 || r->data == 1) {
			data->driver_features_sel = (uint8_t)r->data;
		}
	} break;
	case VIRTIO_MMIO_DRIVER_FEATURES: {
		if (data->driver_features_sel == 0) {
			uint64_t *drvfeats = &data->driver_features;

			*drvfeats = (r->data | (*drvfeats & 0xFFFFFFFF00000000));
		} else {
			uint64_t *drvfeats = &data->driver_features;

			*drvfeats = ((r->data << 32) | (*drvfeats & UINT32_MAX));
		}
	} break;
	case VIRTIO_MMIO_INTERRUPT_ACK: {
		if (r->data) {
			atomic_and(&data->irq_status, ~r->data);
		}
	} break;
	case VIRTIO_MMIO_STATUS: {
		if (r->data & BIT(DEVICE_STATUS_FEATURES_OK)) {
			const bool ok = ((data->driver_features & ~config->device_features) == 0);

			if (ok) {
				atomic_or(&data->status, BIT(DEVICE_STATUS_FEATURES_OK));
			} else {
				LOG_ERR("%" PRIx64 " d driver_feats=%" PRIx64
					" device_feats=%" PRIx64,
					r->data, data->driver_features, config->device_features);
				atomic_or(&data->status, BIT(DEVICE_STATUS_FAILED));
				atomic_and(&data->status, ~BIT(DEVICE_STATUS_FEATURES_OK));
			}
		} else if (r->data == 0) {
			reset_device(dev);
		} else {
			atomic_or(&data->status, r->data);
		}
	} break;
	case VIRTIO_MMIO_QUEUE_DESC_LOW:
	case VIRTIO_MMIO_QUEUE_DESC_HIGH:
	case VIRTIO_MMIO_QUEUE_AVAIL_LOW:
	case VIRTIO_MMIO_QUEUE_AVAIL_HIGH:
	case VIRTIO_MMIO_QUEUE_USED_LOW:
	case VIRTIO_MMIO_QUEUE_USED_HIGH: {
		const uint16_t queue_id = atomic_get(&data->queue_sel);

		if (queue_id < config->num_queues) {
			const size_t part = (addr_offset - VIRTIO_MMIO_QUEUE_DESC_LOW) / 0x10;
			const bool hi = !!((addr_offset - VIRTIO_MMIO_QUEUE_DESC_LOW) % 0x10);
			uint64_t *p_gpa = &data->vq_ctx[queue_id].meta[part].gpa;

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
		const uint16_t queue_sel = atomic_get(&data->queue_sel);

		if (queue_sel < config->num_queues) {
			const bool is_pow2 = (POPCOUNT((int)r->data) == 1);

			if (r->data > 0 && r->data <= config->queue_size_max && is_pow2) {
				k_spinlock_key_t key = k_spin_lock(&data->lock);

				data->vq_ctx[queue_sel].queue_size = r->data;
				k_spin_unlock(&data->lock, key);
			} else {
				LOG_ERR("queue_size %" PRIu64 " invalid queue size %u", r->data,
					config->queue_size_max);
				atomic_or(&data->status, BIT(DEVICE_STATUS_FAILED));
			}
		}
	} break;
	case VIRTIO_MMIO_QUEUE_READY: {
		const uint16_t queue_sel = atomic_get(&data->queue_sel);
		const uint16_t queue_id = queue_sel;

		if (r->data == 0) {
			reset_queue(dev, queue_id);
		} else if (r->data && (queue_sel < config->num_queues)) {
			int err = setup_queue(dev, queue_id);

			if (err < 0) {
				atomic_or(&data->status, BIT(DEVICE_STATUS_FAILED));
				LOG_ERR("queue%u setup failed: %d", queue_sel, err);
			} else if (data->ready_callback.cb) {
				data->ready_callback.cb(dev, queue_id, data->ready_callback.data);
			}
		}
	} break;
	case VIRTIO_MMIO_QUEUE_SEL: {
		atomic_set(&data->queue_sel, r->data);
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

static void xs_notify_handler(char *buf, size_t len, void *param)
{
	const struct device *dev = param;
	struct vhost_xen_mmio_data *data = dev->data;
	struct xsd_sockmsg *hdr = (void *)buf;

	LOG_DBG("%s: %p %zu %d", __func__, buf, len, hdr->type);

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

	if (vq_ctx && vq_ctx->notify_callback.cb) {
		vq_ctx->notify_callback.cb(dev, queue_id, vq_ctx->notify_callback.data);
	}
}

static void ready_workhandler(struct k_work *work)
{
	const struct k_work_delayable *delayable = k_work_delayable_from_work(work);
	struct vhost_xen_mmio_data *data =
		CONTAINER_OF(delayable, struct vhost_xen_mmio_data, ready_work);
	const struct device *dev = data->dev;
	const struct vhost_xen_mmio_config *config = dev->config;

	for (int i = 0; i < config->num_queues; i++) {
		bool notified = atomic_get(&data->vq_ctx[i].notified);

		if (vhost_queue_ready(dev, i) && data->ready_callback.cb && !notified) {
			data->ready_callback.cb(dev, i, data->ready_callback.data);
			atomic_set(&data->vq_ctx[i].notified, 1);
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
	char baseaddr[HEX_64BIT_DIGITS + 3];
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

	struct query_param params[] = {{
		.key = "base",
		.expected = baseaddr,
	}};

	ret = query_virtio_backend(&data->xs, params, ARRAY_SIZE(params), &data->fe.domid,
				   &data->fe.deviceid);
	if (ret < 0) {
		LOG_INF("%s: failed %d", __func__, ret);
		goto retry;
	}

	data->fe.base = config->base;
	data->fe.irq = query_irq(&data->xs, data->fe.domid, data->fe.deviceid);
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

	ret = dmop_create_ioreq_server(data->fe.domid, HVM_IOREQSRV_BUFIOREQ_OFF, &data->fe.servid);
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

	LOG_INF("%s: backend ready base=%lx fe.domid=%d irq=%d vcpus=%d shared_iopage=%p "
		"ioserv_port=%d",
		dev->name, data->fe.base, data->fe.domid, data->fe.irq, data->vcpus,
		data->shared_iopage, data->ioserv_port);

	atomic_set(&data->initialized, 1);

	ret = 0;

retry:
	if (ret < 0) {
		const uint32_t retry_count = atomic_inc(&data->retry);

		reset_device(dev);
		k_work_schedule_for_queue(&data->workq, &data->init_work,
					  K_MSEC(RETRY_DELAY_BASE_MS * (1 << retry_count)));
	}

	LOG_INF("exit work_inithandler: %d", ret);
}

static bool vhost_xen_mmio_queue_is_ready(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	const struct vhost_xen_mmio_data *data = dev->data;
	const struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID%s", "");
		return false;
	}

	if (vq_ctx->queue_size == 0) {
		return false;
	}

	for (size_t i = 0; i < NUM_OF_VIRTQ_PARTS; i++) {
		if (vq_ctx->meta[i].buf == NULL) {
			return false;
		}

		for (size_t j = 0; j < vq_ctx->meta[i].unmap_count; j++) {
			if (vq_ctx->meta[i].unmap[j].status != GNTST_okay) {
				return false;
			}
		}
	}

	return vq_ctx->meta[DESC].gpa != 0 && vq_ctx->meta[AVAIL].gpa != 0 &&
	       vq_ctx->meta[USED].gpa != 0;
}

static int vhost_xen_mmio_get_virtq(const struct device *dev, uint16_t queue_id, void **parts,
				    size_t *queue_size)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	const struct vhost_xen_mmio_data *data = dev->data;
	const struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID%s", "");
		return -EINVAL;
	}

	if (!vhost_xen_mmio_queue_is_ready(dev, queue_id)) {
		LOG_ERR_Q("is not ready%s", "");
		return -ENODEV;
	}

	for (int i = 0; i < NUM_OF_VIRTQ_PARTS; i++) {
		parts[i] = vq_ctx->meta[i].buf + (vq_ctx->meta[i].gpa & (XEN_PAGE_SIZE - 1));
	}

	if (!parts[DESC] || !parts[AVAIL] || !parts[USED]) {
		LOG_ERR("%s: q=%u: failed to get ring base addresses", __func__, queue_id);
		return -EINVAL;
	}

	*queue_size = vq_ctx->queue_size;

	LOG_DBG("%s: q=%u: rings desc=%p, avail=%p, used=%p, size=%zu", __func__, queue_id,
		parts[DESC], parts[AVAIL], parts[USED], *queue_size);

	return 0;
}

static int vhost_xen_mmio_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	const struct vhost_xen_mmio_data *data = dev->data;

	*drv_feats = data->driver_features;

	return 0;
}

static int vhost_xen_mmio_queue_notify(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID%s", "");
		return -EINVAL;
	}

	atomic_or(&data->irq_status, VIRTIO_QUEUE_INTERRUPT);

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

	atomic_or(&data->status, status);
	atomic_or(&data->irq_status, VIRTIO_DEVICE_CONFIGURATION_INTERRUPT);

	dmop_set_irq_level(data->fe.domid, data->fe.irq, 1);
	dmop_set_irq_level(data->fe.domid, data->fe.irq, 0);

	return 0;
}

static int vhost_xen_mmio_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	int ret;

	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID%s", "");
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	if (head >= vq_ctx->queue_size) {
		LOG_ERR_Q("Invalid head: head=%u >= queue_size=%zu", head, vq_ctx->queue_size);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	if (vq_ctx->chains[head].chain_head != head) {
		LOG_ERR_Q("Head not in use: head=%u (stored=%d)", head,
			  vq_ctx->chains[head].chain_head);
		k_spin_unlock(&data->lock, key);
		return -EINVAL;
	}

	const size_t count = vq_ctx->chains[head].pages->unmap_count;

	k_spin_unlock(&data->lock, key);

	if (count == 0) {
		LOG_DBG("%s: q=%u: No buffers to unmap", __func__, queue_id);
		return 0;
	}

	ret = unmap_pages(vq_ctx->chains[head].pages);
	if (ret < 0) {
		LOG_ERR_Q("gnttab_unmap_refs failed: %d", ret);
	}

	key = k_spin_lock(&data->lock);
	vq_ctx->chains[head].chain_head = -1;
	vq_ctx->chains[head].pages->unmap_count = 0;

#if 0
	/* Free the buffers inside pages before freeing pages structure */
	if (vq_ctx->chains[head].pages->buf) {
		gnttab_put_pages(vq_ctx->chains[head].pages->buf,
				 vq_ctx->chains[head].pages->unmap_pages);
		vq_ctx->chains[head].pages->buf = NULL;
	}
	if (vq_ctx->chains[head].pages->unmap) {
		k_free(vq_ctx->chains[head].pages->unmap);
		vq_ctx->chains[head].pages->unmap = NULL;
	}

	k_free(vq_ctx->chains[head].pages);
	vq_ctx->chains[head].pages = NULL;
#endif
	k_spin_unlock(&data->lock, key);

	return ret;
}

static int vhost_xen_mmio_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
					const struct vhost_gpa_range *ranges, size_t range_count,
					struct vhost_iovec *read_iovec, size_t max_read_iovecs,
					struct vhost_iovec *write_iovec, size_t max_write_iovecs,
					size_t *read_count, size_t *write_count)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];
	struct descriptor_chain *dchain = &vq_ctx->chains[head];
	int ret = 0;
	struct gnttab_map_grant_ref *map_grant = NULL;
	uint16_t total_pages = 0;
	const size_t max_iovecs = max_read_iovecs + max_write_iovecs;
	size_t read_iovec_count = 0, write_iovec_count = 0;

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID%s", "");
		return -EINVAL;
	}

	for (size_t i = 0; i < range_count; i++) {
		const uint64_t gpa = ranges[i].gpa;
		const size_t len = ranges[i].len;
		const uint64_t start_page = gpa >> 12;
		const uint64_t end_page = (gpa + len - 1) >> 12;

		if (!(gpa & XEN_GRANT_ADDR_OFF)) {
			LOG_ERR_Q("addr missing grant marker: 0x%" PRIx64, gpa);
			return -EINVAL;
		}

		total_pages += (uint16_t)(end_page - start_page + 1);
	}

	if (total_pages == 0) {
		*read_count = 0;
		*write_count = 0;

		return 0;
	}

	LOG_DBG("%s: q=%u: range_count=%zu total_pages=%u max_read=%zu max_write=%zu", __func__,
		queue_id, range_count, total_pages, max_read_iovecs, max_write_iovecs);

	if (head >= vq_ctx->queue_size) {
		LOG_ERR("Invalid head: head=%u >= queue_size=%zu", head, vq_ctx->queue_size);
		return -EINVAL;
	}

	if (vq_ctx->chains[head].chain_head == head) {
		LOG_WRN("Found unreleased head: %d", head);

		ret = vhost_xen_mmio_release_iovec(dev, queue_id, head);
		if (ret < 0) {
			LOG_ERR("%s: vhost_xen_mmio_release_iovec: q=%u: failed %d", __func__,
				queue_id, ret);
			return ret;
		}
	}

	/* Allocate or reallocate buffer based on total_pages (handles both initial allocation and
	 * expansion) */
	if (vq_ctx->chains[head].pages == NULL ||
	    vq_ctx->chains[head].pages->unmap_pages < total_pages) {

		ret = allocate_chain_buffer(dev, queue_id, head, total_pages);
		if (ret < 0) {
			LOG_ERR_Q("allocate_chain_buffer failed: %d", ret);
			return ret;
		}
	}

	/* Additional check for allocation success and consistency */
	if (vq_ctx->chains[head].pages->unmap_pages < total_pages) {
		LOG_ERR_Q("Chain buffer allocation inconsistent: allocated=%zu needed=%d",
			  vq_ctx->chains[head].pages->unmap_pages, total_pages);
		return -ENOMEM;
	}

	map_grant = k_malloc(sizeof(struct gnttab_map_grant_ref) * max_iovecs);
	if (!map_grant) {
		LOG_ERR_Q("k_malloc failed%s", "");
		return -ENOMEM;
	}

	k_spinlock_key_t key = k_spin_lock(&data->lock);

	const int iovec_count = setup_iovec_mappings(
		dchain, data->fe.domid, ranges, range_count, read_iovec, max_read_iovecs, write_iovec,
		max_write_iovecs, map_grant, &read_iovec_count, &write_iovec_count);

	k_spin_unlock(&data->lock, key);

	if (iovec_count < 0) {
		LOG_ERR_Q("setup_iovec_mappings failed: %d", iovec_count);
		ret = iovec_count;
		goto cleanup;
	}

	ret = gnttab_map_refs(map_grant, iovec_count);
	if (ret < 0) {
		LOG_ERR("gnttab_map_refs failed: %d", ret);
		goto cleanup;
	}

	ret = setup_unmap_info(dchain, ranges, range_count, map_grant, iovec_count);
	if (ret < 0) {
		LOG_ERR("setup_unmap_info failed: %d", ret);
		goto cleanup;
	}

	vq_ctx->chains[head].chain_head = head;

	*read_count = read_iovec_count;
	*write_count = write_iovec_count;

cleanup:
	if (map_grant) {
		k_free(map_grant);
	}
	return ret;
}

static int vhost_xen_mmio_set_ready_callback(const struct device *dev,
					     void (*callback)(const struct device *dev,
							      uint16_t queue_id, void *user_data),
					     void *user_data)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;

	data->ready_callback.cb = callback;
	data->ready_callback.data = user_data;

	for (int i = 0; i < config->num_queues; i++) {
		atomic_set(&data->vq_ctx[i].notified, 0);
	}

	k_work_schedule_for_queue(&data->workq, &data->ready_work, K_NO_WAIT);

	return 0;
}

static int vhost_xen_mmio_set_notify_callback(const struct device *dev, uint16_t queue_id,
					      void (*callback)(const struct device *dev,
							       uint16_t queue_id, void *user_data),
					      void *user_data)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	struct virtq_context *vq_ctx = &data->vq_ctx[queue_id];

	if (queue_id >= config->num_queues) {
		LOG_ERR_Q("Invalid queue ID%s", "");
		return -EINVAL;
	}

	vq_ctx->notify_callback.cb = callback;
	vq_ctx->notify_callback.data = user_data;

	return 0;
}

static const struct vhost_controller_api vhost_driver_xen_mmio_api = {
	.is_ready = vhost_xen_mmio_queue_is_ready,
	.get_virtq = vhost_xen_mmio_get_virtq,
	.get_driver_features = vhost_xen_mmio_get_driver_features,
	.set_device_status = vhost_xen_mmio_set_device_status,
	.queue_notify = vhost_xen_mmio_queue_notify,
	.prepare_iovec = vhost_xen_mmio_prepare_iovec,
	.release_iovec = vhost_xen_mmio_release_iovec,
	.set_ready_callback = vhost_xen_mmio_set_ready_callback,
	.set_notify_callback = vhost_xen_mmio_set_notify_callback,
};

static int vhost_xen_mmio_init(const struct device *dev)
{
	const struct vhost_xen_mmio_config *config = dev->config;
	struct vhost_xen_mmio_data *data = dev->data;
	char buf[65] = {0};
	int ret;

	data->dev = dev;

	/* Initialize atomic fields */
	atomic_set(&data->initialized, 0);
	atomic_set(&data->retry, 0);
	atomic_set(&data->status, 0);
	atomic_set(&data->queue_sel, 0);
	atomic_set(&data->irq_status, 0);
	atomic_set(&data->notify_queue_id, 0);

	k_work_init_delayable(&data->init_work, init_workhandler);
	k_work_init_delayable(&data->isr_work, isr_workhandler);
	k_work_init_delayable(&data->ready_work, ready_workhandler);
	k_work_queue_init(&data->workq);
	k_work_queue_start(&data->workq, config->workq_stack,
			   K_THREAD_STACK_SIZEOF(config->workq_stack), config->workq_priority,
			   NULL);

	xen_events_init();
	xs_set_notify_callback(xs_notify_handler, (void *)dev);
	ret = xs_init_xenstore(&data->xs);

	if (ret < 0) {
		LOG_INF("xs_init_xenstore failed: %d", ret);
		return ret;
	}

	ret = xs_watch(&data->xs, "backend/virtio", "watch", buf, ARRAY_SIZE(buf) - 1);

	if (ret < 0) {
		LOG_INF("xs_watch failed: %d", ret);
		return ret;
	}

	return 0;
}

#define VQCTX_INIT(n, idx)                                                                         \
	{                                                                                          \
		.chains = vhost_xen_mmio_vq_ctx_chains_##idx[n],                                   \
	}

#define Q_NUM(idx)    DT_INST_PROP_OR(idx, num_queues, 1)
#define Q_SZ_MAX(idx) DT_INST_PROP_OR(idx, queue_size_max, 1)

#define VHOST_XEN_MMIO_INST(idx)                                                                   \
	static K_THREAD_STACK_DEFINE(workq_stack_##idx, DT_INST_PROP_OR(idx, stack_size, 1024));   \
	static const struct vhost_xen_mmio_config vhost_xen_mmio_config_##idx = {                  \
		.queue_size_max = DT_INST_PROP_OR(idx, queue_size_max, 1),                         \
		.num_queues = DT_INST_PROP_OR(idx, num_queues, 1),                                 \
		.device_id = DT_INST_PROP(idx, device_id),                                         \
		.vendor_id = DT_INST_PROP_OR(idx, vendor_id, 0),                                   \
		.base = DT_INST_PROP(idx, base),                                                   \
		.reg_size = XEN_PAGE_SIZE,                                                         \
		.workq_stack = (k_thread_stack_t *)&workq_stack_##idx,                             \
		.workq_priority = DT_INST_PROP_OR(idx, priority, 0),                               \
		.device_features = BIT(VIRTIO_F_VERSION_1) | BIT(VIRTIO_F_ACCESS_PLATFORM),        \
	};                                                                                         \
	struct descriptor_chain vhost_xen_mmio_vq_ctx_chains_##idx[Q_NUM(idx)][Q_SZ_MAX(idx)];     \
	static struct virtq_context vhost_xen_mmio_vq_ctx_##idx[Q_NUM(idx)] = {                    \
		LISTIFY(Q_NUM(idx), VQCTX_INIT, (,), idx),                                            \
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
