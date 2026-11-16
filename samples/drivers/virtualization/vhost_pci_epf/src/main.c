/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include "rng.h"

LOG_MODULE_REGISTER(vhost_pci_epf_sample, LOG_LEVEL_INF);

#define VHOST_NODE DT_NODELABEL(vhost_rng)

int main(void)
{
	const struct device *vhost = DEVICE_DT_GET(VHOST_NODE);
	const struct device *epc = DEVICE_DT_GET(DT_PHANDLE(VHOST_NODE, epc));
	int err;

	/* The controller has added its endpoint function to the EPC by now */
	if (!device_is_ready(vhost)) {
		LOG_ERR("VHost device %s not ready", vhost->name);
		return 0;
	}

	err = vhost_register_virtq_ready_cb(vhost, rng_queue_ready, NULL);
	if (err != 0) {
		LOG_ERR("Failed to register ready callback for %s: %d", vhost->name, err);
		return 0;
	}

	err = vhost_register_virtq_reset_cb(vhost, rng_queue_reset, NULL);
	if (err != 0) {
		LOG_ERR("Failed to register reset callback for %s: %d", vhost->name, err);
		return 0;
	}

	LOG_INF("VHost device %s ready", vhost->name);

	/* Let the PCI host enumerate the VIRTIO PCI function */
	err = pcie_epc_start(epc);
	if (err != 0) {
		LOG_ERR("Failed to start the PCI link: %d", err);
		return 0;
	}

	LOG_INF("PCI link started, waiting for the host");

	return 0;
}
