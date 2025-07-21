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
 * This API follows Linux vringh terminology where practical, but is not source
 * or ABI compatible with it.
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
 * Host-side interface for processing guest split virtqueues.
 */
struct vringh {
	bool event_indices;          /**< Reserved; VIRTIO_RING_F_EVENT_IDX is not supported */
	bool weak_barriers;          /**< Reserved; weak barriers are not implemented */
	uint16_t last_avail_idx;     /**< Next available ring index to consume */
	uint16_t last_used_idx;      /**< Next used ring index to publish */
	uint32_t completed;          /**< Used entries published since last notification */
	struct vhost_vring vring;    /**< VirtQueue ring components */
	const struct device *dev;    /**< Associated VHost backend device */
	uint16_t queue_id;           /**< Queue ID within VHost device */
	struct vhost_buf *desc_bufs; /**< Descriptor scratch buffer */
	size_t desc_bufs_count;      /**< Number of entries in desc_bufs */
	struct k_spinlock lock;      /**< Spinlock for vring state updates */

	/**
	 * @brief Optional guest notification callback
	 *
	 * Overrides the VHost backend notification operation when set.
	 */
	void (*notify)(struct vringh *vr);

	/**
	 * @brief Optional queue kick callback
	 *
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
 * @param kick_callback    Optional queue kick callback. May be @c NULL.
 *
 * @retval 0         Success
 * @retval -EINVAL   Invalid parameters or queue size
 * @retval -E2BIG    Descriptor scratch buffer is too small for the queue
 * @retval -ENOTSUP  A negotiated ring feature is not supported
 * @retval -errno    Error returned by the VHost backend
 */
int vringh_init_device(struct vringh *vrh, const struct device *dev, uint16_t queue_id,
		       struct vhost_buf *desc_bufs, size_t desc_bufs_count,
		       void (*kick_callback)(struct vringh *vrh));

/**
 * @brief Retrieve next available descriptor from VirtQueue
 *
 * Maps descriptor chain into host-accessible iovecs.
 * Separates readable buffers followed by writable buffers as required by VIRTIO.
 * Calls to this function, vringh_complete(), and vringh_abandon() for the same
 * vringh must be serialized by the caller.
 *
 * @param vrh       VirtQueue ring handler
 * @param riov      IOV for readable buffers
 * @param wiov      IOV for writable buffers
 * @param head_out  Descriptor head index for completion
 *
 * @retval 1         Descriptor retrieved
 * @retval 0         No descriptors available
 * @retval -EINVAL   Invalid parameters or malformed descriptor chain
 * @retval -ENODEV   Ring handler not initialized with a device
 * @retval -ENOTSUP  Unsupported descriptor flags
 * @retval -E2BIG    Descriptor chain or scratch buffer is too large
 * @retval -errno    Error returned by the VHost backend
 */
int vringh_getdesc(struct vringh *vrh, struct vringh_iov *riov, struct vringh_iov *wiov,
		   uint16_t *head_out);

/**
 * @brief Complete processing of VirtQueue descriptor
 *
 * Marks descriptor as completed and adds entry to used ring.
 *
 * @param vrh   VirtQueue ring handler
 * @param head  Descriptor head index from vringh_getdesc()
 * @param len   Total bytes written to writable buffers
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Ring handler not initialized with a device
 * @retval -errno   vhost_release_iovec() failed
 * @warning Do not call multiple times for the same descriptor.
 * @warning Calls to vringh_getdesc(), vringh_complete(), and vringh_abandon()
 *          for the same vringh must be serialized by the caller.
 */
int vringh_complete(struct vringh *vrh, uint16_t head, uint32_t len);

/**
 * @brief Abandon processing of descriptors without completion
 *
 * Undoes the @p num most recent successful calls to vringh_getdesc().
 * Subsequent calls to vringh_getdesc() return those descriptors again.
 * None of those calls may have been completed with vringh_complete().
 *
 * @param vrh  VirtQueue ring handler
 * @param num  Number of vringh_getdesc() calls to undo
 *
 * @retval 0        Success
 * @retval -EINVAL  Invalid parameters
 * @retval -ENODEV  Ring handler not initialized with a device
 * @retval -ERANGE  Cannot abandon more than retrieved
 * @retval -errno   vhost_release_iovec() failed
 * @warning Calls to vringh_getdesc(), vringh_complete(), and vringh_abandon()
 *          for the same vringh must be serialized by the caller.
 */
int vringh_abandon(struct vringh *vrh, uint32_t num);

/**
 * @brief Initialize an I/O vector
 *
 * @param iov   I/O vector to initialize
 * @param kvec  Iovec storage
 * @param num   Number of entries in @p kvec
 */
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
 * Restores a partially consumed current entry and resets the traversal state.
 *
 * @param iov  IOV structure to reset
 */
void vringh_iov_reset(struct vringh_iov *iov);

/**
 * @brief Check if guest notification is required
 *
 * Checks the VIRTQ_AVAIL_F_NO_INTERRUPT notification suppression flag.
 * VIRTIO_RING_F_EVENT_IDX is not supported.
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
 * Does nothing when notifications are suppressed. Otherwise, invokes the
 * optional notification callback or the VHost backend notification operation.
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
