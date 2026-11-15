/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 TOKITA Hiroshi
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
 * This is the VHost device class for VIRTIO backends. It is the boundary
 * between transport or hypervisor-specific code and a VIRTIO device backend.
 * It deliberately does not prescribe buffer allocation, locking, callback
 * dispatch, or request scheduling.
 * Implementations may process requests in parallel when they preserve the
 * lifetime and completion rules described by each operation below.
 *
 * A buffer returned by vhost_prepare_iovec() remains valid until the matching
 * vhost_release_iovec() has completed. A queue reset invalidates outstanding
 * queue references; users must stop accessing them before reset reclamation
 * starts. Callback execution context and queue-to-queue concurrency are
 * controller-specific.
 *
 * This class does not require the vringh helper. A backend may use vringh or
 * another ring-processing implementation while using this controller API for
 * transport-specific operations.
 *
 * @defgroup vhost_apis VHost Controller APIs
 * @ingroup io_interfaces
 * @{
 */

#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <zephyr/device.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Represents a memory buffer segment for VHost operations.
 *
 * The address is valid only for the lifetime of the mapping returned by
 * vhost_prepare_iovec(). The controller may map or copy the guest buffer;
 * this type does not require either strategy.
 */
struct vhost_iovec {
	void *iov_base;
	size_t iov_len;
};

/**
 * Represents a guest buffer reference and length pair for VHost operations.
 *
 * @p gpa carries the address field of a VIRTIO descriptor verbatim. It is an
 * opaque, controller-specific token for guest memory (for example a guest
 * physical address or an encoded grant reference). Users must not interpret
 * it or perform arithmetic on it; it is only meaningful as an argument to
 * vhost_prepare_iovec() on the controller it came from.
 */
struct vhost_buf {
	uint64_t gpa;    /**< Opaque guest buffer reference (descriptor address field). */
	size_t len;      /**< Buffer length in bytes. */
	bool is_write;   /**< True when the backend writes to the guest buffer. */
};

struct virtq_desc;
struct virtq_avail;
struct virtq_used;

/**
 * @brief Split virtqueue ring components
 *
 * Host-side view of a guest split virtqueue: descriptor table, available
 * ring, and used ring, as defined by the VIRTIO specification. This
 * structure describes the split virtqueue format only; controllers that
 * implement another format, or that cannot expose the rings in the host
 * address space, do not provide it.
 */
struct vhost_vring {
	uint16_t num;              /**< Number of descriptors in ring (power of 2) */
	struct virtq_desc *desc;   /**< Descriptor table pointer (guest memory) */
	struct virtq_avail *avail; /**< Available ring pointer (guest memory) */
	struct virtq_used *used;   /**< Used ring pointer (guest memory) */
};

/**
 * VHost controller API structure.
 *
 * A controller owns transport-specific state and may support multiple
 * outstanding requests. Operations referring to different queue/head pairs
 * may be executed concurrently when the controller supports it. Callers must
 * provide any additional synchronization required for concurrent operations
 * on the same queue.
 */
__subsystem struct vhost_driver_api {
	int (*prepare_iovec)(const struct device *dev, uint16_t queue_id, uint16_t head,
			     const struct vhost_buf *bufs, size_t bufs_count,
			     struct vhost_iovec *read_iovec, size_t max_read_iovecs,
			     struct vhost_iovec *write_iovec, size_t max_write_iovecs,
			     size_t *read_count, size_t *write_count);
	int (*release_iovec)(const struct device *dev, uint16_t queue_id, uint16_t head,
			     uint32_t written);
	int (*get_virtq)(const struct device *dev, uint16_t queue_id, struct vhost_vring *vring);
	int (*get_driver_features)(const struct device *dev, uint64_t *drv_feats);
	int (*get_supported_features)(const struct device *dev, uint64_t *supported);
	int (*set_device_features)(const struct device *dev, uint64_t wanted, uint64_t *offered);
	bool (*virtq_is_ready)(const struct device *dev, uint16_t queue_id);
	int (*register_virtq_ready_cb)(const struct device *dev,
				       void (*callback)(const struct device *dev, uint16_t queue_id,
							void *data),
				       void *data);
	int (*register_virtq_notify_cb)(const struct device *dev, uint16_t queue_id,
					void (*callback)(const struct device *dev,
							 uint16_t queue_id, void *data),
					void *data);
	int (*notify_virtq)(const struct device *dev, uint16_t queue_id);
	int (*set_device_status)(const struct device *dev, uint32_t status);
};

/**
 * @brief Prepare iovecs for virtq process
 *
 * Maps guest physical addresses to host virtual addresses for the given
 * GPAs and fills the provided read and write iovec arrays.
 * If this function fails, the caller must call vhost_release_iovec() to
 * release any partially prepared iovecs.
 *
 * @param dev              VHost device
 * @param queue_id         Queue identifier
 * @param head             Head of descriptor
 * @param bufs             Array of GPA/length pairs
 * @param bufs_count       Number of bufs in the array
 * @param read_iovec       Array to fill with read iovecs
 * @param max_read_iovecs  Maximum number of read iovecs that can be stored
 * @param write_iovec      Array to fill with write iovecs
 * @param max_write_iovecs Maximum number of write iovecs that can be stored
 * @param read_count       Number of read iovecs prepared
 * @param write_count      Number of write iovecs prepared
 *
 * @retval 0             Success
 * @retval -EINVAL       Invalid parameters
 * @retval -ENOMEM       Insufficient memory
 * @retval -E2BIG        Buffer too large (in other words, iovecs are too small)
 *
 * @note On success, the returned iovecs and their backing mappings remain
 *       valid until vhost_release_iovec() is called for the same queue and
 *       descriptor head. The caller must call vhost_release_iovec() exactly
 *       once per vhost_prepare_iovec() call, on success and on failure
 *       alike; a failed call may leave a partial mapping that only the
 *       release operation reclaims.
 */
static inline int vhost_prepare_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
				      const struct vhost_buf *bufs, size_t bufs_count,
				      struct vhost_iovec *read_iovec, size_t max_read_iovecs,
				      struct vhost_iovec *write_iovec, size_t max_write_iovecs,
				      size_t *read_count, size_t *write_count)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->prepare_iovec(dev, queue_id, head, bufs, bufs_count, read_iovec,
				  max_read_iovecs, write_iovec, max_write_iovecs, read_count,
				  write_count);
}

/**
 * @brief Release all iovecs
 *
 * Release iovecs prepared by vhost_prepare_iovec(), including any partial
 * preparation left by a failed call. It must also succeed when there is
 * nothing to release.
 *
 * @p written is the total number of bytes the backend wrote to the writable
 * iovecs, counted from the start of the writable buffer sequence. Controllers
 * that map guest memory directly may ignore it; controllers that copy between
 * guest and host buffers use it to bound the write-back. Pass 0 when nothing
 * was written, including after a failed vhost_prepare_iovec() call.
 *
 * @param dev       VHost controller device
 * @param queue_id  Queue ID
 * @param head      Head of descriptor
 * @param written   Bytes written to the writable buffers
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 */
static inline int vhost_release_iovec(const struct device *dev, uint16_t queue_id, uint16_t head,
				      uint32_t written)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->release_iovec(dev, queue_id, head, written);
}

/**
 * @brief Get split virtqueue ring components
 *
 * Fills @p vring with host-accessible pointers to the queue's split
 * virtqueue rings and the queue size. Only the split virtqueue format is
 * expressible through this operation. A controller that cannot expose the
 * rings in the host address space returns -ENOTSUP; such a controller cannot
 * be used with ring-processing helpers that require direct ring access.
 *
 * @param dev       VHost controller device
 * @param queue_id  Queue ID
 * @param vring     Output for ring pointers and queue size
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -ENODEV    Queue not ready
 * @retval -ENOTSUP   Rings are not accessible in the host address space
 *
 * @note The returned ring pointers remain valid only while the queue remains
 *       configured. A queue reset or reconfiguration invalidates them.
 */
static inline int vhost_get_virtq(const struct device *dev, uint16_t queue_id,
				  struct vhost_vring *vring)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->get_virtq(dev, queue_id, vring);
}

/**
 * @brief Get the VirtIO feature bits the controller can offer
 *
 * Bits outside the returned mask are never offered to the driver, whatever
 * the backend asks for in vhost_set_device_features(). The mask depends on
 * the transport: a transport that cannot carry bits above 31, or that
 * cannot implement a feature, leaves them out. The bits the transport
 * requires, such as @ref VIRTIO_F_VERSION_1 where the transport has no
 * legacy mode, are part of the mask and are always offered.
 *
 * @param dev         VHost controller device
 * @param supported   Output for feature mask
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 */
static inline int vhost_get_supported_features(const struct device *dev, uint64_t *supported)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->get_supported_features(dev, supported);
}

/**
 * @brief Set the VirtIO feature bits the backend wants to offer
 *
 * The backend decides which features the device implements, the controller
 * decides which of them it can carry. The bits offered to the driver are
 * those in @p wanted that the controller supports, plus the bits the
 * transport requires. The result is returned in @p offered, so that the
 * backend can tell whether a feature it depends on is available, and which
 * transport bits it has to handle.
 *
 * The driver accepts a subset of the offered bits, available with
 * vhost_get_driver_features(). A controller that finds bits outside the
 * offered ones in the driver's selection, rejects it as the transport
 * allows, or masks them where it has no way to reject.
 *
 * Call it before the driver can see the device, for example before the link
 * to the PCI host is started. Without a call only the bits the transport
 * requires are offered. The setting survives a device reset.
 *
 * A backend that uses the vringh helpers must not ask for the bits
 * @ref VRINGH_UNSUPPORTED_FEATURES.
 *
 * @param dev         VHost controller device
 * @param wanted      Feature bits the backend implements
 * @param offered     Output for the bits offered to the driver, can be NULL
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -EBUSY     The driver has started to initialize the device
 * @retval -ENODEV    The device is not available
 */
static inline int vhost_set_device_features(const struct device *dev, uint64_t wanted,
					    uint64_t *offered)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->set_device_features(dev, wanted, offered);
}

/**
 * @brief Get negotiated VirtIO feature bits
 *
 * @param dev         VHost controller device
 * @param drv_feats   Output for feature mask
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 */
static inline int vhost_get_driver_features(const struct device *dev, uint64_t *drv_feats)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

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
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->virtq_is_ready(dev, queue_id);
}

/**
 * @brief Register device-wide queue ready callback
 *
 * The callback signals that the backend may start processing the queue: the
 * controller delivers it for each queue no earlier than when the guest driver
 * has enabled the queue and completed device initialization (DRIVER_OK), as
 * required by the VIRTIO specification.
 *
 * The controller may unregister this callback when the device or a queue is
 * reset. The callback can be delivered asynchronously and may be coalesced;
 * its execution context is controller-specific.
 *
 * @param dev         VHost controller device
 * @param callback    Function to call when a queue becomes ready for processing
 * @param user_data   User data for callback
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 *
 * @note The callback must not retain queue pointers across reset. The API does
 *       not require a particular worker, interrupt, or thread context.
 */
static inline int vhost_register_virtq_ready_cb(const struct device *dev,
						void (*callback)(const struct device *dev,
								 uint16_t queue_id, void *data),
						void *user_data)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->register_virtq_ready_cb(dev, callback, user_data);
}

/**
 * @brief Register per-queue guest notification callback
 *
 * The controller may unregister this callback when the queue is reset. The
 * callback can be delivered asynchronously and may be coalesced; its
 * execution context is controller-specific.
 *
 * @param dev         VHost controller device
 * @param queue_id    Queue ID (0-based)
 * @param callback    Function to call on guest notifications
 * @param user_data   User data for callback
 *
 * @retval 0          Success
 * @retval -EINVAL    Invalid parameters
 * @retval -ENODEV    Queue not found
 *
 * @note The callback must not retain queue pointers across reset. The API does
 *       not require a particular worker, interrupt, or thread context.
 */
static inline int vhost_register_virtq_notify_cb(const struct device *dev, uint16_t queue_id,
						 void (*callback)(const struct device *dev,
								  uint16_t queue_id, void *data),
						 void *user_data)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->register_virtq_notify_cb(dev, queue_id, callback, user_data);
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
static inline int vhost_notify_virtq(const struct device *dev, uint16_t queue_id)
{
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->notify_virtq(dev, queue_id);
}

/**
 * @brief Set device status bits and notify guest
 *
 * ORs @p status into the VIRTIO device status field and delivers a device
 * configuration change notification to the guest. Status bits cannot be
 * cleared through this API; the status field is cleared only by a device
 * reset initiated by the guest driver.
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
	const struct vhost_driver_api *api = DEVICE_API_GET(vhost, dev);

	return api->set_device_status(dev, status);
}

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_VHOST_H_ */
