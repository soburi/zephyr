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

Sample Output
=============

.. code-block:: console

   VirtIO block disk VIRTIOBLK0: 2048 sectors of 512 bytes
   Wrote and verified sector 1024
   Original sector restored
   VirtIO block sample completed successfully
