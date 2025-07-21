/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <zephyr/spinlock.h>
#include <string.h>

LOG_MODULE_REGISTER(vhost_vringh);

int vringh_init(struct vringh *vrh, uint64_t features, uint16_t num, bool weak_barriers,
		struct virtq_desc *desc, struct virtq_avail *avail, struct virtq_used *used)
{
	if (!vrh || !desc || !avail || !used) {
		return -EINVAL;
	}

	memset(vrh, 0, sizeof(*vrh));

	vrh->event_indices = false; /* not supported */
	vrh->weak_barriers = weak_barriers;
	vrh->last_avail_idx = 0;
	vrh->last_used_idx = 0;
	vrh->completed = 0;

	vrh->vring.num = num;
	vrh->vring.desc = desc;
	vrh->vring.avail = avail;
	vrh->vring.used = used;

	return 0;
}

static void vringh_callback(const struct device *dev, uint16_t queue_id, void *ptr)
{
	struct vringh *vrh = ptr;

	if (vrh->received) {
		vrh->received(vrh);
	}
}

int vringh_init_device(struct vringh *vrh, const struct device *dev, uint16_t queue_id,
		       void (*notify_callback)(struct vringh *vrh))
{
	uint64_t drv_feats;
	void *parts[3];
	size_t q_num;
	int ret;

	if (!vrh || !dev) {
		return -EINVAL;
	}

	ret = vhost_get_virtq(dev, queue_id, parts, &q_num);
	if (ret < 0) {
		LOG_ERR("vhost_get_virtq failed: %d", ret);
		return ret;
	}

	ret = vhost_get_driver_features(dev, &drv_feats);
	if (ret < 0) {
		LOG_ERR("vhost_get_driver_features failed: %d", ret);
		return ret;
	}

	ret = vringh_init(vrh, drv_feats, q_num, false, parts[0], parts[1], parts[2]);
	if (ret < 0) {
		LOG_ERR("vringh_init failed: %d", ret);
		return ret;
	}

	vrh->dev = dev;
	vrh->queue_id = queue_id;
	vrh->received = notify_callback;

	ret = vhost_set_notify_callback(dev, queue_id, vringh_callback, (void *)vrh);
	if (ret < 0) {
		LOG_ERR("vhost_set_notify_callback failed: %d", ret);
		return ret;
	}

	return 0;
}

int vringh_getdesc(struct vringh *vrh, struct vringh_iov *riov, struct vringh_iov *wiov,
		   uint16_t *head_out)
{
	if (!vrh || !riov || !wiov || !head_out) {
		return -EINVAL;
	}

	barrier_dmem_fence_full();

	k_spinlock_key_t key = k_spin_lock(&vrh->lock);
	struct vring *vr = &vrh->vring;
	const uint16_t avail_idx = sys_le16_to_cpu(vrh->vring.avail->idx);
	const uint16_t slot = vrh->last_avail_idx % vr->num;
	const uint16_t head = sys_le16_to_cpu(vr->avail->ring[slot]);
	uint16_t idx = head;
	size_t count = 0;
	uint16_t flags;

	if (vrh->last_avail_idx == avail_idx) {
		k_spin_unlock(&vrh->lock, key);
		return 0;
	}

	if (head >= vrh->vring.num) {
		k_spin_unlock(&vrh->lock, key);
		LOG_ERR("Invalid descriptor head: %u >= %u", head, vrh->vring.num);
		return -EINVAL;
	}

	if (!vrh->event_indices) {
		flags = sys_le16_to_cpu(vr->used->flags);
		flags |= VIRTQ_USED_F_NO_NOTIFY;
		vr->used->flags = sys_cpu_to_le16(flags);
	}

	barrier_dmem_fence_full();

	k_spin_unlock(&vrh->lock, key);

	vringh_iov_reset(riov);
	vringh_iov_reset(wiov);

	do {
		const struct virtq_desc *d = &vr->desc[idx];
		const uint64_t gpa = sys_le64_to_cpu(d->addr);
		const uint32_t len = sys_le32_to_cpu(d->len);
		const uint16_t next = sys_le16_to_cpu(d->next);

		flags = sys_le16_to_cpu(d->flags);

		if (count >= vr->num) {
			LOG_ERR("Descriptor chain too long: %zu", count);
			return -ENOMEM;
		}
		count++;

		/* Validate next descriptor index */
		if ((flags & VIRTQ_DESC_F_NEXT) && next >= vr->num) {
			LOG_ERR("Invalid next descriptor: %u >= %u", next, vr->num);
			int rc = vhost_release_iovec(vrh->dev, vrh->queue_id, head);

			if (rc < 0) {
				LOG_ERR("vhost_release_iovec failed: %d", rc);
				vhost_set_device_status(vrh->dev, DEVICE_STATUS_FAILED);
			}
			vringh_iov_reset(riov);
			vringh_iov_reset(wiov);
			return -EINVAL;
		}

		if (len == 0) {
			LOG_WRN("Zero-length descriptor at index %u", idx);
			idx = next;
			continue;
		}

		struct vringh_iov *iov = (flags & VIRTQ_DESC_F_WRITE) ? wiov : riov;

		if (!iov->iov || iov->used >= iov->max_num) {
			LOG_ERR("IOV buffer full or invalid: used=%u max_num=%u iov=%p", iov->used,
				iov->max_num, iov->iov);
			int rc = vhost_release_iovec(vrh->dev, vrh->queue_id, head);

			if (rc < 0) {
				LOG_ERR("vhost_release_iovec failed: %d", rc);
				vhost_set_device_status(vrh->dev, DEVICE_STATUS_FAILED);
			}
			vringh_iov_reset(riov);
			vringh_iov_reset(wiov);
			return -E2BIG;
		}

		size_t filled;
		int ret = vhost_prepare_iovec(vrh->dev, vrh->queue_id, gpa, len, head,
					      &iov->iov[iov->used], iov->max_num - iov->used,
					      &filled);

		if (ret < 0) {
			LOG_ERR("vhost_prepare_iovec failed: %d", ret);
			int rc = vhost_release_iovec(vrh->dev, vrh->queue_id, head);

			if (rc < 0) {
				LOG_ERR("vhost_release_iovec failed: %d", rc);
				vhost_set_device_status(vrh->dev, DEVICE_STATUS_FAILED);
			}
			vringh_iov_reset(riov);
			vringh_iov_reset(wiov);

			return ret;
		}

		iov->used += filled;
		idx = next;
	} while (flags & VIRTQ_DESC_F_NEXT);

	/* Success - update state and return */
	*head_out = head;

	key = k_spin_lock(&vrh->lock);
	vrh->last_avail_idx++;
	k_spin_unlock(&vrh->lock, key);

	return 1;
}

int vringh_complete(struct vringh *vrh, uint16_t head, uint32_t total_len)
{
	struct vring *vr = &vrh->vring;
	int rc = 0;

	if (!vrh) {
		return -EINVAL;
	}

	rc = vhost_release_iovec(vrh->dev, vrh->queue_id, head);
	if (rc < 0) {
		LOG_ERR("vhost_release_iovec failed: %d", rc);
		vhost_set_device_status(vrh->dev, DEVICE_STATUS_FAILED);
		return rc;
	}

	k_spinlock_key_t key = k_spin_lock(&vrh->lock);

	const uint16_t used_idx = sys_le16_to_cpu(vr->used->idx);
	struct virtq_used_elem *ue = &vr->used->ring[used_idx % vr->num];

	LOG_DBG("used_idx %u ue={%u, %u}", used_idx, head, total_len);

	ue->id = sys_cpu_to_le32((uint32_t)head);
	ue->len = sys_cpu_to_le32(total_len);

	barrier_dmem_fence_full();
	vr->used->idx = sys_cpu_to_le16(used_idx + 1);
	vrh->last_used_idx++;

	uint16_t flags = sys_le16_to_cpu(vr->used->flags);

	flags &= ~VIRTQ_USED_F_NO_NOTIFY;
	vr->used->flags = sys_cpu_to_le16(flags);
	barrier_dmem_fence_full();

	k_spin_unlock(&vrh->lock, key);

	return rc;
}

static uint16_t abandon_head(struct vringh *vrh, uint16_t before)
{
	const struct vring *vr = &vrh->vring;
	const uint16_t avail_idx = sys_le16_to_cpu(vr->avail->idx);

	/* Prevent unsigned underflow */
	if (before > avail_idx) {
		LOG_ERR("Invalid before value: %u > %u", before, avail_idx);
		return 0;
	}

	const uint16_t ring_idx = (avail_idx - before) % vr->num;

	return sys_le16_to_cpu(vr->avail->ring[ring_idx]);
}

int vringh_abandon(struct vringh *vrh, uint32_t num)
{
	struct vring *vr = &vrh->vring;
	int rc = 0;

	if (num == 0) {
		return 0;
	}

	if (!vrh) {
		return -EINVAL;
	}

	for (int i = 0; i < num; i++) {
		const uint16_t head = abandon_head(vrh, i);

		rc = vhost_release_iovec(vrh->dev, vrh->queue_id, head);
		if (rc < 0) {
			LOG_ERR("vhost_release_iovec failed: %d", rc);
			vhost_set_device_status(vrh->dev, DEVICE_STATUS_FAILED);
		}
	}

	k_spinlock_key_t key = k_spin_lock(&vrh->lock);

	if (num <= vrh->last_avail_idx) {
		vrh->last_avail_idx -= num;
		LOG_DBG("Abandoned %u descs, new last_avail_idx: %u", num, vrh->last_avail_idx);
	} else {
		LOG_ERR("Cannot abandon %u descs, avail=%u", num, vrh->last_avail_idx);
		rc = -ERANGE;
	}

	uint16_t flags = sys_le16_to_cpu(vr->used->flags);

	flags &= ~VIRTQ_USED_F_NO_NOTIFY;
	vr->used->flags = sys_cpu_to_le16(flags);

	barrier_dmem_fence_full();

	k_spin_unlock(&vrh->lock, key);

	return rc;
}

void vringh_iov_reset(struct vringh_iov *iov)
{
	if (!iov || !iov->iov) {
		return;
	}

	if (iov->consumed > 0 && iov->i < iov->used) {
		iov->iov[iov->i].iov_len += iov->consumed;
		iov->iov[iov->i].iov_base = (char *)iov->iov[iov->i].iov_base - iov->consumed;
	}

	iov->consumed = 0;
	iov->i = 0;
	iov->used = 0;
}

int vringh_need_notify(struct vringh *vrh)
{
	if (!vrh) {
		return -EINVAL;
	}

	k_spinlock_key_t key = k_spin_lock(&vrh->lock);
	const uint16_t flags = sys_le16_to_cpu(vrh->vring.used->flags);

	k_spin_unlock(&vrh->lock, key);

	return !(flags & VIRTQ_USED_F_NO_NOTIFY);
}

void vringh_notify(struct vringh *vrh)
{
	k_spinlock_key_t key = k_spin_lock(&vrh->lock);
	const uint16_t flags = sys_le16_to_cpu(vrh->vring.avail->flags);

	k_spin_unlock(&vrh->lock, key);

	if (flags & VIRTQ_AVAIL_F_NO_INTERRUPT) {
		return;
	}

	vhost_queue_notify(vrh->dev, vrh->queue_id);
}
