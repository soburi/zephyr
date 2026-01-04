RPi5 RP1 IRQ Test
=================

This sample triggers an RP1 MSI-X test interrupt and observes the MIP and
GIC pending state on Raspberry Pi 5.

Configuration
-------------

Edit ``samples/boards/rpi_5/rpi5_rp1_irq_test/src/rp1_irq_test_config.h`` and set:

- ``RP1_MIP_BASE_ADDR``: MIP MMIO base address
- ``RP1_MIP_MSG_ADDR``: MSI-X message (doorbell) address
- ``RP1_MIP_MSI_BASE_INTID``: GIC INTID base for the MIP SPI range
- ``RP1_MIP_MSI_NUM_SPIS``: Number of SPI lines exposed by MIP
- ``RP1_MIP_MSI_OFFSET``: Optional MSI data offset (usually 0)
- ``RP1_CFG_SWEEP_STEP``: BAR sweep stride when locating RP1 config block
- ``RP1_ENABLE_CPU_DOORBELL_TEST``: Enable CPU MSI doorbell sanity test
- ``RP1_CFG_BAR_INDEX``: BAR index for MSIX_CFG/INTSTAT
  (use 0xFF to reuse the MSI-X table BAR)
- ``RP1_CFG_BAR_PHYS_ADDR``: Optional forced MMIO address for the config BAR
- ``RP1_CFG_BAR_SIZE``: Optional forced size for the config BAR
- ``RP1_CFG_USE_PCIE_CFG``: Use PCIe config space for MSIX_CFG/INTSTAT (recommended)
- ``RP1_PCIE_CFG_BASE_OFFSET``: Base offset inside BAR for MSIX_CFG/INTSTAT

Build
-----

.. code-block:: shell

   west build -b rpi_5 samples/rpi5_rp1_irq_test

Run
---

Flash with your usual RPi5 flow and monitor the console output. The sample
prints MIP status and GIC pending bits before and after the trigger.

Notes
-----

- If MIP status never changes, check MSI-X table programming and msg_addr.
- If MIP status changes but GIC does not, check base INTID and GIC config.
- The sample provides ``boards/rpi_5.overlay`` to widen the PCIe window to 8 MiB,
  ensuring RP1 BAR0/BAR2 (>= 0x00400000) are reachable.
