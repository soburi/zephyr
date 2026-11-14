/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(vhost);

#define DT_DRV_COMPAT xen_vhost_mmio

void rng_queue_ready(const struct device *dev, uint16_t queue_id, void *user_data);

static int register_backend(const struct device *dev)
{
	int err;

	if (!device_is_ready(dev)) {
		LOG_ERR("VHost device %s not ready", dev->name);
		return -ENODEV;
	}

	err = vhost_register_virtq_ready_cb(dev, rng_queue_ready, NULL);
	if (err != 0) {
		LOG_ERR("Failed to register ready callback for %s: %d", dev->name, err);
		return err;
	}

	LOG_INF("VHost device %s ready", dev->name);

	return 0;
}

#define REGISTER_BACKEND(inst) (void)register_backend(DEVICE_DT_INST_GET(inst));

int main(void)
{
	DT_INST_FOREACH_STATUS_OKAY(REGISTER_BACKEND)

	LOG_INF("VHost sample application started, waiting for guest connections...");
	k_sleep(K_FOREVER);

	return 0;
}
