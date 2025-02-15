/*
 * RM68140 Zephyr Display Driver
 *
 * This driver implements display initialization and write API for the
 * RM68140 controller via the MIPI DBI interface.
 *
 * The fixed initialization values are based on Bodmer's TFT_eSPI library.
 *
 * (C) 202X Your Name
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>

#include "mipi_dbi.h" /* MIPI DBI interface header (assumed available) */

LOG_MODULE_REGISTER(display_rm68140, CONFIG_DISPLAY_LOG_LEVEL);

/**
 * @brief RM68140 configuration structure.
 *
 * Each field holds a fixed initialization value for the corresponding
 * register as specified in the RM68140 datasheet and used in Bodmer's library.
 *
 * The values below are based on the following initial sequence:
 *
 *   SLPOUT
 *   (Delay 20ms)
 *
 *   0xD0: 0x07, 0x42, 0x18
 *      - 0x07: VCOM/control parameter (e.g. internal voltage setting)
 *      - 0x42: VCOM voltage level adjustment
 *      - 0x18: Additional power setting for boost circuitry
 *
 *   0xD1: 0x00, 0x07, 0x10
 *      - 0x00: Power setting (offset/control)
 *      - 0x07: Additional bias or voltage ratio parameter
 *      - 0x10: Further power tuning parameter
 *
 *   0xD2: 0x01, 0x02
 *      - 0x01, 0x02: Minor adjustments for power circuitry timing
 *
 *   0xC0: 0x10, 0x3B, 0x00, 0x02, 0x11
 *      - 0x10: Panel drive mode or start voltage level
 *      - 0x3B: Drive strength/timing parameter
 *      - 0x00: Reserved/offset (typically zero)
 *      - 0x02: Timing parameter (e.g. front/back porch setting)
 *      - 0x11: Final drive parameter for panel operation
 *
 *   0xC5: 0x03
 *      - 0x03: VCOM adjustment – fine-tunes the VCOM output level
 *
 *   0xC8: 0x00, 0x32, 0x36, 0x45, 0x06, 0x16, 0x37, 0x75, 0x77, 0x54, 0x0C, 0x00
 *      - These 12 bytes define the gamma correction curve for proper color reproduction.
 *
 *   MADCTL: 0x0A
 *      - 0x0A: Memory Access Control value to set display orientation, mirror, etc.
 *
 *   0x3A: 0x55
 *      - 0x55: Pixel format setting (0x55 typically selects RGB565 mode)
 *
 *   CASET: 0x00, 0x00, 0x01, 0x3F
 *      - Sets column range from 0x0000 to 0x013F (0 to 319 pixels)
 *
 *   PASET: 0x00, 0x00, 0x01, 0xDF
 *      - Sets row range from 0x0000 to 0x01DF (0 to 479 pixels)
 */
struct rm68140_config {
	/* MIPI DBI parameters */
	const struct device *mipi_dev;
	struct mipi_dbi_config dbi_config;
	enum display_pixel_format pixel_format;
	uint16_t x_resolution;
	uint16_t y_resolution;
	bool inversion;

	/* Register initialization parameters */

	/* 0xD0: Power / VCOM settings */
	uint8_t power_vcom_1; /* 0x07: Internal voltage bias control */
	uint8_t power_vcom_2; /* 0x42: VCOM voltage level adjustment */
	uint8_t power_vcom_3; /* 0x18: Boost circuit timing/setting */

	/* 0xD1: Additional power settings */
	uint8_t power_extra_1; /* 0x00: Offset control, reserved */
	uint8_t power_extra_2; /* 0x07: Bias or ratio control */
	uint8_t power_extra_3; /* 0x10: Additional voltage tuning */

	/* 0xD2: Other power settings */
	uint8_t power_misc_1;  /* 0x01: Minor power adjustment */
	uint8_t power_misc_2;  /* 0x02: Further fine-tuning */

	/* 0xC0: Panel drive timing settings */
	uint8_t panel_drive_1; /* 0x10: Drive mode/start voltage */
	uint8_t panel_drive_2; /* 0x3B: Drive strength/timing */
	uint8_t panel_drive_3; /* 0x00: Reserved/offset */
	uint8_t panel_drive_4; /* 0x02: Timing (porch) parameter */
	uint8_t panel_drive_5; /* 0x11: Final drive tuning */

	/* 0xC5: VCOM adjustment */
	uint8_t vcom_adj;      /* 0x03: Fine adjustment for VCOM level */

	/* 0xC8: Gamma correction settings (12 bytes) */
	uint8_t gamma_corr_1;  /* 0x00: Gamma curve start value */
	uint8_t gamma_corr_2;  /* 0x32: Gamma adjustment parameter */
	uint8_t gamma_corr_3;  /* 0x36: Gamma curve mid-range value */
	uint8_t gamma_corr_4;  /* 0x45: Gamma adjustment parameter */
	uint8_t gamma_corr_5;  /* 0x06: Lower gamma correction value */
	uint8_t gamma_corr_6;  /* 0x16: Gamma curve fine-tuning */
	uint8_t gamma_corr_7;  /* 0x37: Gamma adjustment parameter */
	uint8_t gamma_corr_8;  /* 0x75: Gamma curve peak value */
	uint8_t gamma_corr_9;  /* 0x77: Gamma adjustment parameter */
	uint8_t gamma_corr_10; /* 0x54: Gamma curve final tuning */
	uint8_t gamma_corr_11; /* 0x0C: End of gamma curve */
	uint8_t gamma_corr_12; /* 0x00: Reserved/termination */

	/* MADCTL: Memory Access Control (display orientation, mirror, etc.) */
	uint8_t mem_access;    /* 0x0A: Sets display rotation and RGB/BGR order */

	/* 0x3A: Pixel format (COLMOD) */
	uint8_t pixel_format_reg; /* 0x55: 16-bit RGB565 mode */

	/* CASET: Column Address Set */
	uint16_t column_start; /* 0x0000: Start column */
	uint16_t column_end;   /* 0x013F: End column (319) */

	/* PASET: Page (Row) Address Set */
	uint16_t row_start;    /* 0x0000: Start row */
	uint16_t row_end;      /* 0x01DF: End row (479) */

	/* Initialization function pointer */
	int (*regs_init_fn)(const struct device *dev);
};

/* Runtime data structure */
struct rm68140_data {
	enum display_pixel_format pixel_format;
	uint8_t bytes_per_pixel;
	enum display_orientation orientation;
};

/**
 * @brief Transmit a command via MIPI DBI.
 */
static int rm68140_transmit(const struct device *dev, uint8_t cmd,
			    const void *tx_data, size_t tx_len)
{
	const struct rm68140_config *config = dev->config;
	return mipi_dbi_command_write(config->mipi_dev, &config->dbi_config,
				      cmd, tx_data, tx_len);
}

/**
 * @brief Initialize RM68140 registers.
 *
 * Sends the fixed initialization commands using the values defined in rm68140_config.
 */
static int rm68140_regs_init(const struct device *dev)
{
	const struct rm68140_config *config = dev->config;
	int r;
	uint8_t tx_data[12];

	/* 1. Exit Sleep (0x11) */
	r = rm68140_transmit(dev, RM68140_SLPOUT, NULL, 0);
	if (r < 0) {
		return r;
	}
	k_sleep(K_MSEC(20));

	/* 2. 0xD0: Power / VCOM settings */
	tx_data[0] = config->power_vcom_1; /* 0x07 */
	tx_data[1] = config->power_vcom_2; /* 0x42 */
	tx_data[2] = config->power_vcom_3; /* 0x18 */
	r = rm68140_transmit(dev, 0xD0, tx_data, 3);
	if (r < 0) {
		return r;
	}

	/* 3. 0xD1: Additional power settings */
	tx_data[0] = config->power_extra_1; /* 0x00 */
	tx_data[1] = config->power_extra_2; /* 0x07 */
	tx_data[2] = config->power_extra_3; /* 0x10 */
	r = rm68140_transmit(dev, 0xD1, tx_data, 3);
	if (r < 0) {
		return r;
	}

	/* 4. 0xD2: Other power settings */
	tx_data[0] = config->power_misc_1;  /* 0x01 */
	tx_data[1] = config->power_misc_2;  /* 0x02 */
	r = rm68140_transmit(dev, 0xD2, tx_data, 2);
	if (r < 0) {
		return r;
	}

	/* 5. 0xC0: Panel drive timing settings */
	tx_data[0] = config->panel_drive_1; /* 0x10 */
	tx_data[1] = config->panel_drive_2; /* 0x3B */
	tx_data[2] = config->panel_drive_3; /* 0x00 */
	tx_data[3] = config->panel_drive_4; /* 0x02 */
	tx_data[4] = config->panel_drive_5; /* 0x11 */
	r = rm68140_transmit(dev, 0xC0, tx_data, 5);
	if (r < 0) {
		return r;
	}

	/* 6. 0xC5: VCOM adjustment */
	tx_data[0] = config->vcom_adj;      /* 0x03 */
	r = rm68140_transmit(dev, 0xC5, tx_data, 1);
	if (r < 0) {
		return r;
	}

	/* 7. 0xC8: Gamma correction settings */
	tx_data[0]  = config->gamma_corr_1;  /* 0x00 */
	tx_data[1]  = config->gamma_corr_2;  /* 0x32 */
	tx_data[2]  = config->gamma_corr_3;  /* 0x36 */
	tx_data[3]  = config->gamma_corr_4;  /* 0x45 */
	tx_data[4]  = config->gamma_corr_5;  /* 0x06 */
	tx_data[5]  = config->gamma_corr_6;  /* 0x16 */
	tx_data[6]  = config->gamma_corr_7;  /* 0x37 */
	tx_data[7]  = config->gamma_corr_8;  /* 0x75 */
	tx_data[8]  = config->gamma_corr_9;  /* 0x77 */
	tx_data[9]  = config->gamma_corr_10; /* 0x54 */
	tx_data[10] = config->gamma_corr_11; /* 0x0C */
	tx_data[11] = config->gamma_corr_12; /* 0x00 */
	r = rm68140_transmit(dev, 0xC8, tx_data, 12);
	if (r < 0) {
		return r;
	}

	/* 8. MADCTL: Memory Access Control */
	tx_data[0] = config->mem_access;    /* 0x0A: Sets rotation, mirror, and color order */
	r = rm68140_transmit(dev, RM68140_MADCTL, tx_data, 1);
	if (r < 0) {
		return r;
	}

	/* 9. 0x3A: Pixel format (COLMOD) */
	tx_data[0] = config->pixel_format_reg; /* 0x55: Selects 16-bit RGB565 mode */
	r = rm68140_transmit(dev, 0x3A, tx_data, 1);
	if (r < 0) {
		return r;
	}

	/* 10. CASET: Column Address Set */
	{
		uint8_t caset_data[4];
		caset_data[0] = (uint8_t)(config->column_start >> 8);
		caset_data[1] = (uint8_t)(config->column_start & 0xFF);
		caset_data[2] = (uint8_t)(config->column_end >> 8);
		caset_data[3] = (uint8_t)(config->column_end & 0xFF);
		r = rm68140_transmit(dev, RM68140_CASET, caset_data, 4);
		if (r < 0) {
			return r;
		}
	}

	/* 11. PASET: Page (Row) Address Set */
	{
		uint8_t paset_data[4];
		paset_data[0] = (uint8_t)(config->row_start >> 8);
		paset_data[1] = (uint8_t)(config->row_start & 0xFF);
		paset_data[2] = (uint8_t)(config->row_end >> 8);
		paset_data[3] = (uint8_t)(config->row_end & 0xFF);
		r = rm68140_transmit(dev, RM68140_PASET, paset_data, 4);
		if (r < 0) {
			return r;
		}
	}

	/* 12. Final delay and Display ON */
	k_sleep(K_MSEC(120));
	r = rm68140_transmit(dev, RM68140_DISPON, NULL, 0);
	if (r < 0) {
		return r;
	}
	k_sleep(K_MSEC(25));

	return 0;
}

/**
 * @brief Set the memory area (column and row range) for subsequent data write.
 *
 * Similar to ili9xxx_set_mem_area().
 */
static int rm68140_set_mem_area(const struct device *dev, uint16_t x, uint16_t y,
				uint16_t w, uint16_t h)
{
	int r;
	uint16_t spi_data[2];

	/* Column address set */
	spi_data[0] = sys_cpu_to_be16(x);
	spi_data[1] = sys_cpu_to_be16(x + w - 1U);
	r = rm68140_transmit(dev, RM68140_CASET, &spi_data, 4U);
	if (r < 0) {
		return r;
	}

	/* Page address set */
	spi_data[0] = sys_cpu_to_be16(y);
	spi_data[1] = sys_cpu_to_be16(y + h - 1U);
	r = rm68140_transmit(dev, RM68140_PASET, &spi_data, 4U);
	if (r < 0) {
		return r;
	}

	return 0;
}

/**
 * @brief Write display data to the screen.
 *
 * Writes pixel data to the display RAM via the RAMWR command.
 */
static int rm68140_write(const struct device *dev, uint16_t x, uint16_t y,
			 const struct display_buffer_descriptor *desc,
			 const void *buf)
{
	const struct rm68140_config *config = dev->config;
	struct rm68140_data *data = dev->data;
	struct display_buffer_descriptor mipi_desc;
	int r;
	const uint8_t *write_data_start = buf;
	uint16_t write_cnt;
	uint16_t nbr_of_writes;
	uint16_t write_h;

	__ASSERT(desc->width <= desc->pitch, "Pitch is smaller than width");
	__ASSERT((desc->pitch * data->bytes_per_pixel * desc->height) <=
		 desc->buf_size, "Input buffer too small");

	r = rm68140_set_mem_area(dev, x, y, desc->width, desc->height);
	if (r < 0) {
		return r;
	}

	if (desc->pitch > desc->width) {
		write_h = 1U;
		nbr_of_writes = desc->height;
		mipi_desc.height = 1;
		mipi_desc.buf_size = desc->pitch * data->bytes_per_pixel;
	} else {
		write_h = desc->height;
		mipi_desc.height = desc->height;
		mipi_desc.buf_size = desc->width * data->bytes_per_pixel * write_h;
		nbr_of_writes = 1U;
	}
	mipi_desc.width = desc->width;
	mipi_desc.pitch = desc->width;
	mipi_desc.frame_incomplete = desc->frame_incomplete;

	/* Send RAMWR command (0x2C) to start memory write */
	r = rm68140_transmit(dev, RM68140_RAMWR, NULL, 0);
	if (r < 0) {
		return r;
	}

	for (write_cnt = 0U; write_cnt < nbr_of_writes; ++write_cnt) {
		r = mipi_dbi_write_display(config->mipi_dev,
					   &config->dbi_config,
					   write_data_start,
					   &mipi_desc,
					   data->pixel_format);
		if (r < 0) {
			return r;
		}
		write_data_start += desc->pitch * data->bytes_per_pixel;
	}

	return 0;
}

/* Blanking control functions */
static int rm68140_display_blanking_on(const struct device *dev)
{
	return rm68140_transmit(dev, RM68140_DISPON, NULL, 0);
}

static int rm68140_display_blanking_off(const struct device *dev)
{
	return rm68140_transmit(dev, RM68140_DISPOFF, NULL, 0);
}

/* API structure for the display driver */
static const struct display_driver_api rm68140_api = {
	.write = rm68140_write,
	.blanking_on = rm68140_display_blanking_on,
	.blanking_off = rm68140_display_blanking_off,
	/* Additional API functions (e.g., read, set_pixel_format, set_orientation, get_capabilities)
	 * can be implemented similarly.
	 */
};

/* Device instance definitions.
 * In an actual project, the fixed register values would be populated from devicetree.
 * For this example, the values are hardcoded as per Bodmer's library.
 */
static struct rm68140_data rm68140_data_instance;

static const struct rm68140_config rm68140_config_instance = {
	.mipi_dev = DEVICE_DT_GET(DT_PARENT(DT_INST(0, ilitek,rm68140))),
	.dbi_config = {
		.mode = MIPI_DBI_MODE_SPI_4WIRE,
		.config = {
			SPI_OP_MODE_MASTER | SPI_WORD_SET(8),
			0,
		},
	},
	.pixel_format = PIXEL_FORMAT_RGB_565,
	.x_resolution = 320,
	.y_resolution = 480,
	.inversion = false,

	/* Fixed register values from Bodmer's TFT_eSPI library for RM68140 */
	/* 0xD0: Power / VCOM settings */
	.power_vcom_1 = 0x07, /* VCOM control */
	.power_vcom_2 = 0x42, /* VCOM voltage adjustment */
	.power_vcom_3 = 0x18, /* Boost circuit setting */

	/* 0xD1: Additional power settings */
	.power_extra_1 = 0x00, /* Offset control */
	.power_extra_2 = 0x07, /* Bias/ratio control */
	.power_extra_3 = 0x10, /* Additional voltage tuning */

	/* 0xD2: Other power settings */
	.power_misc_1 = 0x01, /* Minor power adjustment */
	.power_misc_2 = 0x02, /* Fine tuning */

	/* 0xC0: Panel drive timing settings */
	.panel_drive_1 = 0x10, /* Drive mode/start voltage */
	.panel_drive_2 = 0x3B, /* Drive strength/timing */
	.panel_drive_3 = 0x00, /* Reserved */
	.panel_drive_4 = 0x02, /* Timing parameter (porch) */
	.panel_drive_5 = 0x11, /* Final drive tuning */

	/* 0xC5: VCOM adjustment */
	.vcom_adj = 0x03, /* VCOM level adjustment */

	/* 0xC8: Gamma correction settings */
	.gamma_corr_1  = 0x00,
	.gamma_corr_2  = 0x32,
	.gamma_corr_3  = 0x36,
	.gamma_corr_4  = 0x45,
	.gamma_corr_5  = 0x06,
	.gamma_corr_6  = 0x16,
	.gamma_corr_7  = 0x37,
	.gamma_corr_8  = 0x75,
	.gamma_corr_9  = 0x77,
	.gamma_corr_10 = 0x54,
	.gamma_corr_11 = 0x0C,
	.gamma_corr_12 = 0x00,

	/* MADCTL: Memory Access Control */
	.mem_access = 0x0A, /* Sets display rotation/mirroring */

	/* 0x3A: Pixel format (COLMOD) */
	.pixel_format_reg = 0x55, /* 16-bit RGB565 mode */

	/* CASET: Column Address Set */
	.column_start = 0x0000, /* Start column 0 */
	.column_end   = 0x013F, /* End column 319 */

	/* PASET: Page (Row) Address Set */
	.row_start    = 0x0000, /* Start row 0 */
	.row_end      = 0x01DF, /* End row 479 */

	.regs_init_fn = rm68140_regs_init,
};

DEVICE_DT_DEFINE(DT_INST(0, ilitek,rm68140),
		 mipi_dbi_device_init, /* Assume appropriate MIPI DBI init function */
		 NULL,
		 &rm68140_data_instance,
		 &rm68140_config_instance,
		 POST_KERNEL,
		 CONFIG_DISPLAY_INIT_PRIORITY,
		 &rm68140_api);

