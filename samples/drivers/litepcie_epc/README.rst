.. _litepcie_epc_sample:

LitePCIe shared BAR endpoint
###########################

This sample binds a single function to the LitePCIe EPC controller and exposes
1 MiB of DDR as BAR0. It uses the fixed Gowin identity 22c2:1100. It does not
implement the Linux ``pci_endpoint_test`` protocol.

The sample disables the AE350 data cache after flushing it, so PCIe writes
are visible to the CPU. The BAR contents are little-endian 32-bit words:

======== ===========================
Offset   Contents
======== ===========================
0x00     Signature 0x45504331
0x04     Host request
0x08     Device echo of the request
0x0c     Device heartbeat, every 10 ms
======== ===========================

Build
*****

The companion bitstream must enable ``--with-pcie-epc``, put DDR at address 0,
and use the DLM for the BIOS. The supplied overlay matches the CSR allocation
of that configuration with SPI flash, JTAG and UART2 enabled. Verify
``pcie_epc`` against ``csr.csv`` if changing the gateware peripherals.

From the LiteX workspace::

   source litex-env.sh
   CCACHE_DISABLE=1 west build \
       -b tang_mega_138k_pro_dock/ae350_a25/litex \
       zephyr/samples/drivers/litepcie_epc \
       -d build/tang-pcie-epc-ddr/zephyr -- -DDTC=/usr/bin/dtc

The code is linked at XIP address 0x80010000. With the companion LiteSPI
mapping, the BIOS image belongs at physical flash offset 0x600000 and
``zephyr.bin`` at 0x610000. DDR must be initialized by the matching BIOS
before entering Zephyr. Building does not program either image or the FPGA.

Host check
**********

Start the endpoint firmware before enumerating it on the host. Once the
host lists 22c2:1100, enable the device's memory resources through the Linux
PCI subsystem and run the checker. Replace ``<BDF>`` with its PCI address::

   echo 1 | sudo tee /sys/bus/pci/devices/<BDF>/enable
   sudo python3 zephyr/samples/drivers/litepcie_epc/host_check.py \
       /sys/bus/pci/devices/<BDF>/resource0

A successful check verifies host-to-device writes, device-to-host reads and
that the Zephyr thread continues to run. Hardware enumeration and this test
must be performed on a PCIe host connected to the board.
