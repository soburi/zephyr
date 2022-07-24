/*
 * Copyright (c) 2022 TOKITA Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/disk_access.h>

#include <ff.h>

static const char *anim_data[] = {CONFIG_FS_MOUNT_POINT "/" CONFIG_ANIMATION_DATA_0,
				  CONFIG_FS_MOUNT_POINT "/" CONFIG_ANIMATION_DATA_1};

static FATFS fat_fs;
static struct fs_mount_t mountpoint = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = CONFIG_FS_MOUNT_POINT,
};

static struct display_buffer_descriptor descriptor;
static struct display_capabilities capabilities;

static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);

int mount_disk_and_check_files(struct fs_mount_t *mp, const char **anim_data, size_t file_num)
{
	struct fs_dirent ent;
	int res;

	printk("Mounting disk...\n");

	res = fs_mount(mp);
	if (res != FR_OK) {
		printk("Error mounting disk.\n");
	}

	printk("Disk mounted.\n");

	printk("Checking files...\n");

	for (size_t i = 0; i < file_num; i++) {
		res = fs_stat(anim_data[i], &ent);
		if (res < 0) {
			return res;
		}
	}

	printk("All files exists.\n");

	return 0;
}

static uint8_t *setup_display_and_buffer(const struct device *disp,
					 struct display_buffer_descriptor *desc,
					 struct display_capabilities *caps, size_t buf_size)
{
	size_t depth;
	uint8_t *buf = NULL;
	size_t transfer_size;

	display_get_capabilities(disp, caps);
	printk("display_capabilities %p %p %p\n", disp, desc, caps);

	switch (caps->current_pixel_format) {
	case PIXEL_FORMAT_RGB_888:
	case PIXEL_FORMAT_ARGB_8888:
		depth = 4;
		break;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565:
		depth = 2;
		break;
	default:
		printk("pixel format not supported\n");
		return NULL;
	}

	transfer_size = caps->y_resolution * caps->x_resolution * depth;
	for (; transfer_size > buf_size; transfer_size /= 2) {
	}

	desc->pitch = caps->x_resolution;
	desc->width = caps->x_resolution;
	desc->height = transfer_size / (caps->x_resolution * depth);
	desc->buf_size = desc->height * caps->x_resolution * depth;
	printk("buffer_descriptor width=%d height=%d buf_size=%d\n", desc->width, desc->height,
	       desc->buf_size);

	buf = k_malloc(desc->buf_size);
	if (buf == NULL) {
		printk("Could not allocate memory. Aborting sample.\n");
		return NULL;
	}

	(void)memset(buf, 0xFFu, buf_size);

	printk("clear display\n");
	for (size_t y = 0; y < caps->y_resolution; y += desc->height) {
		display_write(disp, 0, y, desc, buf);
	}

	printk("display_blanking_off\n");
	display_blanking_off(disp);

	printk("Display ready. buf=%p\n", buf);

	return buf;
}

static int play_animation(const struct device *disp, struct display_buffer_descriptor *desc,
			  struct display_capabilities *caps, const char *filename, uint8_t *buf)
{
	struct fs_file_t file;
	struct fs_dirent ent;
	int res;

	fs_file_t_init(&file);

	res = fs_stat(filename, &ent);
	if (res < 0) {
		printk("fs stat() failed %d\n", res);
		return res;
	}

	res = fs_open(&file, filename, FS_O_READ);
	if (res < 0) {
		printk("fs open() failed %d\n", res);
		return res;
	}

	printk("File opend.\n");

	for (off_t offset = 0; offset < ent.size;) {
		for (size_t y = 0; y < caps->y_resolution; y += desc->height) {
			res = fs_read(&file, buf, desc->buf_size);
			if (res < 0 || res != desc->buf_size) {
				goto on_error;
			} else if (res != desc->buf_size) {
				res = -1;
				goto on_error;
			}

			display_write(disp, 0, y, desc, buf);
			offset += desc->buf_size;

			gpio_pin_toggle_dt(&led);
		}
	}
on_error:
	printk("fs_close %d\n", res);
	fs_close(&file);
	return res;
}

void main(void)
{
	const struct device *disp;
	size_t anim_count = 0;
	size_t anim_max = 0;
	uint8_t *buf;
	int res;

	for (size_t i = 0; i < ARRAY_SIZE(anim_data); i++) {
		if (strlen(anim_data[i]) == strlen(CONFIG_FS_MOUNT_POINT "/")) {
			break;
		}
		anim_max++;
	}
	printk("%d animation data available.\n", anim_max);

	disp = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(disp)) {
		printk("Device %s not ready. Aborting sample.\n", disp->name);
		return;
	}

	if (!device_is_ready(led.port)) {
		printk("Device %s not found. Aborting sample.\n", led.port->name);
		goto on_error;
	}

	res = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (res < 0) {
		goto on_error;
	}

	buf = setup_display_and_buffer(disp, &descriptor, &capabilities, CONFIG_HEAP_MEM_POOL_SIZE);
	if (!buf) {
		printk("setup_display_and_buffer() failed.\n");
		goto on_error;
	}

	res = mount_disk_and_check_files(&mountpoint, anim_data, anim_max);
	if (res < 0) {
		printk("mount_disk_and_check_files() failed %d.\n", res);
		goto on_error;
	}

	anim_count = 0;
	while (1) {
		res = play_animation(disp, &descriptor, &capabilities,
				     anim_data[anim_count % anim_max], buf);
		if (res < 0) {
			printk("play_animation error %d\n", res);
			goto on_error;
		}
		anim_count++;

		k_msleep(CONFIG_INTERVAL_TO_NEXT_ANIMATION);
	}

on_error:
	while (1) {
	}

	return;
}
