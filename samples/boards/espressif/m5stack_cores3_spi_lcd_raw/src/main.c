/*
* SPDX-License-Identifier: Apache-2.0
*/

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(spi_lcd_raw, CONFIG_LOG_DEFAULT_LEVEL);

#define LCD_NODE DT_NODELABEL(lcd_spi)
#define LCD_BUS_NODE DT_NODELABEL(spi2)

BUILD_ASSERT(DT_NODE_HAS_STATUS(LCD_NODE, okay),
"LCD SPI device not defined in the device tree");

static struct spi_cs_control lcd_cs_control = {
        .gpio = GPIO_DT_SPEC_GET_BY_IDX(LCD_BUS_NODE, cs_gpios, 0),
        .delay = 0U,
        .cs_is_gpio = true,
};

static const struct spi_dt_spec lcd_spi = {
        .bus = DEVICE_DT_GET(LCD_BUS_NODE),
        .config = {
                .frequency = 20000000U,
                .operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(9) |
                             SPI_TRANSFER_MSB | SPI_HALF_DUPLEX |
                             SPI_HOLD_ON_CS,
                .slave = DT_REG_ADDR(LCD_NODE),
                .cs = &lcd_cs_control,
                .word_delay = 0U,
        },
};

#define LCD_WIDTH  320U
#define LCD_HEIGHT 240U

#define ILI9XXX_SWRESET 0x01
#define ILI9XXX_SLPOUT  0x11
#define ILI9XXX_DISPON  0x29
#define ILI9XXX_CASET   0x2A
#define ILI9XXX_PASET   0x2B
#define ILI9XXX_RAMWR   0x2C
#define ILI9XXX_MADCTL  0x36
#define ILI9XXX_PIXSET  0x3A
#define ILI9XXX_INVON   0x21

#define ILI9XXX_MADCTL_BGR BIT(3)
#define ILI9XXX_PIXSET_16BIT 0x55

static int lcd_write_words(uint8_t dc, const uint8_t *data, size_t len)
{
	uint8_t buffer[64];
	size_t buffered = 0U;
	uint8_t current_byte = 0U;
	uint8_t bits_filled = 0U;

	while (len > 0U) {
		uint16_t word = (uint16_t)((dc & 0x1U) << 8) | *data++;

		for (int bit = 8; bit >= 0; --bit) {
			current_byte = (uint8_t)((current_byte << 1) |
				      ((word >> bit) & 0x1U));
			bits_filled++;

			if (bits_filled == 8U) {
				buffer[buffered++] = current_byte;
				current_byte = 0U;
				bits_filled = 0U;

				if (buffered == sizeof(buffer)) {
					struct spi_buf tx_buf = {
						.buf = buffer,
						.len = buffered,
					};
					struct spi_buf_set tx_set = {
						.buffers = &tx_buf,
						.count = 1,
					};

					int ret = spi_write_dt(&lcd_spi, &tx_set);
					if (ret < 0) {
						return ret;
					}

					buffered = 0U;
				}
			}
		}

		len--;
	}

	if (bits_filled > 0U) {
		current_byte <<= (8U - bits_filled);
		buffer[buffered++] = current_byte;
	}

	if (buffered > 0U) {
		struct spi_buf tx_buf = {
			.buf = buffer,
			.len = buffered,
		};
		struct spi_buf_set tx_set = {
			.buffers = &tx_buf,
			.count = 1,
		};

		int ret = spi_write_dt(&lcd_spi, &tx_set);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int lcd_send_command(uint8_t cmd)
{
	int ret = lcd_write_words(0U, &cmd, 1U);
	int release_ret = spi_release_dt(&lcd_spi);

	if (ret == 0 && release_ret < 0) {
		ret = release_ret;
	}

	return ret;
}

static int lcd_send_command_with_data(uint8_t cmd, const uint8_t *data, size_t len)
{
	int ret = lcd_write_words(0U, &cmd, 1U);

	if (ret < 0) {
		(void)spi_release_dt(&lcd_spi);
		return ret;
	}

	if (len > 0U) {
		ret = lcd_write_words(1U, data, len);
	}

	int release_ret = spi_release_dt(&lcd_spi);

	if (ret == 0 && release_ret < 0) {
		ret = release_ret;
	}

	return ret;
}

static int lcd_set_address_window(uint16_t x0, uint16_t y0,
uint16_t x1, uint16_t y1)
{
	uint8_t column_range[] = {
		(uint8_t)(x0 >> 8),
		(uint8_t)(x0 & 0xFF),
		(uint8_t)(x1 >> 8),
		(uint8_t)(x1 & 0xFF),
	};
	uint8_t row_range[] = {
		(uint8_t)(y0 >> 8),
		(uint8_t)(y0 & 0xFF),
		(uint8_t)(y1 >> 8),
		(uint8_t)(y1 & 0xFF),
	};
	int ret;

	ret = lcd_send_command_with_data(ILI9XXX_CASET, column_range,
	sizeof(column_range));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9XXX_PASET, row_range,
	sizeof(row_range));

	return ret;
}

static uint16_t gradient_color(uint16_t x, uint16_t y)
{
	uint16_t r = (x * 31U) / (LCD_WIDTH - 1U);
	uint16_t g = (y * 63U) / (LCD_HEIGHT - 1U);
	uint16_t b = ((x + y) * 31U) / ((LCD_WIDTH - 1U) + (LCD_HEIGHT - 1U));

	return (uint16_t)((r << 11) | (g << 5) | b);
}

static int lcd_fill_gradient(void)
{
	uint8_t command = ILI9XXX_RAMWR;
	uint8_t line_buffer[64];
	const size_t pixels_per_chunk = sizeof(line_buffer) / 2U;
	int ret = lcd_write_words(0U, &command, 1U);

	if (ret < 0) {
		(void)spi_release_dt(&lcd_spi);
		return ret;
	}

	for (uint16_t y = 0U; y < LCD_HEIGHT && ret == 0; ++y) {
		for (uint16_t x = 0U; x < LCD_WIDTH; x += pixels_per_chunk) {
			size_t pixels = MIN((size_t)(LCD_WIDTH - x), pixels_per_chunk);

			for (size_t i = 0U; i < pixels; ++i) {
				uint16_t color = gradient_color(x + i, y);

				line_buffer[2U * i] = (uint8_t)(color >> 8);
				line_buffer[(2U * i) + 1U] = (uint8_t)(color & 0xFF);
			}

			ret = lcd_write_words(1U, line_buffer, pixels * 2U);
			if (ret < 0) {
				break;
			}
		}
	}

	int release_ret = spi_release_dt(&lcd_spi);

	if (ret == 0 && release_ret < 0) {
		ret = release_ret;
	}

	return ret;
}

void main(void)
{
	if (!spi_is_ready_dt(&lcd_spi)) {
		LOG_ERR("SPI device not ready");
		return;
	}

	LOG_INF("Resetting LCD");
	if (lcd_send_command(ILI9XXX_SWRESET) < 0) {
		LOG_ERR("Failed to reset LCD");
		return;
	}

	k_msleep(120);

	if (lcd_send_command(ILI9XXX_SLPOUT) < 0) {
		LOG_ERR("Failed to exit sleep mode");
		return;
	}

	k_msleep(120);

	uint8_t pixel_format = ILI9XXX_PIXSET_16BIT;
	if (lcd_send_command_with_data(ILI9XXX_PIXSET, &pixel_format, 1U) < 0) {
		LOG_ERR("Failed to set pixel format");
		return;
	}

	uint8_t madctl = ILI9XXX_MADCTL_BGR;
	if (lcd_send_command_with_data(ILI9XXX_MADCTL, &madctl, 1U) < 0) {
		LOG_ERR("Failed to set MADCTL");
		return;
	}

	if (lcd_send_command(ILI9XXX_INVON) < 0) {
		LOG_WRN("Display inversion command failed");
	}

	if (lcd_set_address_window(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U) < 0) {
		LOG_ERR("Failed to set display area");
		return;
	}

	if (lcd_send_command(ILI9XXX_DISPON) < 0) {
		LOG_ERR("Failed to turn display on");
		return;
	}

	k_msleep(20);

	LOG_INF("Drawing gradient");

	if (lcd_fill_gradient() < 0) {
		LOG_ERR("Failed to draw gradient");
		return;
	}

	LOG_INF("Gradient complete");
}
