#include <sys/types.h>
#include <inttypes.h>
#include <stdint.h>

#include <xenstore_cli.h>

#include <xen/public/xen.h>
#include <xen/public/hvm/ioreq.h>
#include <xen/public/hvm/dm_op.h>
#include <xen/public/hvm/hvm_op.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/spinlock.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/xen/generic.h>
#include <zephyr/xen/events.h>
#include <zephyr/xen/hvm.h>
#include <zephyr/xen/gnttab.h>
#include <zephyr/xen/memory.h>
#include <zephyr/xen/dmop.h>
#include <zephyr/drivers/virtio.h>
#include <zephyr/drivers/virtio/virtio_config.h>
#include <zephyr/drivers/virtio/virtqueue.h>
#include <zephyr/drivers/vhost.h>
#include <zephyr/drivers/vhost/vringh.h>
#include <zephyr/sys/util_macro.h>

#include <zephyr/logging/log.h>

/**
 * Get the nth string from a null-separated string buffer
 */
static const char *nth_str(const char *buf, size_t len, size_t n)
{
	int cnt = 0;

	if (n == 0) {
		return buf;
	}

	for (size_t i = 0; i < len; i++) {
		if (buf[i] == '\0') {
			cnt++;
		}

		if (cnt == n && (i != (len - 1))) {
			return &buf[i + 1];
		}
	}

	return NULL;
}

/**
 * Query VIRTIO frontend's domid/deviceid from XenStore
 */
int query_virtio_backend(const char *key, const char *expected, domid_t *domid, int *deviceid)
{
	char buf[65] = {0};
	const size_t len = ARRAY_SIZE(buf) - 1;
	const char *ptr_i, *ptr_j;
	int i, j;

	const ssize_t len_i = xs_directory("backend/virtio", buf, len, 0);

	if (len_i < 0) {
		return -EIO;
	}
	if (len_i == 6 && strncmp(buf, "ENOENT", len) == 0) {
		return -ENOENT;
	}

	for (i = 0, ptr_i = buf; ptr_i; ptr_i = nth_str(buf, len_i, i++)) {
		char *endptr;

		*domid = strtol(ptr_i, &endptr, 10);
		if (*endptr != '\0') {
			continue;
		}

		snprintf(buf, len, "backend/virtio/%d", *domid);

		const ssize_t len_j = xs_directory(buf, buf, ARRAY_SIZE(buf), 0);

		if (len_j < 0 || strncmp(buf, "ENOENT", ARRAY_SIZE(buf)) == 0) {
			continue;
		}

		for (j = 0, ptr_j = buf; ptr_j; ptr_j = nth_str(buf, len_j, j++)) {
			*deviceid = strtol(ptr_j, &endptr, 10);
			if (*endptr != '\0') {
				continue; /* Skip invalid device ID */
			}

			bool match = true;

			snprintf(buf, len, "backend/virtio/%d/%d/%s", *domid, *deviceid, key);
			const ssize_t len_k = xs_read(buf, buf, ARRAY_SIZE(buf), 0);

			if ((len_k < 0) || (strncmp(buf, "ENOENT", ARRAY_SIZE(buf)) == 0) ||
			    (strncmp(expected, buf, ARRAY_SIZE(buf)) != 0)) {
				match = false;
			}

			if (match) {
				return 0;
			}
		}
	}

	return -ENOENT;
}

uintptr_t query_irq(domid_t domid, int deviceid)
{
	char buf[65] = {0};
	size_t len = ARRAY_SIZE(buf) - 1;
	char *endptr;

	snprintf(buf, len, "backend/virtio/%d/%d/irq", domid, deviceid);

	len = xs_read(buf, buf, ARRAY_SIZE(buf) - 1, 0);
	if ((len < 0) || (strncmp(buf, "ENOENT", ARRAY_SIZE(buf)) == 0)) {
		return (uintptr_t)-1;
	}

	uintptr_t irq_val = strtol(buf, &endptr, 10);

	if (*endptr != '\0') {
		return (uintptr_t)-1;
	}

	return irq_val;
}

