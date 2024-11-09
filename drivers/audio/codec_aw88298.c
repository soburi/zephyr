/*
 * Copyright (c) 2023 Centralp
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT awinic_aw88298

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/audio/codec.h>

//#include "aw88298.h"

#define AW88298_REG_ID       0x00
#define AW88298_REG_SYSST 0x01
#define AW88298_REG_SYSINT 0x02
#define AW88298_REG_SYSINTM 0x03
#define AW88298_REG_SYSCTRL 0x04
#define AW88298_REG_SYSCTRL2 0x05
#define AW88298_REG_I2SCTRL 0x06
#define AW88298_REG_I2SCFG1 0x07
#define AW88298_REG_HAGCCFG1 0x09
#define AW88298_REG_HAGCCFG2 0x0a
#define AW88298_REG_HAGCCFG3 0x0b
#define AW88298_REG_HAGCCFG4 0x0c
#define AW88298_REG_HAGCST 0x10
#define AW88298_REG_VDD 0x12
#define AW88298_REG_TEMP 0x13
#define AW88298_REG_PVDD 0x14
#define AW88298_REG_BSTCTRL1 0x60
#define AW88298_REG_BSTCTRL2 0x61

#define AW88298_I2CSR_8K      0x00
#define AW88298_I2CSR_11P025K 0x01
#define AW88298_I2CSR_12K     0x02
#define AW88298_I2CSR_16K     0x03
#define AW88298_I2CSR_22P05K  0x04
#define AW88298_I2CSR_24K     0x05
#define AW88298_I2CSR_32K     0x06
#define AW88298_I2CSR_44P1K   0x07
#define AW88298_I2CSR_48K     0x08
#define AW88298_I2CSR_96K     0x09
#define AW88298_I2CSR_192K    0x0a

#define LOG_LEVEL CONFIG_AUDIO_CODEC_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(codec_aw88298);

#define CODEC_OUTPUT_VOLUME_MAX (24 * 2)
#define CODEC_OUTPUT_VOLUME_MIN (-100 * 2)

struct aw88298_config {
	struct i2c_dt_spec bus;
#if DT_ANY_INST_HAS_PROP_STATUS_OKAY(reset_gpios)
        struct gpio_dt_spec reset_gpio;
#endif
#if DT_ANY_INST_HAS_PROP_STATUS_OKAY(int_gpios)
        struct gpio_dt_spec int_gpio;
        gpio_callback_handler_t int_cb;
#endif
};

enum aw88298_channel_t {
	AW88298_CHANNEL_1,
	AW88298_CHANNEL_2,
	AW88298_CHANNEL_ALL,
	AW88298_CHANNEL_UNKNOWN,
};

static enum aw88298_channel_t audio_to_aw88298_channel[] = {
	[AUDIO_CHANNEL_FRONT_LEFT] = AW88298_CHANNEL_1,
	[AUDIO_CHANNEL_FRONT_RIGHT] = AW88298_CHANNEL_2,
	[AUDIO_CHANNEL_LFE] = AW88298_CHANNEL_UNKNOWN,
	[AUDIO_CHANNEL_FRONT_CENTER] = AW88298_CHANNEL_UNKNOWN,
	[AUDIO_CHANNEL_REAR_LEFT] = AW88298_CHANNEL_1,
	[AUDIO_CHANNEL_REAR_RIGHT] = AW88298_CHANNEL_2,
	[AUDIO_CHANNEL_REAR_CENTER] = AW88298_CHANNEL_UNKNOWN,
	[AUDIO_CHANNEL_SIDE_LEFT] = AW88298_CHANNEL_1,
	[AUDIO_CHANNEL_SIDE_RIGHT] = AW88298_CHANNEL_2,
	[AUDIO_CHANNEL_ALL] = AW88298_CHANNEL_ALL,
};

static int aw88298_write_reg(const struct device *dev, uint8_t reg, uint16_t val)
{
	const struct aw88298_config *const dev_cfg = dev->config;
	const uint8_t bytes[2] = { val & 0xFF, val >> 8 };

	return i2c_burst_write_dt(&dev_cfg->bus, reg, bytes, 2);
}

static int aw88298_read_reg(const struct device *dev, uint8_t reg, uint16_t *val)
{
	const struct aw88298_config *const dev_cfg = dev->config;
	uint8_t bytes[2];
	int ret;

	ret = i2c_burst_read_dt(&dev_cfg->bus, reg, bytes, 2);

	if (ret == 0) {
		*val = bytes[0] | bytes[1] << 8;
	}

	return ret;
}

static int aw88298_update_reg(const struct device *dev, uint8_t reg, uint16_t mask, uint16_t val)
{
	uint16_t reg_val;
	int ret;

	ret = aw88298_read_reg(dev, reg, &reg_val);

	val = (reg_val & ~mask) | (val & mask);

	ret = aw88298_write_reg(dev, reg, val);

	return ret;
}
#if 0
static int aw88298_configure_dai(const struct device *dev, audio_dai_cfg_t *cfg)
{
	uint16_t val;

	aw88298_read_reg(dev, SAP_CTRL_ADDR, &val);

	/* I2S mode */
	val &= ~SAP_CTRL_INPUT_FORMAT_MASK;
	val |= SAP_CTRL_INPUT_FORMAT(SAP_CTRL_INPUT_FORMAT_I2S);

	/* Input sampling rate */
	val &= ~SAP_CTRL_INPUT_SAMPLING_RATE_MASK;
	switch (cfg->i2s.frame_clk_freq) {
	case AUDIO_PCM_RATE_8K:
		val = AW88298_I2CSR_8K;
		break;
	case AUDIO_PCM_RATE_11P025K:
		val = AW88298_I2CSR_11P025K;
		break;
	case AUDIO_PCM_RATE_16K:
		val = AW88298_I2CSR_16K;
		break;
	case AUDIO_PCM_RATE_22P05K:
		val = AW88298_I2CSR_22P05K;
		break;
	case AUDIO_PCM_RATE_24K:
		val = AW88298_I2CSR_24K;
		break;
	case AUDIO_PCM_RATE_32K:
		val = AW88298_I2CSR_32K;
		break;
	case AUDIO_PCM_RATE_44P1K:
		val = AW88298_I2CSR_44P1K;
		break;
	case AUDIO_PCM_RATE_48K:
		val = AW88298_I2CSR_48K;
		break;
	case AUDIO_PCM_RATE_96K:
		val = AW88298_I2CSR_96K;
		break;
	case AUDIO_PCM_RATE_192K:
		val = AW88298_I2CSR_192K;
		break;
	default:
		LOG_ERR("Invalid sampling rate %zu", cfg->i2s.frame_clk_freq);
		return -EINVAL;
	}

	aw88298_write_reg(dev, SAP_CTRL_ADDR, val);

	return 0;
}

static void aw88298_configure_output(const struct device *dev)
{
	uint16_t val;

	/* Overcurrent level = 1 */
	aw88298_read_reg(dev, MISC_CTRL_1_ADDR, &val);
	val &= ~MISC_CTRL_1_OC_CONTROL_MASK;
	aw88298_write_reg(dev, MISC_CTRL_1_ADDR, val);

	/*
	 * PWM frequency = 10 fs
	 * Reduce PWM frequency to prevent component overtemperature
	 */
	aw88298_read_reg(dev, MISC_CTRL_2_ADDR, &val);
	val &= ~MISC_CTRL_2_PWM_FREQUENCY_MASK;
	val |= MISC_CTRL_2_PWM_FREQUENCY(MISC_CTRL_2_PWM_FREQUENCY_10_FS);
	aw88298_write_reg(dev, MISC_CTRL_2_ADDR, val);
}

static int aw88298_set_output_volume(const struct device *dev, enum aw88298_channel_t channel,
				   int vol)
{
	uint8_t vol_val;

	if ((vol > CODEC_OUTPUT_VOLUME_MAX) || (vol < CODEC_OUTPUT_VOLUME_MIN)) {
		LOG_ERR("Invalid volume %d.%d dB", vol >> 1, ((uint32_t)vol & 1) ? 5 : 0);
		return -EINVAL;
	}

	vol_val = vol + 0xcf;
	switch (channel) {
	case AW88298_CHANNEL_1:
		aw88298_write_reg(dev, CH1_VOLUME_CTRL_ADDR, CH_VOLUME_CTRL_VOLUME(vol_val));
		break;
	case AW88298_CHANNEL_2:
		aw88298_write_reg(dev, CH2_VOLUME_CTRL_ADDR, CH_VOLUME_CTRL_VOLUME(vol_val));
		break;
	case AW88298_CHANNEL_ALL:
		aw88298_write_reg(dev, CH1_VOLUME_CTRL_ADDR, CH_VOLUME_CTRL_VOLUME(vol_val));
		aw88298_write_reg(dev, CH2_VOLUME_CTRL_ADDR, CH_VOLUME_CTRL_VOLUME(vol_val));
		break;
	case AW88298_CHANNEL_UNKNOWN:
	default:
		LOG_ERR("Invalid codec channel %u", channel);
		return -EINVAL;
	}

	return 0;
}

static int codec_aw88298_configure(const struct device *dev, struct audio_aw88298_cfg *cfg)
{
	int ret;
	uint8_t val;

	if (cfg->dai_type != AUDIO_DAI_TYPE_I2S) {
		LOG_ERR("dai_type must be AUDIO_DAI_TYPE_I2S");
		return -EINVAL;
	}

	{
        self->In_I2C.bitOn(aw9523_i2c_addr, 0x02, 0b00000100, 400000);
        /// サンプリングレートに応じてAW88298のレジスタの設定値を変える;
        static constexpr uint8_t rate_tbl[] = {4,5,6,8,10,11,15,20,22,44};
        size_t reg0x06_value = 0;
        size_t rate = (cfg.sample_rate + 1102) / 2205;
        while (rate > rate_tbl[reg0x06_value] && ++reg0x06_value < sizeof(rate_tbl)) {}

        reg0x06_value |= 0x14C0;  // I2SBCK=0 (BCK mode 16*2)
				    
	aw88298_write_reg(dev, AW88298_REG_BSTCTRL2, 0x0673);  // boost mode disabled 
	aw88298_write_reg(dev, AW88298_REG_SYSCTRL,  0x4040);  // I2SEN=1 AMPPD=0 PWDN=0
	aw88298_write_reg(dev, AW88298_REG_SYSCTRL2, 0x0008);  // RMSE=0 HAGCE=0 HDCCE=0 HMUTE=0
	aw88298_write_reg(dev, AW88298_REG_I2SCTRL,  reg0x06_value);
	aw88298_write_reg(dev, AW88298_REG_HAGCCFG4, 0x0064);  // volume setting (full volume)
        }
        //else /// disableにする場合および内蔵スピーカ以外を操作対象とした場合、内蔵スピーカを停止する。
        {
	aw88298_write_reg(dev, AW88298_REG_SYSCTRL, 0x4000 );  // I2SEN=0 AMPPD=0 PWDN=0
	self->In_I2C.bitOff(aw9523_i2c_addr, 0x02, 0b00000100, 400000);
	}

	ret = aw88298_configure_dai(dev, &cfg->dai_cfg);
	aw88298_configure_output(dev);

	return ret;
}

static void codec_aw88298_start_output(const struct device *dev)
{
	aw88298_unmute_output(dev, AW88298_CHANNEL_ALL);
}

static void codec_aw88298_stop_output(const struct device *dev)
{
	aw88298_mute_output(dev, AW88298_CHANNEL_ALL);
}

static void aw88298_mute_output(const struct device *dev, enum aw88298_channel_t channel)
{
	uint8_t val;

	aw88298_read_reg(dev, CH_STATE_CTRL_ADDR, &val);
	switch (channel) {
	case AW88298_CHANNEL_1:
		val &= ~CH_STATE_CTRL_CH1_STATE_CTRL_MASK;
		val |= CH_STATE_CTRL_CH1_STATE_CTRL(CH_STATE_CTRL_MUTE);
		break;
	case AW88298_CHANNEL_2:
		val &= ~CH_STATE_CTRL_CH2_STATE_CTRL_MASK;
		val |= CH_STATE_CTRL_CH2_STATE_CTRL(CH_STATE_CTRL_MUTE);
		break;
	case AW88298_CHANNEL_ALL:
		val &= ~(CH_STATE_CTRL_CH1_STATE_CTRL_MASK | CH_STATE_CTRL_CH2_STATE_CTRL_MASK);
		val |= CH_STATE_CTRL_CH1_STATE_CTRL(CH_STATE_CTRL_MUTE) |
		       CH_STATE_CTRL_CH2_STATE_CTRL(CH_STATE_CTRL_MUTE);
		break;
	case AW88298_CHANNEL_UNKNOWN:
	default:
		LOG_ERR("Invalid codec channel %u", channel);
		return;
	}
	aw88298_write_reg(dev, CH_STATE_CTRL_ADDR, val);
}

static void aw88298_unmute_output(const struct device *dev, enum aw88298_channel_t channel)
{
	uint8_t val;

	aw88298_read_reg(dev, CH_STATE_CTRL_ADDR, &val);
	switch (channel) {
	case AW88298_CHANNEL_1:
		val &= ~CH_STATE_CTRL_CH1_STATE_CTRL_MASK;
		val |= CH_STATE_CTRL_CH1_STATE_CTRL(CH_STATE_CTRL_PLAY);
		break;
	case AW88298_CHANNEL_2:
		val &= ~CH_STATE_CTRL_CH2_STATE_CTRL_MASK;
		val |= CH_STATE_CTRL_CH2_STATE_CTRL(CH_STATE_CTRL_PLAY);
		break;
	case AW88298_CHANNEL_ALL:
		val &= ~(CH_STATE_CTRL_CH1_STATE_CTRL_MASK | CH_STATE_CTRL_CH2_STATE_CTRL_MASK);
		val |= CH_STATE_CTRL_CH1_STATE_CTRL(CH_STATE_CTRL_PLAY) |
		       CH_STATE_CTRL_CH2_STATE_CTRL(CH_STATE_CTRL_PLAY);
		break;
	case AW88298_CHANNEL_UNKNOWN:
	default:
		LOG_ERR("Invalid codec channel %u", channel);
		return;
	}
	aw88298_write_reg(dev, CH_STATE_CTRL_ADDR, val);
}

static int codec_aw88298_set_property(const struct device *dev, audio_property_t property,
			      audio_channel_t channel, audio_property_value_t val)
{
	enum aw88298_channel_t aw88298_channel = audio_to_aw88298_channel[channel];

	if (aw88298_channel == AW88298_CHANNEL_UNKNOWN) {
		LOG_ERR("Invalid channel %u", channel);
		return -EINVAL;
	}

	switch (property) {
	case AUDIO_PROPERTY_OUTPUT_VOLUME:
		return aw88298_set_output_volume(dev, aw88298_channel, val.vol);

	case AUDIO_PROPERTY_OUTPUT_MUTE:
		if (val.mute) {
			aw88298_mute_output(dev, aw88298_channel);
		} else {
			aw88298_unmute_output(dev, aw88298_channel);
		}
		return 0;

	default:
		break;
	}

	return -EINVAL;
}

static int codec_aw88298_apply_properties(const struct device *dev)
{
	/* nothing to do because there is nothing cached */
	return 0;
}
#endif
static const struct audio_codec_api aw88298_driver_api = {
//	.configure = codec_aw88298_configure,
//	.start_output = codec_aw88298_start_output,
//	.stop_output = codec_aw88298_stop_output,
//	.set_property = codec_aw88298_set_property,
//	.apply_properties = codec_aw88298_apply_properties,
};

static int codec_aw88298_init(const struct device *dev)
{
	const struct aw88298_config *const config = dev->config;
	uint16_t id = 0; 
	int ret;

	if (!device_is_ready(config->bus.bus)) {
		LOG_ERR("I2C device not ready");
		return -ENODEV;
	}
	
#if DT_ANY_INST_HAS_PROP_STATUS_OKAY(reset_gpios)
        if (!config->reset_gpio.port) {
                goto end_hw_reset;
        }

        if (!gpio_is_ready_dt(&config->reset_gpio)) {
                LOG_ERR("%s: Reset GPIO not ready", dev->name);
                return -ENODEV;
        }

        err = gpio_pin_configure_dt(&config->reset_gpio, GPIO_OUTPUT_ACTIVE);
        if (err) {
                LOG_ERR("%s: Failed to configure reset pin %d (%d)", dev->name,
                        config->reset_gpio.pin, err);
                return err;
        }

        //k_busy_wait(AW9523B_RESET_PULSE_WIDTH);

        err = gpio_pin_set_dt(&config->reset_gpio, 0);
        if (err) {
                LOG_ERR("%s: Failed to set 0 reset pin %d (%d)", dev->name, config->reset_gpio.pin,
                        err);
                return err;
        }

end_hw_reset:
#endif

	ret = aw88298_read_reg(dev, AW88298_REG_ID, &id);

	printk("ret = %d\n", ret);
	printk("ID = %x\n", id);

	return 0;
}

#define TAS6422DAC_INIT(n)                                                                         \
	static struct aw88298_config aw88298_config_##n = {                              \
		.bus = I2C_DT_SPEC_INST_GET(n), \
                IF_ENABLED(DT_INST_PROP_HAS_IDX(inst, int_gpios, 0), (                             \
                           .int_gpio = GPIO_DT_SPEC_INST_GET(inst, int_gpios),                     \
                           .int_cb = gpio_aw9523b_int_cb##inst,                                    \
                ))                                                                                 \
                IF_ENABLED(DT_INST_PROP_HAS_IDX(inst, reset_gpios, 0), (                           \
                           .reset_gpio = GPIO_DT_SPEC_INST_GET(inst, reset_gpios),                 \
                ))     \
	};                     \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, codec_aw88298_init, NULL, NULL,                   \
			      &aw88298_config_##n, POST_KERNEL,                               \
			      CONFIG_AUDIO_CODEC_INIT_PRIORITY, &aw88298_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TAS6422DAC_INIT)
