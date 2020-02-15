# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd "--cmd-pre-load=gd32vf103-pre-load")
board_runner_args(openocd "--cmd-load=gd32vf103-load")
board_runner_args(openocd "--cmd-post-verify=gd32vf103-post-verify")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
