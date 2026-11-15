# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_XIP)
  set(flash_offset 0x600000)

  if(DEFINED ENV{TANG_MEGA_138K_PRO_DOCK_FLASH_OFFSET})
    set(flash_offset "$ENV{TANG_MEGA_138K_PRO_DOCK_FLASH_OFFSET}")
  elseif(CONFIG_FLASH_USES_MAPPED_PARTITION)
    dt_chosen(code_partition_path PROPERTY "zephyr,code-partition")
    dt_reg_addr(code_partition_addr PATH "${code_partition_path}")
    dt_chosen(flash_path PROPERTY "zephyr,flash")
    dt_reg_addr(flash_addr PATH "${flash_path}")
    math(EXPR flash_offset
      "${flash_offset} + ${code_partition_addr} - ${flash_addr}"
      OUTPUT_FORMAT HEXADECIMAL
    )
  elseif(CONFIG_FLASH_LOAD_OFFSET)
    math(EXPR flash_offset "${flash_offset} + ${CONFIG_FLASH_LOAD_OFFSET}"
      OUTPUT_FORMAT HEXADECIMAL
    )
  endif()

  board_runner_args(openfpgaloader
    --board tangmega138k
    --external-flash
    --offset=${flash_offset}
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
