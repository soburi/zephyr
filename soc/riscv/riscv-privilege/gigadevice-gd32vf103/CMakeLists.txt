# SPDX-License-Identifier: Apache-2.0

if (SOC_RISCV_GIGADEVICE_GD32VF103)
  zephyr_compile_options(-march=rv32imac -mcmodel=medlow)
endif()

zephyr_include_directories(GD32VF103_standard_peripheral/)
zephyr_include_directories(GD32VF103_standard_peripheral/Include)
zephyr_include_directories(RISCV/drivers)
zephyr_include_directories(${ZEPHYR_BASE}/drivers)

zephyr_compile_definitions(HXTAL_VALUE=8000000U)

zephyr_sources()
zephyr_sources(entry.S)
zephyr_sources(gd32vf103_init.c)

