# SPDX-License-Identifier: Apache-2.0

if(SOC_SERIES_RISCV_GIGADEVICE_GD32VF103)
  if (SOC_RISCV_GIGADEVICE_GD32VF103)
    zephyr_compile_options(-march=rv32imac -mcmodel=medlow)
  endif()
endif()

zephyr_sources()
zephyr_sources(entry.S)
zephyr_sources(gd32vf103_init.c)

