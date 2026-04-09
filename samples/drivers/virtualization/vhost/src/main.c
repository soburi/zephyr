/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost);

void rng_queue_ready(const struct device *dev, uint16_t qid, void *data);

int register_handler(const struct device *dev,
		     void (*handler)(const struct device *, uint16_t, void *))
{
	LOG_INF("VHost device %s registering handler...", dev->name);
	if (!device_is_ready(dev)) {
		LOG_ERR("VHost device %s not ready", dev->name);
		return -ENODEV;
	}

	LOG_INF("VHost device ready: %s", dev->name);
	vhost_register_virtq_ready_cb(dev, handler, (void *)dev);

	return 0;
}

int main(void)
{
	const struct device *rng_dev = DEVICE_DT_GET(DT_PATH(vhost, rng));

	LOG_INF("VHost sample...");
	register_handler(rng_dev, rng_queue_ready);

	LOG_INF("VHost sample application started, waiting for guest connections...");
	k_sleep(K_FOREVER);

	return 0;
}
