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

The function implements the modern VIRTIO PCI interface, so a Linux host needs
``CONFIG_VIRTIO_PCI``, and ``CONFIG_HW_RANDOM_VIRTIO`` for the virtio-rng driver.

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
with vendor ID ``0x1af4`` and device ID ``0x1044``.

The QEMU pair must provide the EPC1 software BAR access extension described below, forwarding
of the vendor-specific capabilities, subsystem ID and Interrupt Pin forwarding, and
level-controlled INTx. The original
``epf-bridge/v1`` implementation alone is insufficient. With these extensions, read from the
hardware random number generator of the host:

.. code-block:: console

   # head -c 16 /dev/hwrng | xxd

and the sample reports the requests it serves::

   [00:00:04.340,000] <inf> vhost_rng: virtio-rng request queue ready
   [00:00:04.380,000] <inf> vhost_rng: Served 64 random bytes

QEMU integration test
*********************

The host test drives PCI configuration, I/O registers and host RAM through QEMU's qtest
interface while the endpoint runs this sample. It checks queue selection, DMA into host RAM,
queue notifications, ISR read-to-clear, INTx assertion and deassertion at the host IOAPIC,
reset with an interrupt pending followed by reinitialization, and controller stop/restart.
It does not boot Linux.

.. code-block:: console

   west build -b qemu_x86_64 samples/drivers/virtualization/vhost_pci_epf
   west build -t qemu_kernel_target
   python3 samples/drivers/virtualization/vhost_pci_epf/test_qemu.py \
       --qemu /path/to/patched/qemu-system-x86_64 --build-dir build

Only one QEMU pair can use the transport's fixed ``/tmp/qemu-epc.sock`` path at a time.
The script prints the directory containing the two QEMU logs.

Software BAR access
*******************

The EPC delivers each host register access to the EPF as a request. The EPF processes requests
on its own work queue and completes reads with their register value. The EPC does not interpret
VIRTIO registers. BARs without software servicing retain the direct memory path used by the
endpoint test function.

The QEMU control BAR exposes the following little-endian, 32-bit registers. The data register
is accessed as two 32-bit halves. Reading it returns request data; writing it stages response
data independently of the request.

.. list-table:: EPC1 mailbox
   :header-rows: 1
   :widths: 15 85

   * - Offset
     - Meaning
   * - ``0x50``
     - Read-only ABI signature ``0x45504331``.
   * - ``0x54``
     - Software-serviced BAR mask, bits 0 through 5; configure while stopped.
   * - ``0x58``
     - Request info: bit 31 pending, bit 30 write, bits 18:16 BAR, bits 7:0 size in bytes.
   * - ``0x5c``
     - Request offset within the BAR.
   * - ``0x60``, ``0x64``
     - Request data on read; staged response data on write, low half first.
   * - ``0x68``
     - Read-only features: bit 0 set when a posted request can interrupt the endpoint.
   * - ``0x6c``
     - Write zero to deassert INTA, nonzero to assert it.
   * - ``0x70``
     - Request ID. Check before and after reading the request to detect expiration.
   * - ``0x74``
     - Write the request ID to complete it; stale IDs are ignored.
   * - ``0x78``
     - Staged response error: zero for success, nonzero for failure.

There is one outstanding request. The EPF writes the response data and error before writing the
completion ID. QEMU commits them only for the pending ID. An unanswered request expires after
five seconds; failed reads return all ones and failed writes are discarded. Writes are serialized
with subsequent reads and writes on the same connection, although the host write itself is posted.
Writing zero to the existing START register at ``0x00`` disables inbound BAR accesses and aborts
an outstanding request. Starting again resumes accesses on the same connection.

With bit 0 of the features register set, setting bit 30 of the doorbell control register at
``0x40`` makes a posted request set bit 1 of the doorbell status register at ``0x4c`` and assert
the interrupt of the QEMU EPC device, until the endpoint writes the bit back. The Zephyr driver
uses it when the devicetree node of the controller has an ``interrupts`` property, as the
``qemu_x86_64`` overlay of this sample does, and notifies the EPF of each request; otherwise the
EPF polls the mailbox.

The Zephyr driver probes the ABI signature. Older emulators retain the memory BAR polling
fallback, which cannot implement ISR read-to-clear or reliably observe reset writes, and report
``-ENOTSUP`` on stop. Applications must check stop before attempting to remove an EPF.

Limitations
***********

The software-serviced path supports one PCI function, split virtqueues, static read-only device
configuration and INTx. MSI/MSI-X, host hot-unplug/reconnect, and propagation of host PCI
function resets are not implemented. With the ``legacy`` property of the controller node, the
function implements the legacy interface instead, with one virtqueue behind an I/O BAR. The QEMU pair must be restarted
when changing the function's PCI identity or BAR layout. The mailbox
provides register semantics for functional testing, not representative PCIe performance.
