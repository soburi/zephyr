# SPDX-License-Identifier: Apache-2.0

set_property(GLOBAL PROPERTY CSTD "gnu11")

if(SOC_SERIES_RISCV_GIGADEVICE_GD32VF103)
  if (SOC_RISCV_GIGADEVICE_GD32VF103)
    zephyr_compile_options(-march=rv32imac -mcmodel=medlow)
  endif()
endif()

zephyr_sources()
zephyr_sources(empty.c)
#zephyr_sources(fe310_clock.c)
