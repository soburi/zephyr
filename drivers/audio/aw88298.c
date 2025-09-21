/*
 * Copyright (c) 2025 The Zephyr Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT awinic_aw88298

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/audio/codec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define LOG_LEVEL CONFIG_AUDIO_CODEC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aw88298);

#define AW88298_REG_SYSCTRL   0x04
#define AW88298_REG_SYSCTRL2  0x05
#define AW88298_REG_I2SCTRL   0x06
#define AW88298_REG_DSPCFG    0x0C
#define AW88298_REG_BOOSTCTRL 0x61

#define AW88298_SYSCTRL_ENABLE   0x4040
#define AW88298_SYSCTRL_DISABLE  0x4000
#define AW88298_SYSCTRL2_DEFAULT 0x0008
#define AW88298_BOOSTCTRL_OFF    0x0673

#define AW88298_RATE_TABLE_COUNT ARRAY_SIZE(rate_table)

#define AW88298_VOLUME_MAX     0x00FF
#define AW88298_DEFAULT_VOLUME 0x0064

struct aw88298_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec enable_gpio;
	uint16_t default_volume;
	bool has_enable_gpio;
};

struct aw88298_data {
	struct k_mutex lock;
	uint32_t sample_rate;
	uint16_t volume;
	bool mute;
	bool started;
	bool configured;
	bool dirty;
};

static const uint8_t rate_table[] = {4, 5, 6, 8, 10, 11, 15, 20, 22, 44};

static int aw88298_write_reg(const struct device *dev, uint8_t reg, uint16_t value)
{
	const struct aw88298_config *cfg = dev->config;
	struct aw88298_data *data = dev->data;
	uint8_t tx[3];
	int ret;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	tx[0] = reg;
	sys_put_be16(value, &tx[1]);

	k_mutex_lock(&data->lock, K_FOREVER);
	ret = i2c_write_dt(&cfg->bus, tx, sizeof(tx));
	k_mutex_unlock(&data->lock);

	if (ret < 0) {
		LOG_ERR("Failed to write reg 0x%02x (%d)", reg, ret);
	}

	return ret;
}

static uint16_t aw88298_calculate_rate(uint32_t sample_rate)
{
	uint32_t rate = (sample_rate + 1102U) / 2205U;
	uint16_t idx = 0U;

	while ((idx < AW88298_RATE_TABLE_COUNT) && (rate > rate_table[idx])) {
		idx++;
	}

	if (idx >= AW88298_RATE_TABLE_COUNT) {
		idx = AW88298_RATE_TABLE_COUNT - 1U;
	}

	return 0x14C0U | idx;
}

static int aw88298_gpio_enable(const struct device *dev, bool enable)
{
	const struct aw88298_config *cfg = dev->config;

	if (!cfg->has_enable_gpio) {
		return 0;
	}

	return gpio_pin_set_dt(&cfg->enable_gpio, enable);
}

static int aw88298_program(const struct device *dev)
{
	struct aw88298_data *data = dev->data;
	uint16_t gain = data->mute ? 0U : data->volume;
	uint16_t reg06 = aw88298_calculate_rate(data->sample_rate);
	int ret;

	ret = aw88298_write_reg(dev, AW88298_REG_BOOSTCTRL, AW88298_BOOSTCTRL_OFF);
	if (ret < 0) {
		return ret;
	}

	ret = aw88298_write_reg(dev, AW88298_REG_SYSCTRL, AW88298_SYSCTRL_ENABLE);
	if (ret < 0) {
		return ret;
	}

	ret = aw88298_write_reg(dev, AW88298_REG_SYSCTRL2, AW88298_SYSCTRL2_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	ret = aw88298_write_reg(dev, AW88298_REG_I2SCTRL, reg06);
	if (ret < 0) {
		return ret;
	}

	ret = aw88298_write_reg(dev, AW88298_REG_DSPCFG, gain);
	if (ret < 0) {
		return ret;
	}

	data->dirty = false;

	return 0;
}

static int aw88298_stop_hw(const struct device *dev, bool disable_gpio)
{
	int ret = aw88298_write_reg(dev, AW88298_REG_SYSCTRL, AW88298_SYSCTRL_DISABLE);

	if (ret < 0) {
		return ret;
	}

	if (disable_gpio) {
		return aw88298_gpio_enable(dev, false);
	}

	return 0;
}

static int aw88298_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
	struct aw88298_data *data = dev->data;

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		LOG_ERR("Unsupported DAI type %d", cfg->dai_type);
		return -EINVAL;
	}

	if (cfg->dai_cfg.i2s.word_size != 16U) {
		LOG_ERR("Unsupported word size %u", cfg->dai_cfg.i2s.word_size);
		return -EINVAL;
	}

	data->sample_rate = cfg->dai_cfg.i2s.frame_clk_freq;
	data->configured = true;
	data->dirty = true;

	return 0;
}

static void aw88298_start_output(const struct device *dev)
{
	struct aw88298_data *data = dev->data;

	if (!data->configured) {
		LOG_WRN("Codec not configured");
		return;
	}

	if (aw88298_gpio_enable(dev, true) < 0) {
		LOG_ERR("Failed to enable speaker GPIO");
		return;
	}

	if (aw88298_program(dev) < 0) {
		LOG_ERR("Failed to initialize AW88298");
		return;
	}

	data->started = true;
}

static void aw88298_stop_output(const struct device *dev)
{
	struct aw88298_data *data = dev->data;

	if (!data->started) {
		return;
	}

	if (aw88298_stop_hw(dev, true) < 0) {
		LOG_ERR("Failed to stop AW88298");
	}

	data->started = false;
}

static int aw88298_set_property(const struct device *dev, audio_property_t property,
				audio_channel_t channel, audio_property_value_t val)
{
	struct aw88298_data *data = dev->data;

	if (channel != AUDIO_CHANNEL_ALL && channel != AUDIO_CHANNEL_FRONT_LEFT &&
	    channel != AUDIO_CHANNEL_FRONT_RIGHT) {
		return -EINVAL;
	}

	switch (property) {
	case AUDIO_PROPERTY_OUTPUT_VOLUME:
		if (val.vol < 0 || val.vol > AW88298_VOLUME_MAX) {
			return -EINVAL;
		}

		data->volume = (uint16_t)val.vol;
		data->dirty = true;
		break;
	case AUDIO_PROPERTY_OUTPUT_MUTE:
		data->mute = val.mute;
		data->dirty = true;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int aw88298_apply_properties(const struct device *dev)
{
	struct aw88298_data *data = dev->data;

	if (!data->started || !data->dirty) {
		return 0;
	}

	return aw88298_program(dev);
}

static int aw88298_clear_errors(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int aw88298_register_error_callback(const struct device *dev,
					   audio_codec_error_callback_t cb)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(cb);
	return -ENOTSUP;
}

static int aw88298_route_input(const struct device *dev, audio_channel_t channel, uint32_t input)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(input);
	return -ENOTSUP;
}

static int aw88298_route_output(const struct device *dev, audio_channel_t channel, uint32_t output)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(channel);
	ARG_UNUSED(output);
	return -ENOTSUP;
}

static const struct audio_codec_api aw88298_api = {
	.configure = aw88298_configure,
	.start_output = aw88298_start_output,
	.stop_output = aw88298_stop_output,
	.set_property = aw88298_set_property,
	.apply_properties = aw88298_apply_properties,
	.clear_errors = aw88298_clear_errors,
	.register_error_callback = aw88298_register_error_callback,
	.route_input = aw88298_route_input,
	.route_output = aw88298_route_output,
};

static int aw88298_init(const struct device *dev)
{
	const struct aw88298_config *cfg = dev->config;
	struct aw88298_data *data = dev->data;
	int ret;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C controller not ready");
		return -ENODEV;
	}

	k_mutex_init(&data->lock);
	data->volume = cfg->default_volume;
	data->mute = false;
	data->started = false;
	data->configured = false;
	data->dirty = true;

	if (cfg->has_enable_gpio) {
		if (!device_is_ready(cfg->enable_gpio.port)) {
			LOG_ERR("Enable GPIO device not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->enable_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure enable GPIO (%d)", ret);
			return ret;
		}
	}

	return 0;
}

#define AW88298_INST(idx)                                                                          \
	static const struct aw88298_config aw88298_config_##idx = {                                \
		.bus = I2C_DT_SPEC_INST_GET(idx),                                                  \
		.enable_gpio = GPIO_DT_SPEC_INST_GET_OR(idx, enable_gpios, {0}),                   \
		.default_volume =                                                                  \
			DT_INST_PROP_OR(idx, awinic_default_volume, AW88298_DEFAULT_VOLUME),       \
		.has_enable_gpio = DT_INST_NODE_HAS_PROP(idx, enable_gpios),                       \
	};                                                                                         \
	static struct aw88298_data aw88298_data_##idx;                                             \
	DEVICE_DT_INST_DEFINE(idx, aw88298_init, NULL, &aw88298_data_##idx, &aw88298_config_##idx, \
			      POST_KERNEL, CONFIG_AUDIO_CODEC_INIT_PRIORITY, &aw88298_api)

DT_INST_FOREACH_STATUS_OKAY(AW88298_INST)
