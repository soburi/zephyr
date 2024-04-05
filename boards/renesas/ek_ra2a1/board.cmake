# Copyright (c) 2024 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

board_runner_args(pyocd "--target=r7fa2a1ab")

board_runner_args(jlink "--device=R7FA2A1AB"
			"--speed=4000"
			"--iface=SWD"
			"--reset-after-load")

include(${ZEPHYR_BASE}/boards/common/jlink.board.cmake)
include(${ZEPHYR_BASE}/boards/common/pyocd.board.cmake)
