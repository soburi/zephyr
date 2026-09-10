# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0

board_runner_args(openfpgaloader
  --board tangmega138k
  --external-flash
  --offset=0x600000
  --verify)

board_runner_args(openocd --no-load)

include(${ZEPHYR_BASE}/boards/common/openfpgaloader.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
