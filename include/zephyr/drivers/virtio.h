/*
 * Copyright (c) 2024 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup virtio_interface
 * @brief Main header file for Virtio driver API.
 */

#ifndef ZEPHYR_VIRTIO_VIRTIO_H_
#define ZEPHYR_VIRTIO_VIRTIO_H_
#include <zephyr/device.h>
#include "virtio/virtqueue.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Interfaces for Virtual I/O (VIRTIO) devices.
 * @defgroup virtio_interface VIRTIO
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Callback used during virtqueue discovery.
 *
 * The callback mirrors the queue sizing step required by Virtio 1.3,
 * §3.1 "Device Initialization", where the driver probes each
 * virtqueue exposed by the transport specific configuration.
 *
 * @param queue_idx Index of the currently inspected queue.
 * @param max_queue_size Maximum permitted size of the queue as reported by
 *                       the transport.
 * @param opaque Pointer to user provided data.
 * @return The virtqueue size that should be programmed for @p queue_idx.
 */
typedef uint16_t (*virtio_enumerate_queues)(
        uint16_t queue_idx, uint16_t max_queue_size, void *opaque
);

/**
 * @brief Virtio driver API contract.
 *
 * The entry points correspond to the generic initialization and
 * operation flow described in Virtio 1.3, Chapter 3 "General
 * Initialization And Device Operation".
 */
__subsystem struct virtio_driver_api {
        struct virtq *(*get_virtqueue)(const struct device *dev, uint16_t queue_idx);
        void (*notify_virtqueue)(const struct device *dev, uint16_t queue_idx);
        void *(*get_device_specific_config)(const struct device *dev);
	bool (*read_device_feature_bit)(const struct device *dev, int bit);
	int (*write_driver_feature_bit)(const struct device *dev, int bit, bool value);
	int (*commit_feature_bits)(const struct device *dev);
	int (*init_virtqueues)(
		const struct device *dev, uint16_t num_queues, virtio_enumerate_queues cb,
		void *opaque
	);
	void (*finalize_init)(const struct device *dev);
};

/**
 * @brief Return a virtqueue handle.
 *
 * Retrieves the virtqueue configured for @p queue_idx. Queue
 * configuration is performed as part of Virtio 1.3, §3.1 "Device
 * Initialization" after reading the transport specific registers
 * described in section "Virtqueues".
 *
 * @param dev Virtio device it operates on.
 * @param queue_idx Index of the virtqueue to get.
 * @return Pointer to the virtqueue or @c NULL if no queue was configured.
 */
static inline struct virtq *virtio_get_virtqueue(const struct device *dev, uint16_t queue_idx)
{
        const struct virtio_driver_api *api = dev->api;

        return api->get_virtqueue(dev, queue_idx);
}

/**
 * @brief Send a driver notification for a virtqueue.
 *
 * Virtio 1.3, section "Driver Notifications" permits the device to read
 * buffers as soon as the driver updates the available ring index,
 * and clarifies that notifications are advisory. The
 * device may therefore consume buffers even if this helper is not
 * called.
 *
 * @param dev Virtio device it operates on.
 * @param queue_idx Virtqueue to be notified.
 */
static inline void virtio_notify_virtqueue(const struct device *dev, uint16_t queue_idx)
{
        const struct virtio_driver_api *api = dev->api;

        api->notify_virtqueue(dev, queue_idx);
}

/**
 * @brief Access the device-specific configuration structure.
 *
 * Virtio 1.3, §2.5 "Device Configuration Space" defines device-specific
 * configuration regions that are readable (and sometimes writable)
 * by the driver.
 *
 * @param dev Virtio device it operates on.
 * @return Pointer to the device specific configuration space or @c NULL if it
 *         is not present.
 */
static inline void *virtio_get_device_specific_config(const struct device *dev)
{
        const struct virtio_driver_api *api = dev->api;

        return api->get_device_specific_config(dev);
}

/**
 * @brief Read a device feature bit.
 *
 * Part of the feature negotiation sequence in Virtio 1.3, §3.1
 * "Device Initialization" and §2.2 "Feature Bits".
 *
 * @param dev Virtio device it operates on.
 * @param bit Selected bit number to probe.
 * @return Value of the offered feature bit.
 */
static inline bool virtio_read_device_feature_bit(const struct device *dev, int bit)
{
        const struct virtio_driver_api *api = dev->api;

        return api->read_device_feature_bit(dev, bit);
}

/**
 * @brief Write a negotiated driver feature bit.
 *
 * Invoked while acknowledging features per Virtio 1.3, §3.1 "Device
 * Initialization" after validating them against §2.2 "Feature Bits".
 *
 * @param dev Virtio device it operates on.
 * @param bit Selected bit number.
 * @param value Bit value to write.
 * @return 0 on success or a negative error code on failure.
 */
static inline int virtio_write_driver_feature_bit(const struct device *dev, int bit, bool value)
{
        const struct virtio_driver_api *api = dev->api;

        return api->write_driver_feature_bit(dev, bit, value);
}

/**
 * @brief Commit the negotiated feature set.
 *
 * Writes the FEATURES_OK status as mandated by Virtio 1.3, §3.1
 * "Device Initialization" once all desired feature bits have been
 * acknowledged.
 *
 * @param dev Virtio device it operates on.
 * @return 0 on success or negative error code on failure.
 */
static inline int virtio_commit_feature_bits(const struct device *dev)
{
        const struct virtio_driver_api *api = dev->api;

        return api->commit_feature_bits(dev);
}

/**
 * @brief Initialize the virtqueues offered by the device.
 *
 * Corresponds to the queue provisioning steps in Virtio 1.3, §3.1
 * "Device Initialization" and the queue layout rules in §2.6
 * "Virtqueues".
 *
 * @param dev Virtio device it operates on.
 * @param num_queues Number of queues to initialize.
 * @param cb Callback called for each available virtqueue.
 * @param opaque Pointer to user provided data that will be passed to the callback.
 * @return 0 on success or negative error code on failure.
 */
static inline int virtio_init_virtqueues(
        const struct device *dev, uint16_t num_queues, virtio_enumerate_queues cb, void *opaque)
{
        const struct virtio_driver_api *api = dev->api;

        return api->init_virtqueues(dev, num_queues, cb, opaque);
}

/**
 * @brief Finalize device initialization.
 *
 * Completes the Virtio 1.3, §3.1 "Device Initialization" sequence by
 * setting DRIVER_OK once queues and features are ready.
 *
 * @param dev Virtio device it operates on.
 */
static inline void virtio_finalize_init(const struct device *dev)
{
        const struct virtio_driver_api *api = dev->api;

        api->finalize_init(dev);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_VIRTIO_VIRTIO_H_ */
