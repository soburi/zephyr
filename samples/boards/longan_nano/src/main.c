/*
 * Copyright (c) 2019 Tavish Naruka <tavishnaruka@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Sample which uses the filesystem API and SDHC driver */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <storage/disk_access.h>
#include <logging/log.h>
#include <fs/fs.h>
#include <ff.h>
#include <stdlib.h>
#include <drivers/display.h>
#include <drivers/gpio.h>
#include <display/cfb.h>

LOG_MODULE_REGISTER(main);

struct led_pin {
	const char *devname;
	uint32_t pin;
	uint32_t flags;
};

struct led_pin pins[] = {
#if DT_NODE_HAS_STATUS(DT_ALIAS(led0), okay)
	{
		DT_GPIO_LABEL(DT_ALIAS(led0), gpios),
		DT_GPIO_PIN(DT_ALIAS(led0), gpios),
		DT_GPIO_FLAGS(DT_ALIAS(led0), gpios)
	},
#endif
#if DT_NODE_HAS_STATUS(DT_ALIAS(led1), okay)
	{
		DT_GPIO_LABEL(DT_ALIAS(led1), gpios),
		DT_GPIO_PIN(DT_ALIAS(led1), gpios),
		DT_GPIO_FLAGS(DT_ALIAS(led1), gpios)
	},
#endif
#if DT_NODE_HAS_STATUS(DT_ALIAS(led2), okay)
	{
		DT_GPIO_LABEL(DT_ALIAS(led2), gpios),
		DT_GPIO_PIN(DT_ALIAS(led2), gpios),
		DT_GPIO_FLAGS(DT_ALIAS(led2), gpios)
	},
#endif
};

#if DT_NODE_HAS_STATUS(DT_INST(0, ilitek_ili9340), okay)
#define DISPLAY_DEV_NAME DT_LABEL(DT_INST(0, ilitek_ili9340))
#endif

#if DT_NODE_HAS_STATUS(DT_INST(0, solomon_ssd1306fb), okay)
#define DISPLAY_DEV_NAME DT_LABEL(DT_INST(0, solomon_ssd1306fb))
#endif

#if DT_NODE_HAS_STATUS(DT_INST(0, solomon_ssd16xxfb), okay)
#define DISPLAY_DEV_NAME DT_LABEL(DT_INST(0, solomon_ssd16xxfb))
#endif

#if DT_NODE_HAS_STATUS(DT_INST(0, sitronix_st7789v), okay)
#define DISPLAY_DEV_NAME DT_LABEL(DT_INST(0, sitronix_st7789v))
#endif

#if DT_NODE_HAS_STATUS(DT_INST(0, sitronix_st7735r), okay)
#define DISPLAY_DEV_NAME DT_LABEL(DT_INST(0, sitronix_st7735r))
#endif

#if DT_NODE_HAS_STATUS(DT_INST(0, fsl_imx6sx_lcdif), okay)
#define DISPLAY_DEV_NAME DT_LABEL(DT_INST(0, fsl_imx6sx_lcdif))
#endif

#ifdef CONFIG_SDL_DISPLAY_DEV_NAME
#define DISPLAY_DEV_NAME CONFIG_SDL_DISPLAY_DEV_NAME
#endif

#ifdef CONFIG_DUMMY_DISPLAY_DEV_NAME
#define DISPLAY_DEV_NAME CONFIG_DUMMY_DISPLAY_DEV_NAME
#endif
#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

#ifdef CONFIG_ARCH_POSIX
#define RETURN_FROM_MAIN(exit_code) posix_exit_main(exit_code)
#else
#define RETURN_FROM_MAIN(exit_code) return
#endif

#define DISK_NAME "SD"
#define MOUNT_POINT  "/" DISK_NAME ":"
#define LOGO_FILE MOUNT_POINT "/LOGO.BIN"
#define  BMP_FILE MOUNT_POINT "/BMP.BIN"

static FATFS fat_fs;
/* mounting info */
static struct fs_mount_t mp = {
	.type = FS_FATFS,
	.fs_data = &fat_fs,
	.mnt_point = MOUNT_POINT,
};

static inline uint32_t get_pixel_depth(const enum display_pixel_format pixel_format)
{
	switch (pixel_format) {
	case PIXEL_FORMAT_ARGB_8888:
		return 4;
	case PIXEL_FORMAT_RGB_888:
		return 3;
	case PIXEL_FORMAT_RGB_565:
	case PIXEL_FORMAT_BGR_565:
		return 2;
	case PIXEL_FORMAT_MONO01:
	case PIXEL_FORMAT_MONO10:
		return 1;
	}

	return 0;
}

static inline uint32_t draw_frame(struct fs_file_t *fs, const struct device *display_dev, void *buf, size_t bufsize)
{
	struct display_capabilities caps;

	display_get_capabilities(display_dev, &caps);

	size_t linebytes = caps.x_resolution * get_pixel_depth(caps.current_pixel_format);
	size_t readlines = bufsize / linebytes;
	size_t lines = caps.y_resolution;

	struct display_buffer_descriptor buf_desc;
	buf_desc.pitch = caps.x_resolution;
	buf_desc.width = caps.x_resolution;
	buf_desc.height = readlines;

	while (lines > 0) {
		ssize_t sz = fs_read(fs, buf, bufsize);

		if (sz == 0) {
			return -ENODATA;
		} else if (sz < 0) {
			return sz;
		}

		buf_desc.height = (lines < readlines) ? lines : readlines;

		display_write(display_dev, 0, caps.y_resolution - lines, &buf_desc, buf);

		lines -= readlines;
	}

	return 0;
}

static uint32_t play_anim(const struct device *display_dev, const char *filename, void *buf, size_t bufsize)
{
	struct fs_file_t fs;

	fs_file_t_init(&fs);
	uint32_t ret = fs_open(&fs, filename, FS_O_READ);
	if (ret) {
		printk("fs_open %s error=%d\n", filename, ret);
		return 0;
	}

	do {
		ret = draw_frame(&fs, display_dev, buf, bufsize);
	} while (ret == 0);

	printk("end drawanim\n");

	fs_close(&fs);
	return ret;
}

void main(void)
{
	const struct device *display_dev;
	struct display_capabilities capabilities;
	int ret;
	char* buf;

	for (int i = 0; i < sizeof(pins) / sizeof(struct led_pin); i++) {
		const struct device *dev = device_get_binding(pins[i].devname);
		gpio_pin_toggle(dev, pins[i].pin);
		if (dev == NULL) {
			continue;
		}
		gpio_pin_configure(dev, pins[i].pin, GPIO_OUTPUT_ACTIVE | pins[i].flags);
	}

	display_dev = device_get_binding(DISPLAY_DEV_NAME);
	printk("Display: %s\n", DISPLAY_DEV_NAME);

	display_get_capabilities(display_dev, &capabilities);

	uint32_t bufsize = capabilities.x_resolution * capabilities.y_resolution * get_pixel_depth(capabilities.current_pixel_format);
	bufsize = 12800;
	printk("display buffer size = %d\n", bufsize);


	if (disk_access_init(DISK_NAME) != 0) {
		LOG_ERR("Storage init ERROR!");
		goto error;
	}

	ret = fs_mount(&mp);
	if (ret) {
		printk("Error mounting disk. %d\n", ret);
		goto error;
	}

	buf = k_malloc(bufsize);

	while (true) {
		printk("draw logo\n");
		ret = play_anim(display_dev, LOGO_FILE, buf, bufsize);
		if (ret != 0 && ret != -ENODATA) {
			printk("end logo %d\n", ret);
			goto error;
		}
		k_msleep(1500);

		printk("draw bmp\n");
		ret = play_anim(display_dev, BMP_FILE, buf, bufsize);
		if (ret != 0 && ret != -ENODATA) {
			goto error;
		}
	}

	k_free(buf);
	return;
error:

	if (cfb_framebuffer_init(display_dev)) {
		printk("Framebuffer initialization failed!\n");
		return;
	}

	cfb_framebuffer_clear(display_dev, true);

	for (int idx = 0; idx < 42; idx++) {
		uint8_t font_width;
		uint8_t font_height;
		if (cfb_get_font_size(display_dev, idx, &font_width, &font_height)) {
			break;
		}
		cfb_framebuffer_set_font(display_dev, idx);
		printk("font width %d, font height %d\n",
		       font_width, font_height);
		break;
	}

	cfb_framebuffer_clear(display_dev, false);
	ret = cfb_print(display_dev, "no card found!", 8, 0);
	ret = cfb_print(display_dev, "no card found!", 8, 16);
	ret = cfb_print(display_dev, "no card found!", 8, 32);
	ret = cfb_print(display_dev, "no card found!", 8, 48);
	ret = cfb_print(display_dev, "no card found!", 8, 64);
	if (ret) {
		printk("Failed to print a string %d\n", ret);
		//continue;
	}

	cfb_framebuffer_finalize(display_dev);

	while (1) {
		for (int i = 0; i < sizeof(pins) / sizeof(struct led_pin); i++) {
			const struct device *dev = device_get_binding(pins[i].devname);
			gpio_pin_toggle(dev, pins[i].pin);
			k_msleep(200);
		}
	}

	return;
}
