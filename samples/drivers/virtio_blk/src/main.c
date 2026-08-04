/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/sys/printk.h>

#define DISK_NAME "VIRTIOBLK0"

static uint8_t test_byte(size_t offset)
{
	return (uint8_t)(0xa5U ^ offset);
}

static int restore_sector(uint32_t sector, const uint8_t *data)
{
	int ret;

	ret = disk_access_write(DISK_NAME, data, sector, 1);
	if (ret != 0) {
		printk("Failed to restore sector %u: %d\n", sector, ret);
		return ret;
	}

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	if (ret != 0) {
		printk("Failed to flush the restored sector: %d\n", ret);
	}

	return ret;
}

int main(void)
{
	uint8_t *original = NULL;
	uint8_t *buffer = NULL;
	uint32_t sector_count;
	uint32_t sector_size;
	uint32_t test_sector;
	bool test_written = false;
	int ret;

	ret = disk_access_init(DISK_NAME);
	if (ret != 0) {
		printk("Failed to initialize %s: %d\n", DISK_NAME, ret);
		return ret;
	}

	ret = disk_access_status(DISK_NAME);
	if (ret != DISK_STATUS_OK) {
		printk("Disk %s is not ready: 0x%x\n", DISK_NAME, ret);
		return -EIO;
	}

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_COUNT, &sector_count);
	if (ret != 0) {
		printk("Failed to get the sector count: %d\n", ret);
		return ret;
	}

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_GET_SECTOR_SIZE, &sector_size);
	if (ret != 0) {
		printk("Failed to get the sector size: %d\n", ret);
		return ret;
	}

	if ((sector_count == 0U) || (sector_size == 0U)) {
		printk("Disk reports an invalid geometry\n");
		return -EINVAL;
	}

	printk("VirtIO block disk %s: %u sectors of %u bytes\n", DISK_NAME, sector_count,
	       sector_size);

	original = k_malloc(sector_size);
	buffer = k_malloc(sector_size);
	if ((original == NULL) || (buffer == NULL)) {
		printk("Failed to allocate sector buffers\n");
		ret = -ENOMEM;
		goto out;
	}

	test_sector = sector_count / 2U;
	ret = disk_access_read(DISK_NAME, original, test_sector, 1);
	if (ret != 0) {
		printk("Failed to save sector %u: %d\n", test_sector, ret);
		goto out;
	}

	for (size_t i = 0; i < sector_size; i++) {
		buffer[i] = test_byte(i);
	}

	ret = disk_access_write(DISK_NAME, buffer, test_sector, 1);
	if (ret != 0) {
		printk("Failed to write sector %u: %d\n", test_sector, ret);
		goto out;
	}
	test_written = true;

	ret = disk_access_ioctl(DISK_NAME, DISK_IOCTL_CTRL_SYNC, NULL);
	if (ret != 0) {
		printk("Failed to flush the test sector: %d\n", ret);
		goto out;
	}

	memset(buffer, 0, sector_size);
	ret = disk_access_read(DISK_NAME, buffer, test_sector, 1);
	if (ret != 0) {
		printk("Failed to read sector %u: %d\n", test_sector, ret);
		goto out;
	}

	for (size_t i = 0; i < sector_size; i++) {
		if (buffer[i] != test_byte(i)) {
			printk("Data mismatch at byte %zu\n", i);
			ret = -EIO;
			goto out;
		}
	}

	printk("Wrote and verified sector %u\n", test_sector);

out:
	if (test_written) {
		int restore_ret = restore_sector(test_sector, original);

		if (restore_ret == 0) {
			printk("Original sector restored\n");
		} else if (ret == 0) {
			ret = restore_ret;
		}
	}

	k_free(buffer);
	k_free(original);

	if (ret == 0) {
		printk("VirtIO block sample completed successfully\n");
	}

	return ret;
}
