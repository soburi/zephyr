/*
 * Copyright (c) 2018 PHYTEC Messtechnik GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/display/cfb.h>
#include <stdio.h>

int main(void)
{
	const struct device *dev;
	struct cfb_display *disp;
	struct cfb_framebuffer *fb;
	uint16_t x_res;
	uint16_t y_res;
	uint16_t rows;
	uint8_t ppt;
	uint8_t font_width;
	uint8_t font_height;

	dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(dev)) {
		printf("Device %s not ready\n", dev->name);
		return 0;
	}

	if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO10) != 0) {
		if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO01) != 0) {
			printf("Failed to set required pixel format");
			return 0;
		}
	}

	printf("Initialized %s\n", dev->name);

	disp = cfb_display_alloc(dev);
	if (!disp) {
		printf("Framebuffer allocation failed!\n");
		return 0;
	}

	fb = cfb_display_get_framebuffer(disp);

	cfb_clear(fb, true);

	display_blanking_off(dev);

	x_res = cfb_get_display_parameter(disp, CFB_DISPLAY_WIDTH);
	y_res = cfb_get_display_parameter(disp, CFB_DISPLAY_HEIGH);
	rows = cfb_get_display_parameter(disp, CFB_DISPLAY_ROWS);
	ppt = cfb_get_display_parameter(disp, CFB_DISPLAY_PPT);

	for (int idx = 0; idx < 42; idx++) {
		if (cfb_get_font_size(idx, &font_width, &font_height)) {
			break;
		}
		cfb_set_font(fb, idx);
		printf("font width %d, font height %d\n",
		       font_width, font_height);
	}

	printf("x_res %d, y_res %d, ppt %d, rows %d, cols %d\n",
	       x_res,
	       y_res,
	       ppt,
	       rows,
	       cfb_get_display_parameter(disp, CFB_DISPLAY_COLS));

	cfb_invert(fb);

	cfb_set_kerning(fb, 3);

	while (1) {
		for (int i = 0; i < MIN(x_res, y_res); i++) {
			cfb_clear(fb, false);
			if (cfb_print(fb,
				      "0123456789mMgj!\"§$%&/()=",
				      i, i)) {
				printf("Failed to print a string\n");
				continue;
			}

			cfb_finalize(fb);
#if defined(CONFIG_ARCH_POSIX)
			k_sleep(K_MSEC(20));
#endif
		}
	}
	return 0;
}
