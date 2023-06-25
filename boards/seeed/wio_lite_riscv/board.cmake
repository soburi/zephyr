# Copyright (c) 2023 Tada, Hiroki <hirokin18931@gmail.copm>
# SPDX-License-Identifier: Apache-2.0

board_runner_args(openocd --cmd-pre-init "source [find target/gd32vf103.cfg]")

board_runner_args(openocd "--cmd-pre-load=gd32vf103-pre-load")
board_runner_args(openocd "--cmd-load=gd32vf103-load")
board_runner_args(openocd "--cmd-post-verify=gd32vf103-post-verify")
board_runner_args(dfu-util "--pid=28e9:0189" "--alt=0" "--dfuse")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/dfu-util.board.cmake)
