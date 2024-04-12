/*
 * Copyright (c) 2024 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/display.h>
#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/display/cfb.h>

#include "testdata.h"
#include "utils.h"

LOG_MODULE_REGISTER(print_dot0101, CONFIG_DISPLAY_LOG_LEVEL);

static const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static const uint32_t display_width = DT_PROP(DT_CHOSEN(zephyr_display), width);
static const uint32_t display_height = DT_PROP(DT_CHOSEN(zephyr_display), height);
static struct cfb_display disp;
static struct cfb_framebuffer *fb;

/**
 * Fill the buffer with 0 before running tests.
 */
static void cfb_test_before(void *text_fixture)
{
	struct display_buffer_descriptor desc = {
		.height = display_height,
		.pitch = display_width,
		.width = display_width,
		.buf_size = display_buf_size(dev),
	};
	uint8_t font_width;
	uint8_t font_height;
	bool font_found = false;

	memset(read_buffer, 0, sizeof(read_buffer));
	zassert_ok(display_write(dev, 0, 0, &desc, read_buffer));

	zassert_ok(cfb_display_init(&disp, dev));
	fb = cfb_display_get_framebuffer(&disp);

	for (int idx = 0; idx < cfb_get_numof_fonts(); idx++) {
		if (cfb_get_font_size(idx, &font_width, &font_height)) {
			break;
		}

		if (font_width == 1 && font_height == 1) {
			cfb_set_font(fb, idx);
			font_found = true;
			break;
		}
	}

	zassert_true(font_found);
}

static void cfb_test_after(void *test_fixture)
{
	cfb_display_deinit(&disp);
}

/*
 * normal rendering
 */
ZTEST(print_dot0101, test_print_at_0_0)
{
	zassert_ok(cfb_print(fb, "1", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 0, COLOR_WHITE, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_1_1)
{
	zassert_ok(cfb_print(fb, "1", 1, 1));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(1, 1, COLOR_WHITE, COLOR_BLACK));
}

/*
 * around tile border
 */
ZTEST(print_dot0101, test_print_at_9_15)
{
	zassert_ok(cfb_print(fb, "1", 9, 15));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(9, 15, COLOR_WHITE, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_10_16)
{
	zassert_ok(cfb_print(fb, "1", 10, 16));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(10, 16, COLOR_WHITE, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_11_17)
{
	zassert_ok(cfb_print(fb, "1", 11, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(11, 17, COLOR_WHITE, COLOR_BLACK));
}

/*
 * kerning
 */
ZTEST(print_dot0101, test_print_at_0_0_kerning_3)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(0, 0, kerning_3_2dot0101, 5, 1, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_1_1_kerning_3)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", 1, 1));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(1, 1, kerning_3_2dot0101, 5, 1, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_9_15_kerning_3)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", 9, 15));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(9, 15, kerning_3_2dot0101, 5, 1, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_10_16_kerning_3)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", 10, 16));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(10, 16, kerning_3_2dot0101, 5, 1, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_at_11_17_kerning_3)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", 11, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(11, 17, kerning_3_2dot0101, 5, 1, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_kerning_3_within_right_border)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", display_width - 5, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(
		verify_image_and_bg(display_width - 5, 17, kerning_3_2dot0101, 5, 1, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_kerning_3_over_right_border_wrap)
{
	cfb_set_kerning(fb, 3);
	zassert_ok(cfb_print(fb, "11", display_width - 4, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel(display_width - 4, 17, COLOR_WHITE));
	zassert_true(verify_pixel(display_width - 3, 17, COLOR_BLACK));
	zassert_true(verify_pixel(display_width - 2, 17, COLOR_BLACK));
	zassert_true(verify_pixel(display_width - 1, 17, COLOR_BLACK));
	zassert_true(verify_pixel(0, 18, COLOR_WHITE));
}

ZTEST(print_dot0101, test_print_outside_top_left)
{
	zassert_ok(cfb_print(fb, "1", 0, -1));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_color_inside_rect(0, 0, display_width, display_height, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_outside_top_right)
{
	zassert_ok(cfb_print(fb, "1", display_width, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 1, COLOR_WHITE, COLOR_BLACK), "");
}

ZTEST(print_dot0101, test_print_outside_bottom_right)
{
	zassert_ok(cfb_print(fb, "1", 0, display_height));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_color_inside_rect(0, 0, display_width, display_height, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_outside_bottom_left)
{
	zassert_ok(cfb_print(fb, "1", display_width, -1));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 0, COLOR_WHITE, COLOR_BLACK), "");
}

ZTEST(print_dot0101, test_print_dot0101_at_0_0_red)
{
	SKIP_MONO_DISP();

	zassert_ok(cfb_set_fg_color(fb, 0xFF, 0, 0, 0));

	zassert_ok(cfb_print(fb, "1", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 0, COLOR_RED, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_dot0101_at_0_0_green)
{
	SKIP_MONO_DISP();

	zassert_ok(cfb_set_fg_color(fb, 0, 0xFF, 0, 0));

	zassert_ok(cfb_print(fb, "1", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 0, COLOR_GREEN, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_dot0101_at_0_0_blue)
{
	SKIP_MONO_DISP();

	zassert_ok(cfb_set_fg_color(fb, 0, 0, 0xFF, 0));

	zassert_ok(cfb_print(fb, "1", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 0, COLOR_BLUE, COLOR_BLACK));
}

ZTEST(print_dot0101, test_print_dot0101_at_0_0_color)
{
	SKIP_MONO_DISP();

	zassert_ok(cfb_set_fg_color(fb, 0x4D, 0x75, 0xBA, 0));

	zassert_ok(cfb_print(fb, "1", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_pixel_and_bg(0, 0, COLOR_TEST_COLOR, COLOR_BLACK));
}

ZTEST_SUITE(print_dot0101, NULL, NULL, cfb_test_before, cfb_test_after, NULL);
