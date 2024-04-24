.. _rpi_5_xen_domd:

RPI 5 Xen DomD: snippet for XEN HW domain
#########################################

Overview
********

This snippet allows user to build Zephyr `xenvm` with RPI 5 hardware support as
a Xen hardware domain (DomD) to demonstrate how RPI 5 hardware can be passed to Xen domain.
Only GPIO LED is supported for now.

For example:

.. code-block:: console

   west build -b xenvm -S rpi_5_xen_domd samples/basic/blinky
