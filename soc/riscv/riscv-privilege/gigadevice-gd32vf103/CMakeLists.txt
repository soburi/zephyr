# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(GD32VF103_standard_peripheral/)
zephyr_include_directories(GD32VF103_standard_peripheral/Include)
zephyr_include_directories(RISCV/drivers)
zephyr_include_directories(${ZEPHYR_BASE}/drivers)

zephyr_compile_definitions(HXTAL_VALUE=8000000U)

zephyr_sources()
zephyr_sources(entry.S)
zephyr_sources(gd32vf103_init.c)

