/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 TOKITA Hiroshi
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_DRIVERS_VHOST_VRINGH_H_
#define ZEPHYR_DRIVERS_VHOST_VRINGH_H_

/**
 * @file
 * @brief VIRTIO Ring Handler API
 *
 * VIRTIO ring handler (vringh) provides host-side access to guest VIRTIO rings.
 * Based on Linux kernel's vringh implementation.
 *
 * @defgroup vringh_apis VIRTIO Ring Handler APIs
 * @ingroup vhost_apis
 * @{
 */

#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/spinlock.h>

#ifdef __cplusplus
extern "C" {
#endif

struct virtq_desc;
struct virtq_avail;
struct virtq_used;

/**
 * @brief VirtQueue ring structure
 *
 * Contains pointers to VIRTIO ring components: descriptor table,
 * available ring, and used ring.
 */
struct vhost_vring {
	uint16_t num;              /**< Number of descriptors in ring (power of 2) */
	struct virtq_desc *desc;   /**< Descriptor table pointer (guest memory) */
	struct virtq_avail *avail; /**< Available ring pointer (guest memory) */
	struct virtq_used *used;   /**< Used ring pointer (guest memory) */
};

/**
 * @brief VIRTIO ring host-side handler
 *
 * Host-side interface for processing guest VIRTIO rings.
 * Based on Linux kernel vringh implementation with split virtqueue support.
 */
struct vringh {
	bool event_indices;          /**< Guest supports VIRTIO_RING_F_EVENT_IDX */
	bool weak_barriers;          /**< Use weak memory barriers */
	uint16_t last_avail_idx;     /**< Last available index processed */
	uint16_t last_used_idx;      /**< Last used index written */
	uint32_t completed;          /**< Descriptors completed since last notification */
	struct vhost_vring vring;    /**< VirtQueue ring components */
	const struct device *dev;    /**< Associated VHost backend device */
	uint16_t queue_id;           /**< Queue ID within VHost device */
	struct vhost_buf *desc_bufs; /**< Descriptor scratch buffer */
	size_t desc_bufs_count;      /**< Number of entries in desc_bufs */
	struct k_spinlock lock;      /**< Spinlock for vring state updates */

	/**
	 * @brief Virtqueue notification callback
	 * Called to signal the VirtIO driver about completed buffers.
	 */
	void (*notify)(struct vringh *vr);

	/**
	 * @brief Queue kick callback
	 * Called when the VirtIO driver notifies (kicks) the queue.
	 */
	void (*kick)(struct vringh *vr);
};

/**
 * @brief VirtQueue I/O vector structure
 *
 * Manages iovec array for processing VirtQueue descriptor chains.
 * Tracks current position and handles partial buffer consumption.
 */
struct vringh_iov {
	struct vhost_iovec *iov; /**< Array of I/O vectors */
	size_t i;                /**< Current iovec index */
	size_t consumed;         /**< Bytes consumed from current iovec */
	unsigned int max_num;    /**< Maximum number of iovecs */
	unsigned int used;       /**< Number of iovecs currently used */
};

/**
 * @brief Initialize VirtQueue ring handler with VHost device
 *
 * @param vrh              VirtQueue ring handler to initialize
 * @param dev              VHost backend device
 * @param queue_id         Queue ID to handle
 * @param desc_bufs        Scratch buffer for descriptor chain parsing. It must remain valid
 *                         until the vringh is no longer used.
 * @param desc_bufs_count  Number of entries in desc_bufs
 * @param kick_callback    Queue kick callback invoked when the VirtIO driver notifies the queue
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Device or queue not found
 * @retval -ENOMEM  Insufficient memory
 * @retval -E2BIG   Descriptor scratch buffer is too small for the queue
 * @retval -EBUSY   Queue already in use
 * @retval -ENOTCONN Device not connected
 *
 * @code{.c}
 * static void kick_handler(struct vringh *vrh)
 * {
 *     uint16_t head;
 *
 *     while (vringh_getdesc(vrh, &riov, &wiov, &head) > 0) {
 *         process_buffers(&riov, &wiov);
 *         vringh_complete(vrh, head, total_bytes_written(&wiov));
 *         if (vringh_need_notify(vrh) > 0) {
 *             vringh_notify(vrh);
 *         }
 *     }
 * }
 *
 * int ret = vringh_init_device(&vrh, vhost_dev, 0, desc_bufs,
 *                              ARRAY_SIZE(desc_bufs), kick_handler);
 * @endcode
 */
int vringh_init_device(struct vringh *vrh, const struct device *dev, uint16_t queue_id,
		       struct vhost_buf *desc_bufs, size_t desc_bufs_count,
		       void (*kick_callback)(struct vringh *vrh));

/**
 * @brief Retrieve next available descriptor from VirtQueue
 *
 * Maps descriptor chain into host-accessible iovecs.
 * Separates readable and writable buffers per VIRTIO specification.
 * Calls to this function for the same vringh must be serialized by the caller.
 *
 * @param vrh       VirtQueue ring handler
 * @param riov      IOV for readable buffers
 * @param wiov      IOV for writable buffers
 * @param head_out  Descriptor head index for completion
 *
 * @retval 1        Success - descriptor retrieved
 * @retval 0        No descriptors available
 * @retval -errno   Invalid parameters
 */
int vringh_getdesc(struct vringh *vrh, struct vringh_iov *riov, struct vringh_iov *wiov,
		   uint16_t *head_out);

/**
 * @brief Complete processing of VirtQueue descriptor
 *
 * Marks descriptor as completed and adds entry to used ring.
 * Based on Linux vringh complete operation.
 *
 * @param vrh   VirtQueue ring handler
 * @param head  Descriptor head index from vringh_getdesc()
 * @param len   Total bytes written to writable buffers
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Ring handler not initialized with a device
 * @retval -errno   vhost_release_iovec() failed
 * @warning Do not call multiple times for the same descriptor
 *
 * @code{.c}
 * // After processing a descriptor chain
 * uint32_t bytes_written = generate_response(&wiov);
 * int ret = vringh_complete(&vrh, head, bytes_written);
 * if (ret == 0) {
 *     // Check if guest notification needed
 *     if (vringh_need_notify(&vrh) > 0) {
 *         vringh_notify(&vrh);
 *     }
 * }
 * @endcode
 */
int vringh_complete(struct vringh *vrh, uint16_t head, uint32_t len);

/**
 * @brief Abandon processing of descriptors without completion
 *
 * Returns descriptors to available state for re-processing.
 * Based on Linux vringh abandon operation.
 *
 * @param vrh  VirtQueue ring handler
 * @param num  Number of descriptors to abandon
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Ring handler not initialized with a device
 * @retval -ERANGE  Cannot abandon more than retrieved
 * @retval -errno   vhost_release_iovec() failed
 */
int vringh_abandon(struct vringh *vrh, uint32_t num);

__maybe_unused static inline void vringh_iov_init(struct vringh_iov *iov, struct vhost_iovec *kvec,
						  unsigned int num)
{
	iov->used = iov->i = 0;
	iov->consumed = 0;
	iov->max_num = num;
	iov->iov = kvec;
}

/**
 * @brief Reset IOV structure for reuse
 *
 * @param iov  IOV structure to reset
 */
void vringh_iov_reset(struct vringh_iov *iov);

/**
 * @brief Check if guest notification is required
 *
 * Determines whether guest should be notified based on VIRTIO
 * notification suppression mechanism.
 * Based on Linux vringh need_notify implementation.
 *
 * @param vrh  VirtQueue ring handler
 *
 * @retval 1        Notification required
 * @retval 0        Notification suppressed
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Ring handler not initialized with a device
 */
int vringh_need_notify(struct vringh *vrh);

/**
 * @brief Send notification to guest about completed buffers
 *
 * Invokes registered notification callback to inform guest.
 * Based on Linux vringh notify implementation.
 *
 * @param vrh  VirtQueue ring handler
 */
void vringh_notify(struct vringh *vrh);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_DRIVERS_VHOST_VRINGH_H_ */
