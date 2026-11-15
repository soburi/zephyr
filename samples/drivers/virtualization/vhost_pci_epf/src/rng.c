/*
 * Copyright The Zephyr Project Contributors
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

#include "rng.h"

LOG_MODULE_REGISTER(vhost_rng, LOG_LEVEL_INF);

/* virtio-rng has a single request queue */
#define RNG_REQUESTQ 0

#define QUEUE_SIZE_MAX DT_PROP(DT_NODELABEL(vhost_rng), queue_size_max)
#define NUM_IOVECS     16

/* Runs in the context of the controller, one invocation at a time */
static void vringh_kick_handler(struct vringh *vrh)
{
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
	uint16_t head;

	while (true) {
		uint32_t total_len = 0;
		int ret;

		ret = vringh_getdesc(vrh, &riov, &wiov, &head);
		if (ret < 0) {
			LOG_ERR("vringh_getdesc failed: %d", ret);
			return;
		}

		if (ret == 0) {
			return;
		}

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

		LOG_INF("Served %u random bytes", total_len);

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
	int err;

	ARG_UNUSED(user_data);

	if (queue_id != RNG_REQUESTQ) {
		return;
	}

	err = vringh_init_device(&vrh_inst, dev, queue_id, desc_bufs, ARRAY_SIZE(desc_bufs),
				 vringh_kick_handler);
	if (err != 0) {
		LOG_ERR("vringh_init_device failed: %d", err);
		return;
	}

	LOG_INF("virtio-rng request queue ready");
}

void rng_queue_reset(const struct device *dev, uint16_t queue_id, void *user_data)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(user_data);

	/*
	 * Requests are served to completion in the kick handler, so no
	 * descriptor is held. A backend with work in flight only records the
	 * reset here and stops it from a context of its own. The ring state is
	 * set up again when the queue becomes ready.
	 */
	if (queue_id == RNG_REQUESTQ) {
		LOG_INF("virtio-rng request queue reset");
	}
}
