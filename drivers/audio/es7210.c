/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2025
 */

#define DT_DRV_COMPAT everest_es7210

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "es7210.h"

LOG_MODULE_REGISTER(es7210, CONFIG_AUDIO_CODEC_LOG_LEVEL);

#define ES7210_DEFAULT_GAIN 0x1B
#define ES7210_GAIN_MIN     0x00
#define ES7210_GAIN_MAX     0x1F

struct es7210_config {
	struct i2c_dt_spec i2c;
	struct gpio_dt_spec reset_gpio;
};

struct es7210_data {
	uint8_t mic_gain_left;
	uint8_t mic_gain_right;
	bool muted;
};

struct es7210_reg_val {
	uint8_t reg;
	uint8_t val;
};

static const struct es7210_reg_val es7210_init_seq[] = {
	/* Default bring-up sequence used on M5Stack CoreS3. */
	{ ES7210_REG_RESET_CTL, 0x41 },
	{ ES7210_REG_CLK_ON_OFF, 0x1F },
	{ ES7210_REG_DIGITAL_PDN, 0x00 },
	{ ES7210_REG_ADC_OSR, 0x20 },
	{ ES7210_REG_MODE_CFG, 0x10 },
	{ ES7210_REG_TCT0_CHPINI, 0x30 },
	{ ES7210_REG_TCT1_CHPINI, 0x30 },
	{ ES7210_REG_ADC34_HPF2, 0x0A },
	{ ES7210_REG_ADC34_HPF1, 0x2A },
	{ ES7210_REG_ADC12_HPF2, 0x0A },
	{ ES7210_REG_ADC12_HPF1, 0x2A },
	{ 0x02, 0xC1 },
	{ 0x04, 0x01 },
	{ 0x05, 0x00 },
	{ 0x11, 0x60 },
	{ ES7210_REG_ANALOG_SYS, 0x42 },
	{ ES7210_REG_MICBIAS12, 0x70 },
	{ ES7210_REG_MICBIAS34, 0x70 },
	{ ES7210_REG_MIC1_GAIN, ES7210_DEFAULT_GAIN },
	{ ES7210_REG_MIC2_GAIN, ES7210_DEFAULT_GAIN },
	{ ES7210_REG_MIC3_GAIN, 0x00 },
	{ ES7210_REG_MIC4_GAIN, 0x00 },
	{ ES7210_REG_MIC1_LP, 0x00 },
	{ ES7210_REG_MIC2_LP, 0x00 },
	{ ES7210_REG_MIC3_LP, 0x00 },
	{ ES7210_REG_MIC4_LP, 0x00 },
	{ ES7210_REG_MIC12_PDN, 0x00 },
	{ ES7210_REG_MIC34_PDN, 0xFF },
	{ ES7210_REG_CLK_ON_OFF, 0x14 },
};

static int es7210_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	const struct es7210_config *cfg = dev->config;
	int ret;

	ret = i2c_reg_write_byte_dt(&cfg->i2c, reg, val);
	if (ret < 0) {
		LOG_ERR("I2C write failed reg 0x%02x: %d", reg, ret);
	}

	return ret;
}

static int es7210_write_seq(const struct device *dev, const struct es7210_reg_val *seq,
			    size_t len)
{
	for (size_t i = 0; i < len; i++) {
		int ret = es7210_write_reg(dev, seq[i].reg, seq[i].val);

		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static int es7210_reset(const struct device *dev)
{
	int ret;

	ret = es7210_write_reg(dev, ES7210_REG_RESET_CTL, 0xFF);
	if (ret < 0) {
		return ret;
	}

	k_msleep(1);
	return 0;
}

static int es7210_set_input_gain(const struct device *dev, audio_channel_t channel, int gain)
{
	struct es7210_data *data = dev->data;
	int ret;

	if (gain < ES7210_GAIN_MIN || gain > ES7210_GAIN_MAX) {
		return -EINVAL;
	}

	switch (channel) {
	case AUDIO_CHANNEL_FRONT_LEFT:
		data->mic_gain_left = (uint8_t)gain;
		return es7210_write_reg(dev, ES7210_REG_MIC1_GAIN, (uint8_t)gain);
	case AUDIO_CHANNEL_FRONT_RIGHT:
		data->mic_gain_right = (uint8_t)gain;
		return es7210_write_reg(dev, ES7210_REG_MIC2_GAIN, (uint8_t)gain);
	case AUDIO_CHANNEL_ALL:
		data->mic_gain_left = (uint8_t)gain;
		data->mic_gain_right = (uint8_t)gain;
		ret = es7210_write_reg(dev, ES7210_REG_MIC1_GAIN, (uint8_t)gain);
		if (ret < 0) {
			return ret;
		}
		return es7210_write_reg(dev, ES7210_REG_MIC2_GAIN, (uint8_t)gain);
	default:
		return -EINVAL;
	}
}

static int es7210_set_input_mute(const struct device *dev, audio_channel_t channel, bool mute)
{
	struct es7210_data *data = dev->data;

	if (channel != AUDIO_CHANNEL_ALL) {
		return -EINVAL;
	}

	data->muted = mute;
	return es7210_write_reg(dev, ES7210_REG_MIC12_PDN, mute ? 0xFF : 0x00);
}

static int es7210_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	const struct i2s_config *i2s_cfg = &cfg->dai_cfg.i2s;
	struct es7210_data *data = dev->data;
	int ret;

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		LOG_ERR("Only I2S DAI supported");
		return -EINVAL;
	}

	if ((i2s_cfg->format & I2S_FMT_DATA_FORMAT_MASK) != I2S_FMT_DATA_FORMAT_I2S) {
		LOG_ERR("Only I2S data format supported");
		return -EINVAL;
	}

	if (i2s_cfg->word_size != 16) {
		LOG_ERR("Only 16-bit word size supported");
		return -EINVAL;
	}

	if ((i2s_cfg->options & (I2S_OPT_FRAME_CLK_SLAVE | I2S_OPT_BIT_CLK_SLAVE)) != 0) {
		LOG_ERR("I2S controller must be clock master");
		return -EINVAL;
	}

	ret = es7210_reset(dev);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_seq(dev, es7210_init_seq, ARRAY_SIZE(es7210_init_seq));
	if (ret < 0) {
		return ret;
	}

	if (data->mic_gain_left != ES7210_DEFAULT_GAIN) {
		ret = es7210_write_reg(dev, ES7210_REG_MIC1_GAIN, data->mic_gain_left);
		if (ret < 0) {
			return ret;
		}
	}

	if (data->mic_gain_right != ES7210_DEFAULT_GAIN) {
		ret = es7210_write_reg(dev, ES7210_REG_MIC2_GAIN, data->mic_gain_right);
		if (ret < 0) {
			return ret;
		}
	}

	if (data->muted) {
		ret = es7210_write_reg(dev, ES7210_REG_MIC12_PDN, 0xFF);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

static void es7210_start_output(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static void es7210_stop_output(const struct device *dev)
{
	ARG_UNUSED(dev);
}

static int es7210_set_property(const struct device *dev, audio_property_t property,
			       audio_channel_t channel, audio_property_value_t val)
{
	switch (property) {
	case AUDIO_PROPERTY_INPUT_VOLUME:
		return es7210_set_input_gain(dev, channel, val.vol);
	case AUDIO_PROPERTY_INPUT_MUTE:
		return es7210_set_input_mute(dev, channel, val.mute);
	default:
		return -EINVAL;
	}
}

static int es7210_apply_properties(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static const struct audio_codec_api es7210_api = {
	.configure = es7210_configure,
	.start_output = es7210_start_output,
	.stop_output = es7210_stop_output,
	.set_property = es7210_set_property,
	.apply_properties = es7210_apply_properties,
};

static int es7210_init(const struct device *dev)
{
	const struct es7210_config *cfg = dev->config;

	if (!device_is_ready(cfg->i2c.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	if (cfg->reset_gpio.port) {
		if (!gpio_is_ready_dt(&cfg->reset_gpio)) {
			LOG_ERR("Reset GPIO not ready");
			return -ENODEV;
		}

		if (gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE) < 0) {
			LOG_ERR("Failed to configure reset GPIO");
			return -EIO;
		}
	}

	return 0;
}

#define ES7210_INIT(inst)								\
	static struct es7210_data es7210_data_##inst = {				\
		.mic_gain_left = ES7210_DEFAULT_GAIN,					\
		.mic_gain_right = ES7210_DEFAULT_GAIN,					\
	};										\
											\
	static const struct es7210_config es7210_config_##inst = {			\
		.i2c = I2C_DT_SPEC_INST_GET(inst),					\
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(inst, reset_gpios, {0}),	\
	};										\
											\
	DEVICE_DT_INST_DEFINE(inst, es7210_init, NULL, &es7210_data_##inst,		\
			      &es7210_config_##inst, POST_KERNEL,			\
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY, &es7210_api);

DT_INST_FOREACH_STATUS_OKAY(ES7210_INIT)
