.. zephyr:code-sample:: vhost
   :name: Vhost sample application

Overview
********

This sample demonstrates the use of the vhost driver subsystem for implementing
VIRTIO backends in Zephyr. The application shows how to:

* Initialize and configure vhost devices
* Handle VIRTIO queue operations
* Process guest requests using the vringh utility
* Implement a basic VIRTIO backend for Xen virtualization

The sample sets up a vhost device that can communicate with VIRTIO frontend
drivers in guest virtual machines, specifically designed for Xen MMIO
virtualization environments.

The sample provides two backends:

* a VIRTIO entropy device
* a VIRTIO block device with memory, Disk Access, and file storage options

VIRTIO block device
*******************

The block backend implements the mandatory ``VIRTIO_BLK_T_IN`` and
``VIRTIO_BLK_T_OUT`` requests. It also accepts ``VIRTIO_BLK_T_FLUSH``. The
device uses one virtqueue and exposes 512-byte sectors.

The storage backend is selected by the ``vhost-blk-backend`` phandle in the
``zephyr,user`` devicetree node. The default board overlay selects memory::

   zephyr,user {
           vhost-blk-backend = <&blk_memory>;
   };

The available choices are:

``&blk_memory``
   A zero-initialized RAM buffer. Its contents are lost on reboot.

``&blk_sd``
   A Disk Access device. Set its ``disk-name`` property to the name registered
   by the SD card driver. The disk must use 512-byte sectors and contain at
   least ``sector-count`` sectors.

``&blk_file``
   A file on an already mounted Zephyr file system. Set ``file-path`` to the
   desired path. A missing file is created and a short file is extended to the
   configured size; an existing larger file is not truncated.

For example, an application overlay can select an SD card directly::

   &blk_memory {
           status = "disabled";
   };

   &blk_sd {
           disk-name = "SD";
           sector-count = <2048>;
           status = "okay";
   };

   &{/zephyr,user} {
           vhost-blk-backend = <&blk_sd>;
   };

The first eight bytes of the block vhost node's ``config-data`` contain the
little-endian VIRTIO capacity. This capacity must equal the selected backend's
``sector-count``; the sample checks the relationship at build time. For
example, 2048 sectors are represented by::

   &blk {
           config-data = [00 08 00 00 00 00 00 00];
   };

Board-specific SD controller setup, disk driver Kconfig options, file-system
mounts, and file-system implementation options must be supplied by the board
or application using this sample.

Requirements
************

This sample requires:

* A Xen hypervisor environment
* Xen domain management tools
* A board that supports Xen virtualization (e.g., xenvm)

Building and Running
********************

This application can be built and executed on Xen as follows:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/virtualization/vhost
   :host-os: unix
   :board: xenvm
   :goals: run
   :compact:

The application initializes the vhost subsystem and waits for VIRTIO frontend
connections from guest domains. The RNG device uses MMIO base ``0x02000000``
and VIRTIO device ID 4. The block device uses MMIO base ``0x02001000`` and
VIRTIO device ID 2. The Xen frontend configuration must use the corresponding
base address and IRQ.

Expected Output
***************

When running successfully, you should see output similar to::

   *** Booting Zephyr OS build zephyr-v3.x.x ***
   [00:00:00.000,000] <inf> vhost: VHost device ready
   [00:00:00.000,000] <dbg> vhost: queue_ready_handler(dev=0x..., qid=0, data=0x...)
   [00:00:00.000,000] <dbg> vhost: vringh_kick_handler: queue_id=0
   [00:00:00.000,000] <inf> vhost_blk: memory backend ready: 2048 sectors
