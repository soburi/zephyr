# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

if (CONFIG_HAS_FSP)

string(TOUPPER BSP_MCU_${CONFIG_SOC} BSP_MCU_MODEL)
zephyr_compile_definitions(${BSP_MCU_MODEL})


zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/inc)
zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/inc/api)
zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/inc/instances)

zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src)
zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/cmsis/Device/RENESAS/Include)
zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all)

zephyr_include_directories(ra_cfg/fsp_cfg)
zephyr_include_directories(ra_cfg/fsp_cfg/bsp)
zephyr_include_directories(ra_cfg/fsp_cfg/bsp/${CONFIG_SOC_SERIES})
zephyr_include_directories(ra_gen)

zephyr_library_sources(ra_gen/board_init.c)
zephyr_library_sources(ra_gen/common_data.c)
zephyr_library_sources(ra_gen/hal_data.c)

zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_clocks.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_common.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_delay.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_group_irq.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_guard.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_io.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_irq.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_register_protection.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_rom_registers.c)
zephyr_library_sources(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/fsp/ra/fsp/src/bsp/mcu/all/bsp_security.c)

endif()
