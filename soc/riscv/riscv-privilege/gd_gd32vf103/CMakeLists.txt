# Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
# SPDX-License-Identifier: Apache-2.0

zephyr_sources(entry.S)
zephyr_sources(soc.c)
zephyr_sources(riscv/source/system_gd32vf103.c)
zephyr_include_directories(./standard_peripheral/include)
zephyr_include_directories(./riscv/include)
zephyr_include_directories(./riscv/drivers)
zephyr_compile_definitions(HXTAL_VALUE=8000000 HXTAL_VALUE_25M=8000000)


