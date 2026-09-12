# SPDX-FileCopyrightText: Copyright (c) 2026 TOKITA Hiroshi
# SPDX-License-Identifier: Apache-2.0

# The bitstream decides where in the configuration flash the image goes, so the code partition
# of the variant devicetree holds the offset openFPGALoader writes at. Its reg is chip relative,
# unlike the mapped address dt_reg_addr() returns. A variant that describes no code partition
# gets no flash runner: offset 0, the only remaining default, is where the bitstream lives.
dt_chosen(code_partition PROPERTY "zephyr,code-partition")

if(code_partition)
  dt_prop(code_partition_reg PATH "${code_partition}" PROPERTY "reg")
  list(GET code_partition_reg 0 code_partition_offset)
  math(EXPR code_partition_offset "${code_partition_offset}" OUTPUT_FORMAT HEXADECIMAL)

  board_runner_args(openfpgaloader
    --board tangmega138k
    --external-flash
    --offset=${code_partition_offset}
    --verify)

  include(${ZEPHYR_BASE}/boards/common/openfpgaloader.board.cmake)
endif()

board_runner_args(openocd --no-load)

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
