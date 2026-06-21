.. zephyr:code-sample:: pcie_endpoint
   :name: PCIe endpoint BAR mailbox

   Expose a 64 KiB BAR0 mailbox from the RK3568 PCIe 3.0 x2 endpoint.

Overview
********

The sample configures BAR0 as a 64 KiB memory BAR, starts link training, and
updates a small mailbox at BAR0 offset ``0x1000``.  The first page is reserved
for the MSI-X table and pending-bit array.  A host can write ``command``,
``argument``, and a new ``sequence`` value.  The endpoint returns
``command ^ argument`` in ``response`` and raises MSI vector 0.

Hardware requirements
*********************

The RK3568 PCIe 3.0 x2 controller lanes must be connected to a PCIe root
complex.  The root complex must provide the 100 MHz differential reference
clock and drive the RK3568 ``PCIE30X2_PERSTn`` input.  The stock board's
connector routing must be checked against its schematic; building for
``roc_rk3568_pc`` does not imply that its M.2 connector exposes the dual-mode
PCIe 3.0 x2 controller.

The RK3568 PCIe hot-reset sequence temporarily idles the shared ``BIU_PIPE``
interconnect. Do not run uncoordinated USB 3, SATA, or XPCS traffic from
``PD_PIPE`` while exercising endpoint hot reset.

The endpoint does not advertise Function Level Reset because the published
RK3568 TRMs provide no software-visible FLR event. Dedicated ``PERST#`` is
handled by controller hardware and has no driver callback.

See :ref:`pcie_rk3568_endpoint` for the complete initialization, address
translation, DMA, interrupt, and hot-reset control sequences.

Building
********

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/pcie_endpoint
   :board: roc_rk3568_pc
   :goals: build
   :compact:
