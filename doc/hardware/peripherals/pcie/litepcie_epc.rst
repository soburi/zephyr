:orphan:

LitePCIe inbound endpoint controller
####################################

The ``litex,pcie-epc`` driver implements the LitePCIe EPC CSR interface v1.
It requires the Tang Mega 138K Pro target's ``--with-pcie-epc`` option;
``--with-pcie`` exposes a different CSR and DMA interface.

The controller supports one function and one 32-bit, non-prefetchable BAR0
of 1 MiB. The function must supply a physically contiguous BAR buffer of
that size. The driver does not provide a DMA heap. The buffer must be
uncached, or the application must maintain cache coherence with host accesses.
Configure the BAR before starting the controller. Stop the controller before
changing or releasing the buffer.

The configuration header is fixed in the Gowin IP: vendor 0x22c2, device
0x1100, class 0x058000. The driver accepts the header an endpoint
function writes without programming it. Outbound
windows, DMA, interrupts and link notifications are not implemented. The
standard endpoint test function cannot bind to this controller yet.

For the DDR AE350 build with SPI flash, JTAG and UART2, the devicetree node is::

   / {
       pcie_epc: pcie-epc@e8003000 {
           compatible = "litex,pcie-epc";
           reg = <0xe8003000 0x10>;
           bar0-size = <0x100000>;
           max-functions = <1>;
       };
   };

Enable ``CONFIG_PCIE_EPC``. Verify the address against ``csr.csv`` for each
bitstream, since other peripherals can change CSR allocation.

The four 32-bit CSR words are control, BAR0 CPU base, BAR0 size and link
status. Control bit 0 releases the PHY and endpoint reset; bit 1 enables
inbound bus transactions. Both bits reset to zero.

With the installed Gowin EDA available through ``GOWIN_HOME``,
``--with-pcie-epc`` generates the SERDES configuration from the matching
``.ipc`` and installed Gowin templates, then supplies it through ``set_csr``.
A pre-generated file can be selected with ``--pcie-csr``. A re-generated IP
can be selected with ``--pcie-phy-path``; ``--pcie-clk-freq`` must match its
TLP interface frequency.

The sample at ``samples/drivers/litepcie_epc`` demonstrates binding a shared
BAR function to the controller and includes a Linux host checker.
