.. zephyr:code-sample:: vhost-pci-epf
   :name: VHost on a PCI endpoint function

   Serve a virtio-rng device to a PCI host through a PCI endpoint function.

Overview
********

This sample implements a virtio-rng (entropy) backend with the :ref:`VHost <vhost>` controller
API, as the :zephyr:code-sample:`vhost` sample does for a Xen guest domain. Here the VIRTIO
driver does not run in a guest of the same hypervisor but on the PCI host at the other end of a
PCI link: the VIRTIO PCI endpoint function controller (:dtcompatible:`zephyr,vhost-pci-epf`)
adds a function to a PCI endpoint controller, and the host drives that function with its regular
virtio-pci driver.

The application registers the backend with the controller and starts the PCI link. The requests
the host places in the virtqueue are then retrieved with the vringh helper, filled with random
data, and returned.

The random data is generated with :c:func:`sys_rand_get`, which is not cryptographically secure;
a production backend must use a hardware entropy source.

Prerequisites
*************

The sample uses the QEMU PCI endpoint controller (EPC) device, which is not part of upstream
QEMU. It is provided by the QEMU PCI endpoint controller emulation patches:

* https://github.com/ShunsukeMie/qemu/tree/epf-bridge/v1
* RFC: https://lists.gnu.org/archive/html/qemu-devel/2023-08/msg04481.html

The setup uses two QEMU instances: the endpoint instance runs this sample with the ``qemu-epc``
device on its PCI bus, and the host instance runs Linux with the ``epf-bridge`` device, which
forwards the endpoint function configured by this sample to the Linux PCI bus.

The function implements the legacy VIRTIO PCI interface, so a Linux host needs
``CONFIG_VIRTIO_PCI_LEGACY``, and ``CONFIG_HW_RANDOM_VIRTIO`` for the virtio-rng driver.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/virtualization/vhost_pci_epf
   :board: qemu_x86_64
   :goals: build
   :compat: ignore

Running
*******

Run the built image with the patched QEMU and add the EPC device to the machine, for example
with ``-device qemu-epc``. The sample prints::

   [00:00:00.010,000] <inf> vhost_pci_epf_sample: VHost device vhost-rng ready
   [00:00:00.010,000] <inf> vhost_pci_epf_sample: PCI link started, waiting for the host

Then start the Linux host instance. Its virtio-pci driver binds to the function, which shows up
with vendor ID ``0x1af4`` and device ID ``0x1005``.

The legacy driver takes the VIRTIO device type from the PCI subsystem ID. The ``epf-bridge``
device of the ``epf-bridge/v1`` branch does not forward the subsystem ID of the endpoint
function, so the virtio-rng driver does not attach there. Once it does, read from the hardware
random number generator of the host:

.. code-block:: console

   # head -c 16 /dev/hwrng | xxd

and the sample reports the requests it serves::

   [00:00:04.340,000] <inf> vhost_rng: virtio-rng request queue ready
   [00:00:04.380,000] <inf> vhost_rng: Served 64 random bytes
