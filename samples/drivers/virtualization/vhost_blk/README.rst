.. zephyr:code-sample:: xen-vhost-blk
   :name: Xen vhost virtio-blk backend

   Export a RAM-backed virtio block device from a Zephyr driver domain.

Overview
********

This sample is a block-only sibling of the Xen vhost RNG sample. It does not
modify or replace the existing RNG sample.

The Zephyr domain exports one virtio-blk device with:

* virtio device ID 2
* one virtqueue with 16 entries
* MMIO base address ``0x02000000``
* 2048 sectors of 512 bytes (1 MiB)
* volatile RAM storage

The block device deliberately occupies the first Xen virtio-mmio slot. This
matches ``GUEST_VIRTIO_MMIO_BASE`` in the stock Xen Arm tools and avoids
depending on a second-device address stride.

Building
********

The vhost driver also needs a ``zephyr-xenlib`` revision containing the
XenStore client. The existing local ``xs-client`` branch provides it. A build
using that module worktree looks like:

.. code-block:: console

   git -C modules/lib/zephyr-xenlib worktree add /tmp/zephyr-xenlib-xs-client xs-client
   ZEPHYR_BASE=$PWD/zephyr-vhost-blk west build -p always \
     -b xenvm/xenvm/gicv3 \
     zephyr-vhost-blk/samples/drivers/virtualization/vhost_blk \
     -d /tmp/zephyr-vhost-blk-build -- \
     -DZEPHYR_MODULES=/tmp/zephyr-xenlib-xs-client

The image to copy into Dom0 is
``/tmp/zephyr-vhost-blk-build/zephyr/zephyr.bin``.

Xen configuration
*****************

Create the Zephyr backend domain first:

.. code-block:: console

   xl create -c xen/zephyr-vhost-blk.cfg

The backend domain name must remain ``zephyr`` because the DomU configuration
uses that name. Merge the following entry into the existing DomU configuration:

.. code-block:: python

   virtio = [
       "type=virtio,device2,transport=mmio,backend=zephyr,grant_usage=1"
   ]

The complete fragments are in ``xen/zephyr-vhost-blk.cfg`` and
``xen/domu-virtio-blk.cfg``.

DomU Linux requirements
***********************

The DomU kernel needs ``CONFIG_VIRTIO_BLK``, ``CONFIG_VIRTIO_MMIO``,
``CONFIG_XEN_VIRTIO``, and ``CONFIG_XEN_VIRTIO_FORCE_GRANT``. After creating
DomU, verify the device with:

.. code-block:: console

   dmesg | grep -E 'virtio|xen'
   lsblk
   mkfs.ext4 /dev/vda
   mount /dev/vda /mnt

The RAM disk contents disappear whenever the Zephyr backend domain restarts.
For initial QEMU testing, use the existing permissive Flask setup. Enforcing
mode additionally needs a policy that permits the Zephyr driver domain to
create the IOREQ server and map DomU grant references.
