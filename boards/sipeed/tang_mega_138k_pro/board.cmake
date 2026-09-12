# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0

if(CONFIG_BOARD_TANG_MEGA_138K_PRO_AE350_DEMO)
  set(TANG_MEGA_138K_PRO_AE350_FLASH_OFFSET 0x600000)
endif()

board_runner_args(openfpgaloader
  --board tangmega138k
  --external-flash
  --offset=${TANG_MEGA_138K_PRO_AE350_FLASH_OFFSET}
  --verify)

board_runner_args(openocd --no-load)

include(${ZEPHYR_BASE}/boards/common/openfpgaloader.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
