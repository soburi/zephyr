/*
 * Copyright (c) 2025 The Zephyr Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT everest_es7210_dmic

#include <stdint.h>

#include <zephyr/audio/codec.h>
#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(es7210_dmic, CONFIG_AUDIO_DMIC_LOG_LEVEL);

struct es7210_dmic_config {
	const struct device *i2s_dev;
	const struct device *codec_dev;
	i2s_opt_t i2s_options;
	uint8_t data_format;
	bool bit_clk_inverted;
	int32_t timeout_ms;
};

struct es7210_dmic_data {
	enum dmic_state state;
	struct k_mem_slab *mem_slab;
	size_t block_size;
	uint32_t pcm_rate;
	uint8_t pcm_width;
	uint8_t num_channels;
};

static i2s_fmt_t es7210_dmic_format_from_idx(uint8_t idx)
{
	switch (idx) {
	case 1:
		return I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED;
	case 2:
		return I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED;
	case 0:
	default:
		return I2S_FMT_DATA_FORMAT_I2S;
	}
}

static int es7210_dmic_configure(const struct device *dev, struct dmic_cfg *cfg)
{
	const struct es7210_dmic_config *conf = dev->config;
	struct es7210_dmic_data *data = dev->data;
	struct pcm_stream_cfg *stream = &cfg->streams[0];
	i2s_fmt_t format;
	struct i2s_config i2s_cfg;
	struct audio_codec_cfg codec_cfg = {0};
	int ret;

	if (cfg->channel.req_num_streams == 0U) {
		return -EINVAL;
	}

	if (cfg->channel.req_num_streams > 1U) {
		return -ENOTSUP;
	}

	if (stream->pcm_rate == 0U || stream->pcm_width == 0U || stream->block_size == 0U ||
	    stream->mem_slab == NULL) {
		return -EINVAL;
	}

	data->mem_slab = stream->mem_slab;
	data->block_size = stream->block_size;
	data->pcm_rate = stream->pcm_rate;
	data->pcm_width = stream->pcm_width;
	data->num_channels = cfg->channel.req_num_chan;

	format = es7210_dmic_format_from_idx(conf->data_format);
	if (conf->bit_clk_inverted) {
		format |= I2S_FMT_BIT_CLK_INV;
	}

	codec_cfg.dai_route = AUDIO_ROUTE_CAPTURE;
	codec_cfg.dai_type = AUDIO_DAI_TYPE_I2S;
	codec_cfg.dai_cfg.i2s.word_size = stream->pcm_width;
	codec_cfg.dai_cfg.i2s.channels = cfg->channel.req_num_chan;
	codec_cfg.dai_cfg.i2s.format = format;
	codec_cfg.dai_cfg.i2s.options = conf->i2s_options;
	codec_cfg.dai_cfg.i2s.frame_clk_freq = stream->pcm_rate;
	codec_cfg.dai_cfg.i2s.mem_slab = stream->mem_slab;
	codec_cfg.dai_cfg.i2s.block_size = stream->block_size;
	codec_cfg.dai_cfg.i2s.timeout = conf->timeout_ms;

	ret = audio_codec_configure(conf->codec_dev, &codec_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure ES7210 codec (%d)", ret);
		return ret;
	}

	audio_codec_stop_output(conf->codec_dev);

	i2s_cfg.word_size = stream->pcm_width;
	i2s_cfg.channels = cfg->channel.req_num_chan;
	i2s_cfg.format = format;
	i2s_cfg.options = conf->i2s_options;
	i2s_cfg.frame_clk_freq = stream->pcm_rate;
	i2s_cfg.mem_slab = stream->mem_slab;
	i2s_cfg.block_size = stream->block_size;
	i2s_cfg.timeout = conf->timeout_ms;

	ret = i2s_configure(conf->i2s_dev, I2S_DIR_RX, &i2s_cfg);
	if (ret < 0) {
		LOG_ERR("Failed to configure I2S RX (%d)", ret);
		return ret;
	}

	cfg->channel.act_num_chan = cfg->channel.req_num_chan;
	cfg->channel.act_chan_map_lo = cfg->channel.req_chan_map_lo;
	cfg->channel.act_chan_map_hi = cfg->channel.req_chan_map_hi;
	cfg->channel.act_num_streams = 1U;

	data->state = DMIC_STATE_CONFIGURED;

	return 0;
}

static int es7210_dmic_trigger(const struct device *dev, enum dmic_trigger cmd)
{
	const struct es7210_dmic_config *conf = dev->config;
	struct es7210_dmic_data *data = dev->data;
	int ret = 0;

	switch (cmd) {
	case DMIC_TRIGGER_START:
		if (data->state != DMIC_STATE_CONFIGURED && data->state != DMIC_STATE_PAUSED) {
			return -EIO;
		}

		audio_codec_start_output(conf->codec_dev);
		ret = i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
		if (ret < 0) {
			audio_codec_stop_output(conf->codec_dev);
			return ret;
		}

		data->state = DMIC_STATE_ACTIVE;
		break;

	case DMIC_TRIGGER_STOP:
		if (data->state != DMIC_STATE_ACTIVE && data->state != DMIC_STATE_PAUSED) {
			return 0;
		}

		ret = i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_STOP);
		if (ret < 0) {
			return ret;
		}

		audio_codec_stop_output(conf->codec_dev);
		data->state = DMIC_STATE_CONFIGURED;
		break;

	case DMIC_TRIGGER_PAUSE:
		if (data->state != DMIC_STATE_ACTIVE) {
			return 0;
		}

		ret = i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_STOP);
		if (ret < 0) {
			return ret;
		}

		audio_codec_stop_output(conf->codec_dev);
		data->state = DMIC_STATE_PAUSED;
		break;

	case DMIC_TRIGGER_RELEASE:
		if (data->state != DMIC_STATE_PAUSED) {
			return -EIO;
		}

		audio_codec_start_output(conf->codec_dev);
		ret = i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
		if (ret < 0) {
			audio_codec_stop_output(conf->codec_dev);
			return ret;
		}

		data->state = DMIC_STATE_ACTIVE;
		break;

	case DMIC_TRIGGER_RESET:
		i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
		audio_codec_stop_output(conf->codec_dev);
		data->state = DMIC_STATE_CONFIGURED;
		break;

	default:
		return -EINVAL;
	}

	return ret;
}

static int es7210_dmic_read(const struct device *dev, uint8_t stream, void **buffer, size_t *size,
			    int32_t timeout)
{
	const struct es7210_dmic_config *conf = dev->config;
	struct es7210_dmic_data *data = dev->data;
	int ret;
	size_t block_size;

	ARG_UNUSED(stream);

	if (data->state != DMIC_STATE_ACTIVE && data->state != DMIC_STATE_PAUSED) {
		return -EIO;
	}

	if ((timeout != SYS_FOREVER_MS) && (timeout != conf->timeout_ms)) {
		LOG_DBG("Unsupported timeout %d ms", timeout);
	}

	ret = i2s_read(conf->i2s_dev, buffer, &block_size);
	if (ret < 0) {
		return ret;
	}

	if (size != NULL) {
		*size = block_size;
	}

	return 0;
}

static const struct _dmic_ops es7210_dmic_api = {
	.configure = es7210_dmic_configure,
	.trigger = es7210_dmic_trigger,
	.read = es7210_dmic_read,
};

static int es7210_dmic_init(const struct device *dev)
{
	const struct es7210_dmic_config *conf = dev->config;
	struct es7210_dmic_data *data = dev->data;

	if (!device_is_ready(conf->i2s_dev)) {
		LOG_ERR("I2S device is not ready");
		return -ENODEV;
	}

	if (!device_is_ready(conf->codec_dev)) {
		LOG_ERR("ES7210 codec device is not ready");
		return -ENODEV;
	}

	data->state = DMIC_STATE_INITIALIZED;

	return 0;
}

#define ES7210_DMIC_OPTIONS(inst)                                                                  \
	((DT_INST_PROP(inst, bit_clock_master) ? I2S_OPT_BIT_CLK_SLAVE : I2S_OPT_BIT_CLK_MASTER) | \
	 (DT_INST_PROP(inst, frame_clock_master) ? I2S_OPT_FRAME_CLK_SLAVE                         \
						 : I2S_OPT_FRAME_CLK_MASTER))

#define ES7210_DMIC_INIT(inst)                                                                     \
	static struct es7210_dmic_data es7210_dmic_data_##inst;                                    \
	static const struct es7210_dmic_config es7210_dmic_config_##inst = {                       \
		.i2s_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                       \
		.codec_dev = DEVICE_DT_GET(DT_INST_PHANDLE(inst, codec)),                          \
		.i2s_options = ES7210_DMIC_OPTIONS(inst),                                          \
		.data_format = DT_INST_ENUM_IDX_OR(inst, data_format, 0),                          \
		.bit_clk_inverted = DT_INST_PROP_OR(inst, bit_clock_inverted, false),              \
		.timeout_ms =                                                                      \
			DT_INST_PROP_OR(inst, timeout_ms, CONFIG_ES7210_DMIC_I2S_TIMEOUT_MS),      \
	};                                                                                         \
	DEVICE_DT_INST_DEFINE(inst, es7210_dmic_init, NULL, &es7210_dmic_data_##inst,              \
			      &es7210_dmic_config_##inst, POST_KERNEL,                             \
			      CONFIG_AUDIO_DMIC_INIT_PRIORITY, &es7210_dmic_api);

DT_INST_FOREACH_STATUS_OKAY(ES7210_DMIC_INIT)
