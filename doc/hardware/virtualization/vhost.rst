.. _vhost:

VHost (VIRTIO backend)
######################

Overview
********

VHost is the device-side counterpart of :ref:`VIRTIO <virtio>`: it lets a Zephyr application act
as a VIRTIO device (named "device" in the VIRTIO specification) serving a VIRTIO driver that runs
in a guest virtual machine. A typical use is a Zephyr backend domain on a hypervisor providing
virtual devices, such as an entropy source or a console, to other guests.

Architecture
************

The subsystem is split into two layers.

The **VHost controller** is a driver that owns everything specific to the hypervisor and
transport: emulating the VIRTIO transport registers, negotiating feature bits, accessing guest
buffers, and delivering interrupts to the guest. The controller exposes the generic VHost
controller API declared in :zephyr_file:`include/zephyr/drivers/vhost.h`. The Xen MMIO controller
(:dtcompatible:`xen,vhost-mmio`) implements this API using the Xen ioreq server interface for
register emulation and the grant table interface for guest buffer access.

The **backend** implements the device logic (for example, a virtio-rng device). It is notified
when a virtqueue becomes ready for processing and when the guest kicks a queue, retrieves
descriptor chains, processes them, and returns them to the guest. A backend does not interpret
guest addresses itself: descriptor address fields are opaque tokens that only the owning
controller can translate, through ``vhost_prepare_iovec()`` and ``vhost_release_iovec()``. The
iovecs returned by ``vhost_prepare_iovec()`` stay valid until the matching
``vhost_release_iovec()`` call, which must be made exactly once per prepare call, on success and
on failure alike.

Ring processing helper (vringh)
*******************************

The vringh helper, declared in :zephyr_file:`include/zephyr/drivers/vhost/vringh.h`, implements
host-side processing of split virtqueues on top of the controller API, following the Linux kernel
vringh design. A backend binds one ``struct vringh`` per virtqueue with ``vringh_init_device()``,
then consumes descriptor chains with ``vringh_getdesc()``, completes them with
``vringh_complete()``, and notifies the guest with ``vringh_notify()``. Malformed descriptor
chains are rejected and the device is marked as needing a reset before guest buffers are exposed
to the backend.

Using vringh is optional: a backend may process rings itself through ``vhost_get_virtq()``, and a
controller that cannot expose the rings in the host address space (returning ``-ENOTSUP`` from
``vhost_get_virtq()``) requires a different processing strategy.

Limitations
***********

* Only the split virtqueue format is supported; packed virtqueues, indirect descriptors and
  ``VIRTIO_RING_F_EVENT_IDX`` are not.
* Device feature bits and the device-specific configuration space are owned by the controller and
  are static (for the Xen MMIO controller, they come from devicetree). Backends cannot yet offer
  device-specific feature bits or serve dynamic configuration space contents.

The :zephyr:code-sample:`vhost` sample implements a virtio-rng backend for a Xen guest domain.

API Reference
*************

.. doxygengroup:: vhost_apis
.. doxygengroup:: vringh_apis
