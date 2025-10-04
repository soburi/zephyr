/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT awinic_aw88298

#include <zephyr/audio/codec.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>

#define LOG_LEVEL CONFIG_AUDIO_CODEC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(aw88298);

#define AW88298_REG_ID       0x00
#define AW88298_REG_SYSST    0x01
#define AW88298_REG_SYSINT   0x02
#define AW88298_REG_SYSINTM  0x03
#define AW88298_REG_SYSCTRL  0x04
#define AW88298_REG_SYSCTRL2 0x05
#define AW88298_REG_I2SCTRL  0x06
#define AW88298_REG_I2SCFG1  0x07
#define AW88298_REG_HAGCCFG1 0x09
#define AW88298_REG_HAGCCFG2 0x0A
#define AW88298_REG_HAGCCFG3 0x0B
#define AW88298_REG_HAGCCFG4 0x0C

#define AW88298_REG_SYSCTRL_PWDN        BIT(0)
#define AW88298_REG_SYSCTRL_AMPPD       BIT(1)
#define AW88298_REG_SYSCTRL_I2SEN       BIT(6)
#define AW88298_REG_SYSCTRL2_HMUTE      BIT(4)
#define AW88298_REG_I2SCTRL_BCLK_POL    BIT(14)
#define AW88298_REG_I2SCTRL_LRCLK_POL   BIT(13)
#define AW88298_REG_I2SCTRL_FRAME_FLAGS BIT(12)
#define AW88298_REG_I2SCTRL_CHSEL       BIT(11)
#define AW88298_REG_I2SCTRL_I2SMD       (BIT_MASK(3) << 8)
#define AW88298_REG_I2SCTRL_I2SFS       (BIT_MASK(2) << 6)
#define AW88298_REG_I2SCTRL_I2SBCK      (BIT_MASK(2) << 4)
#define AW88298_REG_I2SCTRL_I2SSR       BIT_MASK(4)
#define AW88298_REG_I2SCFG1_RXSEL       (BIT_MASK(4) << 8)
#define AW88298_REG_I2SCFG1_RXEN        BIT_MASK(4)
#define AW88298_REG_HAGCCFG4_VOL        (BIT_MASK(8) << 8)

#define AW88298_I2SCTRL_CHSEL_MASTER 0x0U
#define AW88298_I2SCTRL_CHSEL_SLAVE  AW88298_REG_I2SCTRL_CHSEL
#define AW88298_I2SCTRL_MODE_SLAVE   AW88298_I2SCTRL_CHSEL_SLAVE

#define AW88298_REG_I2SCTRL_I2S_CFG_MASK                                                           \
        (AW88298_REG_I2SCTRL_BCLK_POL | AW88298_REG_I2SCTRL_LRCLK_POL | AW88298_REG_I2SCTRL_CHSEL | \
         AW88298_REG_I2SCTRL_FRAME_FLAGS | AW88298_REG_I2SCTRL_I2SMD | AW88298_REG_I2SCTRL_I2SFS | \
         AW88298_REG_I2SCTRL_I2SBCK | AW88298_REG_I2SCTRL_I2SSR)

#define AW88298_I2SCTRL_I2SMD_VAL(val)   (((uint16_t)(val) << 8) & AW88298_REG_I2SCTRL_I2SMD)
#define AW88298_I2SCTRL_I2SFS_VAL(val)   (((uint16_t)(val) << 6) & AW88298_REG_I2SCTRL_I2SFS)
#define AW88298_I2SCTRL_I2SBCK_VAL(val)  (((uint16_t)(val) << 4) & AW88298_REG_I2SCTRL_I2SBCK)
#define AW88298_I2SCTRL_I2SSR_VAL(val)   ((uint16_t)(val) & AW88298_REG_I2SCTRL_I2SSR)
#define AW88298_I2SCFG1_RXSEL_VAL(val)   (((uint16_t)(val) << 8) & AW88298_REG_I2SCFG1_RXSEL)
#define AW88298_I2SCFG1_RXEN_VAL(val)    ((uint16_t)(val) & AW88298_REG_I2SCFG1_RXEN)
#define AW88298_HAGCCFG4_VOL_VAL(val)    (((uint16_t)(val) << 8) & AW88298_REG_HAGCCFG4_VOL)

#define AW88298_RESET_DELAY_MS 50

#define AW88298_VOLUME_MAX 0x00FF

enum {
	AW88298_I2SCTRL_MODE_I2S = 4,
	AW88298_I2SCTRL_MODE_LEFT_JUSTIFIED,
	AW88298_I2SCTRL_MODE_RIGHT_JUSTIFIED,
};

enum {
	AW88298_I2SCTRL_FS_32BIT,
	AW88298_I2SCTRL_FS_24BIT,
	AW88298_I2SCTRL_FS_20BIT,
	AW88298_I2SCTRL_FS_16BIT,
};

enum {
	AW88298_I2SCTRL_BCK_16BIT,
	AW88298_I2SCTRL_BCK_20BIT,
	AW88298_I2SCTRL_BCK_24BIT,
	AW88298_I2SCTRL_BCK_32BIT,
};

struct aw88298_config {
	struct i2c_dt_spec bus;
	struct gpio_dt_spec reset_gpio;
};

struct aw88298_data {
	struct k_mutex lock;
	uint8_t volume;
	bool mute;
};

static int aw88298_update_reg(const struct device *dev, uint8_t reg, uint16_t mask, uint16_t value)
{
	const struct aw88298_config *cfg = dev->config;
	struct aw88298_data *data = dev->data;
	uint8_t buf[3] = {reg};
	int ret;
	uint16_t regval;

	if (!device_is_ready(cfg->bus.bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	k_mutex_lock(&data->lock, K_FOREVER);

	ret = i2c_write_read_dt(&cfg->bus, &reg, 1, &buf[1], 2);
	if (ret < 0) {
		LOG_ERR("Failed to write_read reg 0x%02x (%d)", reg, ret);
		goto end;
	}

	regval = sys_get_be16(&buf[1]);
	regval = (regval & ~mask) | (mask & value);
	sys_put_be16(regval, &buf[1]);

	ret = i2c_write_dt(&cfg->bus, buf, ARRAY_SIZE(buf));
	if (ret < 0) {
		LOG_ERR("Failed to write reg 0x%02x (%d)", reg, ret);
	}

end:
	k_mutex_unlock(&data->lock);

	return ret;
}

static int aw88298_get_sample_rate_code(uint32_t sample_rate, uint16_t *code)
{
	switch (sample_rate) {
	case AUDIO_PCM_RATE_8K:
		*code = 0x0U;
		break;
	case AUDIO_PCM_RATE_11P025K:
		*code = 0x1U;
		break;
	case AUDIO_PCM_RATE_16K:
		*code = 0x3U;
		break;
	case AUDIO_PCM_RATE_22P05K:
		*code = 0x4U;
		break;
	case AUDIO_PCM_RATE_24K:
		*code = 0x5U;
		break;
	case AUDIO_PCM_RATE_32K:
		*code = 0x6U;
		break;
	case AUDIO_PCM_RATE_44P1K:
		*code = 0x7U;
		break;
	case AUDIO_PCM_RATE_48K:
		*code = 0x8U;
		break;
	case AUDIO_PCM_RATE_96K:
		*code = 0x9U;
		break;
	case AUDIO_PCM_RATE_192K:
		*code = 0xAU;
		break;
	default:
		LOG_ERR("Unsupported sample rate %u", sample_rate);
		return -ENOTSUP;
	}

	return 0;
}

static int aw88298_get_word_size_codes(audio_pcm_width_t width, uint16_t *fs_code,
				       uint16_t *bck_code)
{
	switch (width) {
	case AUDIO_PCM_WIDTH_16_BITS:
		*fs_code = AW88298_I2SCTRL_FS_16BIT;
		*bck_code = AW88298_I2SCTRL_BCK_16BIT;
		break;
	case AUDIO_PCM_WIDTH_20_BITS:
		*fs_code = AW88298_I2SCTRL_FS_20BIT;
		*bck_code = AW88298_I2SCTRL_BCK_20BIT;
		break;
	case AUDIO_PCM_WIDTH_24_BITS:
		*fs_code = AW88298_I2SCTRL_FS_24BIT;
		*bck_code = AW88298_I2SCTRL_BCK_24BIT;
		break;
	case AUDIO_PCM_WIDTH_32_BITS:
		*fs_code = AW88298_I2SCTRL_FS_32BIT;
		*bck_code = AW88298_I2SCTRL_BCK_32BIT;
		break;
	default:
		LOG_ERR("Unsupported word size %u", width);
		return -ENOTSUP;
	}

	return 0;
}

static int aw88298_get_i2s_mode_code(audio_dai_type_t type, i2s_fmt_t format, uint16_t *code)
{
	i2s_fmt_t fmt = format & I2S_FMT_DATA_FORMAT_MASK;

	switch (type) {
	case AUDIO_DAI_TYPE_I2S:
		if (fmt != I2S_FMT_DATA_FORMAT_I2S) {
			LOG_ERR("I2S DAI requires standard I2S format, got 0x%x", fmt);
			return -ENOTSUP;
		}

		*code = AW88298_I2SCTRL_MODE_I2S;
		return 0;
	case AUDIO_DAI_TYPE_LEFT_JUSTIFIED:
		if (fmt != I2S_FMT_DATA_FORMAT_LEFT_JUSTIFIED) {
			LOG_ERR("Left-justified DAI requires matching data format, got 0x%x", fmt);
			return -ENOTSUP;
		}

		*code = AW88298_I2SCTRL_MODE_LEFT_JUSTIFIED;
		return 0;
	case AUDIO_DAI_TYPE_RIGHT_JUSTIFIED:
		if (fmt != I2S_FMT_DATA_FORMAT_RIGHT_JUSTIFIED) {
			LOG_ERR("Right-justified DAI requires matching data format, got 0x%x", fmt);
			return -ENOTSUP;
		}

		*code = AW88298_I2SCTRL_MODE_RIGHT_JUSTIFIED;
		return 0;
	default:
		LOG_ERR("Unsupported DAI type %d", type);
		return -ENOTSUP;
	}
}

static int aw88298_get_sysctrl_cfg(uint16_t *mask, uint16_t *value)
{
        *mask = AW88298_REG_SYSCTRL_I2SEN | AW88298_REG_SYSCTRL_PWDN;
        *value = AW88298_REG_SYSCTRL_I2SEN;

        return 0;
}

static int aw88298_get_i2sctrl_cfg(const struct audio_codec_cfg *cfg, uint16_t *mask,
                                   uint16_t *value)
{
        const i2s_opt_t options = cfg->dai_cfg.i2s.options;
        const i2s_fmt_t format = cfg->dai_cfg.i2s.format;
        uint16_t clk_flags = 0U;
        uint16_t mode_code;
        uint16_t fs_code;
        uint16_t bck_code;
        uint16_t rate_code;
        int ret;

        ret = aw88298_get_i2s_mode_code(cfg->dai_type, format, &mode_code);
        if (ret < 0) {
                return ret;
        }

        ret = aw88298_get_word_size_codes(cfg->dai_cfg.i2s.word_size, &fs_code, &bck_code);
        if (ret < 0) {
                return ret;
        }

        if ((format & I2S_FMT_DATA_ORDER_LSB) != 0U) {
                LOG_ERR("LSB-first data ordering not supported");
                return -ENOTSUP;
        }

        switch (format & I2S_FMT_CLK_FORMAT_MASK) {
        case I2S_FMT_CLK_NF_NB:
                break;
        case I2S_FMT_CLK_NF_IB:
                clk_flags |= AW88298_REG_I2SCTRL_BCLK_POL;
                break;
        case I2S_FMT_CLK_IF_NB:
                clk_flags |= AW88298_REG_I2SCTRL_LRCLK_POL;
                break;
        case I2S_FMT_CLK_IF_IB:
                clk_flags |= AW88298_REG_I2SCTRL_LRCLK_POL | AW88298_REG_I2SCTRL_BCLK_POL;
                break;
        default:
                LOG_ERR("Unsupported I2S clock format 0x%x", format & I2S_FMT_CLK_FORMAT_MASK);
                return -ENOTSUP;
        }

        if ((format & ~(I2S_FMT_DATA_FORMAT_MASK | I2S_FMT_DATA_ORDER_LSB |
                        I2S_FMT_CLK_FORMAT_MASK)) != 0U) {
                LOG_WRN("Ignoring unsupported I2S format bits 0x%x",
                        format & ~(I2S_FMT_DATA_FORMAT_MASK | I2S_FMT_DATA_ORDER_LSB |
                                   I2S_FMT_CLK_FORMAT_MASK));
        }

        ret = aw88298_get_sample_rate_code(cfg->dai_cfg.i2s.frame_clk_freq, &rate_code);
        if (ret < 0) {
                return ret;
        }

        if ((options & (I2S_OPT_LOOPBACK | I2S_OPT_PINGPONG)) != 0U) {
                LOG_WRN("Ignoring unsupported I2S options 0x%x",
                        options & (I2S_OPT_LOOPBACK | I2S_OPT_PINGPONG));
        }

        if ((options & I2S_OPT_BIT_CLK_GATED) != 0U) {
                LOG_WRN("Bit clock gating not supported");
        }

        if (((options & I2S_OPT_BIT_CLK_SLAVE) != 0U) !=
            ((options & I2S_OPT_FRAME_CLK_SLAVE) != 0U)) {
                LOG_ERR("Inconsistent clock master/slave options 0x%x", options);
                return -ENOTSUP;
        }

        if ((options & I2S_OPT_BIT_CLK_SLAVE) == 0U) {
                LOG_ERR("AW88298 requires external LRCLK/BCLK (slave mode)");
                return -ENOTSUP;
        }

        *mask = AW88298_REG_I2SCTRL_I2S_CFG_MASK;
        *value = rate_code | AW88298_REG_I2SCTRL_FRAME_FLAGS | AW88298_I2SCTRL_MODE_SLAVE |
                 AW88298_I2SCTRL_I2SMD_VAL(mode_code) |
                 AW88298_I2SCTRL_I2SFS_VAL(fs_code) | AW88298_I2SCTRL_I2SBCK_VAL(bck_code) |
                 clk_flags;

        return 0;
}

static int aw88298_get_i2scfg1_cfg(const struct audio_codec_cfg *cfg, uint16_t *mask,
                                   uint16_t *value)
{
        if ((cfg->dai_cfg.i2s.channels != 1U) && (cfg->dai_cfg.i2s.channels != 2U)) {
                LOG_ERR("Unsupported channel count %u", cfg->dai_cfg.i2s.channels);
                return -ENOTSUP;
        }

        if (cfg->dai_cfg.i2s.channels == 1U) {
                /* Sum the left/right data stream and feed a single slot */
                *value = AW88298_I2SCFG1_RXSEL_VAL(0x3U) | AW88298_I2SCFG1_RXEN_VAL(BIT(0));
        } else {
                *value = AW88298_I2SCFG1_RXSEL_VAL(0x0U) |
                         AW88298_I2SCFG1_RXEN_VAL(BIT(0) | BIT(1));
        }

        *mask = AW88298_REG_I2SCFG1_RXSEL | AW88298_REG_I2SCFG1_RXEN;

        return 0;
}

static int aw88298_configure(const struct device *dev, struct audio_codec_cfg *cfg)
{
        uint16_t sysctrl_mask;
        uint16_t sysctrl_value;
        uint16_t i2sctrl_mask;
        uint16_t i2sctrl_value;
        uint16_t i2scfg1_mask;
        uint16_t i2scfg1_value;
        int ret;

        if ((cfg->dai_route != AUDIO_ROUTE_PLAYBACK) && (cfg->dai_route != AUDIO_ROUTE_BYPASS)) {
                LOG_ERR("Unsupported route %u", cfg->dai_route);
                return -ENOTSUP;
        }

        LOG_DBG("Configure: rate=%u channels=%u options=0x%x", cfg->dai_cfg.i2s.frame_clk_freq,
                cfg->dai_cfg.i2s.channels, cfg->dai_cfg.i2s.options);

        ret = aw88298_get_sysctrl_cfg(&sysctrl_mask, &sysctrl_value);
        if (ret < 0) {
                return ret;
        }

        ret = aw88298_get_i2sctrl_cfg(cfg, &i2sctrl_mask, &i2sctrl_value);
        if (ret < 0) {
                return ret;
        }

        ret = aw88298_get_i2scfg1_cfg(cfg, &i2scfg1_mask, &i2scfg1_value);
        if (ret < 0) {
                return ret;
        }

        ret = aw88298_update_reg(dev, AW88298_REG_SYSCTRL, sysctrl_mask, sysctrl_value);
        if (ret < 0) {
                return ret;
        }

        ret = aw88298_update_reg(dev, AW88298_REG_I2SCTRL, i2sctrl_mask, i2sctrl_value);
        if (ret < 0) {
                return ret;
        }

        ret = aw88298_update_reg(dev, AW88298_REG_I2SCFG1, i2scfg1_mask, i2scfg1_value);
        if (ret < 0) {
                return ret;
        }

        return 0;
}

static void aw88298_start_output(const struct device *dev)
{
	int ret;

	ret = aw88298_update_reg(dev, AW88298_REG_SYSCTRL, AW88298_REG_SYSCTRL_AMPPD, 0);
	if (ret < 0) {
		LOG_ERR("Failed to initialize AW88298");
		return;
	}

	ret = aw88298_update_reg(dev, AW88298_REG_SYSCTRL2, AW88298_REG_SYSCTRL2_HMUTE, 0);
	if (ret < 0) {
		LOG_ERR("Failed to initialize AW88298");
		return;
	}
}

static void aw88298_stop_output(const struct device *dev)
{
	int ret;

	ret = aw88298_update_reg(dev, AW88298_REG_SYSCTRL2, AW88298_REG_SYSCTRL2_HMUTE, 0xFFFF);
	if (ret < 0) {
		LOG_ERR("Failed to stop AW88298");
		return;
	}

	ret = aw88298_update_reg(dev, AW88298_REG_SYSCTRL, AW88298_REG_SYSCTRL_AMPPD, 0xFFFF);
	if (ret < 0) {
		LOG_ERR("Failed to initialize AW88298");
		return;
	}
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
		break;
	case AUDIO_PROPERTY_OUTPUT_MUTE:
		data->mute = val.mute;
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}

static int aw88298_apply_properties(const struct device *dev)
{
	const struct aw88298_config *cfg = dev->config;
	struct aw88298_data *data = dev->data;
	const uint16_t mute_field = data->mute ? AW88298_REG_SYSCTRL2_HMUTE : 0;
	const uint16_t volume_field = AW88298_HAGCCFG4_VOL_VAL(data->volume);
	int ret;

	LOG_DBG("Using I2C addr 0x%02x", cfg->bus.addr);

	ret = aw88298_update_reg(dev, AW88298_REG_SYSCTRL2, AW88298_REG_SYSCTRL2_HMUTE, mute_field);
	if (ret < 0) {
		return ret;
	}

	ret = aw88298_update_reg(dev, AW88298_REG_HAGCCFG4, AW88298_REG_HAGCCFG4_VOL, volume_field);
	if (ret < 0) {
		return ret;
	}

	LOG_DBG("Program done");

	return 0;
}

static const struct audio_codec_api aw88298_api = {
	.configure = aw88298_configure,
	.start_output = aw88298_start_output,
	.stop_output = aw88298_stop_output,
	.set_property = aw88298_set_property,
	.apply_properties = aw88298_apply_properties,
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

	if (cfg->reset_gpio.port) {
		if (!device_is_ready(cfg->reset_gpio.port)) {
			LOG_ERR("Enable GPIO device not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->reset_gpio, GPIO_OUTPUT_INACTIVE);
		if (ret < 0) {
			LOG_ERR("Failed to configure enable GPIO (%d)", ret);
			return ret;
		}

		ret = gpio_pin_set_dt(&cfg->reset_gpio, false);
		if (ret < 0) {
			LOG_ERR("Failed to configure enable GPIO (%d)", ret);
			return ret;
		}

		k_msleep(AW88298_RESET_DELAY_MS);

		ret = gpio_pin_set_dt(&cfg->reset_gpio, true);
		if (ret < 0) {
			LOG_ERR("Failed to enable speaker GPIO");
			return ret;
		}
	}

	return 0;
}

#define AW88298_INST(idx)                                                                          \
	static const struct aw88298_config aw88298_config_##idx = {                                \
		.bus = I2C_DT_SPEC_INST_GET(idx),                                                  \
		.reset_gpio = GPIO_DT_SPEC_INST_GET_OR(idx, reset_gpios, {0}),                     \
	};                                                                                         \
	static struct aw88298_data aw88298_data_##idx;                                             \
	DEVICE_DT_INST_DEFINE(idx, aw88298_init, NULL, &aw88298_data_##idx, &aw88298_config_##idx, \
			      POST_KERNEL, CONFIG_AUDIO_CODEC_INIT_PRIORITY, &aw88298_api)

DT_INST_FOREACH_STATUS_OKAY(AW88298_INST)
