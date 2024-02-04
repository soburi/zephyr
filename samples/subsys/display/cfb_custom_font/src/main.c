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

static uint8_t transfer_buffer[DIV_ROUND_UP(
	DT_PROP(DT_CHOSEN(zephyr_display), width) * DT_PROP(DT_CHOSEN(zephyr_display), height), 8)];

int main(void)
{
	const struct device *const display = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	struct cfb_display_init_param param = {
		.dev = display,
		.transfer_buf = transfer_buffer,
		.transfer_buf_size = sizeof(transfer_buffer),
	};
	struct cfb_display disp;
	int err;

	if (!device_is_ready(display)) {
		printk("Display device not ready\n");
	}

	if (display_blanking_off(display) != 0) {
		printk("Failed to turn off display blanking\n");
		return 0;
	}

	err = cfb_display_init(&disp, &param);
	if (err) {
		printk("Could not initialize framebuffer (err %d)\n", err);
	}

	err = cfb_clear(&disp.fb, true);
	if (err) {
		printk("Could not clear framebuffer (err %d)\n", err);
	}

	err = cfb_print(&disp.fb, "123456", 0, 0);
	if (err) {
		printk("Could not display custom font (err %d)\n", err);
	}

	err = cfb_finalize(&disp.fb);
	if (err) {
		printk("Could not finalize framebuffer (err %d)\n", err);
	}
	return 0;
}
