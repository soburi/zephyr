.. _xen_domd:

Xen DomD: snippet
#################

Overview
********

This snippet allows user to build Zephyr `xenvm` with RPI 5 hardware support as
a Xen hardware domain (DomD) to demonstrate how RPI 5 hardware can be passed to Xen domain.
Only GPIO LED is supported for now.

For example:

.. code-block:: console

   west build -b rpi_5 -S xen-domd samples/basic/blinky
