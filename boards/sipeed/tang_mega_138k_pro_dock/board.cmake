# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_XIP)
  dt_chosen(code_partition PROPERTY "zephyr,code-partition")
  dt_reg_addr(code_partition_addr PATH "${code_partition}")
  math(EXPR code_partition_offset
       "${code_partition_addr} + (${CONFIG_BUILD_OUTPUT_ADJUST_LMA})"
       OUTPUT_FORMAT HEXADECIMAL
  )

  board_runner_args(openfpgaloader
    --board tangmega138k
    --external-flash
    --offset=${code_partition_offset}
    --verify
  )

  include(${ZEPHYR_BASE}/boards/common/openfpgaloader.board.cmake)
  board_runner_args(openocd --no-load)
else()
  # Non-XIP images are loaded directly into RAM by OpenOCD.
  set(OPENOCD_USE_LOAD_IMAGE YES)
  board_runner_args(openocd --file-type=elf)
endif()

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
