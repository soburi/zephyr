.. _fpb_ra6e1:

Renesas RA6E1 Fast Protptyping Board
#######################

Overview
********

The RA6E1 Fast Protptyping Board is a development board with a
Renesas RA6E1 SoC.

Programming and debugging
*************************

Building & Flashing
===================

Here is an example for building the :ref:`blinky-sample` application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: fpb_ra6e1
   :goals: build

The default runner tries to flash the board via an external programmer using openocd.
To flash via the USB port, select the DFU runner when flashing:

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: fpb_ra6e1
   :goals: flash

Debugging
=========

You can debug an application in the usual way.  Here is an example for the
:ref:`blinky-sample` application.

.. zephyr-app-commands::
   :zephyr-app: samples/basic/blinky
   :board: longan_nano
   :maybe-skip-config:
   :goals: debug
