#if 0
/*
 * Copyright (c) 2017 Linaro Limited
 * Copyright (c) 2018 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <errno.h>
#include <string.h>

#define LOG_LEVEL 4
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main);

#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/sys/util.h>

#define STRIP_NODE		DT_ALIAS(led_strip)
#define STRIP_NUM_PIXELS	DT_PROP(DT_ALIAS(led_strip), chain_length)

#define DELAY_TIME K_MSEC(50)

#define RGB(_r, _g, _b) { .r = (_r), .g = (_g), .b = (_b) }

static const struct led_rgb colors[] = {
	RGB(0x0f, 0x00, 0x00), /* red */
	RGB(0x00, 0x0f, 0x00), /* green */
	RGB(0x00, 0x00, 0x0f), /* blue */
};

struct led_rgb pixels[STRIP_NUM_PIXELS];

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);

int main(void)
{
	size_t cursor = 0, color = 0;
	int rc;

	if (device_is_ready(strip)) {
		LOG_INF("Found LED strip device %s", strip->name);
	} else {
		LOG_ERR("LED strip device %s is not ready", strip->name);
		return 0;
	}

	LOG_INF("Displaying pattern on strip");
	while (1) {
		memset(&pixels, 0x00, sizeof(pixels));
		memcpy(&pixels[cursor], &colors[color], sizeof(struct led_rgb));
		rc = led_strip_update_rgb(strip, pixels, STRIP_NUM_PIXELS);

		if (rc) {
			LOG_ERR("couldn't update strip: %d", rc);
		}

		cursor++;
		if (cursor >= STRIP_NUM_PIXELS) {
			cursor = 0;
			color++;
			if (color == ARRAY_SIZE(colors)) {
				color = 0;
			}
		}

		k_sleep(DELAY_TIME);
	}
	return 0;
}
#endif

/*
 * Copyright 2023 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <u8g2.h>
#include <u8g2_zephyr.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

#ifdef CONFIG_ARCH_POSIX
#include "posix_board_if.h"
#endif

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(u8g2_logo, LOG_LEVEL_INF);

static void drawLogo(u8g2_t *u8g2)
{
	uint8_t mdy = 0;

	if (u8g2_GetDisplayHeight(u8g2) < 59) {
		mdy = 5;
	}

	u8g2_SetFontMode(u8g2, 1); /* Transparent */
	u8g2_SetDrawColor(u8g2, 1);
#ifdef MINI_LOGO

	u8g2_SetFontDirection(u8g2, 0);
	u8g2_SetFont(u8g2, u8g2_font_inb16_mf);
	u8g2_DrawStr(u8g2, 0, 22, "U");

	u8g2_SetFontDirection(u8g2, 1);
	u8g2_SetFont(u8g2, u8g2_font_inb19_mn);
	u8g2_DrawStr(u8g2, 14, 8, "8");

	u8g2_SetFontDirection(u8g2, 0);
	u8g2_SetFont(u8g2, u8g2_font_inb16_mf);
	u8g2_DrawStr(u8g2, 36, 22, "g");
	u8g2_DrawStr(u8g2, 48, 22, "\xb2");

	u8g2_DrawHLine(u8g2, 2, 25, 34);
	u8g2_DrawHLine(u8g2, 3, 26, 34);
	u8g2_DrawVLine(u8g2, 32, 22, 12);
	u8g2_DrawVLine(u8g2, 33, 23, 12);
#else

	u8g2_SetFontDirection(u8g2, 0);
	u8g2_SetFont(u8g2, u8g2_font_inb24_mf);
	u8g2_DrawStr(u8g2, 0, 30 - mdy, "U");

	u8g2_SetFontDirection(u8g2, 1);
	u8g2_SetFont(u8g2, u8g2_font_inb30_mn);
	u8g2_DrawStr(u8g2, 21, 8 - mdy, "8");

	u8g2_SetFontDirection(u8g2, 0);
	u8g2_SetFont(u8g2, u8g2_font_inb24_mf);
	u8g2_DrawStr(u8g2, 51, 30 - mdy, "g");
	u8g2_DrawStr(u8g2, 67, 30 - mdy, "\xb2");

	u8g2_DrawHLine(u8g2, 2, 35 - mdy, 47);
	u8g2_DrawHLine(u8g2, 3, 36 - mdy, 47);
	u8g2_DrawVLine(u8g2, 45, 32 - mdy, 12);
	u8g2_DrawVLine(u8g2, 46, 33 - mdy, 12);

#endif
}

void drawURL(u8g2_t *u8g2)
{
#ifndef MINI_LOGO
	u8g2_SetFont(u8g2, u8g2_font_4x6_tr);
	if (u8g2_GetDisplayHeight(u8g2) < 59) {
		u8g2_DrawStr(u8g2, 89, 20 - 5, "github.com");
		u8g2_DrawStr(u8g2, 8973, 29 - 5, "/olikraus/u8g2");
	} else {
		u8g2_DrawStr(u8g2, 1, 54, "github.com/olikraus/u8g2");
	}
#endif
}

int main(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	u8g2_t *u8g2;

	if (!device_is_ready(dev)) {
		LOG_ERR("Device %s not found. Aborting sample.", dev->name);
		return 0;
	}

	if (display_set_pixel_format(dev, PIXEL_FORMAT_MONO10) != 0) {
		LOG_ERR("Failed to set required pixel format.");
		return 0;
	}

	if (display_blanking_off(dev) != 0) {
		LOG_ERR("Failed to turn off display blanking.");
		return 0;
	}

	u8g2 = u8g2_SetupZephyr(dev, U8G2_R0);
	if (!u8g2) {
		LOG_ERR("Failed to allocate u8g2 buffer.");
		return 0;
	}

	u8x8_InitDisplay(u8g2_GetU8x8(u8g2));

	u8g2_ClearBuffer(u8g2);

	u8g2_FirstPage(u8g2);
	do {
		drawLogo(u8g2);
		drawURL(u8g2);
	} while (u8g2_NextPage(u8g2));
	k_sleep(K_MSEC(4000));

#ifdef CONFIG_ARCH_POSIX
	posix_exit(0);
#endif

	return 0;
}

