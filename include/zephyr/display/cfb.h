/*
 * Copyright (c) 2018 PHYTEC Messtechnik GmbH
 * Copyright (c) 2024 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief Public Monochrome Character Framebuffer API
 */

#ifndef ZEPHYR_INCLUDE_DISPLAY_CFB_H_
#define ZEPHYR_INCLUDE_DISPLAY_CFB_H_

#include <zephyr/device.h>
#include <zephyr/drivers/display.h>
#include <zephyr/sys/iterable_sections.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Public Monochrome Character Framebuffer API
 * @defgroup monochrome_character_framebuffer Monochrome Character Framebuffer
 * @ingroup utilities
 * @{
 */

enum cfb_display_param {
	CFB_DISPLAY_HEIGH		= 0,
	CFB_DISPLAY_WIDTH,
	CFB_DISPLAY_PPT,
	CFB_DISPLAY_ROWS,
	CFB_DISPLAY_COLS,
	CFB_DISPLAY_PIXEL_FORMAT,
};

/**
 * 2-dimensional position
 */
struct cfb_position {
	/** X coordinate */
	int16_t x;
	/** Y coordinate */
	int16_t y;
};


/** @cond INTERNAL_HIDDEN */

enum cfb_font_caps {
	CFB_FONT_MONO_VPACKED		= BIT(0),
	CFB_FONT_MONO_HPACKED		= BIT(1),
	CFB_FONT_MSB_FIRST		= BIT(2),
};

struct cfb_font {
	const void *data;
	enum cfb_font_caps caps;
	uint8_t width;
	uint8_t height;
	uint8_t first_char;
	uint8_t last_char;
};

/** @endcond */

/**
 * A representation of framebuffer.
 *
 * This struct is a private definition.
 * Do not have direct access to members of this structure.
 */
struct cfb_framebuffer {
	/**
	 * @private
	 * Pointer to device
	 */
	const struct device *dev;

	/**
	 * @private
	 * Pointer to a buffer in RAM
	 */
	uint8_t *buf;

	/**
	 * @private
	 * Size of the buf
	 */
	uint32_t size;

	/**
	 * @private
	 * Display pixel format
	 */
	enum display_pixel_format pixel_format;

	/**
	 * @private
	 * Display screen info
	 */
	enum display_screen_info screen_info;

	/**
	 * @private
	 * Framebuffer width in pixels.
	 */
	uint16_t width;

	/**
	 * @private
	 * Framebuffer height in pixels.
	 */
	uint16_t height;

	/**
	 * @private
	 * Current font index
	 */
	uint8_t font_idx;

	/**
	 * @private
	 * Current font kerning
	 */
	int8_t kerning;

	/**
	 * @private
	 * Foreground color
	 */
	uint32_t fg_color;

	/**
	 * @private
	 * Background color
	 */
	uint32_t bg_color;
};


/**
 * A parameter structure for ::cfb_display_init.
 */
struct cfb_init_param {
	/** Pointer to a buffer */
	uint8_t *transfer_buf;

	/** Size of the transfer_buf */
	uint32_t transfer_buf_size;
};

/**
 * @brief Macro for creating a font entry.
 *
 * @param _name   Name of the font entry.
 * @param _width  Width of the font in pixels
 * @param _height Height of the font in pixels.
 * @param _caps   Font capabilities.
 * @param _data   Raw data of the font.
 * @param _fc     Character mapped to first font element.
 * @param _lc     Character mapped to last font element.
 */
#define FONT_ENTRY_DEFINE(_name, _width, _height, _caps, _data, _fc, _lc)      \
	static const STRUCT_SECTION_ITERABLE(cfb_font, _name) = {	       \
		.data = _data,						       \
		.caps = _caps,						       \
		.width = _width,					       \
		.height = _height,					       \
		.first_char = _fc,					       \
		.last_char = _lc,					       \
	}

/**
 * @brief Print a string into the framebuffer.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param str String to print
 * @param x Position in X direction of the beginning of the string
 * @param y Position in Y direction of the beginning of the string
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_print(struct cfb_framebuffer *fb, const char *const str, int16_t x, int16_t y);

/**
 * @brief Print a string into the framebuffer.
 * For compare to cfb_print, cfb_draw_text accept non tile-aligned coords
 * and not line wrapping.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param str String to print
 * @param x Position in X direction of the beginning of the string
 * @param y Position in Y direction of the beginning of the string
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_draw_text(struct cfb_framebuffer *fb, const char *const str, int16_t x, int16_t y);

/**
 * @brief Draw a point.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param pos position of the point
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_draw_point(struct cfb_framebuffer *fb, const struct cfb_position *pos);

/**
 * @brief Draw a line.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param start start position of the line
 * @param end end position of the line
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_draw_line(struct cfb_framebuffer *fb, const struct cfb_position *start,
		  const struct cfb_position *end);

/**
 * @brief Draw a rectangle.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param start Top-Left position of the rectangle
 * @param end Bottom-Right position of the rectangle
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_draw_rect(struct cfb_framebuffer *fb, const struct cfb_position *start,
		  const struct cfb_position *end);

/**
 * @brief Clear framebuffer.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param clear_display Clear the display as well
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_clear(struct cfb_framebuffer *fb, bool clear_display);

/**
 * Inverts foreground and background colors.
 *
 * @param fb A framebuffer to rendering.
 *
 * @retval 0 on succeeded
 * @retval -errno Negative errno for other failures
 */
int cfb_invert(struct cfb_framebuffer *fb);

/**
 * @brief Invert Pixels in selected area.
 *
 * @param fb Pointer to framebuffer to rendering
 * @param x Position in X direction of the beginning of area
 * @param y Position in Y direction of the beginning of area
 * @param width Width of area in pixels
 * @param height Height of area in pixels
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_invert_area(struct cfb_framebuffer *fb, int16_t x, int16_t y, uint16_t width,
		    uint16_t height);

/**
 * @brief Finalize framebuffer and write it to display RAM,
 * invert or reorder pixels if necessary.
 *
 * @param fb Pointer to framebuffer to rendering
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_finalize(struct cfb_framebuffer *fb);

/**
 * @brief Get display parameter.
 *
 * @param disp Pointer to display instance
 * @param cfb_display_param One of the display parameters
 *
 * @return Display parameter value
 */
int cfb_get_display_parameter(const struct cfb_framebuffer *fb, enum cfb_display_param);

/**
 * @brief Set font.
 *
 * @param fb Pointer to framebuffer instance
 * @param idx Font index
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_set_font(struct cfb_framebuffer *fb, uint8_t idx);

/**
 * @brief Set font kerning (spacing between individual letters).
 *
 * @param fb Pointer to framebuffer instance
 * @param kerning Font kerning
 *
 * @return 0 on success, negative value otherwise
 */
int cfb_set_kerning(struct cfb_framebuffer *fb, int8_t kerning);

/**
 * Set foreground color.
 *
 * Set foreground color with RGBA values in 32-bit color representation.
 *
 * This function immediately draws to the buffer if the buffer is large enough
 * to store the entire screen. Otherwise, store the command in the command buffer.
 *
 * @param fb A framebuffer to set.
 * @param r The red component of the foreground color in 32-bit color representation.
 * @param g The green component of the foreground color in 32-bit color representation.
 * @param b The blue component of the foreground color in 32-bit color representation.
 * @param a The alpha channel of the foreground color in 32-bit color representation.
 *
 * @retval 0 on succeeded
 * @retval -ENOBUFS The command buffer does not have enough space
 * @retval -errno  Negative errno for other failures.
 */
int cfb_set_fg_color(struct cfb_framebuffer *fb, uint8_t r, uint8_t g,
				   uint8_t b, uint8_t a);

/**
 * Set background color.
 *
 * Set background color with RGBA values in 32-bit color representation.
 *
 * This function immediately draws to the buffer if the buffer is large enough
 * to store the entire screen. Otherwise, store the command in the command buffer.
 *
 * @param fb A framebuffer to set.
 * @param r The red component of the foreground color in 32-bit color representation.
 * @param g The green component of the foreground color in 32-bit color representation.
 * @param b The blue component of the foreground color in 32-bit color representation.
 * @param a The alpha channel of the foreground color in 32-bit color representation.
 *
 * @retval 0 on succeeded
 * @retval -ENOBUFS The command buffer does not have enough space
 * @retval -errno  Negative errno for other failures.
 */
int cfb_set_bg_color(struct cfb_framebuffer *fb, uint8_t r, uint8_t g,
				   uint8_t b, uint8_t a);

/**
 * @brief Get font size.
 *
 * Get width and height of font that is specified by idx.
 *
 * @param idx Font index
 * @param width Pointers to the variable where the font width will be stored.
 * @param height Pointers to the variable where the font height will be stored.
 *
 * @retval 0 on success
 * @retval -EINVAL If you specify idx for a font that does not exist
 */
int cfb_get_font_size(uint8_t idx, uint8_t *width, uint8_t *height);

/**
 * @brief Get number of fonts.
 *
 * @return number of fonts
 */
int cfb_get_numof_fonts(void);

/**
 * @brief Initialize display
 *
 * @param disp A display instance to initialize.
 * @param param Pointer to display initialize parameter.
 *
 * @return 0 on succeeded
 */
int cfb_init(struct cfb_framebuffer *fb, const struct device *dev,
	     const struct cfb_init_param *param);

/**
 * Allocate a full-screen buffer and initialize a display instance.
 *
 * @param dev A display device which is used to displaying.
 *
 * @retval NULL If memory allocation fails.
 * @retval Non-NULL on succeeded
 */
struct cfb_framebuffer *cfb_alloc(const struct device *dev);

/**
 * Deinitialize display instance.
 *
 * @param disp A display instance to deinitialize.
 */
void cfb_deinit(struct cfb_framebuffer *fb);

/**
 * Release an allocated display instance.
 *
 * @param disp A display instance that was allocated by ::cfb_display_alloc.
 */
void cfb_free(struct cfb_framebuffer *fb);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DISPLAY_CFB_H_ */
