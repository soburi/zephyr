/*
 * Copyright (c) 2017 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 * Copyright (c) 2019 Nordic Semiconductor ASA
 * Copyright (c) 2019 Marc Reilly
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "display_st7735s.h"

#include <device.h>
#include <drivers/spi.h>
#include <drivers/gpio.h>
#include <sys/byteorder.h>
#include <drivers/display.h>

#define LOG_LEVEL CONFIG_DISPLAY_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(display_st7735s);

#define ST7735S_CS_PIN		DT_INST_0_SITRONIX_ST7735S_CS_GPIOS_PIN
#define ST7735S_CMD_DATA_PIN	DT_INST_0_SITRONIX_ST7735S_CMD_DATA_GPIOS_PIN
#define ST7735S_RESET_PIN	DT_INST_0_SITRONIX_ST7735S_RESET_GPIOS_PIN

struct st7735s_data {
	struct device *spi_dev;
	struct spi_config spi_config;
#ifdef DT_INST_0_SITRONIX_ST7735S_CS_GPIOS_CONTROLLER
	struct spi_cs_control cs_ctrl;
#endif

#ifdef DT_INST_0_SITRONIX_ST7735S_RESET_GPIOS_CONTROLLER
	struct device *reset_gpio;
#endif
	struct device *cmd_data_gpio;

	u16_t height;
	u16_t width;
	u16_t x_offset;
	u16_t y_offset;
};

#ifdef CONFIG_ST7735S_RGB565
#define ST7735S_PIXEL_SIZE 2u
#else
#define ST7735S_PIXEL_SIZE 3u
#endif

static int st7735s_blanking_off(const struct device *dev);
static int st7735s_blanking_on(const struct device *dev);

void st7735s_set_lcd_margins(struct st7735s_data *data,
			     u16_t x_offset, u16_t y_offset)
{
	data->x_offset = x_offset;
	data->y_offset = y_offset;
}

static void st7735s_set_cmd(struct st7735s_data *data, int is_cmd)
{
	gpio_pin_write(data->cmd_data_gpio, ST7735S_CMD_DATA_PIN, !is_cmd);
}

void st7735s_transmit(struct st7735s_data *data, u8_t cmd,
		u8_t *tx_data, size_t tx_count)
{
	struct spi_buf tx_buf = { .buf = &cmd, .len = 1 };
	struct spi_buf_set tx_bufs = { .buffers = &tx_buf, .count = 1 };

	st7735s_set_cmd(data, true);
	spi_write(data->spi_dev, &data->spi_config, &tx_bufs);

	if (tx_data != NULL) {
		tx_buf.buf = tx_data;
		tx_buf.len = tx_count;
		st7735s_set_cmd(data, false);
		spi_write(data->spi_dev, &data->spi_config, &tx_bufs);
	}
}

static void st7735s_exit_sleep(struct st7735s_data *data)
{
	st7735s_transmit(data, ST7735S_CMD_SLEEP_OUT, NULL, 0);
	k_sleep(K_MSEC(120));
}

static void st7735s_reset_display(struct st7735s_data *data)
{
	LOG_DBG("Resetting display");
#ifdef DT_INST_0_SITRONIX_ST7735S_RESET_GPIOS_CONTROLLER
	gpio_pin_write(data->reset_gpio, ST7735S_RESET_PIN, 1);
	k_sleep(K_MSEC(1));
	gpio_pin_write(data->reset_gpio, ST7735S_RESET_PIN, 0);
	k_sleep(K_MSEC(6));
	gpio_pin_write(data->reset_gpio, ST7735S_RESET_PIN, 1);
	k_sleep(K_MSEC(20));
#else
	st7735s_transmit(p_st7735s, ST7735S_CMD_SW_RESET, NULL, 0);
	k_sleep(K_MSEC(5));
#endif
}

int st7735s_init(struct device *dev)
{
	struct st7735s_data *data = (struct st7735s_data *)dev->driver_data;

    rcu_periph_clock_enable(RCU_GPIOA);
    rcu_periph_clock_enable(RCU_GPIOB);
    rcu_periph_clock_enable(RCU_GPIOC);
    rcu_periph_clock_enable(RCU_AF);
    rcu_periph_clock_enable(RCU_SPI0);
    gpio_init(GPIOC, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_13);
    gpio_init(GPIOA, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_1|GPIO_PIN_2);

    /* SPI0 GPIO config: NSS/PA4, SCK/PA5, MOSI/PA7 */
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5 |GPIO_PIN_6| GPIO_PIN_7);
    gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);

	//spi_config();

	data->spi_dev = device_get_binding(DT_INST_0_SITRONIX_ST7735S_BUS_NAME);
	if (data->spi_dev == NULL) {
		LOG_ERR("Could not get SPI device for LCD");
		return -EPERM;
	}

	data->spi_config.frequency = DT_INST_0_SITRONIX_ST7735S_SPI_MAX_FREQUENCY;
	data->spi_config.operation = SPI_OP_MODE_MASTER | SPI_WORD_SET(8);
	data->spi_config.slave = DT_INST_0_SITRONIX_ST7735S_BASE_ADDRESS;

#ifdef DT_INST_0_SITRONIX_ST7735S_CS_GPIOS_CONTROLLER
	data->cs_ctrl.gpio_dev =
		device_get_binding(DT_INST_0_SITRONIX_ST7735S_CS_GPIOS_CONTROLLER);
	data->cs_ctrl.gpio_pin = DT_INST_0_SITRONIX_ST7735S_CS_GPIOS_PIN;
	data->cs_ctrl.delay = 0U;
	data->spi_config.cs = &(data->cs_ctrl);
#else
	data->spi_config.cs = NULL;
#endif

#ifdef DT_INST_0_SITRONIX_ST7735S_RESET_GPIOS_CONTROLLER
	data->reset_gpio =
		device_get_binding(DT_INST_0_SITRONIX_ST7735S_RESET_GPIOS_CONTROLLER);
	if (data->reset_gpio == NULL) {
		LOG_ERR("Could not get GPIO port for display reset");
		return -EPERM;
	}

	if (gpio_pin_configure(data->reset_gpio, ST7735S_RESET_PIN, GPIO_DIR_OUT)) {
		LOG_ERR("Couldn't configure reset pin");
		return -EIO;
	}
#endif

	data->cmd_data_gpio =
		device_get_binding(DT_INST_0_SITRONIX_ST7735S_CMD_DATA_GPIOS_CONTROLLER);
	if (data->cmd_data_gpio == NULL) {
		LOG_ERR("Could not get GPIO port for cmd/DATA port");
		return -EPERM;
	}
	if (gpio_pin_configure(data->cmd_data_gpio, ST7735S_CMD_DATA_PIN,
			       GPIO_DIR_OUT)) {
		LOG_ERR("Couldn't configure cmd/DATA pin");
		return -EIO;
	}

	data->width = 240;
	data->height = 320;
	data->x_offset = 0;
	data->y_offset = 0;

#ifdef DT_INST_0_SITRONIX_ST7735S_WIDTH
	data->width = DT_INST_0_SITRONIX_ST7735S_WIDTH;
#endif
#ifdef DT_INST_0_SITRONIX_ST7735S_HEIGHT
	data->height = DT_INST_0_SITRONIX_ST7735S_HEIGHT;
#endif

	st7735s_reset_display(data);

	st7735s_blanking_on(dev);

	st7735s_lcd_init(data);

	st7735s_exit_sleep(data);

	return 0;
}

int st7735s_cmd_read8(struct st7735s_data *data, int cmd, u8_t *pRet)
{
	u8_t sendbuff[4];

	sendbuff[0] = cmd;

	const struct spi_buf tx_buf[2] = {
		{ .buf = sendbuff, .len = 1 },
		{ .buf = 0, .len = 1 },
	};
	const struct spi_buf rx_buf[2] = {
		{ .buf = 0, .len = 1 },
		{ .buf = pRet, .len = 1 }
	};
	struct spi_buf_set tx_bufs = { .buffers = tx_buf, .count = 2 };
	struct spi_buf_set rx_bufs = { .buffers = rx_buf, .count = 2 };

	st7735s_set_cmd(data, 1);
	int ret = spi_transceive(data->spi_dev, &data->spi_config, &tx_bufs,
				 &rx_bufs);
	st7735s_set_cmd(data, 0);

	return ret;
}

static int st7735s_blanking_on(const struct device *dev)
{
	struct st7735s_data *driver = (struct st7735s_data *)dev->driver_data;

	st7735s_transmit(driver, ST7735S_CMD_DISP_OFF, NULL, 0);
	return 0;
}

static int st7735s_blanking_off(const struct device *dev)
{
	struct st7735s_data *driver = (struct st7735s_data *)dev->driver_data;

	st7735s_transmit(driver, ST7735S_CMD_DISP_ON, NULL, 0);
	return 0;
}

static int st7735s_read(const struct device *dev,
			const u16_t x,
			const u16_t y,
			const struct display_buffer_descriptor *desc,
			void *buf)
{
	return -ENOTSUP;
}

static void st7735s_set_mem_area(struct st7735s_data *data, const u16_t x,
				 const u16_t y, const u16_t w, const u16_t h)
{
	u16_t spi_data[2];

	u16_t ram_x = x + data->x_offset;
	u16_t ram_y = y + data->y_offset;

	spi_data[0] = sys_cpu_to_be16(ram_x);
	spi_data[1] = sys_cpu_to_be16(ram_x + w - 1);
	st7735s_transmit(data, ST7735S_CMD_CASET, (u8_t *)&spi_data[0], 4);

	spi_data[0] = sys_cpu_to_be16(ram_y);
	spi_data[1] = sys_cpu_to_be16(ram_y + h - 1);
	st7735s_transmit(data, ST7735S_CMD_RASET, (u8_t *)&spi_data[0], 4);
}

static int st7735s_write(const struct device *dev,
			 const u16_t x,
			 const u16_t y,
			 const struct display_buffer_descriptor *desc,
			 const void *buf)
{
	struct st7735s_data *data = (struct st7735s_data *)dev->driver_data;
	const u8_t *write_data_start = (u8_t *) buf;
	struct spi_buf tx_buf;
	struct spi_buf_set tx_bufs;
	u16_t write_cnt;
	u16_t nbr_of_writes;
	u16_t write_h;

	__ASSERT(desc->width <= desc->pitch, "Pitch is smaller then width");
	__ASSERT((desc->pitch * ST7735S_PIXEL_SIZE * desc->height) <= desc->buf_size,
			"Input buffer to small");

	LOG_DBG("Writing %dx%d (w,h) @ %dx%d (x,y)",
			desc->width, desc->height, x, y);
	st7735s_set_mem_area(data, x, y, desc->width, desc->height);

	if (desc->pitch > desc->width) {
		write_h = 1U;
		nbr_of_writes = desc->height;
	} else {
		write_h = desc->height;
		nbr_of_writes = 1U;
	}

	st7735s_transmit(data, ST7735S_CMD_RAMWR,
			 (void *) write_data_start,
			 desc->width * ST7735S_PIXEL_SIZE * write_h);

	tx_bufs.buffers = &tx_buf;
	tx_bufs.count = 1;

	write_data_start += (desc->pitch * ST7735S_PIXEL_SIZE);
	for (write_cnt = 1U; write_cnt < nbr_of_writes; ++write_cnt) {
		tx_buf.buf = (void *)write_data_start;
		tx_buf.len = desc->width * ST7735S_PIXEL_SIZE * write_h;
		spi_write(data->spi_dev, &data->spi_config, &tx_bufs);
		write_data_start += (desc->pitch * ST7735S_PIXEL_SIZE);
	}

	return 0;
}

void *st7735s_get_framebuffer(const struct device *dev)
{
	return NULL;
}

int st7735s_set_brightness(const struct device *dev,
			   const u8_t brightness)
{
	return -ENOTSUP;
}

int st7735s_set_contrast(const struct device *dev,
			 const u8_t contrast)
{
	return -ENOTSUP;
}

void st7735s_get_capabilities(const struct device *dev,
			      struct display_capabilities *capabilities)
{
	struct st7735s_data *data = (struct st7735s_data *)dev->driver_data;

	memset(capabilities, 0, sizeof(struct display_capabilities));
	capabilities->x_resolution = data->width;
	capabilities->y_resolution = data->height;

#ifdef CONFIG_ST7735S_RGB565
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_565;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_565;
#else
	capabilities->supported_pixel_formats = PIXEL_FORMAT_RGB_888;
	capabilities->current_pixel_format = PIXEL_FORMAT_RGB_888;
#endif
	capabilities->current_orientation = DISPLAY_ORIENTATION_NORMAL;
}

int st7735s_set_pixel_format(const struct device *dev,
			     const enum display_pixel_format pixel_format)
{
#ifdef CONFIG_ST7735S_RGB565
	if (pixel_format == PIXEL_FORMAT_RGB_565) {
#else
	if (pixel_format == PIXEL_FORMAT_RGB_888) {
#endif
		return 0;
	}
	LOG_ERR("Pixel format change not implemented");
	return -ENOTSUP;
}

int st7735s_set_orientation(const struct device *dev,
			    const enum display_orientation orientation)
{
	if (orientation == DISPLAY_ORIENTATION_NORMAL) {
		return 0;
	}
	LOG_ERR("Changing display orientation not implemented");
	return -ENOTSUP;
}

static const struct display_driver_api st7735s_api = {
	.blanking_on = st7735s_blanking_on,
	.blanking_off = st7735s_blanking_off,
	.write = st7735s_write,
	.read = st7735s_read,
	.get_framebuffer = st7735s_get_framebuffer,
	.set_brightness = st7735s_set_brightness,
	.set_contrast = st7735s_set_contrast,
	.get_capabilities = st7735s_get_capabilities,
	.set_pixel_format = st7735s_set_pixel_format,
	.set_orientation = st7735s_set_orientation,
};

static struct st7735s_data st7735s_data;

DEVICE_AND_API_INIT(st7735s, DT_INST_0_SITRONIX_ST7735S_LABEL, &st7735s_init,
		    &st7735s_data, NULL, APPLICATION,
		    CONFIG_APPLICATION_INIT_PRIORITY, &st7735s_api);
