/*
 * Copyright (c) 2025 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_rcar_cpg_mssr_emul

#include <errno.h>
#include <zephyr/arch/cpu.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/renesas_cpg_mssr.h>
#include <zephyr/dt-bindings/clock/renesas_cpg_mssr.h>
#include <zephyr/irq.h>
#include "clock_control_renesas_cpg_mssr.h"

static int rcar_cpg_mssr_emul_start(const struct device *dev, clock_control_subsys_t sys)
{
	return 0;
}

static int rcar_cpg_mssr_emul_stop(const struct device *dev, clock_control_subsys_t sys)
{
	return 0;
}

static int rcar_cpg_mssr_emul_get_rate(const struct device *dev, clock_control_subsys_t sys,
				       uint32_t *rate)
{
	return 0;
}

static int rcar_cpg_mssr_emul_set_rate(const struct device *dev, clock_control_subsys_t sys,
				       clock_control_subsys_rate_t rate)
{
	return 0;
}

static const struct clock_control_driver_api rcar_cpg_mssr_emul_api = {
	.on = rcar_cpg_mssr_emul_start,
	.off = rcar_cpg_mssr_emul_stop,
	.get_rate = rcar_cpg_mssr_emul_get_rate,
	.set_rate = rcar_cpg_mssr_emul_set_rate,
};

#define RCAR_CPG_MSSR_EMUL_INIT(inst)                                                              \
	DEVICE_DT_INST_DEFINE(inst, NULL, NULL, NULL, NULL, PRE_KERNEL_1,                          \
			      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &rcar_cpg_mssr_emul_api);

DT_INST_FOREACH_STATUS_OKAY(RCAR_CPG_MSSR_EMUL_INIT)
