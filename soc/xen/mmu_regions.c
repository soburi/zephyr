/*
 * Copyright (c) 2020-2022 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/devicetree.h>
#include <zephyr/sys/util.h>

#if defined(CONFIG_ARM64)
#include <zephyr/arch/arm64/arm_mmu.h>
#define XEN_GIC_ATTR (MT_DEVICE_nGnRnE | MT_P_RW_U_NA | MT_NS)
#elif defined(CONFIG_ARM)
#include <zephyr/arch/arm/mmu/arm_mmu.h>
#define XEN_GIC_ATTR (MT_DEVICE | MPERM_R | MPERM_W | MATTR_NON_SECURE)
#else
#error "Unsupported architecture for Xen MMU region configuration"
#endif

static const struct arm_mmu_region mmu_regions[] = {

        MMU_REGION_FLAT_ENTRY("GIC",
                              DT_REG_ADDR_BY_IDX(DT_INST(0, arm_gic), 0),
                              DT_REG_SIZE_BY_IDX(DT_INST(0, arm_gic), 0),
                              XEN_GIC_ATTR),

        MMU_REGION_FLAT_ENTRY("GIC",
                              DT_REG_ADDR_BY_IDX(DT_INST(0, arm_gic), 1),
                              DT_REG_SIZE_BY_IDX(DT_INST(0, arm_gic), 1),
                              XEN_GIC_ATTR),
};

const struct arm_mmu_config mmu_config = {
	.num_regions = ARRAY_SIZE(mmu_regions),
	.mmu_regions = mmu_regions,
};
