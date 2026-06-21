.. _pcie_rk3568_endpoint:

RK3568 PCIe endpoint control sequences
######################################

Overview
********

The Rockchip RK3568 contains a dual-mode PCIe 3.0 x2 controller. The Zephyr
endpoint driver configures this controller as a single PCIe endpoint function
and provides BAR, outbound address translation, interrupt generation, DMA,
link control, and in-band hot-reset handling.

The implementation follows the RK3568 Technical Reference Manual (TRM). It
does not use register values inferred from another operating system. RK3588
documentation was used only to clarify the intended relationship between the
PCIe reset grant and the Rockchip bus-idle handshake; all addresses and bit
definitions used by the driver come from the RK3568 TRM.

The relevant implementation and sample are:

* ``drivers/pcie/endpoint/pcie_ep_rockchip_rk3568.c``
* ``samples/drivers/pcie_endpoint``

Hardware blocks
***************

.. list-table:: RK3568 blocks used by the endpoint driver
   :header-rows: 1
   :widths: 20 35 45

   * - Block
     - Purpose
     - Important operations
   * - CRU
     - Controller and PHY clocks and resets
     - Enable PCIe clocks, assert resets, release PHY before controller
   * - PMU GRF and SYS GRF
     - PCIe sideband pin selection
     - Select the M0 ``CLKREQ#``, ``WAKE#``, and ``PERST#`` functions
   * - PCIe PHY GRF
     - Reference clock and PHY startup
     - Select the host reference clock, use internal protocol settings,
       wait for MPLLA lock
   * - PCIe Client
     - Endpoint mode, LTSSM, interrupts, and reset warning
     - Select EP mode, start or stop training, detect link and reset events
   * - DesignWare DBI and DBI2
     - PCI configuration space and BAR sizing
     - Program IDs, capabilities, BAR type, and BAR size masks
   * - iATU
     - Inbound and outbound address translation
     - Map BARs to local memory and local apertures to host memory
   * - Embedded DMA
     - Host-to-device and device-to-host copies
     - Program channel zero and wait synchronously for completion
   * - PMU
     - Shared ``BIU_PIPE`` idle handshake
     - Quiesce the interconnect before granting a hot reset

Initialization sequence
***********************

Initialization leaves the LTSSM stopped. The application configures its BARs
and then calls :c:func:`pcie_ep_start` when it is ready to be enumerated.

.. mermaid::
   :caption: RK3568 PCIe endpoint initialization
   :alt: Flowchart showing clock, reset, PHY, endpoint configuration, and
         interrupt initialization before link training starts.

   flowchart TD
       A["Driver initialization"]
       B["Map DBI, Client, CRU, GRF, PMU,<br/>iATU aperture, and PHY registers"]
       D["Select PCIe3x2 M0 I/O function and configure<br/>CLKREQ#, WAKE#, and PERST# pinmux"]

       A --> B
       B --> C{"M0 sideband pins enabled?"}
       C -- Yes --> D
       C -- No --> E["Keep board-provided pin configuration"]
       D --> F["Enable controller, auxiliary, PIPE, and PHY APB clocks"]
       E --> F
       F --> G["Assert controller and PHY resets"]
       G --> H["Select reference clock and force MPLLA enable"]
       H --> I["Clear EXT_CTRL_SEL so PCS uses internal<br/>per-protocol PHY settings"]
       I --> J["Release PHY resets"]
       J --> K{"MPLLA locked before timeout?"}
       K -- No --> X["Initialization fails with -ETIMEDOUT"]
       K -- Yes --> L["Release controller resets"]
       L --> M["Disable all inbound and outbound iATU regions"]
       M --> N["Select endpoint mode and hold LTSSM stopped"]
       N --> O["Program IDs and class code"]
       O --> P["Disable FLR capability"]
       P --> Q["Configure MSI and MSI-X capabilities"]
       Q --> R["Enable enhanced delayed-hot-reset handling"]
       R --> S["Clear and unmask link interrupts"]
       S --> T["Endpoint ready; link training remains stopped"]

.. list-table:: Initialization register sequence
   :header-rows: 1
   :widths: 10 30 30 30

   * - Order
     - Register group
     - Fields
     - Result
   * - 1
     - SYS GRF and PMU GRF
     - PCIe3x2 I/O selection and M0 sideband pinmux
     - Route ``CLKREQ#``, ``WAKE#``, and ``PERST#`` to the controller
   * - 2
     - ``CRU_GATE_CON13`` and ``CRU_GATE_CON33``
     - PCIe3x2 and PCIe30 PHY clock gates
     - Supply the bus, auxiliary, PIPE, and PHY APB clocks
   * - 3
     - ``CRU_SOFTRST_CON12`` and ``CRU_SOFTRST_CON27``
     - PCIe3x2 and PCIe30 PHY reset requests
     - Hold the controller and PHY in reset during clock selection
   * - 4
     - ``PHY_GRF_CON3`` and ``PHY_GRF_CON4``
     - Pad reference clock, MPLLA force enable, and ``EXT_CTRL_SEL``
     - Use the host reference clock and internal PCIe protocol settings
   * - 5
     - ``PHY_GRF_STATUS0``
     - MPLLA state
     - Confirm the PHY clock is stable before releasing the controller
   * - 6
     - PCIe Client ``GENERAL_CON``
     - Device type, link-reset grant, and LTSSM enable
     - Select endpoint mode while keeping training stopped
   * - 7
     - DBI configuration space
     - IDs, class code, FLR capability, MSI, and MSI-X
     - Publish the endpoint function capabilities
   * - 8
     - PCIe Client interrupt and hot-reset control
     - Enhanced LTSSM control and link interrupt mask
     - Enable delayed in-band reset recovery

BAR and link-start sequence
***************************

Each BAR is configured before link training. DBI2 contains the size mask
returned during enumeration, while the normal DBI BAR register contains the
BAR type attributes. An inbound iATU BAR-match region translates host BAR
accesses to the supplied local physical address.

.. mermaid::
   :caption: BAR configuration and link start
   :alt: Sequence diagram showing the application configuring a BAR, starting
         link training, and the root complex enumerating the endpoint.

   sequenceDiagram
       participant App as Endpoint application
       participant API as Zephyr PCIe EP API
       participant DBI as DBI and DBI2
       participant ATU as Inbound iATU
       participant Client as PCIe Client and LTSSM
       participant RC as Root complex

       App->>API: pcie_ep_set_bar(BAR number, local address, size, flags)
       API->>API: Validate size, alignment, BAR pair, and flags
       API->>ATU: Disable selected inbound region
       API->>DBI: Program DBI2 BAR size mask
       API->>DBI: Program DBI BAR attributes
       API->>ATU: Program local target and BAR-match mode
       API->>ATU: Enable inbound region
       ATU-->>API: Confirm region enabled
       API-->>App: Success
       App->>API: pcie_ep_start()
       API->>Client: Grant link reset and enable LTSSM
       Client->>RC: Perform link training
       RC->>DBI: Enumerate function and assign BAR addresses
       Client-->>API: PHY and DLL link-up status

Outbound mapping and DMA
************************

The outbound mapping API allocates one iATU region from the endpoint outbound
aperture. The returned mapped address is used only as a token identifying the
selected region and offset. The DMA implementation reconstructs the original
PCIe target address before programming the embedded DMA engine.

.. mermaid::
   :caption: Outbound mapping and synchronous DMA
   :alt: Sequence diagram showing outbound iATU allocation followed by a
         synchronous embedded DMA transfer.

   sequenceDiagram
       participant App as Endpoint application
       participant Map as Outbound mapper
       participant ATU as Outbound iATU
       participant DMA as Embedded DMA
       participant Host as Host memory

       App->>Map: pcie_ep_map_addr(host address, size)
       Map->>Map: Select a free outbound window
       Map->>ATU: Disable region
       Map->>ATU: Program local base, limit, and host target
       Map->>ATU: Enable region
       ATU-->>Map: Confirm region enabled
       Map-->>App: Mapped aperture address and mapped length
       App->>DMA: pcie_ep_dma_xfer(mapped address, local address, size, direction)
       DMA->>DMA: Reject if reset is in progress
       DMA->>DMA: Lock channel zero and reconstruct host target
       DMA->>DMA: Program source, destination, size, and direction
       DMA->>Host: Start PCIe read or write
       loop Until complete or timeout
           DMA->>DMA: Poll transfer size
       end
       alt Transfer completed
           DMA-->>App: Success
       else Timeout
           DMA->>DMA: Stop channel through the doorbell
           DMA-->>App: -ETIMEDOUT
       end
       App->>Map: pcie_ep_unmap_addr(mapped address)
       Map->>ATU: Disable region
       Map->>Map: Release outbound window

Interrupt generation
********************

Legacy INTx, MSI, and MSI-X generation is supported. New interrupt generation
is rejected while reset handling is active.

* Legacy interrupt generation accepts only interrupt number zero.
* MSI generation requires the host to enable MSI and checks the host-selected
  multiple-message-enable value.
* MSI-X generation requires the host to enable MSI-X. The sample reserves the
  first BAR0 page for the MSI-X table and pending-bit array.

In-band hot-reset sequence
**************************

The PCIe Client raises ``link_req_rst_not_int`` as an early warning for an
in-band hot reset or link-down reset. The ISR performs only the immediate
operation needed to hold the controller reset. The remaining sequence runs in
a work item because it may wait for DMA, bus idle, and link training.

``BIU_PIPE`` is shared with other clients in ``PD_PIPE``, including USB 3,
SATA, and XPCS. A platform using those clients concurrently must coordinate
ownership before allowing this driver to idle the shared interconnect.

.. mermaid::
   :caption: RK3568 in-band hot-reset handling
   :alt: Sequence diagram showing the reset warning interrupt, DMA quiesce,
         BIU_PIPE idle handshake, reset grant, and link retraining.

   sequenceDiagram
       participant RC as Root complex
       participant ISR as PCIe ISR
       participant Work as Hot-reset worker
       participant DMA as DMA and IRQ producers
       participant PMU as PMU BIU_PIPE control
       participant App as Endpoint reset callback
       participant GRF as PIPE GRF
       participant LTSSM as PCIe LTSSM

       RC->>ISR: Hot-reset TS1 or link-down reset request
       ISR->>ISR: Clear W1C interrupt status
       ISR->>DMA: Set reset_in_progress
       Note over DMA: New DMA and interrupt generation return -EBUSY
       ISR->>LTSSM: Clear LTSSM enable immediately
       ISR->>Work: Submit deferred reset work
       Work->>DMA: Lock DMA path and wait for active transfer
       Work->>PMU: Set BUS_IDLE_SFTCON0[BIU_PIPE]
       loop Until idle or timeout
           Work->>PMU: Read BUS_IDLE_ACK and BUS_IDLE_ST
       end
       alt BIU_PIPE did not become idle
           Work->>LTSSM: Leave link stopped
           Work->>PMU: Clear BIU_PIPE idle request
           Work->>DMA: Clear reset_in_progress
       else BIU_PIPE is idle
           Work->>App: Invoke PCIE_PERST_INB callback
           Work->>GRF: Assert PCIe link-reset grant
           Work->>LTSSM: Enable link training
           loop Until link up or timeout
               Work->>LTSSM: Check SMLH and RDLH link-up status
           end
           alt Link recovered
               Work->>GRF: Deassert link-reset grant
           else Link timeout
               Work->>LTSSM: Stop link training
           end
           Work->>PMU: Clear BIU_PIPE idle request
           Work->>DMA: Clear reset_in_progress
       end

.. list-table:: Hot-reset register sequence
   :header-rows: 1
   :widths: 10 30 30 30

   * - Order
     - Register
     - Operation
     - Required observation
   * - 1
     - Client ``INTR_STATUS_MISC``
     - Clear ``link_req_rst_not_int``
     - Early warning has been acknowledged
   * - 2
     - Client ``GENERAL_CON``
     - Clear LTSSM enable
     - Link reset remains delayed
   * - 3
     - ``PMU_BUS_IDLE_SFTCON0``
     - Set ``idle_req_pipe``
     - ``BIU_PIPE`` stops accepting new traffic
   * - 4
     - ``PMU_BUS_IDLE_ACK`` and ``PMU_BUS_IDLE_ST``
     - Poll ``BIU_PIPE`` bit 11
     - Both acknowledge and idle state must be asserted
   * - 5
     - ``PIPE_GRF_PIPE_CON0``
     - Set ``pcie30x2_link_rst_grt``
     - Controller may complete the delayed reset
   * - 6
     - Client ``GENERAL_CON``
     - Set LTSSM enable
     - Link retraining starts
   * - 7
     - Client ``LTSSM_STATUS``
     - Poll SMLH and RDLH link-up bits
     - Both layers must report link up
   * - 8
     - ``PIPE_GRF_PIPE_CON0``
     - Clear ``pcie30x2_link_rst_grt``
     - Prepare for the next reset warning
   * - 9
     - ``PMU_BUS_IDLE_SFTCON0``
     - Clear ``idle_req_pipe``
     - Release the shared ``BIU_PIPE`` interconnect

Reset support boundaries
************************

The reset API distinguishes three reset types:

.. list-table:: RK3568 reset notification support
   :header-rows: 1
   :widths: 25 20 55

   * - Reset type
     - Driver support
     - Reason
   * - Dedicated ``PERST#``
     - Hardware only
     - The controller consumes the pin, but the published RK3568 TRMs do
       not document a software-visible assertion event. Registration of
       a :c:enumerator:`PCIE_PERST` callback returns ``-ENOTSUP``.
   * - In-band hot reset
     - Supported
     - The PCIe Client exposes the early ``link_req_rst_not_int`` warning.
       Registration of a :c:enumerator:`PCIE_PERST_INB` callback is supported.
   * - Function Level Reset
     - Not advertised
     - FLR keeps the PCIe link active, so it cannot be inferred from link
       state. No RK3568 application event is documented. The driver clears
       the FLR-capable bit and rejects :c:enumerator:`PCIE_FLR` callbacks.

Failure behavior
****************

The driver deliberately fails closed during reset recovery:

* A PHY PLL timeout aborts initialization.
* An iATU region that does not report enabled returns ``-EIO``.
* DMA timeout stops the channel and returns ``-ETIMEDOUT``.
* A ``BIU_PIPE`` idle timeout leaves LTSSM stopped and does not grant reset.
* A link-recovery timeout stops LTSSM.
* New DMA and endpoint-generated interrupts return ``-EBUSY`` while reset
  recovery is active.

The link remains stopped after a hot-reset failure. Recovery then requires
platform diagnostics or reinitialization instead of continuing with unknown
interconnect or controller state.

Reference documents
*******************

* `RK3568 TRM Part 1, version 1.3
  <https://rockchip.fr/Rockchip%20RK3568%20TRM%20V1.3%20Part1.pdf>`_
* `RK3568 TRM Part 2, version 1.1
  <https://dl.radxa.com/rock3/docs/hw/datasheet/Rockchip%20RK3568%20TRM%20Part2%20V1.1-20210301.pdf>`_
* `RK3588 TRM Part 1, version 1.0
  <https://rockchip.fr/Rockchip%20RK3588%20TRM%20V1.0%20Part1.pdf>`_
* `RK3588 TRM Part 2, version 1.0
  <https://rockchip.fr/Rockchip%20RK3588%20TRM%20V1.0%20Part2.pdf>`_
