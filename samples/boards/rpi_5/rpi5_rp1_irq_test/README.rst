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
- ``RP1_ENABLE_MIP_RAISE_TEST``: Enable MIP self-raise sanity test
- ``RP1_ENABLE_MIP_RAW_DUMP``: Dump raw MIP registers for offset verification
- ``RP1_MIP_RAW_DUMP_BYTES``: Byte length to dump (default 0x80)
- ``RP1_ENABLE_GPIO_FORCE_TEST``: Enable RP1 GPIO PCIe force test
- ``RP1_GPIO_FORCE_BANK``: GPIO bank index for force test (0..2)
- ``RP1_GPIO_FORCE_PIN``: GPIO number within the bank
- ``RP1_ENABLE_RC_BAR1_PROGRAM``: Program RC_BAR1/UBUS remap for CPU doorbell test
- ``RP1_EXPECT_RC_BAR1_LO``: Expected RC_BAR1 LO register value for comparison
- ``RP1_EXPECT_RC_BAR1_HI``: Expected RC_BAR1 HI register value for comparison
- ``RP1_EXPECT_UBUS_BAR1_LO``: Expected UBUS BAR1 LO register value
- ``RP1_EXPECT_UBUS_BAR1_HI``: Expected UBUS BAR1 HI register value
- ``RP1_EXPECT_RC_BAR2_LO``: Expected RC_BAR2 LO register value
- ``RP1_EXPECT_RC_BAR2_HI``: Expected RC_BAR2 HI register value
- ``RP1_EXPECT_UBUS_BAR2_LO``: Expected UBUS BAR2 LO register value
- ``RP1_EXPECT_UBUS_BAR2_HI``: Expected UBUS BAR2 HI register value
- ``RP1_CFG_BAR_INDEX``: BAR index for MSIX_CFG/INTSTAT
  (use 0xFF to reuse the MSI-X table BAR)
- ``RP1_CFG_BAR_PHYS_ADDR``: Optional forced MMIO address for the config BAR
- ``RP1_CFG_BAR_SIZE``: Optional forced size for the config BAR
- ``RP1_ENABLE_BAR_PROGRAM``: Program BAR1/2 if firmware left them unassigned
- ``RP1_BAR1_PHYS_ADDR``: CPU address to map RP1 BAR1 (typically 0x1f00000000)
- ``RP1_BAR1_SIZE``: Size of RP1 BAR1 window (typically 4 MiB)
- ``RP1_CFG_USE_PCIE_CFG``: Use PCIe config space for MSIX_CFG/INTSTAT (recommended)
- ``RP1_PCIE_CFG_BASE_OFFSET``: Base offset inside BAR for MSIX_CFG/INTSTAT
- ``RP1_PCIE_APBS_BASE``: RP1 PCIe APBS base inside BAR1 (default 0x108000)
- ``RP1_PCIE_REG_SET_OFFSET``: APBS SET window offset (default 0x800)
- ``RP1_PCIE_REG_CLR_OFFSET``: APBS CLR window offset (default 0xc00)
- ``RP1_USE_APBS_SETCLR``: Use SET/CLR windows for MSIX_CFG writes
- ``RP1_GPIO_PCIE_INTE_OFFSET``: IO_BANK PCIe INTE offset
- ``RP1_GPIO_PCIE_INTF_OFFSET``: IO_BANK PCIe INTF offset
- ``RP1_GPIO_PCIE_INTS_OFFSET``: IO_BANK PCIe INTS offset

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
  Use a pristine build (``west build -p always``) so the overlay is applied.
