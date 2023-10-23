/*
 * Copyright (c) 2023 Chen Xingyu <hi@xingrz.me>
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT wch_ch56x_hclk

#include <zephyr/arch/cpu.h>
#include <zephyr/sys/util.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/ch56x.h>
#include <zephyr/drivers/syscon.h>

#define SYSCON_SAFE_ACCRESS_SIG (0x00)
#define SYSCON_CLK_PLL_DIV      (0x08)
#define SYSCON_CLK_CFG_CTRL     (0x0A)
#define SYSCON_SLP_CLK          (0x0C)

/* SYSCON_SAFE_ACCRESS_SIG Register */
#define SYSCON_SAFE_ACCRESS_SIG_KEY_1 (0x57)
#define SYSCON_SAFE_ACCRESS_SIG_KEY_2 (0xA8)

/* SYSCON_CLK_CFG_CTRL Register */
#define SYSCON_CLK_SEL_PLL BIT(1)
#define SYSCON_CLK_SEL_OSC (0)

#define DEV_CFG(dev) ((const struct clock_control_ch56x_config *const)(dev)->config)

struct clock_control_ch56x_config {
	const struct device *syscon;
	uint32_t hclk_freq;
};

static inline void syscon_read_reg_unlock(const struct device *syscon, uint16_t reg, uint32_t *val)
{
	syscon_write_reg(syscon, SYSCON_SAFE_ACCRESS_SIG, SYSCON_SAFE_ACCRESS_SIG_KEY_1);
	syscon_write_reg(syscon, SYSCON_SAFE_ACCRESS_SIG, SYSCON_SAFE_ACCRESS_SIG_KEY_2);
	syscon_read_reg(syscon, reg, val);
}

static inline void syscon_write_reg_unlock(const struct device *syscon, uint16_t reg, uint32_t val)
{
	syscon_write_reg(syscon, SYSCON_SAFE_ACCRESS_SIG, SYSCON_SAFE_ACCRESS_SIG_KEY_1);
	syscon_write_reg(syscon, SYSCON_SAFE_ACCRESS_SIG, SYSCON_SAFE_ACCRESS_SIG_KEY_2);
	syscon_write_reg(syscon, reg, val);
}

static inline void syscon_relock(const struct device *syscon)
{
	syscon_write_reg(syscon, SYSCON_SAFE_ACCRESS_SIG, 0x00);
}

static inline bool check_dev_config(const struct ch56x_clock_dev_config *ccfg)
{
	return ccfg->offset <= 1 && ccfg->bit < 8;
}

static int clock_control_ch56x_on(const struct device *dev, clock_control_subsys_t sys)
{
	const struct clock_control_ch56x_config *cfg = DEV_CFG(dev);
	const struct ch56x_clock_dev_config *ccfg = (const struct ch56x_clock_dev_config *)sys;
	uint32_t regval;

	if (!check_dev_config(ccfg)) {
		return -EINVAL;
	}

	syscon_read_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + ccfg->offset, &regval);
	regval &= ~BIT(ccfg->bit);
	syscon_write_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + ccfg->offset, regval);

	syscon_relock(cfg->syscon);

	return 0;
}

static int clock_control_ch56x_off(const struct device *dev, clock_control_subsys_t sys)
{
	const struct clock_control_ch56x_config *cfg = DEV_CFG(dev);
	const struct ch56x_clock_dev_config *ccfg = (const struct ch56x_clock_dev_config *)sys;
	uint32_t regval;

	if (!check_dev_config(ccfg)) {
		return -EINVAL;
	}

	syscon_read_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + ccfg->offset, &regval);
	regval |= BIT(ccfg->bit);
	syscon_write_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + ccfg->offset, regval);

	syscon_relock(cfg->syscon);

	return 0;
}

static int clock_control_ch56x_get_rate(const struct device *dev, clock_control_subsys_t sys,
					uint32_t *rate)
{
	const struct clock_control_ch56x_config *cfg = DEV_CFG(dev);

	ARG_UNUSED(sys);

	*rate = cfg->hclk_freq;

	return 0;
}

static enum clock_control_status clock_control_ch56x_get_status(const struct device *dev,
								clock_control_subsys_t sys)
{
	const struct clock_control_ch56x_config *cfg = DEV_CFG(dev);
	const struct ch56x_clock_dev_config *ccfg = (const struct ch56x_clock_dev_config *)sys;
	uint32_t regval;

	if (!check_dev_config(ccfg)) {
		return CLOCK_CONTROL_STATUS_UNKNOWN;
	}

	syscon_read_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + ccfg->offset, &regval);

	syscon_relock(cfg->syscon);

	if (regval & BIT(ccfg->bit)) {
		return CLOCK_CONTROL_STATUS_OFF;
	} else {
		return CLOCK_CONTROL_STATUS_ON;
	}
}

static int clock_control_ch56x_init(const struct device *dev)
{
	const struct clock_control_ch56x_config *cfg = DEV_CFG(dev);
	uint32_t source, divider;

	switch (cfg->hclk_freq) {
	case MHZ(2):
		source = SYSCON_CLK_SEL_OSC;
		/* For 2MHz, divider is 0 */
		divider = 0;
		break;
	case MHZ(15):
		source = SYSCON_CLK_SEL_OSC;
		divider = 2;
		break;
	case MHZ(30):
		source = SYSCON_CLK_SEL_PLL;
		/* For 30MHz, divider is 0 */
		divider = 0;
		break;
	case MHZ(60):
		source = SYSCON_CLK_SEL_PLL;
		divider = 8;
		break;
	case MHZ(80):
		source = SYSCON_CLK_SEL_PLL;
		divider = 6;
		break;
	case MHZ(96):
		source = SYSCON_CLK_SEL_PLL;
		divider = 5;
		break;
	case MHZ(120):
		source = SYSCON_CLK_SEL_PLL;
		divider = 4;
		break;
	default:
		return -EINVAL;
	}

	/* Disable all pheripheral clocks by default */
	syscon_write_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + 0, UINT8_MAX);
	syscon_write_reg_unlock(cfg->syscon, SYSCON_SLP_CLK + 1, UINT8_MAX);

	/* Configure HCLK source and divider */
	syscon_write_reg_unlock(cfg->syscon, SYSCON_CLK_PLL_DIV, 0x40 | divider);
	syscon_write_reg_unlock(cfg->syscon, SYSCON_CLK_CFG_CTRL, 0x80 | source);

	syscon_relock(cfg->syscon);

	return 0;
}

static const struct clock_control_driver_api clock_control_ch56x_api = {
	.on = clock_control_ch56x_on,
	.off = clock_control_ch56x_off,
	.get_rate = clock_control_ch56x_get_rate,
	.get_status = clock_control_ch56x_get_status,
};

#define CLOCK_CONTROL_CH56X_INST(n)                                                                \
	static const struct clock_control_ch56x_config clock_control_ch56x_cfg_##n = {             \
		.syscon = DEVICE_DT_GET(DT_INST_PARENT(n)),                                        \
		.hclk_freq = DT_INST_PROP(n, clock_frequency),                                     \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, clock_control_ch56x_init, NULL, NULL,                             \
			      &clock_control_ch56x_cfg_##n, PRE_KERNEL_1,                          \
			      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &clock_control_ch56x_api);

DT_INST_FOREACH_STATUS_OKAY(CLOCK_CONTROL_CH56X_INST)
