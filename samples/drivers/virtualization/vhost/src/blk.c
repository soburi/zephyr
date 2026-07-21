/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <inttypes.h>

#include <zephyr/devicetree.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/fs/fs.h>
#include <zephyr/logging/log.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(vhost_blk);

#define VHOST_BLK_NODE    DT_NODELABEL(blk)
#define VHOST_BLK_BACKEND DT_PHANDLE(DT_PATH(zephyr_user), vhost_blk_backend)

#define VIRTIO_BLK_SECTOR_SIZE 512U
#define VIRTIO_BLK_HEADER_SIZE 16U
#define VIRTIO_BLK_IOV_COUNT   16U

#define VIRTIO_BLK_T_IN    0U
#define VIRTIO_BLK_T_OUT   1U
#define VIRTIO_BLK_T_FLUSH 4U

#define VIRTIO_BLK_S_OK     0U
#define VIRTIO_BLK_S_IOERR  1U
#define VIRTIO_BLK_S_UNSUPP 2U

#define VHOST_BLK_CONFIG_CAPACITY                                                                  \
	((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 0) |                                \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 1) << 8) |                         \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 2) << 16) |                        \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 3) << 24) |                        \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 4) << 32) |                        \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 5) << 40) |                        \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 6) << 48) |                        \
	 ((uint64_t)DT_PROP_BY_IDX(VHOST_BLK_NODE, config_data, 7) << 56))

#define VHOST_BLK_SECTOR_COUNT DT_PROP(VHOST_BLK_BACKEND, sector_count)

BUILD_ASSERT(DT_NODE_HAS_STATUS(VHOST_BLK_NODE, okay), "the vhost blk node must be enabled");
BUILD_ASSERT(DT_NODE_HAS_PROP(DT_PATH(zephyr_user), vhost_blk_backend),
	     "zephyr,user must select vhost-blk-backend");
BUILD_ASSERT(DT_NODE_HAS_STATUS(VHOST_BLK_BACKEND, okay),
	     "the selected vhost-blk-backend must be enabled");
BUILD_ASSERT(DT_PROP_LEN(VHOST_BLK_NODE, config_data) >= sizeof(uint64_t),
	     "virtio-blk config-data must contain the 64-bit capacity");
BUILD_ASSERT(VHOST_BLK_CONFIG_CAPACITY == VHOST_BLK_SECTOR_COUNT,
	     "virtio-blk config capacity must match backend sector-count");
BUILD_ASSERT(VHOST_BLK_SECTOR_COUNT > 0, "virtio-blk backend must not be empty");

struct vhost_blk_backend_api {
	int (*init)(void);
	int (*read)(uint32_t sector, uint8_t *buf);
	int (*write)(uint32_t sector, const uint8_t *buf);
	int (*flush)(void);
	const char *name;
};

static struct vhost_iovec read_iovecs[VIRTIO_BLK_IOV_COUNT];
static struct vhost_iovec write_iovecs[VIRTIO_BLK_IOV_COUNT];

static struct vringh_iov read_iov = {
	.iov = read_iovecs,
	.max_num = ARRAY_SIZE(read_iovecs),
};

static struct vringh_iov write_iov = {
	.iov = write_iovecs,
	.max_num = ARRAY_SIZE(write_iovecs),
};

static struct vringh blk_vring;
static bool backend_initialized;

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

#if DT_NODE_HAS_COMPAT(VHOST_BLK_BACKEND, zephyr_vhost_blk_memory)

static uint8_t memory_backend[VHOST_BLK_SECTOR_COUNT * VIRTIO_BLK_SECTOR_SIZE] __aligned(4);

static int backend_init(void)
{
	return 0;
}

static int backend_read(uint32_t sector, uint8_t *buf)
{
	memcpy(buf, &memory_backend[sector * VIRTIO_BLK_SECTOR_SIZE], VIRTIO_BLK_SECTOR_SIZE);
	return 0;
}

static int backend_write(uint32_t sector, const uint8_t *buf)
{
	memcpy(&memory_backend[sector * VIRTIO_BLK_SECTOR_SIZE], buf, VIRTIO_BLK_SECTOR_SIZE);
	return 0;
}

static int backend_flush(void)
{
	return 0;
}

#define VHOST_BLK_BACKEND_NAME "memory"

#elif DT_NODE_HAS_COMPAT(VHOST_BLK_BACKEND, zephyr_vhost_blk_disk)

#define VHOST_BLK_DISK_NAME DT_PROP(VHOST_BLK_BACKEND, disk_name)

static int backend_init(void)
{
	uint32_t sector_count;
	uint32_t sector_size;
	int ret;

	ret = disk_access_init(VHOST_BLK_DISK_NAME);
	if (ret < 0) {
		return ret;
	}

	ret = disk_access_ioctl(VHOST_BLK_DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret < 0) {
		return ret;
	}

	ret = disk_access_ioctl(VHOST_BLK_DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (ret < 0) {
		return ret;
	}

	if (sector_size != VIRTIO_BLK_SECTOR_SIZE || sector_count < VHOST_BLK_SECTOR_COUNT) {
		LOG_ERR("disk %s has %u sectors of %u bytes; need at least %u sectors of %u bytes",
			VHOST_BLK_DISK_NAME, sector_count, sector_size, VHOST_BLK_SECTOR_COUNT,
			VIRTIO_BLK_SECTOR_SIZE);
		return -EINVAL;
	}

	return 0;
}

static int backend_read(uint32_t sector, uint8_t *buf)
{
	return disk_access_read(VHOST_BLK_DISK_NAME, buf, sector, 1);
}

static int backend_write(uint32_t sector, const uint8_t *buf)
{
	return disk_access_write(VHOST_BLK_DISK_NAME, buf, sector, 1);
}

static int backend_flush(void)
{
	return disk_access_ioctl(VHOST_BLK_DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
}

#define VHOST_BLK_BACKEND_NAME "disk " VHOST_BLK_DISK_NAME

#elif DT_NODE_HAS_COMPAT(VHOST_BLK_BACKEND, zephyr_vhost_blk_file)

#define VHOST_BLK_FILE_PATH DT_PROP(VHOST_BLK_BACKEND, file_path)
#define VHOST_BLK_FILE_SIZE ((off_t)VHOST_BLK_SECTOR_COUNT * VIRTIO_BLK_SECTOR_SIZE)

static struct fs_file_t backend_file;

static int backend_init(void)
{
	off_t size;
	int ret;

	fs_file_t_init(&backend_file);
	ret = fs_open(&backend_file, VHOST_BLK_FILE_PATH, FS_O_CREATE | FS_O_RDWR);
	if (ret < 0) {
		return ret;
	}

	ret = fs_seek(&backend_file, 0, FS_SEEK_END);
	if (ret < 0) {
		goto close_file;
	}

	size = fs_tell(&backend_file);
	if (size < 0) {
		ret = (int)size;
		goto close_file;
	}

	if (size < VHOST_BLK_FILE_SIZE) {
		ret = fs_truncate(&backend_file, VHOST_BLK_FILE_SIZE);
		if (ret < 0) {
			goto close_file;
		}

		ret = fs_seek(&backend_file, 0, FS_SEEK_END);
		if (ret < 0) {
			goto close_file;
		}
		size = fs_tell(&backend_file);
		if (size < 0) {
			ret = (int)size;
			goto close_file;
		}
		if (size < VHOST_BLK_FILE_SIZE) {
			ret = -ENOSPC;
			goto close_file;
		}
	}

	return 0;

close_file:
	fs_close(&backend_file);
	fs_file_t_init(&backend_file);
	return ret;
}

static int backend_file_seek(uint32_t sector)
{
	return fs_seek(&backend_file, (off_t)sector * VIRTIO_BLK_SECTOR_SIZE, FS_SEEK_SET);
}

static int backend_read(uint32_t sector, uint8_t *buf)
{
	int ret = backend_file_seek(sector);

	if (ret < 0) {
		return ret;
	}

	ssize_t count = fs_read(&backend_file, buf, VIRTIO_BLK_SECTOR_SIZE);

	return count == VIRTIO_BLK_SECTOR_SIZE ? 0 : (count < 0 ? (int)count : -EIO);
}

static int backend_write(uint32_t sector, const uint8_t *buf)
{
	int ret = backend_file_seek(sector);

	if (ret < 0) {
		return ret;
	}

	ssize_t count = fs_write(&backend_file, buf, VIRTIO_BLK_SECTOR_SIZE);

	return count == VIRTIO_BLK_SECTOR_SIZE ? 0 : (count < 0 ? (int)count : -EIO);
}

static int backend_flush(void)
{
	return fs_sync(&backend_file);
}

#define VHOST_BLK_BACKEND_NAME "file " VHOST_BLK_FILE_PATH

#else
#error "vhost-blk-backend has an unsupported compatible"
#endif

static const struct vhost_blk_backend_api backend = {
	.init = backend_init,
	.read = backend_read,
	.write = backend_write,
	.flush = backend_flush,
	.name = VHOST_BLK_BACKEND_NAME,
};

static int process_rw_request(bool write, uint64_t sector, size_t data_length)
{
	uint8_t sector_buf[VIRTIO_BLK_SECTOR_SIZE];
	const uint64_t sectors = data_length / VIRTIO_BLK_SECTOR_SIZE;

	if ((data_length % VIRTIO_BLK_SECTOR_SIZE) != 0 || sector > VHOST_BLK_SECTOR_COUNT ||
	    sectors > VHOST_BLK_SECTOR_COUNT - sector) {
		return -EINVAL;
	}

	for (uint64_t i = 0; i < sectors; i++) {
		int ret;

		if (write) {
			ret = iov_copy_from(&read_iov,
					    VIRTIO_BLK_HEADER_SIZE + i * VIRTIO_BLK_SECTOR_SIZE,
					    sector_buf, sizeof(sector_buf));
			if (ret == 0) {
				ret = backend.write((uint32_t)(sector + i), sector_buf);
			}
		} else {
			ret = backend.read((uint32_t)(sector + i), sector_buf);
			if (ret == 0) {
				ret = iov_copy_to(&write_iov, i * VIRTIO_BLK_SECTOR_SIZE,
						  sector_buf, sizeof(sector_buf));
			}
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
	uint32_t used_length = 1;
	uint32_t type;
	uint64_t sector;
	int ret = -EINVAL;

	if (write_length < sizeof(status)) {
		LOG_ERR("malformed request: readable=%zu writable=%zu", read_length, write_length);
		return 0;
	}
	if (read_length < sizeof(header) ||
	    iov_copy_from(&read_iov, 0, header, sizeof(header)) < 0) {
		LOG_ERR("malformed request: readable=%zu writable=%zu", read_length, write_length);
		iov_copy_to(&write_iov, write_length - sizeof(status), &status, sizeof(status));
		return used_length;
	}

	type = sys_get_le32(&header[0]);
	sector = sys_get_le64(&header[8]);

	switch (type) {
	case VIRTIO_BLK_T_IN: {
		const size_t data_length = write_length - sizeof(status);

		ret = process_rw_request(false, sector, data_length);
		if (ret == 0) {
			if (data_length <= UINT32_MAX - sizeof(status)) {
				status = VIRTIO_BLK_S_OK;
				used_length = data_length + sizeof(status);
			} else {
				ret = -E2BIG;
			}
		}
		break;
	}
	case VIRTIO_BLK_T_OUT:
		ret = process_rw_request(true, sector, read_length - sizeof(header));
		if (ret == 0) {
			/* Keep completed writes stable even if FLUSH was not negotiated. */
			ret = backend.flush();
		}
		if (ret == 0) {
			status = VIRTIO_BLK_S_OK;
		}
		break;
	case VIRTIO_BLK_T_FLUSH:
		ret = backend.flush();
		if (ret == 0) {
			status = VIRTIO_BLK_S_OK;
		}
		break;
	default:
		LOG_WRN("unsupported request type %u", type);
		status = VIRTIO_BLK_S_UNSUPP;
		break;
	}

	if (status == VIRTIO_BLK_S_IOERR) {
		LOG_ERR("request type %u sector %" PRIu64 " failed: %d", type, sector, ret);
	}

	if (iov_copy_to(&write_iov, write_length - sizeof(status), &status, sizeof(status)) < 0) {
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

	if (qid != 0) {
		LOG_ERR("unexpected queue %u", qid);
		return;
	}

	if (!backend_initialized) {
		ret = backend.init();
		if (ret < 0) {
			LOG_ERR("failed to initialize %s backend: %d", backend.name, ret);
			vhost_set_device_status(dev, BIT(DEVICE_STATUS_FAILED));
			return;
		}
		backend_initialized = true;
		LOG_INF("%s backend ready: %u sectors", backend.name, VHOST_BLK_SECTOR_COUNT);
	}

	vringh_iov_init(&read_iov, read_iov.iov, read_iov.max_num);
	vringh_iov_init(&write_iov, write_iov.iov, write_iov.max_num);

	ret = vringh_init_device(&blk_vring, dev, qid, blk_kick_handler);
	if (ret < 0) {
		LOG_ERR("vringh_init_device failed: %d", ret);
	}
}
