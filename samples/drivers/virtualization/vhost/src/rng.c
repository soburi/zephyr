/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/devicetree.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/logging/log.h>
#include <zephyr/random/random.h>
#include <zephyr/sys/barrier.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(vhost_rng);

/* virtio-rng has a single request queue */
#define RNG_REQUESTQ 0

#define QUEUE_SIZE_MAX DT_PROP(DT_COMPAT_GET_ANY_STATUS_OKAY(xen_vhost_mmio), queue_size_max)
#define NUM_IOVECS     16

static struct vhost_iovec riovec[NUM_IOVECS];
static struct vhost_iovec wiovec[NUM_IOVECS];

static struct vringh_iov riov = {
	.iov = riovec,
	.max_num = NUM_IOVECS,
};

static struct vringh_iov wiov = {
	.iov = wiovec,
	.max_num = NUM_IOVECS,
};

static void vringh_kick_handler(struct vringh *vrh)
{
	uint16_t head;

	LOG_DBG("%s: queue_id=%u", __func__, vrh->queue_id);

	while (true) {
		int ret = vringh_getdesc(vrh, &riov, &wiov, &head);

		if (ret < 0) {
			LOG_ERR("vringh_getdesc failed: %d", ret);
			return;
		}

		if (ret == 0) {
			return;
		}

		uint32_t total_len = 0;

		/*
		 * sys_rand_get() is not cryptographically secure; a production
		 * virtio-rng backend must use a hardware entropy source.
		 */
		for (unsigned int i = 0; i < wiov.used; i++) {
			sys_rand_get(wiov.iov[i].iov_base, wiov.iov[i].iov_len);
			total_len += (uint32_t)wiov.iov[i].iov_len;
		}

		barrier_dmem_fence_full();

		ret = vringh_complete(vrh, head, total_len);
		if (ret < 0) {
			LOG_ERR("vringh_complete failed: %d", ret);
			return;
		}

		if (vringh_need_notify(vrh) > 0) {
			vringh_notify(vrh);
		}

		vringh_iov_reset(&riov);
		vringh_iov_reset(&wiov);
	}
}

void rng_queue_ready(const struct device *dev, uint16_t queue_id, void *user_data)
{
	static struct vringh vrh_inst;
	static struct vhost_buf desc_bufs[QUEUE_SIZE_MAX];

	ARG_UNUSED(user_data);

	LOG_DBG("%s(dev=%p, queue_id=%u)", __func__, dev, queue_id);

	if (queue_id != RNG_REQUESTQ) {
		return;
	}

	vringh_iov_init(&riov, riovec, ARRAY_SIZE(riovec));
	vringh_iov_init(&wiov, wiovec, ARRAY_SIZE(wiovec));

	int err = vringh_init_device(&vrh_inst, dev, queue_id, desc_bufs, ARRAY_SIZE(desc_bufs),
				     vringh_kick_handler);

	if (err != 0) {
		LOG_ERR("vringh_init_device failed: %d", err);
	}
}
