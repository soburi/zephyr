# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd "--cmd-pre-load=gd32vf103-pre-load")
board_runner_args(openocd "--cmd-load=gd32vf103-load")
board_runner_args(openocd "--cmd-post-verify=gd32vf103-post-verify")

if(${CONFIG_BOARD} STREQUAL "sipeed_longan_nano")
  board_runner_args(jlink "--device=GD32VF103CBT6" "--iface=JTAG" "--tool-opt=-JTAGConf -1,-1" "--speed=4000")
else()
  board_runner_args(jlink "--device=GD32VF103C8T6" "--iface=JTAG" "--tool-opt=-JTAGConf -1,-1" "--speed=4000")
endif()

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
