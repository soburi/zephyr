/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost_blk_sample);

#define VHOST_BLK_NODE DT_NODELABEL(blk)

void blk_queue_ready(const struct device *dev, uint16_t qid, void *data);

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(VHOST_BLK_NODE);
	int ret;

	if (!device_is_ready(dev)) {
		LOG_ERR("VHost blk device %s is not ready", dev->name);
		return -ENODEV;
	}

	ret = vhost_register_virtq_ready_cb(dev, blk_queue_ready, (void *)dev);
	if (ret < 0) {
		LOG_ERR("Failed to register queue callback: %d", ret);
		return ret;
	}

	LOG_INF("VHost blk device %s ready", dev->name);
	LOG_INF("VHost virtio-blk sample started, waiting for DomU");
	k_sleep(K_FOREVER);

	return 0;
}
