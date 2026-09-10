.. zephyr:board:: tang_mega_138k_pro

Overview
********

The Sipeed Tang Mega 138K Pro Dock can run the Gowin ``ae350_demo`` FPGA
reference design.  The design contains a single 800 MHz AndesCore CPU,
256 MiB of DDR3 memory, and 50 MHz AHB and APB buses.

Supported Features
******************

The board configuration supports the peripherals enabled by the
``ae350_demo`` FPGA IP configuration:

- UART2 as the console
- Three LEDs and three keys through the AE350 GPIO controller
- AE350 platform interrupt controller and machine timer
- AE350 PIT and watchdog

The FPGA image also contains an RTC and a dedicated SPI flash instruction
memory interface.  Zephyr does not currently provide an RTC driver for this
AE350 block, and the flash interface is not exposed as an ATCSPI200 controller.

.. zephyr:board-supported-hw::

Building
********

Build the :zephyr:code-sample:`hello_world` sample with:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :board: tang_mega_138k_pro/ae350/demo
   :goals: build

Programming and Debugging
*************************

Load ``build/zephyr/zephyr.elf`` into DDR3 through an Andes-compatible JTAG
debugger.  Flash and debug runners are not defined for this board.
