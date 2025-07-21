/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_VHOST_H_
#define ZEPHYR_DRIVERS_VHOST_H_

/**
 * @brief VHost API
 *
 * The VHost provides functions for VIRTIO device backends in
 * a hypervisor environment.
 * VHost backends handle guest VIRTIO requests and respond to them.
 *
 * @defgroup vhost_apis VHost Controller APIs
 * @ingroup io_interfaces
 * @{
 */

#include <stdint.h>
#include <zephyr/kernel.h>

/**
 * Represents a memory buffer segment for VHost operations.
 */
struct vhost_iovec {
	void *iov_base;
	size_t iov_len;
};

/**
 * VHost controller API structure
 */
__subsystem struct vhost_controller_api {
	int (*prepare_iovec)(const struct device *dev, uint16_t queue_id, uint64_t gpa, size_t len,
			     uint16_t head, struct vhost_iovec *iovec, size_t max_iovecs,
			     size_t *out_count);
	int (*release_iovec)(const struct device *dev, uint16_t queue_id, uint16_t head);
	int (*get_virtq)(const struct device *dev, uint16_t queue_id, void **parts,
			 size_t *queue_size);
	int (*get_driver_features)(const struct device *dev, uint64_t *drv_feats);
	bool (*is_ready)(const struct device *dev, uint16_t queue_id);
	int (*set_ready_callback)(const struct device *dev,
				  void (*callback)(const struct device *dev, uint16_t queue_id,
						   void *data),
				  void *data);
	int (*set_notify_callback)(const struct device *dev, uint16_t queue_id,
				   void (*callback)(const struct device *dev, uint16_t queue_id,
						    void *data),
				   void *data);
	int (*queue_notify)(const struct device *dev, uint16_t queue_id);
	int (*set_device_status)(const struct device *dev, uint32_t status);
};

/**
 * @brief Prepare iovec for executing data transfer
 *
 * @param dev              VHost controller device
 * @param queue_id         Queue ID
 * @param gpa              Guest physical address
 * @param len              Buffer length
 * @param iovec            Output iovec array
 * @param max_iovecs       Maximum iovecs available
 * @param out_count        Number of iovecs prepared
 *
 * @retval 0             Success
 * @retval -EINVAL       Invalid parameters
 * @retval -ENOMEM       Insufficient memory
 * @retval -EFAULT       Invalid guest address
 * @retval -E2BIG        Buffer too large
 */
static inline int vhost_prepare_iovec(const struct device *dev, uint16_t queue_id, uint64_t gpa,
				      size_t len, uint16_t head, struct vhost_iovec *iovec,
				      size_t max_iovecs, size_t *out_count)
{
	const struct vhost_controller_api *api = dev->api;

	return api->prepare_iovec(dev, queue_id, gpa, len, head, iovec, max_iovecs, out_count);
}

/**
 * @brief Release all iovecs for a queue
 *
 * @param dev       VHost controller device
 * @param queue_id  Queue ID
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 */
static inline int vhost_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head)
{
	const struct vhost_controller_api *api = dev->api;

	return api->release_iovec(dev, queue_id, head);
}

/**
 * @brief Get VirtQueue components
 *
 * @param dev         VHost controller device
 * @param queue_id    Queue ID
 * @param parts       Array for descriptor, available, used ring pointers
 * @param queue_size  Queue size output
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -ENODEV    Queue not ready
 */
static inline int vhost_get_virtq(const struct device *dev, uint16_t queue_id, void **parts,
				  size_t *queue_size)
{
	const struct vhost_controller_api *api = dev->api;

	return api->get_virtq(dev, queue_id, parts, queue_size);
}

/**
 * @brief Get negotiated VirtIO feature bits
 *
 * @param dev         VHost controller device
 * @param drv_feats   Output for feature mask
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -ENODATA   Feature negotiation incomplete
 */
static inline int vhost_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	const struct vhost_controller_api *api = dev->api;

	return api->get_driver_features(dev, drv_feats);
}

/**
 * @brief Check if queue is ready for processing
 *
 * @param dev       VHost controller device
 * @param queue_id  Queue ID (0-based)
 *
 * @retval true     Queue is ready
 * @retval false    Queue not ready or invalid
 */
static inline bool vhost_queue_ready(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_controller_api *api = dev->api;

	return api->is_ready(dev, queue_id);
}

/**
 * @brief Register device-wide queue ready callback
 *
 * @param dev         VHost controller device
 * @param callback    Function to call when any queue becomes ready
 * @param user_data   User data for callback
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 */
static inline int vhost_set_ready_callback(const struct device *dev,
					   void (*callback)(const struct device *dev,
							    uint16_t queue_id, void *data),
					   void *user_data)
{
	const struct vhost_controller_api *api = dev->api;

	return api->set_ready_callback(dev, callback, user_data);
}

/**
 * @brief Register per-queue guest notification callback
 *
 * @param dev         VHost controller device
 * @param queue_id    Queue ID (0-based)
 * @param callback    Function to call on guest notifications
 * @param user_data   User data for callback
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -ENODEV    Queue not found
 */
static inline int vhost_set_notify_callback(const struct device *dev, uint16_t queue_id,
					    void (*callback)(const struct device *dev,
							     uint16_t queue_id, void *data),
					    void *user_data)
{
	const struct vhost_controller_api *api = dev->api;

	return api->set_notify_callback(dev, queue_id, callback, user_data);
}

/**
 * @brief Send interrupt notification to guest
 *
 * @param dev        VHost controller device
 * @param queue_id   Queue ID (0-based)
 *
 * @retval 0         Success
 * @retval -EINVAL   Invalid parameters
 * @retval -ENODEV   Queue not ready
 * @retval -EIO      Interrupt delivery failed
 */
static inline int vhost_queue_notify(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_controller_api *api = dev->api;

	return api->queue_notify(dev, queue_id);
}

/**
 * @brief Set device status and notify guest
 *
 * @param dev         VHost controller device
 * @param status      VirtIO device status bits to set
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -EIO       Notification failed
 */
static inline int vhost_set_device_status(const struct device *dev, uint32_t status)
{
	const struct vhost_controller_api *api = dev->api;

	return api->set_device_status(dev, status);
}

/**
 * @}
 */

#endif /* ZEPHYR_DRIVERS_VHOST_H_ */
