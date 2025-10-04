/*
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/audio/codec.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "es7210.h"

LOG_MODULE_REGISTER(everest_es7210, CONFIG_AUDIO_CODEC_LOG_LEVEL);

#define DT_DRV_COMPAT everest_es7210

struct es7210_config {
	struct i2c_dt_spec i2c;
	uint32_t mclk_freq;
	uint8_t mic_select;
	uint8_t mic_gain;
};

struct es7210_data {
	uint8_t clock_off_value;
	uint8_t mic_select;
	uint8_t mic_gain;
	uint32_t mclk;
	uint32_t sample_rate;
	bool muted;
	bool started;
};

struct es7210_coeff {
	uint32_t mclk;
	uint32_t lrck;
	uint8_t adc_div;
	uint8_t dll;
	uint8_t doubler;
	uint8_t osr;
	uint8_t lrck_h;
	uint8_t lrck_l;
};

static const struct es7210_coeff es7210_clk_coeffs[] = {
	{12288000U, 8000U, 0x03, 0x01, 0x00, 0x20, 0x06, 0x00},
	{16384000U, 8000U, 0x04, 0x01, 0x00, 0x20, 0x08, 0x00},
	{19200000U, 8000U, 0x1e, 0x00, 0x01, 0x28, 0x09, 0x60},
	{4096000U, 8000U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{11289600U, 11025U, 0x02, 0x01, 0x00, 0x20, 0x01, 0x00},
	{12288000U, 12000U, 0x02, 0x01, 0x00, 0x20, 0x04, 0x00},
	{19200000U, 12000U, 0x14, 0x00, 0x01, 0x28, 0x06, 0x40},
	{4096000U, 16000U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
	{19200000U, 16000U, 0x0a, 0x00, 0x00, 0x1e, 0x04, 0x80},
	{16384000U, 16000U, 0x02, 0x01, 0x00, 0x20, 0x04, 0x00},
	{12288000U, 16000U, 0x03, 0x01, 0x01, 0x20, 0x03, 0x00},
	{11289600U, 22050U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{12288000U, 24000U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{19200000U, 24000U, 0x0a, 0x00, 0x01, 0x28, 0x03, 0x20},
	{12288000U, 32000U, 0x03, 0x00, 0x00, 0x20, 0x01, 0x80},
	{16384000U, 32000U, 0x01, 0x01, 0x00, 0x20, 0x02, 0x00},
	{19200000U, 32000U, 0x05, 0x00, 0x00, 0x1e, 0x02, 0x58},
	{11289600U, 44100U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
	{12288000U, 48000U, 0x01, 0x01, 0x01, 0x20, 0x01, 0x00},
	{19200000U, 48000U, 0x05, 0x00, 0x01, 0x28, 0x01, 0x90},
	{16384000U, 64000U, 0x01, 0x01, 0x00, 0x20, 0x01, 0x00},
	{19200000U, 64000U, 0x05, 0x00, 0x01, 0x1e, 0x01, 0x2c},
	{11289600U, 88200U, 0x01, 0x01, 0x01, 0x20, 0x00, 0x80},
	{12288000U, 96000U, 0x01, 0x01, 0x01, 0x20, 0x00, 0x80},
	{19200000U, 96000U, 0x05, 0x00, 0x01, 0x28, 0x00, 0xC8},
};

static inline const struct es7210_config *dev_cfg(const struct device *dev)
{
	return dev->config;
}

static inline struct es7210_data *dev_data(const struct device *dev)
{
	return dev->data;
}

static int es7210_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
	int ret = i2c_reg_write_byte_dt(&dev_cfg(dev)->i2c, reg, val);

	if (ret < 0) {
		LOG_ERR("Failed to write reg 0x%02x", reg);
	}

	return ret;
}

static int es7210_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
	int ret = i2c_reg_read_byte_dt(&dev_cfg(dev)->i2c, reg, val);

	if (ret < 0) {
		LOG_ERR("Failed to read reg 0x%02x", reg);
	}

	return ret;
}

static int es7210_update_bits(const struct device *dev, uint8_t reg, uint8_t mask, uint8_t value)
{
	uint8_t cur;
	int ret = es7210_read_reg(dev, reg, &cur);

	if (ret < 0) {
		return ret;
	}

	uint8_t new_val = (cur & ~mask) | (value & mask);

	if (new_val == cur) {
		return 0;
	}

	return es7210_write_reg(dev, reg, new_val);
}

static const struct es7210_coeff *es7210_find_coeff(uint32_t mclk, uint32_t lrck)
{
	for (size_t i = 0U; i < ARRAY_SIZE(es7210_clk_coeffs); i++) {
		if (es7210_clk_coeffs[i].mclk == mclk && es7210_clk_coeffs[i].lrck == lrck) {
			return &es7210_clk_coeffs[i];
		}
	}

	return NULL;
}

static int es7210_reset(const struct device *dev)
{
	int ret = es7210_write_reg(dev, ES7210_RESET_REG00, 0xFF);

	if (ret < 0) {
		return ret;
	}

	k_msleep(5);

	ret = es7210_write_reg(dev, ES7210_RESET_REG00, 0x41);
	if (ret < 0) {
		return ret;
	}

	k_msleep(2);

	return 0;
}

static int es7210_config_sample_rate(const struct device *dev, uint32_t mclk, uint32_t lrck)
{
	const struct es7210_coeff *coeff = es7210_find_coeff(mclk, lrck);

	if (coeff == NULL) {
		LOG_ERR("Unsupported sample rate %u Hz with MCLK %u Hz", lrck, mclk);
		return -EINVAL;
	}

	int ret = es7210_write_reg(dev, ES7210_MAINCLK_REG02,
				   coeff->adc_div | (coeff->doubler << 6) | (coeff->dll << 7));
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_OSR_REG07, coeff->osr);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_LRCK_DIVH_REG04, coeff->lrck_h);
	if (ret < 0) {
		return ret;
	}

	return es7210_write_reg(dev, ES7210_LRCK_DIVL_REG05, coeff->lrck_l);
}

static int es7210_config_format(const struct device *dev, audio_dai_cfg_t *cfg)
{
	uint8_t reg;
	int ret = es7210_read_reg(dev, ES7210_SDP_INTERFACE1_REG11, &reg);

	if (ret < 0) {
		return ret;
	}

	reg &= ~0x03U;

	switch (cfg->i2s.format & I2S_FMT_DATA_FORMAT_MASK) {
	case I2S_FMT_DATA_FORMAT_I2S:
		break;
	case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
	case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
		reg |= 0x01;
		break;
	default:
		LOG_ERR("Unsupported I2S data format 0x%08x", cfg->i2s.format);
		return -ENOTSUP;
	}

	reg &= ~0xE0U;

	switch (cfg->i2s.word_size) {
	case 16U:
		reg |= 0x60;
		break;
	case 24U:
		reg |= 0x00;
		break;
	case 32U:
		reg |= 0x80;
		break;
	default:
		LOG_WRN("Unsupported word size %u bits, falling back to 16 bits",
			cfg->i2s.word_size);
		reg |= 0x60;
		break;
	}

	return es7210_write_reg(dev, ES7210_SDP_INTERFACE1_REG11, reg);
}

static int es7210_apply_gain(const struct device *dev)
{
	struct es7210_data *data = dev_data(dev);
	uint8_t gain = MIN(data->mic_gain, ES7210_MAX_GAIN_STEP) & 0x0FU;
	uint8_t value = 0x10 | gain;
	int ret = es7210_write_reg(dev, ES7210_MIC1_GAIN_REG43, value);

	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC2_GAIN_REG44, value);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC3_GAIN_REG45, 0x00);
	if (ret < 0) {
		return ret;
	}

	return es7210_write_reg(dev, ES7210_MIC4_GAIN_REG46, 0x00);
}

static int es7210_apply_mute(const struct device *dev, bool mute)
{
	uint8_t mask = 0x03;
	uint8_t value = mute ? mask : 0x00;
	int ret = es7210_update_bits(dev, ES7210_ADC34_MUTERANGE_REG14, mask, value);

	if (ret < 0) {
		return ret;
	}

	return es7210_update_bits(dev, ES7210_ADC12_MUTERANGE_REG15, mask, value);
}

static int es7210_start_stream(const struct device *dev)
{
	struct es7210_data *data = dev_data(dev);
	int ret = es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, data->clock_off_value);

	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_POWER_DOWN_REG06, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ANALOG_REG40, 0x42);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC1_POWER_REG47, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC2_POWER_REG48, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC3_POWER_REG49, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC4_POWER_REG4A, 0x00);
	if (ret < 0) {
		return ret;
	}

	uint8_t mic12 = (data->mic_select & (ES7210_MIC1_BIT | ES7210_MIC2_BIT)) ? 0x00 : 0xFF;
	uint8_t mic34 = (data->mic_select & (ES7210_MIC3_BIT | ES7210_MIC4_BIT)) ? 0x00 : 0xFF;

	ret = es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, mic12);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, mic34);
	if (ret < 0) {
		return ret;
	}

	data->started = true;

	return 0;
}

static int es7210_stop_stream(const struct device *dev)
{
	int ret = es7210_write_reg(dev, ES7210_MIC1_POWER_REG47, 0xFF);

	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC2_POWER_REG48, 0xFF);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC3_POWER_REG49, 0xFF);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC4_POWER_REG4A, 0xFF);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, 0xFF);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, 0xFF);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ANALOG_REG40, 0xC0);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, 0x1F);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_POWER_DOWN_REG06, 0x07);
	if (ret < 0) {
		return ret;
	}

	dev_data(dev)->started = false;

	return 0;
}

static int es7210_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	const struct es7210_config *conf = dev_cfg(dev);
	struct es7210_data *data = dev_data(dev);

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		return -ENOTSUP;
	}

	if (cfg->dai_cfg.i2s.frame_clk_freq == 0U) {
		LOG_ERR("Frame clock frequency must be provided");
		return -EINVAL;
	}

	uint32_t mclk = cfg->mclk_freq ? cfg->mclk_freq : conf->mclk_freq;

	if (mclk == 0U) {
		mclk = cfg->dai_cfg.i2s.frame_clk_freq * 256U;
	}

	int ret = es7210_reset(dev);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, 0x1F);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_POWER_DOWN_REG06, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_TIME_CONTROL0_REG09, 0x30);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_TIME_CONTROL1_REG0A, 0x30);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ADC34_HPF2_REG20, 0x0A);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ADC34_HPF1_REG21, 0x2A);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ADC12_HPF2_REG22, 0x0A);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ADC12_HPF1_REG23, 0x2A);
	if (ret < 0) {
		return ret;
	}

	uint8_t mode = 0x10;
	uint32_t master_mask = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
	uint32_t opt = cfg->dai_cfg.i2s.options & master_mask;

	if (opt == master_mask) {
		mode |= 0x01;
	} else if (opt != 0U) {
		LOG_ERR("Inconsistent master configuration (options: 0x%08x)",
			cfg->dai_cfg.i2s.options);
		return -EINVAL;
	}

	ret = es7210_write_reg(dev, ES7210_MODE_CONFIG_REG08, mode);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_config_sample_rate(dev, mclk, cfg->dai_cfg.i2s.frame_clk_freq);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_config_format(dev, &cfg->dai_cfg);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_ANALOG_REG40, 0x42);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC12_BIAS_REG41, 0x70);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC34_BIAS_REG42, 0x70);
	if (ret < 0) {
		return ret;
	}

	data->mic_gain = MIN(conf->mic_gain, ES7210_MAX_GAIN_STEP);
	ret = es7210_apply_gain(dev);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC1_POWER_REG47, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC2_POWER_REG48, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC3_POWER_REG49, 0x00);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC4_POWER_REG4A, 0x00);
	if (ret < 0) {
		return ret;
	}

	data->mic_select = conf->mic_select;
	uint8_t mic12 = (data->mic_select & (ES7210_MIC1_BIT | ES7210_MIC2_BIT)) ? 0x00 : 0xFF;
	uint8_t mic34 = (data->mic_select & (ES7210_MIC3_BIT | ES7210_MIC4_BIT)) ? 0x00 : 0xFF;

	ret = es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, mic12);
	if (ret < 0) {
		return ret;
	}

	ret = es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, mic34);
	if (ret < 0) {
		return ret;
	}

	data->clock_off_value = 0x14;
	ret = es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, data->clock_off_value);
	if (ret < 0) {
		return ret;
	}

	data->muted = false;
	data->mclk = mclk;
	data->sample_rate = cfg->dai_cfg.i2s.frame_clk_freq;

	ret = es7210_apply_mute(dev, data->muted);
	if (ret < 0) {
		return ret;
	}

	return es7210_start_stream(dev);
}

static void es7210_start_output(const struct device *dev)
{
	int ret = es7210_start_stream(dev);

	if (ret < 0) {
		LOG_ERR("Failed to start ES7210: %d", ret);
	}
}

static void es7210_stop_output(const struct device *dev)
{
	int ret = es7210_stop_stream(dev);

	if (ret < 0) {
		LOG_ERR("Failed to stop ES7210: %d", ret);
	}
}

static int es7210_set_property(const struct device *dev, audio_property_t property,
			       audio_channel_t channel, audio_property_value_t val)
{
	struct es7210_data *data = dev_data(dev);

	ARG_UNUSED(channel);

	switch (property) {
	case AUDIO_PROPERTY_INPUT_VOLUME:
		if (val.vol < 0) {
			data->mic_gain = 0U;
		} else if (val.vol > ES7210_MAX_GAIN_STEP) {
			data->mic_gain = ES7210_MAX_GAIN_STEP;
		} else {
			data->mic_gain = (uint8_t)val.vol;
		}

		if (!data->started) {
			return 0;
		}

		return es7210_apply_gain(dev);

	case AUDIO_PROPERTY_INPUT_MUTE:
		data->muted = val.mute;
		if (!data->started) {
			return 0;
		}

		return es7210_apply_mute(dev, data->muted);

	default:
		return -ENOTSUP;
	}
}

static int es7210_apply_properties(const struct device *dev)
{
	int ret = es7210_apply_gain(dev);

	if (ret < 0) {
		return ret;
	}

	return es7210_apply_mute(dev, dev_data(dev)->muted);
}

static int es7210_clear_errors(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int es7210_register_error_callback(const struct device *dev, audio_codec_error_callback_t cb)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	return -ENOTSUP;
}

static int es7210_route_input(const struct device *dev, audio_channel_t channel, uint32_t input)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(input);
	return 0;
}

static const struct audio_codec_api es7210_driver_api = {
	.configure = es7210_configure,
	.start_output = es7210_start_output,
	.stop_output = es7210_stop_output,
	.set_property = es7210_set_property,
	.apply_properties = es7210_apply_properties,
	.clear_errors = es7210_clear_errors,
	.register_error_callback = es7210_register_error_callback,
	.route_input = es7210_route_input,
};

#define ES7210_INIT(inst)									   \
	static struct es7210_data es7210_data_##inst;						   \
	static const struct es7210_config es7210_config_##inst = {				   \
		.i2c = I2C_DT_SPEC_INST_GET(inst),						   \
		.mclk_freq = DT_INST_PROP_OR(inst, mclk_frequency, 0),				   \
		.mic_select =									   \
			DT_INST_PROP_OR(inst, mic_select, ES7210_MIC1_BIT | ES7210_MIC2_BIT),	   \
		.mic_gain = DT_INST_PROP_OR(inst, mic_gain, 11),				   \
	};											   \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, &es7210_data_##inst, &es7210_config_##inst,	   \
			      POST_KERNEL, CONFIG_AUDIO_CODEC_INIT_PRIORITY, &es7210_driver_api);

DT_INST_FOREACH_STATUS_OKAY(ES7210_INIT)
