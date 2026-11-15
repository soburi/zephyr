.. zephyr:code-sample:: pcie-epf-test
   :name: PCI endpoint test function

   Expose a PCI endpoint test function to a Linux host through the QEMU EPC
   device.

Overview
********

This sample registers the PCI endpoint test function driver on the QEMU PCI
endpoint controller (EPC) device. The endpoint function implements the
register interface expected by the Linux ``pci_endpoint_test`` host driver,
so the standard ``pcitest`` tool can exercise read, write, copy and
interrupt tests against the Zephyr endpoint.

Prerequisites
*************

The QEMU EPC device is not part of upstream QEMU. It is provided by the
QEMU PCI endpoint controller emulation patches:

* https://github.com/ShunsukeMie/qemu/tree/epf-bridge/v1
* RFC: https://lists.gnu.org/archive/html/qemu-devel/2023-08/msg04481.html

The setup uses two QEMU instances: the endpoint instance runs this sample
with the ``qemu-epc`` device on its PCI bus, and the host instance runs
Linux with the ``epf-bridge`` device, which forwards the endpoint function
configured by this sample to the Linux PCI bus.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/pcie_epf_test
   :board: qemu_x86_64
   :goals: build
   :compat: ignore

Running
*******

Run the built image with the patched QEMU and add the EPC device to the
machine, for example with ``-device qemu-epc``. On the Linux host instance,
load the ``pci_endpoint_test`` driver and run ``pcitest``:

.. code-block:: console

   # pcitest -b 0
   BAR0:           OKAY
   # pcitest -w -s 1024
   WRITE ( 1024 bytes):            OKAY
   # pcitest -r -s 1024
   READ ( 1024 bytes):             OKAY
