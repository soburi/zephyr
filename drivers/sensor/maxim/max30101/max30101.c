/*
 * Copyright (c) 2017, NXP
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/logging/log.h>

#include "max30101.h"

LOG_MODULE_REGISTER(MAX301XX, CONFIG_SENSOR_LOG_LEVEL);

static inline uint8_t max301xx_max_num_channels(const struct device *dev)
{
	const struct max301xx_config *config = dev->config;

	if (config->model == MAX301XX_MODEL_30101) {
		return 3;
	}

	return 2;
}

static inline uint16_t max301xx_adc_full_scale(const struct device *dev)
{
	const struct max301xx_config *config = dev->config;
	const uint16_t full_scale[] = {2048, 4096, 8192, 16384};
	const uint8_t adc_sel = (config->spo2 >> MAX301XX_SPO2_ADC_RGE_SHIFT) & 0x03;

	return full_scale[adc_sel];
}

static inline uint8_t max301xx_fifo_data_bits(const struct device *dev)
{
	const struct max301xx_config *config = dev->config;
	const uint8_t pw_sel = (config->spo2 >> MAX301XX_SPO2_PW_SHIFT) & 0x03;

	return 15 + pw_sel;
}

static int max301xx_sample_fetch(const struct device *dev,
				 enum sensor_channel chan)
{
	struct max301xx_data *data = dev->data;
	const struct max301xx_config *config = dev->config;
	uint8_t buffer[MAX30101_MAX_BYTES_PER_SAMPLE];
	uint32_t fifo_data;
	int fifo_chan;
	int num_bytes;
	int i;

	/* Read all the active channels for one sample */
	num_bytes = data->num_channels * MAX30101_BYTES_PER_CHANNEL;
	if (i2c_burst_read_dt(&config->i2c, MAX301XX_REG_FIFO_DATA, buffer,
			      num_bytes)) {
		LOG_ERR("Could not fetch sample");
		return -EIO;
	}

	fifo_chan = 0;
	for (i = 0; i < num_bytes; i += MAX30101_BYTES_PER_CHANNEL) {
		/* Each channel is 18-bits */
		fifo_data = (buffer[i] << 16) | (buffer[i + 1] << 8) | (buffer[i + 2]);
		fifo_data >>= (MAX30101_BYTES_PER_CHANNEL * 8 - max301xx_fifo_data_bits(dev));

		/* Save the raw data */
		data->raw[fifo_chan++] = fifo_data;
	}

	return 0;
}

static int max301xx_channel_get(const struct device *dev,
				enum sensor_channel chan,
				struct sensor_value *val)
{
	const struct max301xx_config *config = dev->config;
	const uint8_t max_num_channels = max301xx_max_num_channels(dev);
	struct max301xx_data *data = dev->data;
	enum max301xx_led_channel led_chan;
	int fifo_chan;

	switch (chan) {
	case SENSOR_CHAN_PHOTOCURRENT_RED:
		led_chan = MAX301XX_LED_CHANNEL_RED;
		break;

	case SENSOR_CHAN_PHOTOCURRENT_IR:
		led_chan = MAX301XX_LED_CHANNEL_IR;
		break;

	case SENSOR_CHAN_PHOTOCURRENT_GREEN:
		if (config->model == MAX301XX_MODEL_30101) {
			led_chan = MAX301XX_LED_CHANNEL_GREEN;
			break;
		}
		/* fall through */
	default:
		LOG_ERR("Unsupported sensor channel");
		return -ENOTSUP;
	}

	/* Check if the led channel is active by looking up the associated fifo
	 * channel. If the fifo channel isn't valid, then the led channel
	 * isn't active.
	 */
	fifo_chan = data->map[led_chan];
	if (fifo_chan >= max_num_channels) {
		LOG_ERR("Inactive sensor channel");
		return -ENOTSUP;
	}

	const uint64_t raw = data->raw[fifo_chan];
	const uint32_t na = raw * max301xx_adc_full_scale(dev) / max301xx_fifo_data_bits(dev);

	val->val1 = na / 1000U;
	val->val2 = (na % 1000U) * 1000000;

	return 0;
}

static DEVICE_API(sensor, max301xx_driver_api) = {
	.sample_fetch = max301xx_sample_fetch,
	.channel_get = max301xx_channel_get,
};

static int max301xx_init(const struct device *dev)
{
	const struct max301xx_config *config = dev->config;
	const uint8_t max_num_channels = max301xx_max_num_channels(dev);
	struct max301xx_data *data = dev->data;
	uint8_t part_id;
	uint8_t mode_cfg;
	uint32_t led_chan;
	int fifo_chan;

	if (!device_is_ready(config->i2c.bus)) {
		LOG_ERR("Bus device is not ready");
		return -ENODEV;
	}

	/* Check the part id to make sure this is MAX30101 */
	if (i2c_reg_read_byte_dt(&config->i2c, MAX301XX_REG_PART_ID,
				 &part_id)) {
		LOG_ERR("Could not get Part ID");
		return -EIO;
	}
	if (part_id != MAX30101_PART_ID) {
		LOG_ERR("Got Part ID 0x%02x, expected 0x%02x",
			    part_id, MAX30101_PART_ID);
		return -EIO;
	}

	/* Reset the sensor */
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_MODE_CFG,
				  BIT(MAX301XX_MODE_CFG_RESET_SHIFT))) {
		return -EIO;
	}

	/* Wait for reset to be cleared */
	do {
		if (i2c_reg_read_byte_dt(&config->i2c, MAX301XX_REG_MODE_CFG,
					 &mode_cfg)) {
			LOG_ERR("Could read mode cfg after reset");
			return -EIO;
		}
	} while (mode_cfg & BIT(MAX301XX_MODE_CFG_RESET_SHIFT));

	/* Write the FIFO configuration register */
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_FIFO_CFG,
				  config->fifo)) {
		return -EIO;
	}

	/* Write the mode configuration register */
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_MODE_CFG,
				  config->mode)) {
		return -EIO;
	}

	/* Write the SpO2 configuration register */
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_SPO2_CFG,
				  config->spo2)) {
		return -EIO;
	}

	/* Write the LED pulse amplitude registers */
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_LED1_PA,
				  config->led_pa[0])) {
		return -EIO;
	}
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_LED2_PA,
				  config->led_pa[1])) {
		return -EIO;
	}
	if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_LED3_PA,
				  config->led_pa[2])) {
		return -EIO;
	}

	if (config->mode == MAX301XX_MODE_MULTI_LED) {
		uint8_t multi_led[2];

		/* Write the multi-LED mode control registers */
		multi_led[0] = (config->slot[1] << 4) | (config->slot[0]);
		multi_led[1] = (config->slot[3] << 4) | (config->slot[2]);

		if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_MULTI_LED,
					  multi_led[0])) {
			return -EIO;
		}
		if (i2c_reg_write_byte_dt(&config->i2c, MAX301XX_REG_MULTI_LED + 1,
					  multi_led[1])) {
			return -EIO;
		}
	}

	/* Initialize the channel map and active channel count */
	data->num_channels = 0U;
	for (led_chan = 0U; led_chan < max_num_channels; led_chan++) {
		data->map[led_chan] = max_num_channels;
	}

	/* Count the number of active channels and build a map that translates
	 * the LED channel number (red/ir/green) to the fifo channel number.
	 */
	for (fifo_chan = 0; fifo_chan < max_num_channels;
	     fifo_chan++) {
		led_chan = (config->slot[fifo_chan] & MAX301XX_SLOT_LED_MASK) - 1;
		if (led_chan < max_num_channels) {
			data->map[led_chan] = fifo_chan;
			data->num_channels++;
		}
	}

	return 0;
}

#define MAX301XX_SLOT_MULTI_LED(nd, i)                                                             \
	UTIL_CAT(MAX301XX_SLOT_,                                                                   \
		 COND_CODE_1(DT_PROP_HAS_IDX(nd, multi_led_slot, i),                               \
			(DT_STRING_UPPER_TOKEN_BY_IDX(nd, multi_led_slot, i)), (DISABLED)))

#define MAX301XX_SLOT0_MULTI_LED(nd)  MAX301XX_SLOT_MULTI_LED(nd, 0)
#define MAX301XX_SLOT1_MULTI_LED(nd)  MAX301XX_SLOT_MULTI_LED(nd, 1)
#define MAX301XX_SLOT2_MULTI_LED(nd)  MAX301XX_SLOT_MULTI_LED(nd, 2)
#define MAX301XX_SLOT3_MULTI_LED(nd)  MAX301XX_SLOT_MULTI_LED(nd, 3)
#define MAX301XX_SLOT0_HEART_RATE(nd) MAX301Xx_SLOT_LED1
#define MAX301XX_SLOT1_HEART_RATE(nd) MAX301XX_SLOT_NONE
#define MAX301XX_SLOT2_HEART_RATE(nd) MAX301XX_SLOT_NONE
#define MAX301XX_SLOT3_HEART_RATE(nd) MAX301XX_SLOT_NONE
#define MAX301XX_SLOT0_SPO2(nd)       MAX301XX_SLOT_LED1
#define MAX301XX_SLOT1_SPO2(nd)       MAX301XX_SLOT_LED2
#define MAX301XX_SLOT2_SPO2(nd)       MAX301XX_SLOT_NONE
#define MAX301XX_SLOT3_SPO2(nd)       MAX301XX_SLOT_NONE

#define MAX301XX_FIFO_A_FULL(nd)      DT_PROP_OR(nd, fifo_almost_full, 0)
#define MAX301XX_FIFO_ROLLOVER_EN(nd) DT_PROP_OR(nd, fifo_rollover, 0)

#define MAX301XX_ADC_RGE(nd) UTIL_CAT(MAX301XX_ADC_RGE_, DT_PROP_OR(nd, adc_full_scale, 0))
#define MAX301XX_SMP_AVE(nd) UTIL_CAT(MAX301XX_SMP_AVE_, DT_PROP_OR(nd, sample_averaging, 0))
#define MAX301XX_SR(nd)      UTIL_CAT(MAX301XX_SR_, DT_PROP_OR(nd, sample_rate, 0))
#define MAX301XX_MODE(nd)    UTIL_CAT(MAX301XX_MODE_, DT_STRING_UPPER_TOKEN(nd, mode))
#define MAX301XX_PW(nd)      UTIL_CAT(UTIL_CAT(MAX301XX_PW_, DT_PROP(nd, adc_resolution)), BITS)

#define MAX301XX_LED_PA(nd, pr, i) (DT_PROP_BY_IDX(nd, pr, i) / 200)
#define MAX301XX_SLOT(nd, pr, i)                                                                   \
	UTIL_CAT(UTIL_CAT(UTIL_CAT(MAX301XX_SLOT, i), _), DT_STRING_UPPER_TOKEN(nd, mode))(nd)

#define MAX301XX_DEFINE(node, mdl)                                                                 \
	static struct max301xx_config max301xx_cfg_##mdl##_##node = {                              \
		.i2c = I2C_DT_SPEC_GET(node),                                                      \
		.fifo = (MAX301XX_SMP_AVE(node) << MAX301XX_FIFO_CFG_SMP_AVE_SHIFT) |              \
			(MAX301XX_FIFO_A_FULL(node) << MAX301XX_FIFO_CFG_FIFO_FULL_SHIFT) |        \
			(MAX301XX_FIFO_ROLLOVER_EN(node) << MAX301XX_FIFO_CFG_ROLLOVER_EN_SHIFT),  \
		.spo2 = (MAX301XX_ADC_RGE(node) << MAX301XX_SPO2_ADC_RGE_SHIFT) |                  \
			(MAX301XX_SR(node) << MAX301XX_SPO2_SR_SHIFT) |                            \
			(MAX301XX_PW(node) << MAX301XX_SPO2_PW_SHIFT),                             \
		.led_pa = {                                                                        \
			DT_FOREACH_PROP_ELEM_SEP(node, led_pulse_amplitude, MAX301XX_LED_PA, (,))  \
		},                                                                                 \
		.mode = MAX301XX_MODE(node),                                                       \
		.slot = {                                                                          \
			DT_FOREACH_PROP_ELEM_SEP(node, multi_led_slot, MAX301XX_SLOT, (,))         \
		},                                                                                 \
		.model = mdl,                                                                      \
	};                                                                                         \
	static struct max301xx_data max301xx_data_##mdl##_##node;                                  \
	SENSOR_DEVICE_DT_DEFINE(node, max301xx_init, NULL, &max301xx_data_##mdl##_##node,          \
				&max301xx_cfg_##mdl##_##node, POST_KERNEL,                         \
				CONFIG_SENSOR_INIT_PRIORITY, &max301xx_driver_api);

DT_FOREACH_STATUS_OKAY_VARGS(maxim_max30101, MAX301XX_DEFINE, 1)
DT_FOREACH_STATUS_OKAY_VARGS(maxim_max30102, MAX301XX_DEFINE, 2)
