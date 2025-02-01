.. zephyr:board:: pico_plus2

Overview
********

The Pimoroni Pico Plus 2 is a compact and versatile board featuring the Raspberry Pi RP2350B SoC.
It includes USB Type-C, Qw/ST connectors, SP/CE connectors, a debug connector, a reset button,
and a BOOT button.

Hardware
********

- Dual Cortex-M33 or Hazard3 processors at up to 150MHz
- 520KB of SRAM, and 4MB of on-board flash memory
- 16MB of on-board QSPI flash (supports XiP)
- 8MB of PSRAM
- USB 1.1 with device and host support
- Low-power sleep and dormant modes
- Drag-and-drop programming using mass storage over USB
- 48 multi-function GPIO pins including 8 that can be used for ADC
- 2 SPI, 2 I2C, 2 UART, 3 12-bit 500ksps Analogue to Digital - Converter (ADC), 24 controllable PWM channels
- 2 Timer with 4 alarms, 1 AON Timer
- Temperature sensor
- 3 Programmable IO (PIO) blocks, 12 state machines total for custom peripheral support
- USB-C connector for power, programming, and data transfer
- Qw/ST (Qwiic/STEMMA QT) connector
- SP/CE connector
- 3-pin debug connector (JST-SH)
- Reset button and BOOT button (BOOT button also usable as a user switch)

.. figure:: img/pico_plus2.webp
     :align: center
     :alt: Pimoroni Pico Plus2

     Pimoroni Pico Plus2 (Image courtesy of Pimoroni)

Supported Features
==================

The Pimoroni Pico Plus 2 supports the following hardware features:

.. list-table::
   :header-rows: 1

   * - Peripheral
     - Kconfig option
     - Devicetree compatible
   * - NVIC
     - N/A
     - :dtcompatible:`arm,v8m-nvic`
   * - ADC
     - :kconfig:option:`CONFIG_ADC`
     - :dtcompatible:`raspberrypi,pico-adc`
   * - Clock controller
     - :kconfig:option:`CONFIG_CLOCK_CONTROL`
     - :dtcompatible:`raspberrypi,pico-clock-controller`
   * - Counter
     - :kconfig:option:`CONFIG_COUNTER`
     - :dtcompatible:`raspberrypi,pico-timer`
   * - DMA
     - :kconfig:option:`CONFIG_DMA`
     - :dtcompatible:`raspberrypi,pico-dma`
   * - GPIO
     - :kconfig:option:`CONFIG_GPIO`
     - :dtcompatible:`raspberrypi,pico-gpio`
   * - HWINFO
     - :kconfig:option:`CONFIG_HWINFO`
     - N/A
   * - I2C
     - :kconfig:option:`CONFIG_I2C`
     - :dtcompatible:`snps,designware-i2c`
   * - PWM
     - :kconfig:option:`CONFIG_PWM`
     - :dtcompatible:`raspberrypi,pico-pwm`
   * - SPI
     - :kconfig:option:`CONFIG_SPI`
     - :dtcompatible:`raspberrypi,pico-spi`
   * - UART
     - :kconfig:option:`CONFIG_SERIAL`
     - :dtcompatible:`raspberrypi,pico-uart`
   * - USB Device
     - :kconfig:option:`CONFIG_USB_DEVICE_STACK`
     - :dtcompatible:`raspberrypi,pico-usbd`
   * - Watchdog Timer (WDT)
     - :kconfig:option:`CONFIG_WATCHDOG`
     - :dtcompatible:`raspberrypi,pico-watchdog`

Programming and Debugging
*************************

Flashing
========

Using OpenOCD
-------------

The overall explanation regarding flashing and debugging is the same as or
:zephyr:board:`rpi_pico`.
See :ref:`rpi_pico_flashing_using_openocd`. in ``rpi_pico`` documentation.

A typical build command for pico_plus2 is as follows.
This assumes a CMSIS-DAP adapter such as the RaspberryPi Debug Probe,
but if you are using something else, specify ``RPI_PICO_DEBUG_ADAPTER``.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: pico_plus2
   :goals: build flash
   :gen-args: -DOPENOCD=/usr/local/bin/openocd

Using UF2
---------

If you don't have an SWD adapter, you can flash the Raspberry Pi Pico with
a UF2 file. By default, building an app for this board will generate a
:file:`build/zephyr/zephyr.uf2` file. If the Pico is powered on with the ``BOOTSEL``
button pressed, it will appear on the host as a mass storage device. The
UF2 file should be drag-and-dropped to the device, which will flash the Pico.

Debugging
=========

Like flashing, debugging can also be performed using Zephyr's usual way.
The following sample shows how to debug using OpenOCD and
the ``RaspberryPi Debug Probe``. The default is ``openocd``.

If you use another tool, please specify your debugging tool,
such as ``jlink`` with the ``--runner`` option.

Using OpenOCD
-------------

Install OpenOCD as described for flashing the board.

Here is an example for debugging the :zephyr:code-sample:`blinky` application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: pico_plus2
   :maybe-skip-config:
   :goals: debug
   :gen-args: -DOPENOCD=/usr/local/bin/openocd

.. target-notes::

.. _Pimoroni Pico Plus 2:
  https://shop.pimoroni.com/products/pimoroni-pico-plus-2
