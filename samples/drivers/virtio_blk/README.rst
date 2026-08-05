.. zephyr:code-sample:: virtio-blk
   :name: VirtIO block disk
   :relevant-api: disk_access_interface

   Access a VirtIO block device through the Disk Access API.

Overview
********

This sample demonstrates how to access a VirtIO block device through the
:ref:`Disk Access API <disk_access_api>`. It reports the disk geometry, saves
one sector, writes and reads back a test pattern, and then restores the original
sector contents.

The sample supports both VirtIO transports available in Zephyr. The
``qemu_x86_64`` configuration uses VirtIO PCI, while ``qemu_cortex_a53`` uses
VirtIO MMIO.

Building and Running
********************

Build and run the sample with VirtIO PCI as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/virtio_blk
   :board: qemu_x86_64
   :goals: run
   :compact:

To exercise the VirtIO MMIO transport instead, use ``qemu_cortex_a53``:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/virtio_blk
   :board: qemu_cortex_a53
   :goals: run
   :compact:

The QEMU integration creates a raw disk image in the build directory and
attaches it to the emulated VirtIO block device. Its size can be changed with
:kconfig:option:`CONFIG_QEMU_VIRTIO_BLK_DISK_SIZE`.

xenvm
=====

The ``xenvm/xenvm/gicv3`` configuration uses the first Xen Arm VirtIO-MMIO
slot at ``0x02000000`` and IRQ 33. Build it with:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/virtio_blk
   :board: xenvm/xenvm/gicv3
   :goals: build
   :compact:

The Xen domain configuration must provide a writable VirtIO block device at
that slot. As with the other Xen configurations, the frontend currently uses
guest physical DMA addresses and does not implement Xen grant DMA addresses;
grant-backed backends therefore require additional frontend support.

Sparrow Hawk R-Car V4H
======================

The ``sparrowhawk_rcar_v4h/r8a779g0/a76`` configuration runs as a Xen guest;
the physical board does not provide a VirtIO device when booted bare metal.
Build it with the ``xen-guest`` snippet:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/virtio_blk
   :board: sparrowhawk_rcar_v4h/r8a779g0/a76
   :snippets: xen-guest
   :goals: build
   :compact:

The board overlay describes the first Xen Arm VirtIO-MMIO slot at
``0x02000000`` with IRQ 33. The VirtIO block disk must therefore be the first
VirtIO-MMIO device assigned to the domain. The Xen configuration must use
``grant_usage=0`` because the Zephyr VirtIO frontend currently publishes guest
physical addresses and does not implement Xen grant DMA addresses. A backend
that requires ``grant_usage=1``, including the Xen ``vhost_blk`` sample, is not
compatible with this frontend yet.

The guest configuration must allocate 16 MiB of RAM, matching the
``xen-guest`` snippet, and expose a writable VirtIO block device. The sample
image is ``build/zephyr/zephyr.bin``.

Sample Output
=============

.. code-block:: console

   VirtIO block disk VIRTIOBLK0: 2048 sectors of 512 bytes
   Wrote and verified sector 1024
   Original sector restored
   VirtIO block sample completed successfully
