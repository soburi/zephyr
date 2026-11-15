/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX drivers/pci/pcie_qemu_epc.c
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

#define DT_DRV_COMPAT qemu_pci_epc

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/pcie/pcie.h>
#include <zephyr/drivers/pcie/epc/pcie_epc.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pcie_qemu_epc, CONFIG_PCIE_EPC_LOG_LEVEL);

/* The epc register description */

#define QEMU_EPC_BAR_CTRL               0
#define QEMU_EPC_BAR_PCI_CFG            1
#define QEMU_EPC_BAR_BAR_CFG            2
#define QEMU_EPC_BAR_WINDOW             3

#define QEMU_EPC_CTRL_OFF_START         0x00
#define QEMU_EPC_CTRL_OFF_WIN_START     0x08
#define QEMU_EPC_CTRL_OFF_WIN_SIZE      0x10
#define QEMU_EPC_CTRL_OFF_IRQ_TYPE      0x18
#define QEMU_EPC_CTRL_OFF_IRQ_NUM       0x1c
#define QEMU_EPC_CTRL_OFF_OB_MAP_MASK   0x20
#define QEMU_EPC_CTRL_OFF_OB_IDX        0x24
#define QEMU_EPC_CTRL_OFF_OB_MAP_PHYS   0x28
#define QEMU_EPC_CTRL_OFF_OB_MAP_PCI    0x30
#define QEMU_EPC_CTRL_OFF_OB_MAP_SIZE   0x38
#define QEMU_EPC_CTRL_OFF_DB_CTRL       0x40
#define QEMU_EPC_CTRL_OFF_DB_OFFSET     0x44
#define QEMU_EPC_CTRL_OFF_DB_SIZE       0x48
#define QEMU_EPC_CTRL_OFF_DB_STATUS     0x4c

#define QEMU_EPC_DB_CTRL_ENABLE         BIT(31)
#define QEMU_EPC_DB_STATUS_PENDING      BIT(0)

/* The device model has the BARs, and so the doorbell, of one function */
#define QEMU_EPC_DB_FUNCNO              0U

#define QEMU_EPC_BAR_CFG_OFF_MASK       0x00
#define QEMU_EPC_BAR_CFG_OFF_NUMBER     0x01
#define QEMU_EPC_BAR_CFG_OFF_FLAGS      0x02
#define QEMU_EPC_BAR_CFG_OFF_RSV        0x04
#define QEMU_EPC_BAR_CFG_OFF_PHYS_ADDR  0x08
#define QEMU_EPC_BAR_CFG_OFF_SIZE       0x10
#define QEMU_EPC_BAR_CFG_SIZE           0x8000

#define QEMU_EPC_BAR_CFG_MSI            0x40
#define QEMU_EPC_BAR_CFG_MSIX           0x60

/* The capabilities of the endpoint function follow MSI and MSI-X */
#define QEMU_EPC_CAP_AREA_START         0x80
#define QEMU_EPC_CAP_AREA_END           0x100

#define QEMU_EPC_CONFIG_SPACE_SIZE      0x1000
#define QEMU_EPC_MAX_FUNCTIONS          0x8
#define QEMU_EPC_ALIGN_PAGE_SIZE        0x1000

#define QEMU_EPC_NUM_OB_WINDOWS         32

struct qemu_epc_caps_s {
	/* Offset the next capability is placed at, 0 when none was added */
	uint8_t next;
	/* Offset of the last capability, 0 when none was added */
	uint8_t tail;
};

struct qemu_epc_s {
	/* Must stay first, the device data starts with the EPC control */
	struct pcie_epc_ctrl_s epc;
	struct qemu_epc_caps_s caps[QEMU_EPC_MAX_FUNCTIONS];
	struct pcie_dev *pdev;
	mm_reg_t ctl_base;
	mm_reg_t cfg_base;
	mm_reg_t bar_base;
	uintptr_t ob_phys[QEMU_EPC_NUM_OB_WINDOWS];
	void *msi_vaddr;
	uintptr_t msi_paddr;
	uint32_t msi_data;
	void *msix_vaddr;
	void *msix_vob;
	uintptr_t msix_pob;
	struct pcie_epc_mem_s mem;
};

BUILD_ASSERT(offsetof(struct qemu_epc_s, epc) == 0,
	     "the EPC control structure must be the first member");

K_HEAP_DEFINE(g_qemu_epc_dmaheap, CONFIG_PCIE_EPC_QEMU_DMA_HEAP_SIZE);

SYS_BITARRAY_DEFINE_STATIC(g_qemu_epc_ob_bitmap,
			   CONFIG_PCIE_EPC_QEMU_WINDOW_PAGES);

static const struct pcie_epc_features_s g_qemu_epc_features = {
	.linkup_notifier = false,
	.core_init_notifier = false,
	.msi_capable = true,
	.msix_capable = true,
	.capability_writable = true,
};

struct qemu_epc_config_s {
	struct pcie_dev *pcie;
	/* The BAR range that raises the doorbell, valid with has_doorbell */
	bool has_doorbell;
	uint8_t doorbell_bar;
	uint8_t doorbell_width;
	uint32_t doorbell_offset;
	unsigned int irq_priority;
	uint32_t irq_flags;
};

static void qemu_epc_ctl_write8(struct qemu_epc_s *qep, unsigned int offset,
				uint8_t value)
{
	sys_write8(value, qep->ctl_base + offset);
}

static void qemu_epc_ctl_write32(struct qemu_epc_s *qep, unsigned int offset,
				 uint32_t value)
{
	sys_write32(value, qep->ctl_base + offset);
}

static void qemu_epc_ctl_write64(struct qemu_epc_s *qep, unsigned int offset,
				 uint64_t value)
{
	sys_write64(value, qep->ctl_base + offset);
}

static uint32_t qemu_epc_ctl_read32(struct qemu_epc_s *qep,
				    unsigned int offset)
{
	return sys_read32(qep->ctl_base + offset);
}

static uint64_t qemu_epc_ctl_read64(struct qemu_epc_s *qep,
				    unsigned int offset)
{
	return sys_read64(qep->ctl_base + offset);
}

static uint16_t qemu_epc_cfg_read16(struct qemu_epc_s *qep,
				    unsigned int offset)
{
	return sys_read16(qep->cfg_base + offset);
}

static uint32_t qemu_epc_cfg_read32(struct qemu_epc_s *qep,
				    unsigned int offset)
{
	return sys_read32(qep->cfg_base + offset);
}

static uint64_t qemu_epc_cfg_read64(struct qemu_epc_s *qep,
				    unsigned int offset)
{
	return sys_read64(qep->cfg_base + offset);
}

static void qemu_epc_cfg_write8(struct qemu_epc_s *qep, unsigned int offset,
				uint8_t value)
{
	sys_write8(value, qep->cfg_base + offset);
}

static void qemu_epc_cfg_write16(struct qemu_epc_s *qep, unsigned int offset,
				 uint16_t value)
{
	sys_write16(value, qep->cfg_base + offset);
}

static void qemu_epc_cfg_write32(struct qemu_epc_s *qep, unsigned int offset,
				 uint32_t value)
{
	sys_write32(value, qep->cfg_base + offset);
}

static uint8_t qemu_epc_bar_cfg_read8(struct qemu_epc_s *qep,
				      unsigned int offset)
{
	return sys_read8(qep->bar_base + offset);
}

static void qemu_epc_bar_cfg_write8(struct qemu_epc_s *qep,
				    unsigned int offset, uint8_t value)
{
	sys_write8(value, qep->bar_base + offset);
}

static void qemu_epc_bar_cfg_write64(struct qemu_epc_s *qep,
				     unsigned int offset, uint64_t value)
{
	sys_write64(value, qep->bar_base + offset);
}

static uint32_t qemu_epc_func_base(struct qemu_epc_s *qep, uint8_t funcno)
{
	ARG_UNUSED(qep);

	return QEMU_EPC_CONFIG_SPACE_SIZE * funcno;
}

static int qemu_epc_write_header(const struct device *dev, uint8_t funcno,
				 struct pcie_epf_header_s *hdr)
{
	struct qemu_epc_s *qep = dev->data;
	uint32_t base;

	base = qemu_epc_func_base(qep, funcno);
	qemu_epc_cfg_write16(qep, base + PCI_VENDOR_ID, hdr->vendorid);
	qemu_epc_cfg_write16(qep, base + PCI_DEVICE_ID, hdr->deviceid);
	qemu_epc_cfg_write8(qep, base + PCI_REVISION_ID, hdr->revid);
	qemu_epc_cfg_write8(qep, base + PCI_CLASS_PROG, hdr->progif_code);
	qemu_epc_cfg_write8(qep, base + PCI_HEADER_TYPE, 0x80);
	qemu_epc_cfg_write8(qep, base + PCI_CLASS_DEVICE, hdr->baseclass_code);
	qemu_epc_cfg_write8(qep, base + PCI_CLASS_DEVICE + 1,
			    hdr->subclass_code);
	qemu_epc_cfg_write8(qep, base + PCI_CACHE_LINE_SIZE,
			    hdr->cache_line_size);
	qemu_epc_cfg_write16(qep, base + PCI_SUBSYSTEM_VENDOR_ID,
			     hdr->subsys_vendor_id);
	qemu_epc_cfg_write16(qep, base + PCI_SUBSYSTEM_ID, hdr->subsys_id);
	qemu_epc_cfg_write8(qep, base + PCI_INTERRUPT_PIN, hdr->interrupt_pin);

	return 0;
}

static int qemu_epc_add_capability(const struct device *dev, uint8_t funcno,
				   uint8_t id, const void *body, size_t len)
{
	struct qemu_epc_s *qep = dev->data;
	struct qemu_epc_caps_s *caps = &qep->caps[funcno];
	const uint8_t *bytes = body;
	/* The capability is dword aligned and starts with its ID and link */
	const size_t size = ROUND_UP(len + 2U, 4U);
	uint32_t base = qemu_epc_func_base(qep, funcno);
	uint8_t off = (caps->next != 0U) ? caps->next : QEMU_EPC_CAP_AREA_START;
	uint16_t status;

	if (size > QEMU_EPC_CAP_AREA_END - off) {
		return -ENOSPC;
	}

	for (size_t i = 0; i < size; i++) {
		/* The first two bytes link the capability, the tail is padding */
		uint8_t val = (i >= 2U && i - 2U < len) ? bytes[i - 2U] : 0U;

		qemu_epc_cfg_write8(qep, base + off + i, (i == 0U) ? id : val);
	}

	/* Link the capability once it is complete */
	if (caps->tail == 0U) {
		qemu_epc_cfg_write8(qep, base + PCI_CAPABILITY_LIST, off);
		status = qemu_epc_cfg_read16(qep, base + PCI_STATUS);
		qemu_epc_cfg_write16(qep, base + PCI_STATUS,
				     status | PCI_STATUS_CAP_LIST);
	} else {
		qemu_epc_cfg_write8(qep, base + caps->tail + PCI_CAP_LIST_NEXT, off);
	}

	caps->tail = off;
	caps->next = off + size;

	return 0;
}

static void qemu_epc_clear_capabilities(const struct device *dev, uint8_t funcno)
{
	struct qemu_epc_s *qep = dev->data;
	struct qemu_epc_caps_s *caps = &qep->caps[funcno];
	uint32_t base = qemu_epc_func_base(qep, funcno);
	uint16_t status;

	if (caps->tail == 0U) {
		return;
	}

	for (uint32_t off = QEMU_EPC_CAP_AREA_START; off < caps->next; off++) {
		qemu_epc_cfg_write8(qep, base + off, 0U);
	}

	qemu_epc_cfg_write8(qep, base + PCI_CAPABILITY_LIST, 0U);
	status = qemu_epc_cfg_read16(qep, base + PCI_STATUS);
	qemu_epc_cfg_write16(qep, base + PCI_STATUS, status & ~PCI_STATUS_CAP_LIST);
	caps->next = 0U;
	caps->tail = 0U;
}

static int qemu_epc_set_bar(const struct device *dev, uint8_t funcno,
			    struct pcie_epf_bar_s *bar)
{
	struct qemu_epc_s *qep = dev->data;
	uint32_t base;
	uint8_t mask;

	base = qemu_epc_func_base(qep, funcno);
	qemu_epc_bar_cfg_write8(qep, base + QEMU_EPC_BAR_CFG_OFF_NUMBER,
				bar->barno);
	qemu_epc_bar_cfg_write64(qep, base + QEMU_EPC_BAR_CFG_OFF_PHYS_ADDR,
				 bar->phys_addr);
	qemu_epc_bar_cfg_write64(qep, base + QEMU_EPC_BAR_CFG_OFF_SIZE,
				 bar->size);
	qemu_epc_bar_cfg_write8(qep, base + QEMU_EPC_BAR_CFG_OFF_FLAGS,
				bar->flags);
	mask = qemu_epc_bar_cfg_read8(qep, base + QEMU_EPC_BAR_CFG_OFF_MASK) |
	       BIT(bar->barno);
	qemu_epc_bar_cfg_write8(qep, base + QEMU_EPC_BAR_CFG_OFF_MASK, mask);

	return 0;
}

static void qemu_epc_clear_bar(const struct device *dev, uint8_t funcno,
			       struct pcie_epf_bar_s *bar)
{
	struct qemu_epc_s *qep = dev->data;
	uint32_t base;
	uint8_t mask;

	base = qemu_epc_func_base(qep, funcno);
	mask = qemu_epc_bar_cfg_read8(qep, base + QEMU_EPC_BAR_CFG_OFF_MASK) &
	       ~BIT(bar->barno);
	qemu_epc_bar_cfg_write8(qep, base + QEMU_EPC_BAR_CFG_OFF_MASK, mask);
}

static int qemu_epc_map_addr(const struct device *dev, uint8_t funcno,
			     uintptr_t addr, uint64_t pci_addr, size_t size)
{
	struct qemu_epc_s *qep = dev->data;
	uint32_t idx = 0;
	uint32_t base;
	uint32_t mask;

	base = qemu_epc_func_base(qep, funcno);
	mask = qemu_epc_ctl_read32(qep, base + QEMU_EPC_CTRL_OFF_OB_MAP_MASK);
	while ((mask & BIT(idx)) != 0U) {
		if (++idx >= QEMU_EPC_NUM_OB_WINDOWS) {
			return -ENOMEM;
		}
	}

	qemu_epc_ctl_write32(qep, base + QEMU_EPC_CTRL_OFF_OB_IDX, idx);
	qemu_epc_ctl_write64(qep, base + QEMU_EPC_CTRL_OFF_OB_MAP_PHYS, addr);
	qemu_epc_ctl_write64(qep, base + QEMU_EPC_CTRL_OFF_OB_MAP_PCI,
			     pci_addr);
	qemu_epc_ctl_write64(qep, base + QEMU_EPC_CTRL_OFF_OB_MAP_SIZE, size);
	qemu_epc_ctl_write32(qep, base + QEMU_EPC_CTRL_OFF_OB_MAP_MASK,
			     mask | BIT(idx));
	qep->ob_phys[idx] = addr;

	return 0;
}

static void qemu_epc_unmap_addr(const struct device *dev, uint8_t funcno,
				uintptr_t addr)
{
	struct qemu_epc_s *qep = dev->data;
	uint32_t base;
	uint32_t mask;
	int i;

	base = qemu_epc_func_base(qep, funcno);
	mask = qemu_epc_ctl_read32(qep, base + QEMU_EPC_CTRL_OFF_OB_MAP_MASK);
	for (i = 0; i < QEMU_EPC_NUM_OB_WINDOWS; i++) {
		if (qep->ob_phys[i] == addr) {
			mask &= ~BIT(i);
			qemu_epc_ctl_write32(qep,
					     base +
					     QEMU_EPC_CTRL_OFF_OB_MAP_MASK,
					     mask);
			break;
		}
	}
}

static void *qemu_epc_get_bar_addr_from_funcno(const struct device *dev,
					       uint8_t funcno, uint8_t bar)
{
	struct pcie_epc_ctrl_s *epc = pcie_epc_get_ctrl(dev);
	struct pcie_epf_device_s *epf;

	SYS_DLIST_FOR_EACH_CONTAINER(&epc->epf, epf, epc_node) {
		if (epf->funcno == funcno) {
			return epf->bar[bar].addr;
		}
	}

	return NULL;
}

static int qemu_epc_get_msix(const struct device *dev, uint8_t funcno)
{
	struct qemu_epc_s *qep = dev->data;
	uint16_t flags;
	uint32_t base;

	base = qemu_epc_func_base(qep, funcno);
	flags = qemu_epc_cfg_read16(qep, base + QEMU_EPC_BAR_CFG_MSIX +
				    PCI_MSIX_FLAGS);
	if ((flags & PCI_MSIX_FLAGS_ENABLE) == 0U) {
		return -EINVAL;
	}

	return flags & PCI_MSIX_FLAGS_QSIZE;
}

static int qemu_epc_raise_irq(const struct device *dev, uint8_t funcno,
			      enum pcie_epc_irq_type type,
			      uint16_t interrupt_num)
{
	struct qemu_epc_s *qep = dev->data;
	uint64_t pci_addr;
	uint32_t offset;
	uint32_t data;
	uint32_t base;

	base = qemu_epc_func_base(qep, funcno);
	switch (type) {
	case PCIE_EPC_IRQ_LEGACY:
		qemu_epc_ctl_write32(qep, base + QEMU_EPC_CTRL_OFF_IRQ_TYPE,
				     type);
		qemu_epc_ctl_write32(qep, base + QEMU_EPC_CTRL_OFF_IRQ_NUM,
				     interrupt_num);
		return 0;
	case PCIE_EPC_IRQ_MSI:
		if (qep->msi_vaddr == NULL) {
			uint16_t flags =
				qemu_epc_cfg_read16(qep,
						    base +
						    QEMU_EPC_BAR_CFG_MSI +
						    PCI_MSI_FLAGS);

			if ((flags & PCI_MSI_FLAGS_64BIT) != 0U) {
				pci_addr = qemu_epc_cfg_read64(
					qep, base + QEMU_EPC_BAR_CFG_MSI +
					PCI_MSI_ADDRESS_LO);
				qep->msi_data = qemu_epc_cfg_read32(
					qep, base + QEMU_EPC_BAR_CFG_MSI +
					PCI_MSI_DATA_64);
			} else {
				pci_addr = qemu_epc_cfg_read32(
					qep, base + QEMU_EPC_BAR_CFG_MSI +
					PCI_MSI_ADDRESS_LO);
				qep->msi_data = qemu_epc_cfg_read32(
					qep, base + QEMU_EPC_BAR_CFG_MSI +
					PCI_MSI_DATA_32);
			}

			if (pci_addr == 0U || qep->msi_data == 0U) {
				return -EINVAL;
			}

			qep->msi_vaddr = pcie_epc_mem_alloc_addr(
				dev, &qep->msi_paddr, 0x1000);
			if (qep->msi_vaddr == NULL) {
				return -ENOMEM;
			}

			qemu_epc_map_addr(dev, funcno, qep->msi_paddr,
					  pci_addr, 0x1000);
		}

		sys_write64(qep->msi_data, (mem_addr_t)qep->msi_vaddr);
		return 0;
	case PCIE_EPC_IRQ_MSIX:
		if (--interrupt_num > qemu_epc_get_msix(dev, funcno)) {
			return -EINVAL;
		}

		data = qemu_epc_cfg_read32(qep, base + QEMU_EPC_BAR_CFG_MSIX +
					   PCI_MSIX_TABLE);
		offset = data & PCI_MSIX_TABLE_OFFSET;
		if (qep->msix_vaddr == NULL) {
			uint8_t bar = data & PCI_MSIX_TABLE_BIR;

			qep->msix_vaddr = qemu_epc_get_bar_addr_from_funcno(
				dev, funcno, bar);
			if (qep->msix_vaddr == NULL) {
				return -EINVAL;
			}

			qep->msix_vob = pcie_epc_mem_alloc_addr(
				dev, &qep->msix_pob, 0x1000);
			if (qep->msix_vob == NULL) {
				return -ENOMEM;
			}

			pci_addr = sys_read64(
				(mem_addr_t)((uint8_t *)qep->msix_vaddr +
					     offset));
			qemu_epc_map_addr(dev, funcno, qep->msix_pob,
					  pci_addr, 0x1000);
		}

		data = sys_read32(
			(mem_addr_t)((uint8_t *)qep->msix_vaddr + offset +
				     interrupt_num * PCI_MSIX_ENTRY_SIZE +
				     PCI_MSIX_ENTRY_DATA));
		sys_write64(data, (mem_addr_t)qep->msix_vob);
		return 0;
	default:
		LOG_ERR("Failed to raise IRQ, unknown type");
		return -EINVAL;
	}
}

static int qemu_epc_start(const struct device *dev)
{
	struct qemu_epc_s *qep = dev->data;

	qemu_epc_ctl_write8(qep, QEMU_EPC_CTRL_OFF_START, 1);
	return 0;
}

static const struct pcie_epc_features_s *
qemu_epc_get_features(const struct device *dev, uint8_t funcno)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(funcno);

	return &g_qemu_epc_features;
}

static int qemu_epc_get_msi(const struct device *dev, uint8_t funcno)
{
	struct qemu_epc_s *qep = dev->data;
	uint16_t flags;
	uint32_t base;

	base = qemu_epc_func_base(qep, funcno);
	flags = qemu_epc_cfg_read16(qep, base + QEMU_EPC_BAR_CFG_MSI +
				    PCI_MSI_FLAGS);
	if ((flags & PCI_MSI_FLAGS_ENABLE) == 0U) {
		LOG_ERR("msi is not enabled");
		return -EINVAL;
	}

	return (flags & PCI_MSI_FLAGS_QSIZE) >> PCI_MSI_FLAGS_QSIZE_SHIFT;
}

static int qemu_epc_set_msi(const struct device *dev, uint8_t funcno,
			    uint8_t interrupts)
{
	struct qemu_epc_s *qep = dev->data;
	uint16_t flags;
	uint32_t base;

	base = qemu_epc_func_base(qep, funcno);
	flags = qemu_epc_cfg_read16(qep, base + QEMU_EPC_BAR_CFG_MSI +
				    PCI_MSI_FLAGS);
	flags |= (interrupts << PCI_MSI_FLAGS_QMASK_SHIFT) &
		 PCI_MSI_FLAGS_QMASK;
	qemu_epc_cfg_write16(qep, base + QEMU_EPC_BAR_CFG_MSI + PCI_MSI_FLAGS,
			     flags);
	return 0;
}

static int qemu_epc_set_msix(const struct device *dev, uint8_t funcno,
			     uint16_t interrupts, int barno, uint32_t offset)
{
	struct qemu_epc_s *qep = dev->data;
	uint32_t data;
	uint16_t flags;
	uint32_t base;

	base = qemu_epc_func_base(qep, funcno);
	flags = qemu_epc_cfg_read16(qep, base + QEMU_EPC_BAR_CFG_MSIX +
				    PCI_MSIX_FLAGS);
	flags &= ~PCI_MSIX_FLAGS_QSIZE;
	flags |= interrupts;
	qemu_epc_cfg_write16(qep,
			     base + QEMU_EPC_BAR_CFG_MSIX + PCI_MSIX_FLAGS,
			     flags);
	data = offset | barno;
	qemu_epc_cfg_write32(qep,
			     base + QEMU_EPC_BAR_CFG_MSIX + PCI_MSIX_TABLE,
			     data);
	data = (offset + (interrupts * PCI_MSIX_ENTRY_SIZE)) | barno;
	qemu_epc_cfg_write32(qep,
			     base + QEMU_EPC_BAR_CFG_MSIX + PCI_MSIX_PBA,
			     data);

	return 0;
}

static DEVICE_API(pcie_epc, qemu_epc_api) = {
	.write_header = qemu_epc_write_header,
	.add_capability = qemu_epc_add_capability,
	.clear_capabilities = qemu_epc_clear_capabilities,
	.set_bar      = qemu_epc_set_bar,
	.clear_bar    = qemu_epc_clear_bar,
	.map_addr     = qemu_epc_map_addr,
	.unmap_addr   = qemu_epc_unmap_addr,
	.raise_irq    = qemu_epc_raise_irq,
	.start        = qemu_epc_start,
	.get_features = qemu_epc_get_features,
	.set_msi      = qemu_epc_set_msi,
	.get_msi      = qemu_epc_get_msi,
	.set_msix     = qemu_epc_set_msix,
	.get_msix     = qemu_epc_get_msix,
};

#ifdef CONFIG_PCIE_EPC_QEMU_DOORBELL
static void qemu_epc_doorbell_isr(const void *arg)
{
	const struct device *dev = arg;
	struct qemu_epc_s *qep = dev->data;

	/* The interrupt line can be shared with other devices */
	if ((qemu_epc_ctl_read32(qep, QEMU_EPC_CTRL_OFF_DB_STATUS) &
	     QEMU_EPC_DB_STATUS_PENDING) == 0U) {
		return;
	}

	/* Acknowledge first, a later write then interrupts again */
	qemu_epc_ctl_write32(qep, QEMU_EPC_CTRL_OFF_DB_STATUS,
			     QEMU_EPC_DB_STATUS_PENDING);
	pcie_epc_doorbell_notify(dev, QEMU_EPC_DB_FUNCNO, 0);
}

static int qemu_epc_doorbell_init(const struct device *dev)
{
	const struct qemu_epc_config_s *cfg = dev->config;
	struct qemu_epc_s *qep = dev->data;
	unsigned int irq;

	irq = pcie_alloc_irq(cfg->pcie->bdf);
	if (irq == PCIE_CONF_INTR_IRQ_NONE) {
		LOG_ERR("Cannot allocate the doorbell interrupt");
		return -ENODEV;
	}

	if (!pcie_connect_dynamic_irq(cfg->pcie->bdf, irq, cfg->irq_priority,
				      qemu_epc_doorbell_isr, dev,
				      cfg->irq_flags)) {
		LOG_ERR("Cannot connect the doorbell interrupt");
		return -EIO;
	}

	qemu_epc_ctl_write32(qep, QEMU_EPC_CTRL_OFF_DB_STATUS,
			     QEMU_EPC_DB_STATUS_PENDING);
	qemu_epc_ctl_write32(qep, QEMU_EPC_CTRL_OFF_DB_OFFSET, cfg->doorbell_offset);
	qemu_epc_ctl_write32(qep, QEMU_EPC_CTRL_OFF_DB_SIZE, cfg->doorbell_width);
	qemu_epc_ctl_write32(qep, QEMU_EPC_CTRL_OFF_DB_CTRL,
			     QEMU_EPC_DB_CTRL_ENABLE | cfg->doorbell_bar);
	pcie_irq_enable(cfg->pcie->bdf, irq);

	return 0;
}
#endif /* CONFIG_PCIE_EPC_QEMU_DOORBELL */

static int qemu_epc_map_bar(pcie_bdf_t bdf, uint8_t barno, mm_reg_t *base)
{
	struct pcie_bar mbar;

	if (!pcie_get_mbar(bdf, barno, &mbar)) {
		return -ENODEV;
	}

	device_map(base, mbar.phys_addr, mbar.size, K_MEM_CACHE_NONE);
	return 0;
}

static int qemu_epc_init(const struct device *dev)
{
	const struct qemu_epc_config_s *cfg = dev->config;
	struct qemu_epc_s *qep = dev->data;
	size_t win_pages;
	uintptr_t phys;
	mm_reg_t virt;
	size_t size;
	int ret;

	if (cfg->pcie->bdf == PCIE_BDF_NONE) {
		LOG_ERR("QEMU EPC device not found");
		return -ENODEV;
	}

	pcie_set_cmd(cfg->pcie->bdf,
		     PCIE_CONF_CMDSTAT_MEM | PCIE_CONF_CMDSTAT_MASTER, true);

	ret = qemu_epc_map_bar(cfg->pcie->bdf, QEMU_EPC_BAR_PCI_CFG,
			       &qep->cfg_base);
	if (ret != 0) {
		LOG_ERR("Cannot map device registers");
		return ret;
	}

	ret = qemu_epc_map_bar(cfg->pcie->bdf, QEMU_EPC_BAR_BAR_CFG,
			       &qep->bar_base);
	if (ret != 0) {
		LOG_ERR("Cannot map device register for bar");
		return ret;
	}

	ret = qemu_epc_map_bar(cfg->pcie->bdf, QEMU_EPC_BAR_CTRL,
			       &qep->ctl_base);
	if (ret != 0) {
		LOG_ERR("Cannot map ctrl register");
		return ret;
	}

	qep->pdev = cfg->pcie;

	ret = pcie_epc_create(dev, "qemu_epc", &g_qemu_epc_dmaheap);
	if (ret != 0) {
		LOG_ERR("Failed to create qemu_epc device");
		return ret;
	}

	qep->epc.max_functions = QEMU_EPC_MAX_FUNCTIONS;

	phys = qemu_epc_ctl_read64(qep, QEMU_EPC_CTRL_OFF_WIN_START);
	size = qemu_epc_ctl_read64(qep, QEMU_EPC_CTRL_OFF_WIN_SIZE);

	/* The backing bitmap is statically sized, clamp the usable part of
	 * the window accordingly.
	 */
	win_pages = MIN(size / QEMU_EPC_ALIGN_PAGE_SIZE,
			CONFIG_PCIE_EPC_QEMU_WINDOW_PAGES);
	size = win_pages * QEMU_EPC_ALIGN_PAGE_SIZE;

	device_map(&virt, phys, size, K_MEM_CACHE_NONE);

	qep->mem.bitmap = &g_qemu_epc_ob_bitmap;
	ret = pcie_epc_mem_init(dev, &qep->mem, (void *)virt, phys, size,
			       QEMU_EPC_ALIGN_PAGE_SIZE);
	if (ret != 0) {
		LOG_ERR("epc mem init error");
		pcie_epc_destroy(dev);
		return ret;
	}

#ifdef CONFIG_PCIE_EPC_QEMU_DOORBELL
	if (cfg->has_doorbell) {
		ret = qemu_epc_doorbell_init(dev);
		if (ret != 0) {
			pcie_epc_mem_exit(dev);
			pcie_epc_destroy(dev);
			return ret;
		}
	}
#endif

	return 0;
}

/* The doorbell is the range of a BAR the devicetree node selects */
#define QEMU_EPC_DOORBELL_INIT(n)					\
	.has_doorbell = true,						\
	.doorbell_bar = DT_INST_PROP(n, doorbell_bar),			\
	.doorbell_width = DT_INST_PROP(n, doorbell_width),		\
	.doorbell_offset = DT_INST_PROP(n, doorbell_offset),		\
	.irq_priority = DT_INST_IRQ(n, priority),			\
	.irq_flags = DT_INST_IRQ(n, flags),

#define QEMU_EPC_HAS_DOORBELL(n)					\
	UTIL_AND(IS_ENABLED(CONFIG_PCIE_EPC_QEMU_DOORBELL),		\
		 DT_INST_NODE_HAS_PROP(n, doorbell_offset))

#define QEMU_EPC_DEVICE_INIT(n)						\
	BUILD_ASSERT(n == 0, "only one QEMU EPC instance is supported");\
	DEVICE_PCIE_INST_DECLARE(n);					\
	static struct qemu_epc_s qemu_epc_data_##n;			\
	static const struct qemu_epc_config_s qemu_epc_config_##n = {	\
		DEVICE_PCIE_INST_INIT(n, pcie),				\
		IF_ENABLED(QEMU_EPC_HAS_DOORBELL(n),			\
			   (QEMU_EPC_DOORBELL_INIT(n)))			\
	};								\
	DEVICE_DT_INST_DEFINE(n, qemu_epc_init, NULL,			\
			      &qemu_epc_data_##n, &qemu_epc_config_##n,	\
			      POST_KERNEL, CONFIG_PCIE_EPC_INIT_PRIORITY,\
			      &qemu_epc_api);

DT_INST_FOREACH_STATUS_OKAY(QEMU_EPC_DEVICE_INIT)
