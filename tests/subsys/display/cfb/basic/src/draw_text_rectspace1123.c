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

LOG_MODULE_REGISTER(draw_text_rectspace1123, CONFIG_DISPLAY_LOG_LEVEL);

static struct cfb_display *disp;
static struct cfb_framebuffer *fb;

/**
 * Fill the buffer with 0 before running tests.
 */
static void cfb_test_before(void *text_fixture)
{
	uint8_t font_width;
	uint8_t font_height;
	bool font_found = false;

	disp = display_init();
	fb = cfb_display_get_framebuffer(disp);

	for (int idx = 0; idx < cfb_get_numof_fonts(); idx++) {
		if (cfb_get_font_size(idx, &font_width, &font_height)) {
			break;
		}

		if (font_width == 11 && font_height == 23) {
			cfb_set_font(fb, idx);
			font_found = true;
			break;
		}
	}

	zassert_true(font_found);
}

static void cfb_test_after(void *test_fixture)
{
	display_deinit(disp);
}

/*
 * normal rendering
 */
ZTEST(draw_text_rectspace1123, test_draw_text_at_0_0)
{
	zassert_ok(cfb_draw_text(fb, " ", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(0, 0, rectspace1123, 11, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_1_1)
{
	zassert_ok(cfb_draw_text(fb, " ", 1, 1));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(1, 1, rectspace1123, 11, 23, 0));
}

/*
 * around tile border
 */
ZTEST(draw_text_rectspace1123, test_draw_text_at_9_15)
{
	zassert_ok(cfb_draw_text(fb, " ", 9, 15));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(9, 15, rectspace1123, 11, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_10_16)
{
	zassert_ok(cfb_draw_text(fb, " ", 10, 16));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(10, 16, rectspace1123, 11, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_11_17)
{
	zassert_ok(cfb_draw_text(fb, " ", 11, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(11, 17, rectspace1123, 11, 23, 0));
}

/*
 * with kerning
 */
ZTEST(draw_text_rectspace1123, test_draw_text_at_0_0_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", 0, 0));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(0, 0, kerning_1_2rectspace1123, 23, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_1_1_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", 1, 1));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(1, 1, kerning_1_2rectspace1123, 23, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_9_15_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", 9, 15));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(9, 15, kerning_1_2rectspace1123, 23, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_10_16_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", 10, 16));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(10, 16, kerning_1_2rectspace1123, 23, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_11_17_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", 11, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(11, 17, kerning_1_2rectspace1123, 23, 23, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_right_border_17_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", display_width - 23, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image(display_width - 23, 17, kerning_1_2rectspace1123, 23, 23));
}

ZTEST(draw_text_rectspace1123, test_draw_text_at_right_border_plus1_kerning_1)
{
	cfb_set_kerning(fb, 1);
	zassert_ok(cfb_draw_text(fb, "  ", display_width - 22, 17));
	zassert_ok(cfb_finalize(fb));

	zassert_true(
		verify_image(display_width - 22, 17, kerning_1_rightclip_1_2rectspace1123, 22, 23));
}

ZTEST(draw_text_rectspace1123, test_draw_text_outside_top_left)
{
	zassert_ok(cfb_draw_text(fb, " ", -(11 - 3), -(23 - 4)));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image_and_bg(0, 0, outside_top_left, 3, 4, 0));
}

ZTEST(draw_text_rectspace1123, test_draw_text_outside_top_right)
{
	zassert_ok(cfb_draw_text(fb, " ", display_width - 5, -(23 - 8)));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image(display_width - 5, 0, outside_top_right, 5, 8));
}

ZTEST(draw_text_rectspace1123, test_draw_text_outside_bottom_right)
{
	zassert_ok(cfb_draw_text(fb, " ", display_width - 3, display_height - 5));
	zassert_ok(cfb_finalize(fb));

	zassert_true(
		verify_image(display_width - 3, display_height - 5, outside_bottom_right, 3, 5));
}

ZTEST(draw_text_rectspace1123, test_draw_text_outside_bottom_left)
{
	zassert_ok(cfb_draw_text(fb, " ", -(11 - 3), display_height - 14));
	zassert_ok(cfb_finalize(fb));

	zassert_true(verify_image(0, display_height - 14, outside_bottom_left, 3, 14));
}

ZTEST_SUITE(draw_text_rectspace1123, NULL, NULL, cfb_test_before, cfb_test_after, NULL);
