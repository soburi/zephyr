# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0

# The offset the image is written at within the configuration flash is a property of the
# bitstream, so it is described by the variant devicetree. Without it there is no safe offset
# to write at: offset 0 holds the bitstream itself.
dt_nodelabel(xip_partition NODELABEL "xip_partition")

if(xip_partition)
  dt_reg_addr(xip_offset PATH "${xip_partition}")
  math(EXPR xip_offset "${xip_offset}" OUTPUT_FORMAT HEXADECIMAL)

  board_runner_args(openfpgaloader
    --board tangmega138k
    --external-flash
    --offset=${xip_offset}
    --verify)

  include(${ZEPHYR_BASE}/boards/common/openfpgaloader.board.cmake)
endif()

board_runner_args(openocd --no-load)

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
