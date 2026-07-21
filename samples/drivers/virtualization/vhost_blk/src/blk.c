/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>
#include <string.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(vhost_blk);

#define VIRTIO_BLK_SECTOR_SIZE  512U
#define VIRTIO_BLK_SECTOR_COUNT 2048U
#define VIRTIO_BLK_HEADER_SIZE  16U
#define VIRTIO_BLK_IOV_COUNT    16U

#define VIRTIO_BLK_T_IN    0U
#define VIRTIO_BLK_T_OUT   1U
#define VIRTIO_BLK_T_FLUSH 4U

#define VIRTIO_BLK_S_OK     0U
#define VIRTIO_BLK_S_IOERR  1U
#define VIRTIO_BLK_S_UNSUPP 2U

static uint8_t disk[VIRTIO_BLK_SECTOR_COUNT * VIRTIO_BLK_SECTOR_SIZE] __aligned(4);
static struct vhost_iovec read_iovecs[VIRTIO_BLK_IOV_COUNT];
static struct vhost_iovec write_iovecs[VIRTIO_BLK_IOV_COUNT];
static struct vhost_buf desc_bufs[DT_PROP(DT_NODELABEL(blk), queue_size_max)];
static struct vringh_iov read_iov = {
	.iov = read_iovecs,
	.max_num = ARRAY_SIZE(read_iovecs),
};
static struct vringh_iov write_iov = {
	.iov = write_iovecs,
	.max_num = ARRAY_SIZE(write_iovecs),
};
static struct vringh blk_vring;

static size_t iov_length(const struct vringh_iov *iov)
{
	size_t total = 0;

	for (size_t i = 0; i < iov->used; i++) {
		if (iov->iov[i].iov_len > SIZE_MAX - total) {
			return SIZE_MAX;
		}

		total += iov->iov[i].iov_len;
	}

	return total;
}

static int iov_copy_from(const struct vringh_iov *iov, size_t offset, void *dst, size_t length)
{
	uint8_t *out = dst;

	for (size_t i = 0; i < iov->used && length > 0; i++) {
		const size_t iov_len = iov->iov[i].iov_len;

		if (offset >= iov_len) {
			offset -= iov_len;
			continue;
		}

		const size_t chunk = MIN(length, iov_len - offset);

		memcpy(out, (const uint8_t *)iov->iov[i].iov_base + offset, chunk);
		out += chunk;
		length -= chunk;
		offset = 0;
	}

	return length == 0 ? 0 : -EINVAL;
}

static int iov_copy_to(const struct vringh_iov *iov, size_t offset, const void *src, size_t length)
{
	const uint8_t *in = src;

	for (size_t i = 0; i < iov->used && length > 0; i++) {
		const size_t iov_len = iov->iov[i].iov_len;

		if (offset >= iov_len) {
			offset -= iov_len;
			continue;
		}

		const size_t chunk = MIN(length, iov_len - offset);

		memcpy((uint8_t *)iov->iov[i].iov_base + offset, in, chunk);
		in += chunk;
		length -= chunk;
		offset = 0;
	}

	return length == 0 ? 0 : -EINVAL;
}

static int process_rw_request(bool write, uint64_t sector, size_t data_length)
{
	const uint64_t sectors = data_length / VIRTIO_BLK_SECTOR_SIZE;

	if ((data_length % VIRTIO_BLK_SECTOR_SIZE) != 0 ||
	    sector > VIRTIO_BLK_SECTOR_COUNT ||
	    sectors > VIRTIO_BLK_SECTOR_COUNT - sector) {
		return -EINVAL;
	}

	for (uint64_t i = 0; i < sectors; i++) {
		uint8_t *sector_data = &disk[(sector + i) * VIRTIO_BLK_SECTOR_SIZE];
		size_t offset = i * VIRTIO_BLK_SECTOR_SIZE;
		int ret;

		if (write) {
			ret = iov_copy_from(&read_iov, VIRTIO_BLK_HEADER_SIZE + offset,
					    sector_data, VIRTIO_BLK_SECTOR_SIZE);
		} else {
			ret = iov_copy_to(&write_iov, offset, sector_data,
					  VIRTIO_BLK_SECTOR_SIZE);
		}

		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static uint32_t process_request(void)
{
	uint8_t header[VIRTIO_BLK_HEADER_SIZE];
	const size_t read_length = iov_length(&read_iov);
	const size_t write_length = iov_length(&write_iov);
	uint8_t status = VIRTIO_BLK_S_IOERR;
	uint32_t used_length = sizeof(status);
	uint32_t type;
	uint64_t sector;
	int ret = -EINVAL;

	if (write_length < sizeof(status)) {
		LOG_ERR("Malformed request: readable=%zu writable=%zu",
			read_length, write_length);
		return 0;
	}

	if (read_length < sizeof(header) ||
	    iov_copy_from(&read_iov, 0, header, sizeof(header)) < 0) {
		LOG_ERR("Malformed request header");
		goto write_status;
	}

	type = sys_get_le32(&header[0]);
	sector = sys_get_le64(&header[8]);

	switch (type) {
	case VIRTIO_BLK_T_IN: {
		const size_t data_length = write_length - sizeof(status);

		ret = process_rw_request(false, sector, data_length);
		if (ret == 0 && data_length <= UINT32_MAX - sizeof(status)) {
			status = VIRTIO_BLK_S_OK;
			used_length = data_length + sizeof(status);
		}
		break;
	}
	case VIRTIO_BLK_T_OUT:
		ret = process_rw_request(true, sector, read_length - sizeof(header));
		if (ret == 0) {
			status = VIRTIO_BLK_S_OK;
		}
		break;
	case VIRTIO_BLK_T_FLUSH:
		ret = 0;
		status = VIRTIO_BLK_S_OK;
		break;
	default:
		LOG_WRN("Unsupported request type %u", type);
		status = VIRTIO_BLK_S_UNSUPP;
		break;
	}

	if (status == VIRTIO_BLK_S_IOERR) {
		LOG_ERR("Request type %u sector %" PRIu64 " failed: %d", type, sector, ret);
	}

write_status:
	if (iov_copy_to(&write_iov, write_length - sizeof(status),
			&status, sizeof(status)) < 0) {
		return 0;
	}

	return used_length;
}

static void blk_kick_handler(struct vringh *vrh)
{
	uint16_t head;

	while (true) {
		int ret = vringh_getdesc(vrh, &read_iov, &write_iov, &head);

		if (ret < 0) {
			LOG_ERR("vringh_getdesc failed: %d", ret);
			return;
		}

		if (ret == 0) {
			return;
		}

		const uint32_t used_length = process_request();

		barrier_dmem_fence_full();
		ret = vringh_complete(vrh, head, used_length);
		if (ret < 0) {
			LOG_ERR("vringh_complete failed: %d", ret);
			return;
		}

		if (vringh_need_notify(vrh) > 0) {
			vringh_notify(vrh);
		}

		vringh_iov_reset(&read_iov);
		vringh_iov_reset(&write_iov);
	}
}

void blk_queue_ready(const struct device *dev, uint16_t qid, void *data)
{
	int ret;

	ARG_UNUSED(data);

	if (qid != 0U) {
		LOG_ERR("Unexpected queue %u", qid);
		return;
	}

	vringh_iov_init(&read_iov, read_iov.iov, read_iov.max_num);
	vringh_iov_init(&write_iov, write_iov.iov, write_iov.max_num);

	ret = vringh_init_device(&blk_vring, dev, qid, desc_bufs, ARRAY_SIZE(desc_bufs),
				 blk_kick_handler);
	if (ret < 0) {
		LOG_ERR("vringh_init_device failed: %d", ret);
		return;
	}

	LOG_INF("RAM backend ready: %u sectors", VIRTIO_BLK_SECTOR_COUNT);
}
