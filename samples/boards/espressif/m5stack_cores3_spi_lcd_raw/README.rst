.. _m5stack_cores3_spi_lcd_raw:

M5Stack CoreS3 raw SPI LCD sample
=================================

Overview
--------

This sample shows how to talk to the ILI9342C based display that is fitted to
M5Stack CoreS3 without using Zephyr's display drivers.  It enables the
Espressif SPI controller 3-wire mode so that 9-bit transfers (command bit plus
8 data bits) can be issued directly.  After a minimal initialization sequence a
simple RGB gradient is written to the screen to demonstrate how to pack the
command/data bit into the transmit stream.

Requirements
------------

* `M5Stack CoreS3 <https://docs.m5stack.com/en/core/CoreS3>`_

Building and running
--------------------

Use the following command to build and flash the sample::

   west build -b m5stack_cores3/esp32s3/procpu \
     samples/boards/espressif/m5stack_cores3_spi_lcd_raw --pristine
   west flash

After programming the board and resetting it the display should shortly show a
full-screen colour gradient and the console will print status messages similar
to::

   *** Booting Zephyr OS build v3.x.x ***
   [00:00:00.025,000] <inf> spi_lcd_raw: Resetting LCD
   [00:00:00.320,000] <inf> spi_lcd_raw: Drawing gradient
   [00:00:00.750,000] <inf> spi_lcd_raw: Gradient complete

The SD card slot is disabled by the sample overlay while the display is driven
in half-duplex 3-wire mode.
