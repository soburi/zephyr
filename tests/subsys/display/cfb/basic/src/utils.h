/*
 * Copyright (c) 2024 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef TESTS_SUBSYS_DISPLAY_CFB_DRAW_TEXT_AND_PRINT_SRC_UTILS_H__
#define TESTS_SUBSYS_DISPLAY_CFB_DRAW_TEXT_AND_PRINT_SRC_UTILS_H__

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/display/cfb.h>

#define COLOR_RED        0xFF0000
#define COLOR_GREEN      0x00FF00
#define COLOR_BLUE       0x0000FF
#define COLOR_WHITE      0xFFFFFF
#define COLOR_BLACK      0x000000
#define COLOR_TEST_COLOR 0x4D75BA
#define COLOR_TEST_BG    0xAAAAAA

#define SKIP_MONO_DISP()                                                                           \
	if (fb_is_tiled_format(fb)) {                                                              \
		ztest_test_skip();                                                                 \
	}

static inline bool fb_is_tiled_format(const struct cfb_framebuffer *fb)
{
	if (((cfb_get_display_parameter(fb, CFB_DISPLAY_PIXEL_FORMAT) == PIXEL_FORMAT_MONO01) ||
	     (cfb_get_display_parameter(fb, CFB_DISPLAY_PIXEL_FORMAT) == PIXEL_FORMAT_MONO10))) {
		return true;
	}

	return false;
}

extern const struct device *dev;
extern const uint32_t display_width;
extern const uint32_t display_height;
extern uint8_t read_buffer[DT_PROP(DT_CHOSEN(zephyr_display), width) *
			   DT_PROP(DT_CHOSEN(zephyr_display), height) * 4];

struct cfb_framebuffer *fb_init(void);
void fb_deinit(struct cfb_framebuffer *disp);

uint32_t display_buf_size(const struct device *dev);

uint32_t display_pixel(int x, int y);
uint32_t image_pixel(const uint32_t *img, size_t width, int x, int y);
bool verify_pixel(int x, int y, uint32_t color);
bool verify_image(int x, int y, const uint32_t *img, size_t width, size_t height);
bool verify_color_inside_rect(int x, int y, size_t width, size_t height, uint32_t color);
bool verify_color_outside_rect(int x, int y, size_t width, size_t height, uint32_t color);
bool verify_image_and_bg(int x, int y, const uint32_t *img, size_t width, size_t height,
			 uint32_t color);
bool verify_pixel_and_bg(int x, int y, uint32_t pixcolor, uint32_t bgcolor);

#endif /* TESTS_SUBSYS_DISPLAY_CFB_DRAW_TEXT_AND_PRINT_SRC_UTILS_H__ */
