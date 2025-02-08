/** @file
 * @brief Monochrome Character Framebuffer shell module
 *
 * Provide some Character Framebuffer shell commands that can be useful for
 * testing.
 */

/*
 * Copyright (c) 2018 Diego Sueiro
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/shell/shell.h>
#include <zephyr/display/cfb.h>
#include <zephyr/sys/math_extras.h>

#define DISPLAY_CONTROLLER_COMPATIBLES_0   zephyr_sdl_dc
#define DISPLAY_CONTROLLER_COMPATIBLES_1   nxp_imx_elcdif
#define DISPLAY_CONTROLLER_COMPATIBLES_2   nordic_nrf_led_matrix
#define DISPLAY_CONTROLLER_COMPATIBLES_3   zephyr_dummy_dc
#define DISPLAY_CONTROLLER_COMPATIBLES_4   intel_multiboot_framebuffer
#define DISPLAY_CONTROLLER_COMPATIBLES_5   nxp_dcnano_lcdif
#define DISPLAY_CONTROLLER_COMPATIBLES_6   ultrachip_uc8175
#define DISPLAY_CONTROLLER_COMPATIBLES_7   ultrachip_uc8176
#define DISPLAY_CONTROLLER_COMPATIBLES_8   ultrachip_uc8179
#define DISPLAY_CONTROLLER_COMPATIBLES_9   ilitek_ili9340
#define DISPLAY_CONTROLLER_COMPATIBLES_10  ilitek_ili9341
#define DISPLAY_CONTROLLER_COMPATIBLES_11  ilitek_ili9342c
#define DISPLAY_CONTROLLER_COMPATIBLES_12  ilitek_ili9488
#define DISPLAY_CONTROLLER_COMPATIBLES_13  sharp_ls0xx
#define DISPLAY_CONTROLLER_COMPATIBLES_14  maxim_max7219
#define DISPLAY_CONTROLLER_COMPATIBLES_15  orisetech_otm8009a
#define DISPLAY_CONTROLLER_COMPATIBLES_16  solomon_ssd1306fb
#define DISPLAY_CONTROLLER_COMPATIBLES_17  sinowealth_sh1106
#define DISPLAY_CONTROLLER_COMPATIBLES_18  solomon_ssd1608
#define DISPLAY_CONTROLLER_COMPATIBLES_19  solomon_ssd1673
#define DISPLAY_CONTROLLER_COMPATIBLES_20  solomon_ssd1675a
#define DISPLAY_CONTROLLER_COMPATIBLES_21  solomon_ssd1680
#define DISPLAY_CONTROLLER_COMPATIBLES_22  solomon_ssd1681
#define DISPLAY_CONTROLLER_COMPATIBLES_23  sitronix_st7789v
#define DISPLAY_CONTROLLER_COMPATIBLES_24  sitronix_st7735r
#define DISPLAY_CONTROLLER_COMPATIBLES_25  st_stm32_ltdc
#define DISPLAY_CONTROLLER_COMPATIBLES_26  raydium_rm68200
#define DISPLAY_CONTROLLER_COMPATIBLES_27  raydium_rm67162
#define DISPLAY_CONTROLLER_COMPATIBLES_28  himax_hx8394
#define DISPLAY_CONTROLLER_COMPATIBLES_29  galaxycore_gc9x01x
#define DISPLAY_CONTROLLER_COMPATIBLES_MAX 30

#define DEVICE_DT_GET_COMMA(node, _) DEVICE_DT_GET(node),
#define LIST_DISPLAY_DEVICES(n, _)                                                                 \
	DT_FOREACH_STATUS_OKAY_VARGS(_CONCAT(DISPLAY_CONTROLLER_COMPATIBLES_, n),                  \
				     DEVICE_DT_GET_COMMA)

#define HELP_NONE "[none]"
#define HELP_INIT "call \"cfb init\" first"
#define HELP_DISPLAY_SELECT "<display_id>"
#define HELP_PIXEL_FORMAT "[RGB_888|MONO01|MONO10|ARGB_8888|RGB_565|BGR_565]"
#define HELP_PRINT "<col: pos> <row: pos> \"<text>\""
#define HELP_DRAW_POINT "<x> <y0>"
#define HELP_DRAW_LINE "<x0> <y0> <x1> <y1>"
#define HELP_DRAW_RECT "<x0> <y0> <x1> <y1>"
#define HELP_INVERT "[<x> <y> <width> <height>]"
#define HELP_SET_FGBGCOLOR "[<red> [<green> [<blue> [<alpha>]]]"

static const struct device *const devices[] = {
	LISTIFY(DISPLAY_CONTROLLER_COMPATIBLES_MAX, LIST_DISPLAY_DEVICES, ())};
static const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
static struct cfb_framebuffer *fb;
static const char * const param_name[] = {
	"height", "width", "ppt", "rows", "cols"};

static const char *const pixfmt_name[] = {"RGB_888",   "MONO01",  "MONO10",
					  "ARGB_8888", "RGB_565", "BGR_565"};

#if CONFIG_CHARACTER_FRAMEBUFFER_SHELL_TRANSFER_BUFFER_SIZE != 0
static struct cfb_framebuffer framebuffer;
static uint8_t transfer_buffer[CONFIG_CHARACTER_FRAMEBUFFER_SHELL_TRANSFER_BUFFER_SIZE];
static uint8_t command_buffer[CONFIG_CHARACTER_FRAMEBUFFER_SHELL_COMMAND_BUFFER_SIZE];
#endif

static int cmd_clear(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	err = cfb_clear(fb, true);
	if (err) {
		shell_error(sh, "Framebuffer clear error=%d", err);
		return err;
	}

	err = cfb_finalize(fb);
	if (err) {
		shell_error(sh, "Framebuffer finalize error=%d", err);
		return err;
	}

	shell_print(sh, "Display Cleared");

	return err;
}

static int cmd_cfb_print(const struct shell *sh, int col, int row, char *str)
{
	int err;
	uint8_t ppt;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	ppt = cfb_get_display_parameter(fb, CFB_DISPLAY_PPT);

	err = cfb_clear(fb, false);
	if (err) {
		shell_error(sh, "Framebuffer clear failed error=%d", err);
		return err;
	}

	err = cfb_print(fb, str, col, row * ppt);
	if (err) {
		shell_error(sh, "Failed to print the string %s error=%d",
		      str, err);
		return err;
	}

	err = cfb_finalize(fb);
	if (err) {
		shell_error(sh,
			    "Failed to finalize the Framebuffer error=%d", err);
		return err;
	}

	return err;
}

static int cmd_print(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	int col, row;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	col = strtol(argv[1], NULL, 10);
	if (col > cfb_get_display_parameter(fb, CFB_DISPLAY_COLS)) {
		shell_error(sh, "Invalid col=%d position", col);
		return -EINVAL;
	}

	row = strtol(argv[2], NULL, 10);
	if (row > cfb_get_display_parameter(fb, CFB_DISPLAY_ROWS)) {
		shell_error(sh, "Invalid row=%d position", row);
		return -EINVAL;
	}

	err = cmd_cfb_print(sh, col, row, argv[3]);
	if (err) {
		shell_error(sh, "Failed printing to Framebuffer error=%d",
			    err);
	}

	return err;
}

static int cmd_draw_text(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	int x, y;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	x = strtol(argv[1], NULL, 10);
	y = strtol(argv[2], NULL, 10);
	err = cfb_draw_text(fb, argv[3], x, y);
	if (err) {
		shell_error(sh, "Failed text drawing to Framebuffer error=%d", err);
		return err;
	}

	err = cfb_finalize(fb);

	return err;
}

static int cmd_draw_point(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	struct cfb_position pos;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	pos.x = strtol(argv[1], NULL, 10);
	pos.y = strtol(argv[2], NULL, 10);

	err = cfb_draw_point(fb, &pos);
	if (err) {
		shell_error(sh, "Failed point drawing to Framebuffer error=%d", err);
		return err;
	}

	err = cfb_finalize(fb);

	return err;
}

static int cmd_draw_line(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	struct cfb_position start, end;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	start.x = strtol(argv[1], NULL, 10);
	start.y = strtol(argv[2], NULL, 10);
	end.x = strtol(argv[3], NULL, 10);
	end.y = strtol(argv[4], NULL, 10);

	err = cfb_draw_line(fb, &start, &end);
	if (err) {
		shell_error(sh, "Failed text drawing to Framebuffer error=%d", err);
		return err;
	}

	err = cfb_finalize(fb);

	return err;
}

static int cmd_draw_rect(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	struct cfb_position start, end;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	start.x = strtol(argv[1], NULL, 10);
	start.y = strtol(argv[2], NULL, 10);
	end.x = strtol(argv[3], NULL, 10);
	end.y = strtol(argv[4], NULL, 10);

	err = cfb_draw_rect(fb, &start, &end);
	if (err) {
		shell_error(sh, "Failed rectanble drawing to Framebuffer error=%d", err);
		return err;
	}

	err = cfb_finalize(fb);

	return err;
}

static int cmd_scroll_vert(const struct shell *sh, size_t argc, char *argv[])
{
	int err = 0;
	int col, row;
	int boundary;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	col = strtol(argv[1], NULL, 10);
	if (col > cfb_get_display_parameter(fb, CFB_DISPLAY_COLS)) {
		shell_error(sh, "Invalid col=%d position", col);
		return -EINVAL;
	}

	row = strtol(argv[2], NULL, 10);
	if (row > cfb_get_display_parameter(fb, CFB_DISPLAY_ROWS)) {
		shell_error(sh, "Invalid row=%d position", row);
		return -EINVAL;
	}

	boundary = cfb_get_display_parameter(fb, CFB_DISPLAY_ROWS) - row;

	for (int i = 0; i < boundary; i++) {
		err = cmd_cfb_print(sh, col, row, argv[3]);
		if (err) {
			shell_error(sh,
				    "Failed printing to Framebuffer error=%d",
				    err);
			break;
		}
		row++;
	}

	cmd_cfb_print(sh, 0, 0, "");

	return err;
}

static int cmd_scroll_horz(const struct shell *sh, size_t argc, char *argv[])
{
	int err = 0;
	int col, row;
	int boundary;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	col = strtol(argv[1], NULL, 10);
	if (col > cfb_get_display_parameter(fb, CFB_DISPLAY_COLS)) {
		shell_error(sh, "Invalid col=%d position", col);
		return -EINVAL;
	}

	row = strtol(argv[2], NULL, 10);
	if (row > cfb_get_display_parameter(fb, CFB_DISPLAY_ROWS)) {
		shell_error(sh, "Invalid row=%d position", row);
		return -EINVAL;
	}

	col++;
	boundary = cfb_get_display_parameter(fb, CFB_DISPLAY_COLS) - col;

	for (int i = 0; i < boundary; i++) {
		err = cmd_cfb_print(sh, col, row, argv[3]);
		if (err) {
			shell_error(sh,
				    "Failed printing to Framebuffer error=%d",
				    err);
			break;
		}
		col++;
	}

	cmd_cfb_print(sh, 0, 0, "");

	return err;
}

static int cmd_set_font(const struct shell *sh, size_t argc, char *argv[])
{
	int err;
	int idx;
	uint8_t height;
	uint8_t width;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	idx = strtol(argv[1], NULL, 10);

	err = cfb_get_font_size(idx, &width, &height);
	if (err) {
		shell_error(sh, "Invalid font idx=%d err=%d\n", idx, err);
		return err;
	}

	err = cfb_set_font(fb, idx);
	if (err) {
		shell_error(sh, "Failed setting font idx=%d err=%d", idx,
			    err);
		return err;
	}

	shell_print(sh, "Font idx=%d height=%d widht=%d set", idx, height,
		    width);

	return err;
}

static int cmd_set_kerning(const struct shell *sh, size_t argc, char *argv[])
{
	int err = 0;
	long kerning;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	errno = 0;
	kerning = shell_strtol(argv[1], 10, &err);
	if (err) {
		shell_error(sh, HELP_INIT);
		return -EINVAL;
	}

	err = cfb_set_kerning(fb, kerning);
	if (err) {
		shell_error(sh, "Failed to set kerning err=%d", err);
		return err;
	}

	return err;
}

static void parse_color_args(size_t argc, char *argv[], uint8_t *r, uint8_t *g, uint8_t *b,
			     uint8_t *a)
{
	struct display_capabilities cfg;
	bool is_16bit = false;

	display_get_capabilities(fb->dev, &cfg);

	*r = 0;
	*g = 0;
	*b = 0;
	*a = 0;

	if (cfg.current_pixel_format == PIXEL_FORMAT_RGB_565 ||
	    cfg.current_pixel_format == PIXEL_FORMAT_BGR_565) {
		is_16bit = true;
	}

	switch (argc) {
	case 5:
		*a = strtol(argv[4], NULL, 0);
	case 4:
		*b = strtol(argv[3], NULL, 0);
	case 3:
		*g = strtol(argv[2], NULL, 0);
	case 2:
		*r = strtol(argv[1], NULL, 0);
	default:
	}
}

static int cmd_set_fgcolor(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t r, g, b, a;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	parse_color_args(argc, argv, &r, &g, &b, &a);
	cfb_set_fg_color(fb, r, g, b, a);

	return 0;
}

static int cmd_set_bgcolor(const struct shell *sh, size_t argc, char *argv[])
{
	uint8_t r, g, b, a;

	if (!disp) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	parse_color_args(argc, argv, &r, &g, &b, &a);
	cfb_set_bg_color(fb, r, g, b, a);

	return 0;
}

static int cmd_invert(const struct shell *sh, size_t argc, char *argv[])
{
	int err;

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	if (argc == 1) {
		err = cfb_invert(fb);
		if (err) {
			shell_error(sh, "Error inverting Framebuffer");
			return err;
		}
	} else if (argc == 5) {
		int x, y, w, h;

		x = strtol(argv[1], NULL, 10);
		y = strtol(argv[2], NULL, 10);
		w = strtol(argv[3], NULL, 10);
		h = strtol(argv[4], NULL, 10);

		err = cfb_invert_area(fb, x, y, w, h);
		if (err) {
			shell_error(sh, "Error invert area");
			return err;
		}
	} else {
		shell_help(sh);
		return 0;
	}

	cfb_finalize(fb);

	shell_print(sh, "Framebuffer Inverted");

	return err;
}

static int cmd_get_fonts(const struct shell *sh, size_t argc, char *argv[])
{
	int err = 0;
	uint8_t font_height;
	uint8_t font_width;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	for (int idx = 0; idx < cfb_get_numof_fonts(); idx++) {
		if (cfb_get_font_size(idx, &font_width, &font_height)) {
			break;
		}
		shell_print(sh, "idx=%d height=%d width=%d", idx,
			    font_height, font_width);
	}

	return err;
}

static int cmd_display(const struct shell *sh, size_t argc, char *argv[])
{
	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	if (argc == 1) {
		shell_print(sh, "Displays:");
		for (size_t i = 0; i < ARRAY_SIZE(devices); i++) {
			if (devices[i] == dev) {
				shell_print(sh, "* %2d: %s", i, devices[i]->name);
			} else {
				shell_print(sh, "  %2d: %s", i, devices[i]->name);
			}
		}
	} else if (argc == 2) {
		int idx = strtol(argv[1], NULL, 10);

		if (idx < 0 || ARRAY_SIZE(devices) <= idx) {
			shell_print(sh, "Display: unavailable display id: %d", idx);
		}

		if (fb->dev != devices[idx]) {
			shell_print(sh, "Display: %s deinitialzed.", fb->dev->name);
			cfb_free(fb);
			fb = NULL;
		}

		dev = devices[idx];
	}

	return 0;
}

static int cmd_pixel_format(const struct shell *sh, size_t argc, char *argv[])
{
	enum display_pixel_format pix_fmt;
	struct display_capabilities cfg;
	int err = 0;

	if (argc == 2) {
		if (strcmp(argv[1], "RGB_888") == 0) {
			pix_fmt = PIXEL_FORMAT_RGB_888;
		} else if (strcmp(argv[1], "MONO01") == 0) {
			pix_fmt = PIXEL_FORMAT_MONO01;
		} else if (strcmp(argv[1], "MONO10") == 0) {
			pix_fmt = PIXEL_FORMAT_MONO10;
		} else if (strcmp(argv[1], "ARGB_8888") == 0) {
			pix_fmt = PIXEL_FORMAT_ARGB_8888;
		} else if (strcmp(argv[1], "RGB_565") == 0) {
			pix_fmt = PIXEL_FORMAT_RGB_565;
		} else if (strcmp(argv[1], "BGR_565") == 0) {
			pix_fmt = PIXEL_FORMAT_BGR_565;
		} else {
			shell_error(sh, "Invalid pixel format. use "
					"RGB_888/MONO01/MONO10/ARGB_8888/RGB_565/BGR_565");
			return -EINVAL;
		}

		err = display_set_pixel_format(dev, pix_fmt);
		if (err) {
			shell_error(sh, "Failed to set required pixel format: %d", err);
			return err;
		}
	}

	display_get_capabilities(dev, &cfg);
	pix_fmt = cfg.current_pixel_format ? u32_count_trailing_zeros(cfg.current_pixel_format) : 0;
	shell_print(sh, "Pixel format: %s", pixfmt_name[pix_fmt]);

	return err;
}

static int cmd_get_param_all(const struct shell *sh, size_t argc,
			     char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	for (unsigned int i = 0; i <= CFB_DISPLAY_COLS; i++) {
		shell_print(sh, "param: %s=%d", param_name[i],
				cfb_get_display_parameter(fb, i));

	}

	return 0;
}

static int cmd_get_param_height(const struct shell *sh, size_t argc,
			     char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	shell_print(sh, "param: %s=%d", param_name[CFB_DISPLAY_HEIGH],
		    cfb_get_display_parameter(fb, CFB_DISPLAY_HEIGH));

	return 0;
}

static int cmd_get_param_width(const struct shell *sh, size_t argc,
			     char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	shell_print(sh, "param: %s=%d", param_name[CFB_DISPLAY_WIDTH],
		    cfb_get_display_parameter(fb, CFB_DISPLAY_WIDTH));

	return 0;
}

static int cmd_get_param_ppt(const struct shell *sh, size_t argc,
			     char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	shell_print(sh, "param: %s=%d", param_name[CFB_DISPLAY_PPT],
		    cfb_get_display_parameter(fb, CFB_DISPLAY_PPT));

	return 0;
}

static int cmd_get_param_rows(const struct shell *sh, size_t argc,
			     char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	shell_print(sh, "param: %s=%d", param_name[CFB_DISPLAY_ROWS],
		    cfb_get_display_parameter(fb, CFB_DISPLAY_ROWS));

	return 0;
}

static int cmd_get_param_cols(const struct shell *sh, size_t argc,
			     char *argv[])
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!fb) {
		shell_error(sh, HELP_INIT);
		return -ENODEV;
	}

	shell_print(sh, "param: %s=%d", param_name[CFB_DISPLAY_COLS],
		    cfb_get_display_parameter(fb, CFB_DISPLAY_COLS));

	return 0;
}

static int cmd_init(const struct shell *sh, size_t argc, char *argv[])
{
	enum display_pixel_format pix_fmt;
	struct display_capabilities cfg;
	int err;

#if CONFIG_CHARACTER_FRAMEBUFFER_SHELL_TRANSFER_BUFFER_SIZE != 0
	struct cfb_init_param param = {
		.transfer_buf = transfer_buffer,
		.transfer_buf_size = sizeof(transfer_buffer),
		.command_buf = command_buffer,
		.command_buf_size = sizeof(transfer_buffer),
	};
#endif

	if (!device_is_ready(dev)) {
		shell_error(sh, "Display device not ready");
		return -ENODEV;
	}

	err = display_blanking_off(dev);
	if (err) {
		shell_error(sh, "Failed to turn off display blanking: %d", err);
		return err;
	}

#if CONFIG_CHARACTER_FRAMEBUFFER_SHELL_TRANSFER_BUFFER_SIZE != 0
	fb = &framebuffer;
	if (cfb_init(&framebuffer, &param)) {
		printf("Framebuffer initialization failed!\n");
		return 0;
	}
#else
	if (fb) {
		cfb_free(fb);
	}

	fb = cfb_alloc(dev);
	if (!fb) {
		shell_error(sh, "Framebuffer initialization failed!");
		return -ENOMEM;
	}
#endif

	display_get_capabilities(dev, &cfg);
	pix_fmt = cfg.current_pixel_format ? u32_count_trailing_zeros(cfg.current_pixel_format) : 0;
	shell_print(sh, "Initialized: %s [%dx%d@%s]", dev->name, cfg.x_resolution, cfg.y_resolution,
		    pixfmt_name[pix_fmt]);
	err = cfb_clear(fb, true);

	return err;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmd_get_param,

	SHELL_CMD_ARG(all, NULL, NULL, cmd_get_param_all, 1, 0),
	SHELL_CMD_ARG(height, NULL, NULL, cmd_get_param_height, 1, 0),
	SHELL_CMD_ARG(width, NULL, NULL, cmd_get_param_width, 1, 0),
	SHELL_CMD_ARG(ppt, NULL, NULL, cmd_get_param_ppt, 1, 0),
	SHELL_CMD_ARG(rows, NULL, NULL, cmd_get_param_rows, 1, 0),
	SHELL_CMD_ARG(cols, NULL, NULL, cmd_get_param_cols, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmd_scroll,

	SHELL_CMD_ARG(vertical, NULL, HELP_PRINT, cmd_scroll_vert, 4, 0),
	SHELL_CMD_ARG(horizontal, NULL, HELP_PRINT, cmd_scroll_horz, 4, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_cmd_draw,
	SHELL_CMD_ARG(text, NULL, HELP_PRINT, cmd_draw_text, 4, 0),
	SHELL_CMD_ARG(point, NULL, HELP_DRAW_POINT, cmd_draw_point, 3, 0),
	SHELL_CMD_ARG(line, NULL, HELP_DRAW_LINE, cmd_draw_line, 5, 0),
	SHELL_CMD_ARG(rect, NULL, HELP_DRAW_RECT, cmd_draw_rect, 5, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(cfb_cmds,
	SHELL_CMD_ARG(init, NULL, HELP_NONE, cmd_init, 1, 0),
	SHELL_CMD_ARG(display, NULL, "[<display_id>]", cmd_display, 1, 1),
	SHELL_CMD_ARG(pixel_format, NULL, HELP_PIXEL_FORMAT, cmd_pixel_format, 1, 1),
	SHELL_CMD(get_param, &sub_cmd_get_param,
		  "<all, height, width, ppt, rows, cols>", NULL),
	SHELL_CMD_ARG(get_fonts, NULL, HELP_NONE, cmd_get_fonts, 1, 0),
	SHELL_CMD_ARG(set_font, NULL, "<idx>", cmd_set_font, 2, 0),
	SHELL_CMD_ARG(set_kerning, NULL, "<kerning>", cmd_set_kerning, 2, 0),
	SHELL_CMD_ARG(set_fgcolor, NULL, HELP_SET_FGBGCOLOR, cmd_set_fgcolor, 2, 4),
	SHELL_CMD_ARG(set_bgcolor, NULL, HELP_SET_FGBGCOLOR, cmd_set_bgcolor, 2, 4),
	SHELL_CMD_ARG(invert, NULL, HELP_INVERT, cmd_invert, 1, 5),
	SHELL_CMD_ARG(print, NULL, HELP_PRINT, cmd_print, 4, 0),
	SHELL_CMD(scroll, &sub_cmd_scroll, "scroll a text in vertical or "
		  "horizontal direction", NULL),
	SHELL_CMD(draw, &sub_cmd_draw, "drawing text", NULL),
	SHELL_CMD_ARG(clear, NULL, HELP_NONE, cmd_clear, 1, 0),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(cfb, &cfb_cmds, "Character Framebuffer shell commands",
		   NULL);
