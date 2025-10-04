/*
 * Copyright (c) 2025 The Zephyr Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT everest_es7210_dmic

#include <stdint.h>

#include <zephyr/audio/dmic.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include "es7210.h"

LOG_MODULE_REGISTER(es7210_dmic, CONFIG_AUDIO_DMIC_LOG_LEVEL);

struct es7210_dmic_codec_config {
        struct i2c_dt_spec i2c;
        uint32_t mclk_freq;
        uint8_t mic_select;
        uint8_t mic_gain;
};

struct es7210_dmic_codec_data {
        uint8_t clock_off_value;
        uint8_t mic_select;
        uint8_t mic_gain;
        uint32_t mclk;
        uint32_t sample_rate;
        bool muted;
        bool started;
};

struct es7210_dmic_config {
        const struct device *i2s_dev;
        i2s_opt_t i2s_options;
        uint8_t data_format;
        bool bit_clk_inverted;
        int32_t timeout_ms;
        struct es7210_dmic_codec_config codec;
};

struct es7210_dmic_data {
        enum dmic_state state;
        struct k_mem_slab *mem_slab;
        size_t block_size;
        uint32_t pcm_rate;
        uint8_t pcm_width;
        uint8_t num_channels;
        struct es7210_dmic_codec_data codec;
};

static inline const struct es7210_dmic_config *dev_cfg(const struct device *dev)
{
        return dev->config;
}

static inline struct es7210_dmic_data *dev_data(const struct device *dev)
{
        return dev->data;
}

static int es7210_write_reg(const struct device *dev, uint8_t reg, uint8_t val)
{
        const struct es7210_dmic_config *conf = dev_cfg(dev);
        int ret = i2c_reg_write_byte_dt(&conf->codec.i2c, reg, val);

        if (ret < 0) {
                LOG_ERR("Failed to write reg 0x%02x", reg);
        }

        return ret;
}

static int es7210_read_reg(const struct device *dev, uint8_t reg, uint8_t *val)
{
        const struct es7210_dmic_config *conf = dev_cfg(dev);
        int ret = i2c_reg_read_byte_dt(&conf->codec.i2c, reg, val);

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

static int es7210_config_format(const struct device *dev, i2s_fmt_t format, uint8_t word_size)
{
        uint8_t reg;
        int ret = es7210_read_reg(dev, ES7210_SDP_INTERFACE1_REG11, &reg);

        if (ret < 0) {
                return ret;
        }

        reg &= ~0x03U;

        switch (format & I2S_FMT_DATA_FORMAT_MASK) {
        case I2S_FMT_DATA_FORMAT_I2S:
                break;
        case I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED:
        case I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED:
                reg |= 0x01;
                break;
        default:
                LOG_ERR("Unsupported I2S data format 0x%08x", format);
                return -ENOTSUP;
        }

        reg &= ~0xE0U;

        switch (word_size) {
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
                LOG_WRN("Unsupported word size %u bits, falling back to 16 bits", word_size);
                reg |= 0x60;
                break;
        }

        return es7210_write_reg(dev, ES7210_SDP_INTERFACE1_REG11, reg);
}

static int es7210_apply_gain(const struct device *dev)
{
        struct es7210_dmic_data *data = dev_data(dev);
        uint8_t gain = MIN(data->codec.mic_gain, ES7210_MAX_GAIN_STEP) & 0x0FU;
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
        struct es7210_dmic_data *data = dev_data(dev);
        int ret = es7210_write_reg(dev, ES7210_CLOCK_OFF_REG01, data->codec.clock_off_value);

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

        uint8_t mic12 = (data->codec.mic_select & (ES7210_MIC1_BIT | ES7210_MIC2_BIT)) ? 0x00 : 0xFF;
        uint8_t mic34 = (data->codec.mic_select & (ES7210_MIC3_BIT | ES7210_MIC4_BIT)) ? 0x00 : 0xFF;

        ret = es7210_write_reg(dev, ES7210_MIC12_POWER_REG4B, mic12);
        if (ret < 0) {
                return ret;
        }

        ret = es7210_write_reg(dev, ES7210_MIC34_POWER_REG4C, mic34);
        if (ret < 0) {
                return ret;
        }

        data->codec.started = true;

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

        dev_data(dev)->codec.started = false;

        return 0;
}

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
        const struct es7210_dmic_config *conf = dev_cfg(dev);
        struct es7210_dmic_data *data = dev_data(dev);
        struct pcm_stream_cfg *stream = &cfg->streams[0];
        i2s_fmt_t format;
        struct i2s_config i2s_cfg;
        uint32_t mclk;
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

        mclk = conf->codec.mclk_freq;
        if (mclk == 0U) {
                mclk = stream->pcm_rate * 256U;
        }

        data->codec.mclk = mclk;
        data->codec.sample_rate = stream->pcm_rate;
        data->codec.mic_select = conf->codec.mic_select;
        data->codec.mic_gain = conf->codec.mic_gain;
        data->codec.clock_off_value = 0x14;
        data->codec.muted = false;

        ret = es7210_reset(dev);
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
        uint32_t opt = conf->i2s_options & master_mask;

        if (opt == master_mask) {
                mode |= 0x01;
        } else if (opt != 0U) {
                LOG_ERR("Inconsistent master configuration (options: 0x%08x)", conf->i2s_options);
                return -EINVAL;
        }

        ret = es7210_write_reg(dev, ES7210_MODE_CONFIG_REG08, mode);
        if (ret < 0) {
                return ret;
        }

        ret = es7210_config_sample_rate(dev, data->codec.mclk, stream->pcm_rate);
        if (ret < 0) {
                return ret;
        }

        ret = es7210_config_format(dev, format, stream->pcm_width);
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

        ret = es7210_apply_gain(dev);
        if (ret < 0) {
                return ret;
        }

        ret = es7210_apply_mute(dev, data->codec.muted);
        if (ret < 0) {
                return ret;
        }

        ret = es7210_stop_stream(dev);
        if (ret < 0) {
                return ret;
        }

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
        const struct es7210_dmic_config *conf = dev_cfg(dev);
        struct es7210_dmic_data *data = dev_data(dev);
        int ret = 0;

        switch (cmd) {
        case DMIC_TRIGGER_START:
                if (data->state != DMIC_STATE_CONFIGURED && data->state != DMIC_STATE_PAUSED) {
                        return -EIO;
                }

                ret = es7210_start_stream(dev);
                if (ret < 0) {
                        return ret;
                }

                ret = i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
                if (ret < 0) {
                        es7210_stop_stream(dev);
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

                ret = es7210_stop_stream(dev);
                if (ret < 0) {
                        return ret;
                }
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

                ret = es7210_stop_stream(dev);
                if (ret < 0) {
                        return ret;
                }
                data->state = DMIC_STATE_PAUSED;
                break;

        case DMIC_TRIGGER_RELEASE:
                if (data->state != DMIC_STATE_PAUSED) {
                        return -EIO;
                }

                ret = es7210_start_stream(dev);
                if (ret < 0) {
                        return ret;
                }

                ret = i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_START);
                if (ret < 0) {
                        es7210_stop_stream(dev);
                        return ret;
                }

                data->state = DMIC_STATE_ACTIVE;
                break;

        case DMIC_TRIGGER_RESET:
                i2s_trigger(conf->i2s_dev, I2S_DIR_RX, I2S_TRIGGER_DROP);
                es7210_stop_stream(dev);
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
        const struct es7210_dmic_config *conf = dev_cfg(dev);
        struct es7210_dmic_data *data = dev_data(dev);
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
        const struct es7210_dmic_config *conf = dev_cfg(dev);
        struct es7210_dmic_data *data = dev_data(dev);

        if (!device_is_ready(conf->i2s_dev)) {
                LOG_ERR("I2S device is not ready");
                return -ENODEV;
        }

        if (!device_is_ready(conf->codec.i2c.bus)) {
                LOG_ERR("ES7210 control bus is not ready");
                return -ENODEV;
        }

	data->state = DMIC_STATE_INITIALIZED;

	return 0;
}

#define ES7210_DMIC_OPTIONS(inst)                                                                  \
	((DT_INST_PROP(inst, bit_clock_master) ? I2S_OPT_BIT_CLK_SLAVE : I2S_OPT_BIT_CLK_MASTER) | \
	 (DT_INST_PROP(inst, frame_clock_master) ? I2S_OPT_FRAME_CLK_SLAVE                         \
						 : I2S_OPT_FRAME_CLK_MASTER))

#define ES7210_DMIC_CODEC_NODE(inst) DT_INST_PHANDLE(inst, codec)                                  \

#define ES7210_DMIC_INIT(inst)                                                                     \
        static struct es7210_dmic_data es7210_dmic_data_##inst;                                    \
        static const struct es7210_dmic_config es7210_dmic_config_##inst = {                       \
                .i2s_dev = DEVICE_DT_GET(DT_INST_BUS(inst)),                                       \
                .i2s_options = ES7210_DMIC_OPTIONS(inst),                                          \
                .data_format = DT_INST_ENUM_IDX_OR(inst, data_format, 0),                          \
                .bit_clk_inverted = DT_INST_PROP_OR(inst, bit_clock_inverted, false),              \
                .timeout_ms =                                                                      \
                        DT_INST_PROP_OR(inst, timeout_ms, CONFIG_ES7210_DMIC_I2S_TIMEOUT_MS),      \
                .codec = {                                                                         \
                        .i2c = I2C_DT_SPEC_GET(ES7210_DMIC_CODEC_NODE(inst)),                      \
                        .mclk_freq =                                                               \
                                DT_PROP_OR(ES7210_DMIC_CODEC_NODE(inst), mclk_frequency, 0),      \
                        .mic_select =                                                              \
                                DT_PROP_OR(ES7210_DMIC_CODEC_NODE(inst),                          \
                                            mic_select, ES7210_MIC1_BIT | ES7210_MIC2_BIT),        \
                        .mic_gain = DT_PROP_OR(ES7210_DMIC_CODEC_NODE(inst), mic_gain, 11),       \
                },                                                                                \
        };                                                                                         \
        DEVICE_DT_INST_DEFINE(inst, es7210_dmic_init, NULL, &es7210_dmic_data_##inst,              \
                              &es7210_dmic_config_##inst, POST_KERNEL,                             \
                              CONFIG_AUDIO_DMIC_INIT_PRIORITY, &es7210_dmic_api);

DT_INST_FOREACH_STATUS_OKAY(ES7210_DMIC_INIT)
