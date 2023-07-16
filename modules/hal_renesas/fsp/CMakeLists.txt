# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

if (CONFIG_HAS_FSP)

string(TOUPPER BSP_MCU_${CONFIG_SOC} BSP_MCU_MODEL)
zephyr_compile_definitions(${BSP_MCU_MODEL})

zephyr_include_directories(ra_cfg/fsp_cfg)
zephyr_include_directories(ra_cfg/fsp_cfg/bsp)
zephyr_include_directories(ra_cfg/fsp_cfg/bsp/${CONFIG_SOC_SERIES})
zephyr_include_directories(ra_cfg/ra/fsp/inc)
zephyr_include_directories(ra_cfg/ra/fsp/inc/api)
zephyr_include_directories(ra_cfg/ra/fsp/src/bsp/mcu/all)

endif()
