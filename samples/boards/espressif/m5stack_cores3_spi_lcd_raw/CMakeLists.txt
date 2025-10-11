cmake_minimum_required(VERSION 3.20.0)

set(DTC_OVERLAY_FILE boards/m5stack_cores3.overlay)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(m5stack_cores3_spi_lcd_raw)

target_sources(app PRIVATE src/main.c)
