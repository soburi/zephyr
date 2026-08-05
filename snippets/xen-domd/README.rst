.. _xen_domd:

Xen DomD: snippet
#################

Overview
********

This snippet builds Zephyr as a Xen hardware domain (DomD) with selected
physical devices enabled. The Xen domain configuration must assign the MMIO
ranges and interrupts used by those devices; the snippet only describes the
devices from Zephyr's point of view.

Raspberry Pi 5
**************

The Raspberry Pi 5 configuration enables the ACT LED connected to the always-on
GPIO controller. Build the blinky sample with:

.. code-block:: console

   west build -b rpi_5/bcm2712 -S xen-domd samples/basic/blinky

Sparrow Hawk R-Car V4H
**********************

The ``sparrowhawk_rcar_v4h/r8a779g0/a76`` configuration keeps SDHI0 and its
card-detect and voltage-control GPIOs enabled. It uses an emulated CPG clock
controller so the domain does not access the physical CPG registers. For
example:

.. code-block:: console

   west build -b sparrowhawk_rcar_v4h/r8a779g0/a76 -S xen-domd samples/subsys/fs/fs_sample
