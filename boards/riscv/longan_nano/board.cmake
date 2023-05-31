# Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
# SPDX-License-Identifier: Apache-2.0

if ("${LONGAN_NANO_DEBUG_ADAPTER}" STREQUAL "")
	set(LONGAN_NANO_DEBUG_ADAPTER um232h)
endif()

board_runner_args(openocd --cmd-pre-init "source [find interface/ftdi/${LONGAN_NANO_DEBUG_ADAPTER}.cfg]")
board_runner_args(openocd --cmd-pre-init "source [find target/gd32vf103.cfg]")
board_runner_args(openocd --cmd-pre-init "adapter speed 2000")


board_runner_args(openocd "--cmd-pre-load=gd32vf103-pre-load")
board_runner_args(openocd "--cmd-load=gd32vf103-load")
board_runner_args(openocd "--cmd-post-verify=gd32vf103-post-verify")
board_runner_args(dfu-util "--pid=28e9:0189" "--alt=0" "--dfuse")

include(${ZEPHYR_BASE}/boards/common/openocd.board.cmake)
include(${ZEPHYR_BASE}/boards/common/dfu-util.board.cmake)
