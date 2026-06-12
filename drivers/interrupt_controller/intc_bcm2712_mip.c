/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Broadcom BCM2712 MSI-X Interrupt Peripheral (MIP).
 *
 * The MIP turns inbound PCIe MSI/MSI-X memory writes into GIC SPIs: a
 * write of data value N to the MIP's PCIe target address raises SPI
 * (brcm,msi-base-spi + N). There is no per-interrupt runtime control
 * here; masking is done at the GIC. This driver only performs the
 * one-time setup: route all vectors to the host (not the VPU) and
 * configure them as edge-triggered.
 */

#define DT_DRV_COMPAT brcm_bcm2712_mip

#include <zephyr/arch/cpu.h>
#include <zephyr/device.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>

#define MIP_INT_RAISE        0x00
#define MIP_INT_CLEAR        0x10
#define MIP_INT_CFGL_HOST    0x20
#define MIP_INT_CFGH_HOST    0x30
#define MIP_INT_MASKL_HOST   0x40
#define MIP_INT_MASKH_HOST   0x50
#define MIP_INT_MASKL_VPU    0x60
#define MIP_INT_MASKH_VPU    0x70
#define MIP_INT_STATUSL_HOST 0x80
#define MIP_INT_STATUSH_HOST 0x90

struct mip_config {
	DEVICE_MMIO_ROM;
};

struct mip_data {
	DEVICE_MMIO_RAM;
};

static int mip_init(const struct device *dev)
{
	mm_reg_t base;

	DEVICE_MMIO_MAP(dev, K_MEM_CACHE_NONE);
	base = DEVICE_MMIO_GET(dev);

	/* All MSI-X unmasked for the host, masked for the VPU, and edge-triggered */
	sys_write32(0, base + MIP_INT_MASKL_HOST);
	sys_write32(0, base + MIP_INT_MASKH_HOST);
	sys_write32(0xffffffff, base + MIP_INT_MASKL_VPU);
	sys_write32(0xffffffff, base + MIP_INT_MASKH_VPU);
	sys_write32(0xffffffff, base + MIP_INT_CFGL_HOST);
	sys_write32(0xffffffff, base + MIP_INT_CFGH_HOST);

	return 0;
}

#define MIP_INIT(n)                                                                                \
	static struct mip_data mip_data_##n;                                                       \
                                                                                                   \
	static const struct mip_config mip_cfg_##n = {                                             \
		DEVICE_MMIO_ROM_INIT(DT_DRV_INST(n)),                                              \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, mip_init, NULL, &mip_data_##n, &mip_cfg_##n, PRE_KERNEL_1,        \
			      CONFIG_INTC_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(MIP_INIT)
