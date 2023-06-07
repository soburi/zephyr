# Copyright (c) 2023 Ionut Pavel <iocapa@iocapa.com>
# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources_ifdef(CONFIG_PIO_RPI_PICO pio_rpi_pico.c)
zephyr_library_sources_ifdef(CONFIG_PIO_INTR_DISPATCHER_RPI_PICO pio_intr_dispatcher_rpi_pico.c)
