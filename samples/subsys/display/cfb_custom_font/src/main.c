/*
 * Copyright (c) 2018 Henrik Brix Andersen <henrik@brixandersen.dk>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/printk.h>

#include "cfb_font_dice.h"

#if CONFIG_CHARACTER_FRAMEBUFFER_CUSTOM_FONT_SAMPLE_TRANSFER_BUFFER_SIZE != 0
static uint8_t transfer_buffer[CONFIG_CHARACTER_FRAMEBUFFER_CUSTOM_FONT_SAMPLE_TRANSFER_BUFFER_SIZE];
#endif

int main(void)
{
	const struct device *const dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct cfb_framebuffer *fb;
	int err;
#if CONFIG_CHARACTER_FRAMEBUFFER_CUSTOM_FONT_SAMPLE_TRANSFER_BUFFER_SIZE != 0
	struct cfb_framebuffer framebuffer;
	struct cfb_init_param param = {
		.transfer_buf = transfer_buffer,
		.transfer_buf_size = sizeof(transfer_buffer),
	};
#endif

	if (!device_is_ready(dev)) {
		printk("Display device not ready\n");
	}

	if (display_blanking_off(dev) != 0) {
		printk("Failed to turn off display blanking\n");
		return 0;
	}

#if CONFIG_CHARACTER_FRAMEBUFFER_CUSTOM_FONT_SAMPLE_TRANSFER_BUFFER_SIZE != 0
	fb = &framebuffer;
	err = cfb_init(&framebuffer, &param);
	if (err) {
		printk("Could not initialize framebuffer (err %d)\n", err);
		return 0;
	}
#else
	fb = cfb_alloc(dev);
	if (!fb) {
		printk("Could not allocate framebuffer\n");
		return 0;
	}
#endif

	err = cfb_clear(fb, true);
	if (err) {
		printk("Could not clear framebuffer (err %d)\n", err);
	}

	err = cfb_print(fb, "123456", 0, 0);
	if (err) {
		printk("Could not display custom font (err %d)\n", err);
	}

	err = cfb_finalize(fb);
	if (err) {
		printk("Could not finalize framebuffer (err %d)\n", err);
	}
	return 0;
}
