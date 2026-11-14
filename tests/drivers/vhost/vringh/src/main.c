/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/ztest.h>

#define QUEUE_SIZE 8U
#define NUM_IOVECS 8U

/*
 * Mock VHost controller: the fake guest ring lives in ordinary host memory
 * and descriptor addresses are host pointers, so prepare_iovec() is an
 * identity translation.
 */
struct mock_ctrl {
	struct virtq_desc descs[QUEUE_SIZE];
	uint8_t avail_buf[sizeof(struct virtq_avail) + QUEUE_SIZE * sizeof(uint16_t)] __aligned(4);
	uint8_t used_buf[sizeof(struct virtq_used) +
			 QUEUE_SIZE * sizeof(struct virtq_used_elem)] __aligned(4);

	uint32_t prepare_calls;
	uint32_t release_calls;
	uint16_t last_release_head;
	uint32_t last_release_written;
	uint32_t notify_calls;
	uint32_t status;
	int fail_prepare;
};

static struct mock_ctrl ctrl;

#define MOCK_AVAIL ((struct virtq_avail *)ctrl.avail_buf)
#define MOCK_USED  ((struct virtq_used *)ctrl.used_buf)

static int mock_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
			      const struct vhost_buf *bufs, size_t bufs_count,
			      struct vhost_iovec *read_iovec, size_t max_read_iovecs,
			      struct vhost_iovec *write_iovec, size_t max_write_iovecs,
			      size_t *read_count, size_t *write_count)
{
	size_t r = 0;
	size_t w = 0;

	ctrl.prepare_calls++;

	if (ctrl.fail_prepare != 0) {
		return ctrl.fail_prepare;
	}

	for (size_t i = 0; i < bufs_count; i++) {
		struct vhost_iovec *iov;

		if (bufs[i].is_write) {
			if (w >= max_write_iovecs) {
				return -E2BIG;
			}
			iov = &write_iovec[w++];
		} else {
			if (r >= max_read_iovecs) {
				return -E2BIG;
			}
			iov = &read_iovec[r++];
		}

		iov->iov_base = (void *)(uintptr_t)bufs[i].gpa;
		iov->iov_len = bufs[i].len;
	}

	*read_count = r;
	*write_count = w;

	return 0;
}

static int mock_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
			      uint32_t written)
{
	ctrl.release_calls++;
	ctrl.last_release_head = head;
	ctrl.last_release_written = written;

	return 0;
}

static int mock_get_virtq(const struct device *dev, uint16_t queue_id, struct vhost_vring *vring)
{
	vring->num = QUEUE_SIZE;
	vring->desc = ctrl.descs;
	vring->avail = MOCK_AVAIL;
	vring->used = MOCK_USED;

	return 0;
}

static int mock_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	*drv_feats = BIT64(VIRTIO_F_VERSION_1);

	return 0;
}

static bool mock_virtq_is_ready(const struct device *dev, uint16_t queue_id)
{
	return true;
}

static int mock_register_virtq_ready_cb(const struct device *dev,
					void (*callback)(const struct device *dev,
							 uint16_t queue_id, void *data),
					void *data)
{
	return 0;
}

static int mock_register_virtq_notify_cb(const struct device *dev, uint16_t queue_id,
					 void (*callback)(const struct device *dev,
							  uint16_t queue_id, void *data),
					 void *data)
{
	return 0;
}

static int mock_notify_virtq(const struct device *dev, uint16_t queue_id)
{
	ctrl.notify_calls++;

	return 0;
}

static int mock_set_device_status(const struct device *dev, uint32_t status)
{
	ctrl.status |= status;

	return 0;
}

static DEVICE_API(vhost, mock_vhost_api) = {
	.prepare_iovec = mock_prepare_iovec,
	.release_iovec = mock_release_iovec,
	.get_virtq = mock_get_virtq,
	.get_driver_features = mock_get_driver_features,
	.virtq_is_ready = mock_virtq_is_ready,
	.register_virtq_ready_cb = mock_register_virtq_ready_cb,
	.register_virtq_notify_cb = mock_register_virtq_notify_cb,
	.notify_virtq = mock_notify_virtq,
	.set_device_status = mock_set_device_status,
};

DEVICE_DEFINE(mock_vhost, "mock_vhost", NULL, NULL, NULL, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &mock_vhost_api);

static struct vringh vrh;
static struct vhost_buf desc_bufs[QUEUE_SIZE];
static struct vhost_iovec riovec[NUM_IOVECS];
static struct vhost_iovec wiovec[NUM_IOVECS];
static struct vringh_iov riov;
static struct vringh_iov wiov;

static uint8_t data_bufs[QUEUE_SIZE][64];

static void set_desc(uint16_t i, const void *buf, uint32_t len, uint16_t flags, uint16_t next)
{
	ctrl.descs[i].addr = sys_cpu_to_le64((uint64_t)(uintptr_t)buf);
	ctrl.descs[i].len = sys_cpu_to_le32(len);
	ctrl.descs[i].flags = sys_cpu_to_le16(flags);
	ctrl.descs[i].next = sys_cpu_to_le16(next);
}

static void push_avail(uint16_t head)
{
	const uint16_t idx = sys_le16_to_cpu(MOCK_AVAIL->idx);

	MOCK_AVAIL->ring[idx % QUEUE_SIZE] = sys_cpu_to_le16(head);
	MOCK_AVAIL->idx = sys_cpu_to_le16(idx + 1);
}

static void vringh_suite_before(void *fixture)
{
	ARG_UNUSED(fixture);

	memset(&ctrl, 0, sizeof(ctrl));

	vringh_iov_init(&riov, riovec, ARRAY_SIZE(riovec));
	vringh_iov_init(&wiov, wiovec, ARRAY_SIZE(wiovec));

	zassert_ok(vringh_init_device(&vrh, DEVICE_GET(mock_vhost), 0, desc_bufs,
				      ARRAY_SIZE(desc_bufs), NULL));
}

ZTEST(vringh, test_getdesc_empty)
{
	uint16_t head;

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), 0);
}

ZTEST(vringh, test_single_writable_desc)
{
	uint16_t head;

	set_desc(0, data_bufs[0], 32, VIRTQ_DESC_F_WRITE, 0);
	push_avail(0);

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), 1);
	zassert_equal(head, 0);
	zassert_equal(riov.used, 0);
	zassert_equal(wiov.used, 1);
	zassert_equal_ptr(wiov.iov[0].iov_base, data_bufs[0]);
	zassert_equal(wiov.iov[0].iov_len, 32);

	zassert_ok(vringh_complete(&vrh, head, 32));

	zassert_equal(ctrl.release_calls, 1);
	zassert_equal(ctrl.last_release_head, 0);
	zassert_equal(ctrl.last_release_written, 32);

	zassert_equal(sys_le16_to_cpu(MOCK_USED->idx), 1);
	zassert_equal(sys_le32_to_cpu(MOCK_USED->ring[0].id), 0);
	zassert_equal(sys_le32_to_cpu(MOCK_USED->ring[0].len), 32);

	vringh_notify(&vrh);
	zassert_equal(ctrl.notify_calls, 1);
}

ZTEST(vringh, test_chain_read_write_split)
{
	uint16_t head;

	set_desc(2, data_bufs[0], 8, VIRTQ_DESC_F_NEXT, 3);
	set_desc(3, data_bufs[1], 16, VIRTQ_DESC_F_NEXT, 4);
	set_desc(4, data_bufs[2], 24, VIRTQ_DESC_F_WRITE, 0);
	push_avail(2);

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), 1);
	zassert_equal(head, 2);
	zassert_equal(riov.used, 2);
	zassert_equal(wiov.used, 1);
	zassert_equal_ptr(riov.iov[0].iov_base, data_bufs[0]);
	zassert_equal(riov.iov[0].iov_len, 8);
	zassert_equal_ptr(riov.iov[1].iov_base, data_bufs[1]);
	zassert_equal(riov.iov[1].iov_len, 16);
	zassert_equal_ptr(wiov.iov[0].iov_base, data_bufs[2]);
	zassert_equal(wiov.iov[0].iov_len, 24);

	zassert_ok(vringh_complete(&vrh, head, 24));
}

ZTEST(vringh, test_abandon_and_replay)
{
	uint16_t head;

	set_desc(1, data_bufs[0], 32, VIRTQ_DESC_F_WRITE, 0);
	push_avail(1);

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), 1);
	zassert_equal(head, 1);

	zassert_ok(vringh_abandon(&vrh, 1));
	zassert_equal(ctrl.release_calls, 1);
	zassert_equal(ctrl.last_release_written, 0);

	/* The abandoned descriptor must be returned again */
	vringh_iov_reset(&riov);
	vringh_iov_reset(&wiov);
	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), 1);
	zassert_equal(head, 1);

	zassert_ok(vringh_complete(&vrh, head, 32));
	zassert_equal(sys_le16_to_cpu(MOCK_USED->idx), 1);
}

ZTEST(vringh, test_invalid_next_fails_device)
{
	uint16_t head;

	set_desc(0, data_bufs[0], 8, VIRTQ_DESC_F_NEXT, QUEUE_SIZE);
	push_avail(0);

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), -EINVAL);
	zassert_not_equal(ctrl.status & BIT(DEVICE_STATUS_NEEDS_RESET), 0U);

	/* Further processing is refused once the device has failed */
	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), -EIO);
}

ZTEST(vringh, test_readable_after_writable)
{
	uint16_t head;

	set_desc(0, data_bufs[0], 8, VIRTQ_DESC_F_WRITE | VIRTQ_DESC_F_NEXT, 1);
	set_desc(1, data_bufs[1], 8, 0, 0);
	push_avail(0);

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), -EINVAL);
	zassert_not_equal(ctrl.status & BIT(DEVICE_STATUS_NEEDS_RESET), 0U);
}

ZTEST(vringh, test_chain_loop_rejected)
{
	uint16_t head;

	set_desc(0, data_bufs[0], 8, VIRTQ_DESC_F_NEXT, 1);
	set_desc(1, data_bufs[1], 8, VIRTQ_DESC_F_NEXT, 0);
	push_avail(0);

	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), -E2BIG);
	zassert_not_equal(ctrl.status & BIT(DEVICE_STATUS_NEEDS_RESET), 0U);
}

ZTEST(vringh, test_prepare_failure_is_retried)
{
	uint16_t head;

	set_desc(0, data_bufs[0], 32, VIRTQ_DESC_F_WRITE, 0);
	push_avail(0);

	ctrl.fail_prepare = -ENOMEM;
	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), -ENOMEM);

	/* A backend failure releases the partial mapping but does not fail the device */
	zassert_equal(ctrl.release_calls, 1);
	zassert_equal(ctrl.last_release_written, 0);
	zassert_equal(ctrl.status & BIT(DEVICE_STATUS_NEEDS_RESET), 0U);

	/* The same descriptor is consumed once the backend recovers */
	ctrl.fail_prepare = 0;
	zassert_equal(vringh_getdesc(&vrh, &riov, &wiov, &head), 1);
	zassert_equal(head, 0);
	zassert_ok(vringh_complete(&vrh, head, 32));
}

ZTEST_SUITE(vringh, NULL, NULL, vringh_suite_before, NULL, NULL);
