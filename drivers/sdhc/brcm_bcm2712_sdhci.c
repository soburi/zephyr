/*
 * Copyright (c) 2024 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT brcm_bcm2712_sdhci

#include <zephyr/kernel.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/disk.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/regulator.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/logging/log.h>

#include "sdhci_common.h"

LOG_MODULE_REGISTER(brcm_bcm2712_sdhci, CONFIG_SDHC_LOG_LEVEL);

#define LOG_DEV_ERR(dev, format, ...) LOG_ERR("%s:" #format, (dev)->name, ##__VA_ARGS__)
#define LOG_DEV_WRN(dev, format, ...) LOG_WRN("%s:" #format, (dev)->name, ##__VA_ARGS__)
#define LOG_DEV_INF(dev, format, ...) LOG_INF("%s:" #format, (dev)->name, ##__VA_ARGS__)
#define LOG_DEV_DBG(dev, format, ...) LOG_DBG("%s:" #format, (dev)->name, ##__VA_ARGS__)

#define DEV_CFG(_dev)  ((const struct bcm2712_sdhci_config *)(_dev)->config)
#define DEV_DATA(_dev) ((struct bcm2712_sdhci_data *const)(_dev)->data)

#define SDIO_CFG_CTRL                 0x0
#define SDIO_CFG_CTRL_SDCD_N_TEST_EN  BIT(31)
#define SDIO_CFG_CTRL_SDCD_N_TEST_LEV BIT(30)

#define SDIO_CFG_SD_PIN_SEL      0x44
#define SDIO_CFG_SD_PIN_SEL_MASK 0x3
#define SDIO_CFG_SD_PIN_SEL_CARD BIT(1)

#define SDIO_CFG_CQ_CAPABILITY      0x4c
#define SDIO_CFG_CQ_CAPABILITY_FMUL GENMASK(13, 12)

#define SDIO_CFG_MAX_50MHZ_MODE                0x1ac
#define SDIO_CFG_MAX_50MHZ_MODE_STRAP_OVERRIDE BIT(31)
#define SDIO_CFG_MAX_50MHZ_MODE_ENABLE         BIT(0)

struct bcm2712_sdhci_config {
	DEVICE_MMIO_NAMED_ROM(host);
	DEVICE_MMIO_NAMED_ROM(cfg);
	DEVICE_MMIO_NAMED_ROM(busisol);
	DEVICE_MMIO_NAMED_ROM(lcpll);

	const struct gpio_dt_spec gpio_cd;
	const struct device *regulator_vqmmc;
	const struct device *regulator_vmmc;
	uint32_t clk_freq;

	uint32_t max_bus_freq;
	uint32_t min_bus_freq;
	uint32_t power_delay_ms;
	uint8_t hs200_mode: 1;
	uint8_t hs400_mode: 1;
	uint8_t bw_4bit: 1;
	uint8_t bw_8bit: 1;
	uint8_t non_removable: 1;
};

struct bcm2712_sdhci_data {
	DEVICE_MMIO_NAMED_RAM(host);
	DEVICE_MMIO_NAMED_RAM(cfg);
	DEVICE_MMIO_NAMED_RAM(busisol);
	DEVICE_MMIO_NAMED_RAM(lcpll);

	struct sdhc_io host_io;
	struct sdhc_host_props props;

	struct sdhci_common sdhci_ctx;
};

static int bcm2712_sdhci_request(const struct device *dev, struct sdhc_command *cmd,
				 struct sdhc_data *sd_data)
{
	struct bcm2712_sdhci_data *data = dev->data;
	int ret;

	ret = sdhci_send_req(&data->sdhci_ctx, cmd, sd_data);
	if (ret) {
		LOG_DEV_ERR(dev, "sd cmd request failed (%d)", ret);
	}
	return ret;
}

static int bcm2712_sdhci_get_card_present(const struct device *dev)
{
	const struct bcm2712_sdhci_config *cfg = dev->config;
	struct bcm2712_sdhci_data *data = dev->data;

	int cd = 0;

	if (cfg->non_removable) {
		cd = 1;
		goto out_cd;
	}

	if (cfg->gpio_cd.port) {
		cd = gpio_pin_get_dt(&cfg->gpio_cd) > 0 ? 1 : cd;
		LOG_DEV_DBG(dev, "sd detect gpio %d", cd);
		goto out_cd;
	}

	cd = sdhci_get_cd(&data->sdhci_ctx);

out_cd:
	LOG_DEV_DBG(dev, "sd detect sdhci %d", cd);

	if (!cd) {
		LOG_DEV_INF(dev, "sd card is not detected");
	}
	return cd;
}

static int bcm2712_sdhci_card_busy(const struct device *dev)
{
	struct bcm2712_sdhci_data *data = dev->data;

	return sdhci_is_card_busy(&data->sdhci_ctx);
}

static int bcm2712_sdhci_set_io(const struct device *dev, struct sdhc_io *ios)
{
	const struct bcm2712_sdhci_config *cfg = dev->config;
	struct bcm2712_sdhci_data *data = dev->data;
	struct sdhc_io *host_io = &data->host_io;
	int ret;

	LOG_DEV_DBG(dev,
		    "set_io: bus_width:%d clock:%dHz power:%d timing:%d, voltage:%d drv:%d",
		    ios->bus_width, ios->clock, ios->power_mode,
		    ios->timing, ios->signal_voltage, ios->driver_type);

	if (host_io->clock != ios->clock) {

		if (ios->clock &&
		    (ios->clock > data->props.f_max || ios->clock < data->props.f_min)) {
			LOG_DEV_ERR(dev, "set_io: invalid clock:%d valid range %d - %d Hz",
				    ios->clock, data->props.f_min, data->props.f_max);
			return -EINVAL;
		}

		ret = sdhci_set_clock(&data->sdhci_ctx, ios->clock);
		if (ret) {
			LOG_DEV_ERR(dev, "set_io: set clock failed (%d)", ret);
			return ret;
		}
		host_io->clock = ios->clock;
	}

	if (host_io->bus_width != ios->bus_width) {
		ret = sdhci_set_bus_width(&data->sdhci_ctx, ios->bus_width);
		if (ret) {
			LOG_DEV_ERR(dev, "set_io: set bus width failed (%d)", ret);
			return ret;
		}
		host_io->bus_width = ios->bus_width;
	}

	if (host_io->power_mode != ios->power_mode) {
		if (ios->power_mode == SDHC_POWER_ON) {
			regulator_enable(cfg->regulator_vmmc);

			k_msleep(cfg->power_delay_ms);

			sdhci_set_bus_power(&data->sdhci_ctx, SD_VOL_3_3_V);

			regulator_enable(cfg->regulator_vqmmc);
		} else if (ios->power_mode == SDHC_POWER_OFF) {
			sdhci_set_bus_power(&data->sdhci_ctx, 0);

			if (regulator_is_enabled(cfg->regulator_vqmmc)) {
				regulator_disable(cfg->regulator_vqmmc);
			}
			if (regulator_is_enabled(cfg->regulator_vmmc)) {
				regulator_disable(cfg->regulator_vmmc);
			}
		} else {
			return -EINVAL;
		}
		host_io->power_mode = ios->power_mode;
	}

	if (host_io->signal_voltage != ios->signal_voltage) {
		if (ios->signal_voltage == SD_VOL_3_3_V) {
			ret = regulator_set_voltage(cfg->regulator_vqmmc, 3300000, 3300000);
		} else if (ios->signal_voltage == SD_VOL_1_8_V) {
			ret = regulator_set_voltage(cfg->regulator_vqmmc, 1800000, 1800000);
		} else {
			return -EINVAL;
		}

		ret = sdhci_set_voltage(&data->sdhci_ctx, ios->signal_voltage);
		if (ret) {
			LOG_DEV_ERR(dev, "set_io: set signal voltage failed (%d)", ret);
			return ret;
		}
		host_io->signal_voltage = ios->signal_voltage;
	}

	if (host_io->timing != ios->timing) {
		ret = sdhci_set_uhs_timing(&data->sdhci_ctx, ios->timing);
		if (ret) {
			LOG_DEV_ERR(dev, "set_io: set uhs timings failed (%d)", ret);
			return ret;
		}
		host_io->timing = ios->timing;
	}

	return 0;
}

static int bcm2712_sdhci_get_host_props(const struct device *dev, struct sdhc_host_props *props)
{
	struct bcm2712_sdhci_data *data = dev->data;

	memcpy(props, &data->props, sizeof(*props));
	return 0;
}

static int bcm2712_sdhci_reset(const struct device *dev)
{
	struct bcm2712_sdhci_data *data = dev->data;

	return sdhci_reset(&data->sdhci_ctx, SDHCI_RESET_ALL);
}

static int bcm2712_execute_tuning(const struct device *dev)
{
	struct bcm2712_sdhci_data *data = dev->data;
	struct sdhc_io *host_io = &data->host_io;

	LOG_DEV_DBG(dev, "sdhc: tuning starting...");

	switch (host_io->timing) {
	case SDHC_TIMING_SDR50:
		if (!data->props.host_caps.sdr50_needs_tuning) {
			return 0;
		}
		__fallthrough;
	case SDHC_TIMING_SDR104:
		/*
		 * TODO: add tuning:
		 * set SDHCI_HOST_CONTROL2.SDHCI_CTRL_EXEC_TUNING
		 * send CMD19
		 * check for !SDHCI_HOST_CONTROL2.SDHCI_CTRL_EXEC_TUNING
		 */
		return -ENOSYS;
	case SDHC_TIMING_DDR50:
		/* TODO: it's not clear from SDHC standard if DDR50 need tuning,
		 * so bypass it.
		 */
		__fallthrough;
	default:
		/* no tuning for other timings */
		return 0;
	}

	LOG_DEV_DBG(dev, "sdhc: tuning completed");
	return 0;
}

static const struct sdhc_driver_api bcm2712_sdhci_api = {
	.request = bcm2712_sdhci_request,
	.set_io = bcm2712_sdhci_set_io,
	.get_host_props = bcm2712_sdhci_get_host_props,
	.get_card_present = bcm2712_sdhci_get_card_present,
	.reset = bcm2712_sdhci_reset,
	.card_busy = bcm2712_sdhci_card_busy,
	.execute_tuning = bcm2712_execute_tuning,
};

static int bcm2712_sdhci_init_host_props(const struct device *dev)
{
	const struct bcm2712_sdhci_config *cfg = dev->config;
	struct bcm2712_sdhci_data *data = dev->data;
	struct sdhc_host_props *props = &data->props;
	struct sdhc_host_caps *host_caps = &props->host_caps;
	struct sdhci_common *sdhci_ctx = &data->sdhci_ctx;
	bool prop_sup;
	int ret = 0;

	props->f_max = cfg->max_bus_freq;
	props->f_min = cfg->min_bus_freq;

	props->is_spi = 0;
	props->power_delay = cfg->power_delay_ms;
	host_caps->bus_4_bit_support = cfg->bw_4bit;
	host_caps->hs200_support = cfg->hs200_mode;
	host_caps->hs400_support = cfg->hs400_mode;

	sdhci_init_caps(sdhci_ctx, props);

	prop_sup = regulator_is_supported_voltage(cfg->regulator_vqmmc, 3300000, 3300000);
	if (host_caps->vol_330_support && !prop_sup) {
		LOG_DEV_INF(dev, "vdd vol_330 not supported");
		host_caps->vol_330_support = 0;
	}

	prop_sup = regulator_is_supported_voltage(cfg->regulator_vqmmc, 3000000, 3000000);
	if (host_caps->vol_300_support && !prop_sup) {
		LOG_DEV_INF(dev, "vdd vol_300 not supported");
		host_caps->vol_300_support = 0;
	}

	prop_sup = regulator_is_supported_voltage(cfg->regulator_vqmmc, 1800000, 1800000);
	if (host_caps->vol_180_support && !prop_sup) {
		host_caps->vol_180_support = 0;
		host_caps->sdr50_support = 0;
		host_caps->sdr104_support = 0;
		host_caps->ddr50_support = 0;
		LOG_DEV_INF(dev, "vdd vol_180 is not supported, drop sdr104, sdr50, ddr50 support");
	}

	if (host_caps->bus_8_bit_support && !cfg->bw_8bit) {
		LOG_DEV_INF(dev, "bus_8_bit_support not supported");
		host_caps->bus_8_bit_support = 0;
	}

	if (!sdhci_ctx->max_clk) {
		goto use_dt_freq;
	}

	if (!props->f_max || props->f_max > sdhci_ctx->max_clk) {
		LOG_DEV_INF(dev, "reset max bus frequency to %u from %u", sdhci_ctx->max_clk,
			    props->f_max);
		props->f_max = sdhci_ctx->max_clk;
	}

use_dt_freq:
	if (!props->f_max) {
		LOG_DEV_ERR(dev, "undefined max bus frequency");
		return -EINVAL;
	}

	sdhci_enable_auto_cmd12(sdhci_ctx, true);

	if (IS_ENABLED(CONFIG_BRCM_BCM2712_XFER_SDMA)) {
		ret = sdhci_enable_dma(sdhci_ctx, SDHC_DMA_SDMA);
		if (ret) {
			LOG_DEV_ERR(dev, "enable dma failed (%d)", ret);
		}
	}

	return ret;
}

static int bcm2712_sdhci_init_hw(const struct device *dev)
{
	const struct bcm2712_sdhci_config *cfg = dev->config;
	mm_reg_t cfg_base = DEVICE_MMIO_NAMED_GET(dev, cfg);
	uint32_t reg, base_clk_mhz;

	if (cfg->non_removable) {
		/* Force presence */
		reg = sys_read32(cfg_base + SDIO_CFG_CTRL);
		reg &= ~SDIO_CFG_CTRL_SDCD_N_TEST_LEV;
		reg |= SDIO_CFG_CTRL_SDCD_N_TEST_EN;
		sys_write32(reg, cfg_base + SDIO_CFG_CTRL);
	} else {
		/* Enable card detection line */
		reg = sys_read32(cfg_base + SDIO_CFG_SD_PIN_SEL);
		reg &= ~SDIO_CFG_SD_PIN_SEL_MASK;
		reg |= SDIO_CFG_SD_PIN_SEL_CARD;
		sys_write32(reg, cfg_base + SDIO_CFG_SD_PIN_SEL);
	}

	reg = sys_read32(cfg_base + SDIO_CFG_MAX_50MHZ_MODE);
	reg &= ~SDIO_CFG_MAX_50MHZ_MODE_ENABLE;
	reg |= SDIO_CFG_MAX_50MHZ_MODE_STRAP_OVERRIDE;
	sys_write32(reg, cfg_base + SDIO_CFG_MAX_50MHZ_MODE);

	/* Guesstimate the timer frequency (controller base clock) */
	base_clk_mhz = cfg->clk_freq / (1000 * 1000);
	reg = SDIO_CFG_CQ_CAPABILITY_FMUL | base_clk_mhz;
	sys_write32(reg, cfg_base + SDIO_CFG_CQ_CAPABILITY);
	return 0;
}

static int bcm2712_sdhci_init(const struct device *dev)
{
	const struct bcm2712_sdhci_config *cfg = DEV_CFG(dev);
	struct bcm2712_sdhci_data *data = DEV_DATA(dev);
	int ret;

	DEVICE_MMIO_NAMED_MAP(dev, host, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, cfg, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, busisol, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, lcpll, K_MEM_CACHE_NONE);

	data->sdhci_ctx.reg_base = DEVICE_MMIO_NAMED_GET(dev, host);

	if (cfg->gpio_cd.port) {
		if (!device_is_ready(cfg->regulator_vmmc)) {
			LOG_DEV_ERR(dev, "gpio is not ready");
			return -ENODEV;
		}

		ret = gpio_pin_configure_dt(&cfg->gpio_cd, GPIO_INPUT);
		if (ret) {
			LOG_DEV_ERR(dev, "gpio init failed (%d)", ret);
			return ret;
		}
	}

	if (!device_is_ready(cfg->regulator_vmmc)) {
		LOG_DEV_ERR(dev, "vmmc is not ready");
		return -ENODEV;
	}

	if (!device_is_ready(cfg->regulator_vqmmc)) {
		LOG_DEV_ERR(dev, "vqmmc is not ready");
		return -ENODEV;
	}

	ret = bcm2712_sdhci_init_host_props(dev);
	if (ret) {
		return ret;
	}

	ret = bcm2712_sdhci_init_hw(dev);
	if (ret) {
		LOG_DEV_ERR(dev, "hw init failed (%d)", ret);
		return ret;
	}

	ret = sdhci_init(&data->sdhci_ctx);
	if (ret) {
		LOG_DEV_ERR(dev, "sdhci init failed (%d)", ret);
		return ret;
	}

	if (regulator_is_enabled(cfg->regulator_vmmc)) {
		ret = regulator_disable(cfg->regulator_vmmc);
		if (ret) {
			LOG_DEV_ERR(dev, "vmmc disable failed (%d)", ret);
			return ret;
		}
	}

	if (regulator_is_enabled(cfg->regulator_vqmmc)) {
		ret = regulator_disable(cfg->regulator_vqmmc);
		if (ret) {
			LOG_DEV_ERR(dev, "vqmmc disable failed (%d)", ret);
			return ret;
		}
	}

	return 0;
}

#define BCM2712_SDHCI_INIT(inst)                                                                   \
	static const struct bcm2712_sdhci_config bcm2712_sdhci_config_##inst = {                   \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(host, DT_DRV_INST(inst)),                       \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(cfg, DT_DRV_INST(inst)),                        \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(busisol, DT_DRV_INST(inst)),                    \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(lcpll, DT_DRV_INST(inst)),                      \
                                                                                                   \
		.regulator_vqmmc = DEVICE_DT_GET(DT_INST_PHANDLE(inst, vqmmc_supply)),             \
		.regulator_vmmc = DEVICE_DT_GET(DT_INST_PHANDLE(inst, vmmc_supply)),               \
                                                                                                   \
		.gpio_cd = GPIO_DT_SPEC_INST_GET_OR(inst, cd_gpios, {0}),                          \
                                                                                                   \
		.clk_freq = DT_INST_PROP_BY_PHANDLE(inst, clocks, clock_frequency),                \
                                                                                                   \
		.hs200_mode = DT_INST_PROP_OR(inst, mmc_hs200_1_8v, 0),                            \
		.hs400_mode = DT_INST_PROP_OR(inst, mmc_hs400_1_8v, 0),                            \
		.bw_4bit = DT_INST_ENUM_HAS_VALUE(inst, bus_width, 4),                             \
		.bw_8bit = DT_INST_ENUM_HAS_VALUE(inst, bus_width, 8),                             \
		.max_bus_freq = DT_INST_PROP_OR(inst, max_bus_freq, 0),                            \
		.min_bus_freq = DT_INST_PROP_OR(inst, min_bus_freq, 400000),                       \
		.power_delay_ms = DT_INST_PROP_OR(inst, power_delay_ms, 500),                      \
		.non_removable = DT_INST_PROP_OR(inst, non_removable, 0),                          \
	};                                                                                         \
	static struct bcm2712_sdhci_data bcm2712_sdhci_data_##inst = {};                           \
	DEVICE_DT_INST_DEFINE(inst, &bcm2712_sdhci_init, NULL, &bcm2712_sdhci_data_##inst,         \
			      &bcm2712_sdhci_config_##inst, POST_KERNEL,                           \
			      CONFIG_SDHC_INIT_PRIORITY, &bcm2712_sdhci_api);

DT_INST_FOREACH_STATUS_OKAY(BCM2712_SDHCI_INIT)
