/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef VHOST_PCI_EPF_SAMPLE_RNG_H_
#define VHOST_PCI_EPF_SAMPLE_RNG_H_

#include <stdint.h>

#include <zephyr/device.h>

/**
 * @brief Start the virtio-rng backend on a virtqueue that became ready
 *
 * Matches the queue ready callback of the VHost controller API.
 *
 * @param dev        VHost controller device
 * @param queue_id   Queue that became ready
 * @param user_data  Unused
 */
void rng_queue_ready(const struct device *dev, uint16_t queue_id, void *user_data);

/**
 * @brief Stop the virtio-rng backend on a virtqueue that is going away
 *
 * Matches the queue reset callback of the VHost controller API.
 *
 * @param dev        VHost controller device
 * @param queue_id   Queue that is reset
 * @param user_data  Unused
 */
void rng_queue_reset(const struct device *dev, uint16_t queue_id, void *user_data);

#endif /* VHOST_PCI_EPF_SAMPLE_RNG_H_ */
