/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/mfd/aw9523b.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#pragma push_macro("LOG_ERR")
#pragma push_macro("LOG_DBG")
#undef LOG_ERR
#undef LOG_DBG
#define LOG_ERR(...)
#define LOG_DBG(...)
#include "drivers/spi/spi_context.h"
#pragma pop_macro("LOG_DBG")
#pragma pop_macro("LOG_ERR")

LOG_MODULE_REGISTER(spi_lcd_raw, CONFIG_LOG_DEFAULT_LEVEL);
#include "drivers/spi/spi_esp32_spim.h"
#include <hal/spi_ll.h>
#include <soc/soc.h>
#include <soc/gpio_reg.h>
#include <soc/io_mux_reg.h>

#define LCD_NODE DT_NODELABEL(lcd_spi)
#define LCD_BUS_NODE DT_NODELABEL(spi2)
#define AW_NODE DT_NODELABEL(aw9523b)
#define AXP_NODE DT_PATH(soc, i2c_60013000, axp2101_34)

BUILD_ASSERT(DT_NODE_HAS_STATUS(LCD_NODE, okay),
	     "LCD SPI device not defined in the device tree");
BUILD_ASSERT(DT_NODE_HAS_STATUS(AW_NODE, okay),
	     "AW9523B device not defined in the device tree");
BUILD_ASSERT(DT_NODE_HAS_STATUS(AXP_NODE, okay),
	     "AXP2101 device not defined in the device tree");

static const struct spi_cs_control lcd_cs_control = {
	.gpio = GPIO_DT_SPEC_GET_BY_IDX(LCD_BUS_NODE, cs_gpios, 0),
	.delay = 0U,
	.cs_is_gpio = true,
};

static const struct spi_dt_spec lcd_spi = {
	.bus = DEVICE_DT_GET(LCD_BUS_NODE),
	.config = {
		.frequency = 40000000U,
		.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(9) |
			     SPI_TRANSFER_MSB | SPI_HALF_DUPLEX |
			     SPI_HOLD_ON_CS,
		.slave = DT_REG_ADDR(LCD_NODE),
		.cs = lcd_cs_control,
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
#define ILI9XXX_IDMOFF  0x38

#define ILI9XXX_MADCTL_BGR BIT(3)
#define ILI9XXX_PIXSET_16BIT 0x55

#define ILI9342C_CMD_SETEXTC 0xC8
#define ILI9342C_CMD_PWCTRL1 0xC0
#define ILI9342C_CMD_PWCTRL2 0xC1
#define ILI9342C_CMD_VMCTRL1 0xC5
#define ILI9342C_CMD_FRCTRL  0xB0
#define ILI9342C_CMD_INVCTRL 0xF6
#define ILI9342C_CMD_PGAMCTRL 0xE0
#define ILI9342C_CMD_NGAMCTRL 0xE1
#define ILI9342C_CMD_DISCTRL 0xB6

#define AW_LCD_RST_BIT BIT(5)
#define AXP2101_DLDO1_ENABLE BIT(7)

static const struct i2c_dt_spec aw_i2c = I2C_DT_SPEC_GET(AW_NODE);
static const struct i2c_dt_spec axp_i2c = I2C_DT_SPEC_GET(AXP_NODE);

static const uint8_t ili9342c_extc[] = {0xFF, 0x93, 0x42};
static const uint8_t ili9342c_pwctrl1[] = {0x12, 0x12};
static const uint8_t ili9342c_pwctrl2[] = {0x03};
static const uint8_t ili9342c_vmctrl1[] = {0xF2};
static const uint8_t ili9342c_frctrl[] = {0xE0};
static const uint8_t ili9342c_invctrl[] = {0x01, 0x00, 0x00};
static const uint8_t ili9342c_gammap[] = {
	0x00, 0x0C, 0x11, 0x04, 0x11, 0x08, 0x37, 0x89,
	0x4C, 0x06, 0x0C, 0x0A, 0x2E, 0x34, 0x0F,
};
static const uint8_t ili9342c_gamman[] = {
	0x00, 0x0B, 0x11, 0x05, 0x13, 0x09, 0x33, 0x67,
	0x48, 0x07, 0x0E, 0x0B, 0x2E, 0x33, 0x0F,
};
static const uint8_t ili9342c_disctrl[] = {0x08, 0x82, 0x1D, 0x04};

static int ensure_i2c_ready(const struct i2c_dt_spec *spec, const char *name)
{
	if (!device_is_ready(spec->bus)) {
		LOG_ERR("%s I2C bus not ready", name);
		return -ENODEV;
	}

	return 0;
}

static int aw_reg_read(uint8_t reg, uint8_t *value)
{
	int ret = i2c_reg_read_byte_dt(&aw_i2c, reg, value);

	if (ret < 0) {
		LOG_ERR("AW9523B read 0x%02X failed (%d)", reg, ret);
	}

	return ret;
}

static int aw_reg_write(uint8_t reg, uint8_t value)
{
	int ret = i2c_reg_write_byte_dt(&aw_i2c, reg, value);

	if (ret < 0) {
		LOG_ERR("AW9523B write 0x%02X failed (%d)", reg, ret);
	}

	return ret;
}

static int aw_reg_update(uint8_t reg, uint8_t mask, uint8_t value)
{
	int ret = i2c_reg_update_byte_dt(&aw_i2c, reg, mask, value);

	if (ret < 0) {
		LOG_ERR("AW9523B update 0x%02X failed (%d)", reg, ret);
	}

	return ret;
}

static void aw9523b_dump_registers(void)
{
	if (!device_is_ready(aw_i2c.bus)) {
		LOG_WRN("AW9523B dump skipped (bus not ready)");
		return;
	}

	static const uint8_t regs[] = {
		AW9523B_REG_INPUT0,
		AW9523B_REG_INPUT1,
		AW9523B_REG_OUTPUT0,
		AW9523B_REG_OUTPUT1,
		AW9523B_REG_CONFIG0,
		AW9523B_REG_CONFIG1,
		AW9523B_REG_INT0,
		AW9523B_REG_INT1,
		AW9523B_REG_ID,
		AW9523B_REG_CTL,
		AW9523B_REG_MODE0,
		AW9523B_REG_MODE1,
	};
	uint8_t value;

	for (size_t i = 0; i < ARRAY_SIZE(regs); ++i) {
		if (aw_reg_read(regs[i], &value) == 0) {
			LOG_INF("[REGDUMP] AW9523B[0x%02X] = 0x%02X", regs[i], value);
		}
	}
}

static int aw9523b_configure(void)
{
	int ret = ensure_i2c_ready(&aw_i2c, "AW9523B");

	if (ret < 0) {
		return ret;
	}

	static const struct {
		uint8_t reg;
		uint8_t val;
	} init_seq[] = {
		{AW9523B_REG_OUTPUT0, 0x07},
		{AW9523B_REG_OUTPUT1, 0x83},
		{AW9523B_REG_CONFIG0, 0x18},
		{AW9523B_REG_CONFIG1, 0x0C},
		{AW9523B_REG_CTL,     0x10},
		{AW9523B_REG_MODE0,   0xFF},
		{AW9523B_REG_MODE1,   0xFF},
	};

	for (size_t i = 0; i < ARRAY_SIZE(init_seq); ++i) {
		ret = aw_reg_write(init_seq[i].reg, init_seq[i].val);
		if (ret < 0) {
			return ret;
		}
	}

	ret = aw_reg_update(AW9523B_REG_OUTPUT1, AW_LCD_RST_BIT, 0x00);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int aw9523b_set_lcd_reset(bool level)
{
	return aw_reg_update(AW9523B_REG_OUTPUT1, AW_LCD_RST_BIT,
			     level ? AW_LCD_RST_BIT : 0x00);
}

static int axp_reg_read(uint8_t reg, uint8_t *value)
{
	int ret = i2c_reg_read_byte_dt(&axp_i2c, reg, value);

	if (ret < 0) {
		LOG_ERR("AXP2101 read 0x%02X failed (%d)", reg, ret);
	}

	return ret;
}

static int axp_reg_write(uint8_t reg, uint8_t value)
{
	int ret = i2c_reg_write_byte_dt(&axp_i2c, reg, value);

	if (ret < 0) {
		LOG_ERR("AXP2101 write 0x%02X failed (%d)", reg, ret);
	}

	return ret;
}

static int axp_reg_update(uint8_t reg, uint8_t mask, uint8_t value)
{
	int ret = i2c_reg_update_byte_dt(&axp_i2c, reg, mask, value);

	if (ret < 0) {
		LOG_ERR("AXP2101 update 0x%02X failed (%d)", reg, ret);
	}

	return ret;
}

static void axp2101_dump_registers(void)
{
	if (!device_is_ready(axp_i2c.bus)) {
		LOG_WRN("AXP2101 dump skipped (bus not ready)");
		return;
	}

	static const uint8_t regs[] = {
		0x00, 0x01, 0x02, 0x03,
		0x10, 0x12, 0x90, 0x94,
		0x95, 0x96, 0x99,
	};
	uint8_t value;

	for (size_t i = 0; i < ARRAY_SIZE(regs); ++i) {
		if (axp_reg_read(regs[i], &value) == 0) {
			LOG_INF("[REGDUMP] AXP2101[0x%02X] = 0x%02X", regs[i], value);
		}
	}
}

static int axp2101_configure(void)
{
	int ret = ensure_i2c_ready(&axp_i2c, "AXP2101");

	if (ret < 0) {
		return ret;
	}

	static const struct {
		uint8_t reg;
		uint8_t val;
	} init_seq[] = {
		{0x90, 0xBF},
		{0x94, 0x1C},
		{0x95, 0x1C},
	};

	for (size_t i = 0; i < ARRAY_SIZE(init_seq); ++i) {
		ret = axp_reg_write(init_seq[i].reg, init_seq[i].val);
		if (ret < 0) {
			return ret;
		}
	}

	ret = axp_reg_update(0x90, AXP2101_DLDO1_ENABLE, AXP2101_DLDO1_ENABLE);
	if (ret < 0) {
		return ret;
	}

	ret = axp_reg_write(0x99, 0x17);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static void dump_spi_registers(const char *tag)
{
	spi_dev_t *spi = SPI_LL_GET_HW(SPI2_HOST);

	LOG_INF("[REGDUMP] --- SPI2 %s ---", tag);
	LOG_INF("[REGDUMP] SPI[2] CMD=0x%08X USER=0x%08X USER1=0x%08X USER2=0x%08X",
		spi->cmd.val, spi->user.val, spi->user1.val, spi->user2.val);
	LOG_INF("[REGDUMP] SPI[2] CTRL=0x%08X CLOCK=0x%08X MISC=0x%08X MS_DLEN=0x%08X",
		spi->ctrl.val, spi->clock.val, spi->misc.val, spi->ms_dlen.val);
	LOG_INF("[REGDUMP] SPI[2] DMA_CONF=0x%08X", spi->dma_conf.val);
}

static void dump_pinmux(void)
{
	LOG_INF("[REGDUMP] PIN3:  OUT_SEL=0x%08X IN_SEL=0x%08X PIN=0x%08X IO_MUX=0x%08X",
		REG_READ(GPIO_FUNC3_OUT_SEL_CFG_REG),
		REG_READ(GPIO_FUNC3_IN_SEL_CFG_REG),
		REG_READ(GPIO_PIN3_REG),
		REG_READ(IO_MUX_GPIO3_REG));

	LOG_INF("[REGDUMP] PIN35: OUT_SEL=0x%08X IN_SEL=0x%08X PIN=0x%08X IO_MUX=0x%08X",
		REG_READ(GPIO_FUNC35_OUT_SEL_CFG_REG),
		REG_READ(GPIO_FUNC35_IN_SEL_CFG_REG),
		REG_READ(GPIO_PIN35_REG),
		REG_READ(IO_MUX_GPIO35_REG));

	LOG_INF("[REGDUMP] PIN36: OUT_SEL=0x%08X IN_SEL=0x%08X PIN=0x%08X IO_MUX=0x%08X",
		REG_READ(GPIO_FUNC36_OUT_SEL_CFG_REG),
		REG_READ(GPIO_FUNC36_IN_SEL_CFG_REG),
		REG_READ(GPIO_PIN36_REG),
		REG_READ(IO_MUX_GPIO36_REG));

	LOG_INF("[REGDUMP] PIN37: OUT_SEL=0x%08X IN_SEL=0x%08X PIN=0x%08X IO_MUX=0x%08X",
		REG_READ(GPIO_FUNC37_OUT_SEL_CFG_REG),
		REG_READ(GPIO_FUNC37_IN_SEL_CFG_REG),
		REG_READ(GPIO_PIN37_REG),
		REG_READ(IO_MUX_GPIO37_REG));
}

static void dump_registers(const char *tag)
{
	LOG_INF("[REGDUMP] === %s ===", tag);
	aw9523b_dump_registers();
	axp2101_dump_registers();
	dump_spi_registers(tag);
	dump_pinmux();
	LOG_INF("[REGDUMP] === END ===");
}

static void spi_fixup_esp32_hal(void)
{
#if defined(CONFIG_SOC_SERIES_ESP32S3)
	const struct device *bus = lcd_spi.bus;
	struct spi_esp32_data *data = bus->data;
	spi_hal_dev_config_t *dev_cfg = &data->dev_config;
	spi_hal_trans_config_t *trans_cfg = &data->trans_config;
	spi_hal_context_t *hal = &data->hal;
	spi_dev_t *hw = hal->hw;

	dev_cfg->tx_lsbfirst = 0;
	dev_cfg->rx_lsbfirst = 0;
	dev_cfg->no_compensate = 1;
	dev_cfg->timing_conf.timing_dummy = 0;
	dev_cfg->timing_conf.timing_miso_delay = 0;

	trans_cfg->dummy_bits = 0;

	data->timing_config.clock_reg = dev_cfg->timing_conf.clock_reg;
	data->timing_config.clock_source = dev_cfg->timing_conf.clock_source;
	data->timing_config.timing_dummy = 0;
	data->timing_config.timing_miso_delay = 0;
	data->timing_config.rx_sample_point = dev_cfg->timing_conf.rx_sample_point;

	spi_hal_setup_device(hal, dev_cfg);

	hw->user.val = BIT(27);
	hw->user1.val = 0;
	hw->clock.val = 0x00009109;
	hw->misc.val = 0x0000003F;

	trans_cfg->dummy_bits = 0;

	LOG_DBG("Fixup applied: USER=0x%08X USER1=0x%08X CLOCK=0x%08X",
		hw->user.val, hw->user1.val, hw->clock.val);
#endif
}

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

		spi_dev_t *hw = SPI_LL_GET_HW(SPI2_HOST);
		hw->user.val = BIT(27);
		hw->user1.val = 0;
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

static int lcd_reset_sequence(void)
{
	int ret = aw9523b_set_lcd_reset(false);

	if (ret < 0) {
		return ret;
	}

	k_msleep(10);

	ret = aw9523b_set_lcd_reset(true);
	if (ret < 0) {
		return ret;
	}

	k_msleep(120);

	return 0;
}

static int lcd_configure_panel(void)
{
	int ret;

	ret = lcd_send_command(ILI9XXX_SWRESET);
	if (ret < 0) {
		return ret;
	}

	k_msleep(120);

	ret = lcd_send_command_with_data(ILI9342C_CMD_SETEXTC, ili9342c_extc,
					 sizeof(ili9342c_extc));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_PWCTRL1, ili9342c_pwctrl1,
					 sizeof(ili9342c_pwctrl1));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_PWCTRL2, ili9342c_pwctrl2,
					 sizeof(ili9342c_pwctrl2));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_VMCTRL1, ili9342c_vmctrl1,
					 sizeof(ili9342c_vmctrl1));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_FRCTRL, ili9342c_frctrl,
					 sizeof(ili9342c_frctrl));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_INVCTRL, ili9342c_invctrl,
					 sizeof(ili9342c_invctrl));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_PGAMCTRL, ili9342c_gammap,
					 sizeof(ili9342c_gammap));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_NGAMCTRL, ili9342c_gamman,
					 sizeof(ili9342c_gamman));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command_with_data(ILI9342C_CMD_DISCTRL, ili9342c_disctrl,
					 sizeof(ili9342c_disctrl));
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command(ILI9XXX_SLPOUT);
	if (ret < 0) {
		return ret;
	}

	k_msleep(120);

	ret = lcd_send_command(ILI9XXX_IDMOFF);
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command(ILI9XXX_INVON);
	if (ret < 0) {
		return ret;
	}

	uint8_t pixel_format = ILI9XXX_PIXSET_16BIT;

	ret = lcd_send_command_with_data(ILI9XXX_PIXSET, &pixel_format, 1U);
	if (ret < 0) {
		return ret;
	}

	uint8_t madctl = ILI9XXX_MADCTL_BGR;

	ret = lcd_send_command_with_data(ILI9XXX_MADCTL, &madctl, 1U);
	if (ret < 0) {
		return ret;
	}

	ret = lcd_send_command(ILI9XXX_DISPON);
	if (ret < 0) {
		return ret;
	}

	k_msleep(20);

	return 0;
}

int main(void)
{
	if (!spi_is_ready_dt(&lcd_spi)) {
		LOG_ERR("SPI device not ready");
		return -1;
	}

	spi_fixup_esp32_hal();

	LOG_INF("SPI operation flags: 0x%08X", lcd_spi.config.operation);

	dump_registers("Before configuration");

	if (axp2101_configure() < 0) {
		return -1;
	}

	if (aw9523b_configure() < 0) {
		return -1;
	}

	dump_registers("After AW/AXP setup");

	if (lcd_reset_sequence() < 0) {
		LOG_ERR("Failed to reset LCD");
		return -1;
	}

	dump_registers("After LCD reset");

	if (lcd_configure_panel() < 0) {
		LOG_ERR("Failed to configure LCD panel");
		return -1;
	}

	if (lcd_set_address_window(0U, 0U, LCD_WIDTH - 1U, LCD_HEIGHT - 1U) < 0) {
		LOG_ERR("Failed to set display area");
		return -1;
	}

	LOG_INF("Drawing gradient");

	if (lcd_fill_gradient() < 0) {
		LOG_ERR("Failed to draw gradient");
		return -1;
	}

	dump_registers("After first frame");

	LOG_INF("Gradient complete");

	while(true);
}
