/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX drivers/pci/pcie_epf_test.c
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

#include <string.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/util.h>
#include <zephyr/logging/log.h>
#include <zephyr/drivers/pcie/epc/pcie_epc.h>
#include <zephyr/drivers/pcie/epc/pcie_epf.h>
#include <zephyr/drivers/pcie/epc/pcie_epf_test.h>

LOG_MODULE_REGISTER(pcie_epf_test, CONFIG_PCIE_EPC_LOG_LEVEL);

#define PCIE_EPF_TEST_IRQ_TYPE_LEGACY           0
#define PCIE_EPF_TEST_IRQ_TYPE_MSI              1
#define PCIE_EPF_TEST_IRQ_TYPE_MSIX             2

#define PCIE_EPF_TEST_COMMAND_RAISE_LEGACY_IRQ  BIT(0)
#define PCIE_EPF_TEST_COMMAND_RAISE_MSI_IRQ     BIT(1)
#define PCIE_EPF_TEST_COMMAND_RAISE_MSIX_IRQ    BIT(2)
#define PCIE_EPF_TEST_COMMAND_READ              BIT(3)
#define PCIE_EPF_TEST_COMMAND_WRITE             BIT(4)
#define PCIE_EPF_TEST_COMMAND_COPY              BIT(5)

#define PCIE_EPF_TEST_STATUS_READ_SUCCESS       BIT(0)
#define PCIE_EPF_TEST_STATUS_READ_FAIL          BIT(1)
#define PCIE_EPF_TEST_STATUS_WRITE_SUCCESS      BIT(2)
#define PCIE_EPF_TEST_STATUS_WRITE_FAIL         BIT(3)
#define PCIE_EPF_TEST_STATUS_COPY_SUCCESS       BIT(4)
#define PCIE_EPF_TEST_STATUS_COPY_FAIL          BIT(5)
#define PCIE_EPF_TEST_STATUS_IRQ_RAISED         BIT(6)
#define PCIE_EPF_TEST_STATUS_SRC_ADDR_INVALID   BIT(7)
#define PCIE_EPF_TEST_STATUS_DST_ADDR_INVALID   BIT(8)

#define PCIE_EPF_TEST_WORK_PERIOD               K_MSEC(10)

#define PCI_CLASS_OTHERS                       0xff

struct pcie_epf_test_reg_s {
	uint32_t magic;
	uint32_t command;
	uint32_t status;
	uint64_t src_addr;
	uint64_t dst_addr;
	uint32_t size;
	uint32_t checksum;
	uint32_t irq_type;
	uint32_t irq_number;
	uint32_t flags;
} __packed;

struct pcie_epf_test_s {
	void *reg[PCI_STD_NUM_BARS];
	struct pcie_epf_device_s *epf;
	int test_reg_bar;
	size_t msix_table_offset;
	struct k_work_delayable work;
	struct pcie_epf_header_s header;
	int bar_size[PCI_STD_NUM_BARS];
};

static void pcie_epf_test_unbind(struct pcie_epf_device_s *epf);
static int pcie_epf_test_bind(struct pcie_epf_device_s *epf);
static int pcie_epf_test_core_init(struct pcie_epf_device_s *epf);
static int pcie_epf_test_link_up(struct pcie_epf_device_s *epf);

static const struct pcie_epf_ops_s g_pcie_epf_test_ops = {
	.unbind = pcie_epf_test_unbind,
	.bind   = pcie_epf_test_bind,
};

static const struct pcie_epc_event_ops_s g_pcie_epf_test_event_ops = {
	.core_init = pcie_epf_test_core_init,
	.link_up   = pcie_epf_test_link_up,
};

static struct pcie_epf_test_s g_pcie_epf_test[CONFIG_PCIE_EPF_TEST_FUNC_COUNT];
static struct pcie_epf_device_s g_pcie_epf_test_device[
	CONFIG_PCIE_EPF_TEST_FUNC_COUNT];

/* Equivalent of the Linux crc32_le(~0, buf, size) used by the host side
 * pci_endpoint_test driver.
 */
static uint32_t pcie_epf_test_crc32(const void *buf, size_t size)
{
	return ~crc32_ieee_update(0, buf, size);
}

/* Use naturally aligned word accesses for the memory mapped EPC window.
 * Byte copies turn each byte into a separate emulated PCI transaction.
 */
static void pcie_epf_test_transfer(void *dst, const void *src, size_t size)
{
	volatile uint8_t *d = dst;
	const volatile uint8_t *s = src;

	if ((((uintptr_t)dst | (uintptr_t)src) & (sizeof(uint64_t) - 1)) == 0) {
		while (size >= sizeof(uint64_t)) {
			*(volatile uint64_t *)d = *(const volatile uint64_t *)s;
			d += sizeof(uint64_t);
			s += sizeof(uint64_t);
			size -= sizeof(uint64_t);
		}
	}
	while (size-- > 0) {
		*d++ = *s++;
	}
}

static int pcie_epf_test_copy(struct pcie_epf_test_s *test)
{
	struct pcie_epf_device_s *epf = test->epf;
	const struct device *epc = epf->epc;
	struct pcie_epf_test_reg_s *reg;
	void *vsrc_addr;
	void *vdst_addr;
	uintptr_t src_addr;
	uintptr_t dst_addr;
	int ret;

	reg = test->reg[test->test_reg_bar];
	vsrc_addr = pcie_epc_mem_alloc_addr(epc, &src_addr, reg->size);
	if (vsrc_addr == NULL) {
		LOG_ERR("Failed to allocate source address");
		reg->status = PCIE_EPF_TEST_STATUS_SRC_ADDR_INVALID;
		return -ENOMEM;
	}

	ret = pcie_epc_map_addr(epc, epf->funcno, src_addr, reg->src_addr,
			       reg->size);
	if (ret < 0) {
		LOG_ERR("Failed to map source address");
		reg->status = PCIE_EPF_TEST_STATUS_SRC_ADDR_INVALID;
		goto err_src_addr;
	}

	vdst_addr = pcie_epc_mem_alloc_addr(epc, &dst_addr, reg->size);
	if (vdst_addr == NULL) {
		LOG_ERR("Failed to allocate destination address");
		reg->status = PCIE_EPF_TEST_STATUS_DST_ADDR_INVALID;
		ret = -ENOMEM;
		goto err_src_map_addr;
	}

	ret = pcie_epc_map_addr(epc, epf->funcno, dst_addr, reg->dst_addr,
			       reg->size);
	if (ret < 0) {
		LOG_ERR("Failed to map destination address");
		reg->status = PCIE_EPF_TEST_STATUS_DST_ADDR_INVALID;
		goto err_dst_addr;
	}

	pcie_epf_test_transfer(vdst_addr, vsrc_addr, reg->size);
	pcie_epc_unmap_addr(epc, epf->funcno, dst_addr);

err_dst_addr:
	pcie_epc_mem_free_addr(epc, dst_addr, reg->size);
err_src_map_addr:
	pcie_epc_unmap_addr(epc, epf->funcno, src_addr);
err_src_addr:
	pcie_epc_mem_free_addr(epc, src_addr, reg->size);
	return ret;
}

static int pcie_epf_test_read(struct pcie_epf_test_s *test)
{
	struct pcie_epf_device_s *epf = test->epf;
	const struct device *epc = epf->epc;
	struct pcie_epf_test_reg_s *reg;
	void *vsrc_addr;
	uintptr_t src_addr;
	void *buf;
	int ret;

	reg = test->reg[test->test_reg_bar];
	vsrc_addr = pcie_epc_mem_alloc_addr(epc, &src_addr, reg->size);
	if (vsrc_addr == NULL) {
		LOG_ERR("Failed to allocate address");
		reg->status = PCIE_EPF_TEST_STATUS_SRC_ADDR_INVALID;
		return -ENOMEM;
	}

	ret = pcie_epc_map_addr(epc, epf->funcno, src_addr, reg->src_addr,
			       reg->size);
	if (ret < 0) {
		LOG_ERR("Failed to map address");
		reg->status = PCIE_EPF_TEST_STATUS_SRC_ADDR_INVALID;
		goto err_addr;
	}

	buf = pcie_epc_dma_alloc(epc, reg->size);
	if (buf == NULL) {
		ret = -ENOMEM;
		goto err_map_addr;
	}

	pcie_epf_test_transfer(buf, vsrc_addr, reg->size);
	if (reg->checksum != pcie_epf_test_crc32(buf, reg->size)) {
		ret = -EIO;
	}

	pcie_epc_dma_free(epc, buf);

err_map_addr:
	pcie_epc_unmap_addr(epc, epf->funcno, src_addr);
err_addr:
	pcie_epc_mem_free_addr(epc, src_addr, reg->size);
	return ret;
}

static int pcie_epf_test_write(struct pcie_epf_test_s *test)
{
	struct pcie_epf_device_s *epf = test->epf;
	const struct device *epc = epf->epc;
	struct pcie_epf_test_reg_s *reg;
	void *vdst_addr;
	uintptr_t dst_addr;
	void *buf;
	int ret;

	reg = test->reg[test->test_reg_bar];
	vdst_addr = pcie_epc_mem_alloc_addr(epc, &dst_addr, reg->size);
	if (vdst_addr == NULL) {
		LOG_ERR("Failed to allocate address");
		reg->status = PCIE_EPF_TEST_STATUS_DST_ADDR_INVALID;
		return -ENOMEM;
	}

	ret = pcie_epc_map_addr(epc, epf->funcno, dst_addr, reg->dst_addr,
			       reg->size);
	if (ret < 0) {
		LOG_ERR("Failed to map address");
		reg->status = PCIE_EPF_TEST_STATUS_DST_ADDR_INVALID;
		goto err_addr;
	}

	buf = pcie_epc_dma_alloc(epc, reg->size);
	if (buf == NULL) {
		ret = -ENOMEM;
		goto err_map_addr;
	}

	reg->checksum = pcie_epf_test_crc32(buf, reg->size);
	pcie_epf_test_transfer(vdst_addr, buf, reg->size);
	pcie_epc_dma_free(epc, buf);

err_map_addr:
	pcie_epc_unmap_addr(epc, epf->funcno, dst_addr);
err_addr:
	pcie_epc_mem_free_addr(epc, dst_addr, reg->size);
	return ret;
}

static void pcie_epf_test_raise_irq(struct pcie_epf_test_s *test,
				   uint8_t irq_type, uint16_t irq)
{
	struct pcie_epf_device_s *epf = test->epf;
	const struct device *epc = epf->epc;
	struct pcie_epf_test_reg_s *reg;

	reg = test->reg[test->test_reg_bar];
	reg->status |= PCIE_EPF_TEST_STATUS_IRQ_RAISED;

	switch (irq_type) {
	case PCIE_EPF_TEST_IRQ_TYPE_LEGACY:
		pcie_epc_raise_irq(epc, epf->funcno, PCIE_EPC_IRQ_LEGACY, 0);
		break;
	case PCIE_EPF_TEST_IRQ_TYPE_MSI:
		pcie_epc_raise_irq(epc, epf->funcno, PCIE_EPC_IRQ_MSI, irq);
		break;
	case PCIE_EPF_TEST_IRQ_TYPE_MSIX:
		pcie_epc_raise_irq(epc, epf->funcno, PCIE_EPC_IRQ_MSIX, irq);
		break;
	default:
		LOG_ERR("Failed to raise IRQ, unknown type");
		break;
	}
}

static void pcie_epf_test_cmd_handler(struct k_work *work)
{
	struct k_work_delayable *dwork = k_work_delayable_from_work(work);
	struct pcie_epf_test_s *test =
		CONTAINER_OF(dwork, struct pcie_epf_test_s, work);
	struct pcie_epf_device_s *epf = test->epf;
	const struct device *epc = epf->epc;
	struct pcie_epf_test_reg_s *reg;
	uint32_t command;
	int count;
	int ret;

	reg = test->reg[test->test_reg_bar];
	command = reg->command;
	if (command == 0U) {
		goto reset_handler;
	}

	reg->command = 0;
	reg->status = 0;

	if (reg->irq_type > PCIE_EPF_TEST_IRQ_TYPE_MSIX) {
		LOG_ERR("Failed to detect IRQ type");
		goto reset_handler;
	}

	if ((command & PCIE_EPF_TEST_COMMAND_RAISE_LEGACY_IRQ) != 0U) {
		reg->status = PCIE_EPF_TEST_STATUS_IRQ_RAISED;
		pcie_epc_raise_irq(epc, epf->funcno, PCIE_EPC_IRQ_LEGACY, 0);
	} else if ((command & PCIE_EPF_TEST_COMMAND_WRITE) != 0U) {
		ret = pcie_epf_test_write(test);
		if (ret < 0) {
			reg->status |= PCIE_EPF_TEST_STATUS_WRITE_FAIL;
		} else {
			reg->status |= PCIE_EPF_TEST_STATUS_WRITE_SUCCESS;
		}

		pcie_epf_test_raise_irq(test, reg->irq_type, reg->irq_number);
	} else if ((command & PCIE_EPF_TEST_COMMAND_READ) != 0U) {
		ret = pcie_epf_test_read(test);
		if (ret < 0) {
			reg->status |= PCIE_EPF_TEST_STATUS_READ_FAIL;
		} else {
			reg->status |= PCIE_EPF_TEST_STATUS_READ_SUCCESS;
		}

		pcie_epf_test_raise_irq(test, reg->irq_type, reg->irq_number);
	} else if ((command & PCIE_EPF_TEST_COMMAND_COPY) != 0U) {
		ret = pcie_epf_test_copy(test);
		if (ret < 0) {
			reg->status |= PCIE_EPF_TEST_STATUS_COPY_FAIL;
		} else {
			reg->status |= PCIE_EPF_TEST_STATUS_COPY_SUCCESS;
		}

		pcie_epf_test_raise_irq(test, reg->irq_type, reg->irq_number);
	} else if ((command & PCIE_EPF_TEST_COMMAND_RAISE_MSI_IRQ) != 0U) {
		count = pcie_epc_get_msi(epc, epf->funcno);
		if (reg->irq_number > count || count <= 0) {
			goto reset_handler;
		}

		reg->status = PCIE_EPF_TEST_STATUS_IRQ_RAISED;
		pcie_epc_raise_irq(epc, epf->funcno, PCIE_EPC_IRQ_MSI,
				  reg->irq_number);
	} else if ((command & PCIE_EPF_TEST_COMMAND_RAISE_MSIX_IRQ) != 0U) {
		count = pcie_epc_get_msix(epc, epf->funcno);
		if (reg->irq_number > count || count <= 0) {
			goto reset_handler;
		}

		reg->status = PCIE_EPF_TEST_STATUS_IRQ_RAISED;
		pcie_epc_raise_irq(epc, epf->funcno, PCIE_EPC_IRQ_MSIX,
				  reg->irq_number);
	}

reset_handler:
	k_work_schedule(&test->work, PCIE_EPF_TEST_WORK_PERIOD);
}

static void pcie_epf_test_unbind(struct pcie_epf_device_s *epf)
{
	struct pcie_epf_test_s *test = epf->priv;
	const struct device *epc = epf->epc;
	int bar;

	k_work_cancel_delayable(&test->work);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar++) {
		if (test->reg[bar] != NULL) {
			pcie_epc_clear_bar(epc, epf->funcno, &epf->bar[bar]);
			pcie_epf_free_space(epf, bar, test->reg[bar]);
		}
	}
}

static int pcie_epf_test_set_bar(struct pcie_epf_device_s *epf)
{
	const struct pcie_epc_features_s *features;
	struct pcie_epf_test_s *test = epf->priv;
	const struct device *epc = epf->epc;
	int bar;
	int add;
	int ret;

	features = pcie_epc_get_features(epc, epf->funcno);
	for (bar = 0; bar < PCI_STD_NUM_BARS; bar += add) {
		/* pcie_epc_set_bar() sets PCI_BASE_ADDRESS_MEM_TYPE_64
		 * if the specific implementation required a 64-bit BAR,
		 * even if we only requested a 32-bit BAR.
		 */
		add = ((epf->bar[bar].flags &
			PCI_BASE_ADDRESS_MEM_TYPE_64) != 0) ? 2 : 1;

		if ((features->bar_reserved & BIT(bar)) != 0U) {
			continue;
		}

		ret = pcie_epc_set_bar(epc, epf->funcno, &epf->bar[bar]);
		if (ret < 0) {
			pcie_epf_free_space(epf, bar, test->reg[bar]);
			LOG_ERR("Failed to set BAR%d", bar);
			if (bar == test->test_reg_bar) {
				return ret;
			}
		}
	}

	return 0;
}

static int pcie_epf_test_core_init(struct pcie_epf_device_s *epf)
{
	struct pcie_epf_header_s *header = epf->header;
	const struct pcie_epc_features_s *features;
	struct pcie_epf_test_s *test = epf->priv;
	const struct device *epc = epf->epc;
	bool msix_capable = false;
	bool msi_capable = false;
	int ret;

	features = pcie_epc_get_features(epc, epf->funcno);
	if (features != NULL) {
		msix_capable = features->msix_capable;
		msi_capable = features->msi_capable;
	}

	ret = pcie_epc_write_header(epc, epf->funcno, header);
	if (ret < 0) {
		LOG_ERR("Configuration header write failed");
		return ret;
	}

	ret = pcie_epf_test_set_bar(epf);
	if (ret < 0) {
		return ret;
	}

	if (msi_capable) {
		ret = pcie_epc_set_msi(epc, epf->funcno, epf->msi_interrupts);
		if (ret < 0) {
			LOG_ERR("MSI configuration failed");
			return ret;
		}
	}

	if (msix_capable) {
		ret = pcie_epc_set_msix(epc, epf->funcno, epf->msix_interrupts,
				       test->test_reg_bar,
				       test->msix_table_offset);
		if (ret < 0) {
			LOG_ERR("MSI-X configuration failed");
			return ret;
		}
	}

	return 0;
}

static int pcie_epf_test_link_up(struct pcie_epf_device_s *epf)
{
	struct pcie_epf_test_s *test = epf->priv;

	k_work_schedule(&test->work, PCIE_EPF_TEST_WORK_PERIOD);
	return 0;
}

static int pcie_epf_test_alloc_space(struct pcie_epf_device_s *epf)
{
	const struct pcie_epc_features_s *features;
	struct pcie_epf_test_s *test = epf->priv;
	const struct device *epc = epf->epc;
	int test_reg_bar = test->test_reg_bar;
	size_t msix_table_size = 0;
	size_t test_reg_bar_size;
	size_t test_reg_size;
	bool bar_fixed_64bit;
	size_t pba_size = 0;
	bool msix_capable;
	int bar;
	int add;

	features = pcie_epc_get_features(epc, epf->funcno);
	test_reg_bar_size = ROUND_UP(sizeof(struct pcie_epf_test_reg_s), 128);
	msix_capable = features->msix_capable;
	if (msix_capable) {
		msix_table_size = PCI_MSIX_ENTRY_SIZE * epf->msix_interrupts;
		test->msix_table_offset = test_reg_bar_size;

		/* Align to QWORD or 8 Bytes */
		pba_size = ROUND_UP(DIV_ROUND_UP(epf->msix_interrupts, 8), 8);
	}

	test_reg_size = test_reg_bar_size + msix_table_size + pba_size;
	if (test_reg_size > (size_t)test->bar_size[test_reg_bar]) {
		return -ENOMEM;
	}

	for (bar = 0; bar < PCI_STD_NUM_BARS; bar += add) {
		bar_fixed_64bit = (features->bar_fixed_64bit & BIT(bar)) != 0U;
		if (bar_fixed_64bit) {
			epf->bar[bar].flags |= PCI_BASE_ADDRESS_MEM_TYPE_64;
		}

		add = ((epf->bar[bar].flags &
			PCI_BASE_ADDRESS_MEM_TYPE_64) != 0) ? 2 : 1;
		if ((features->bar_reserved & BIT(bar)) != 0U) {
			continue;
		}

		test->reg[bar] = pcie_epf_alloc_space(epf, bar,
						     test->bar_size[bar],
						     features->align);
		if (test->reg[bar] == NULL) {
			LOG_ERR("Failed to allocate space for BAR%d", bar);
			if (bar == test_reg_bar) {
				return -ENOMEM;
			}
		}
	}

	return 0;
}

static int pcie_epf_test_bind(struct pcie_epf_device_s *epf)
{
	const struct pcie_epc_features_s *features;
	struct pcie_epf_test_s *test = epf->priv;
	const struct device *epc = epf->epc;
	int ret;

	features = pcie_epc_get_features(epc, epf->funcno);
	if (features == NULL) {
		LOG_ERR("features not implemented");
		return -ENOTSUP;
	}

	test->header.interrupt_pin = PCI_INTERRUPT_INTA + epf->funcno;
	test->test_reg_bar = pcie_epc_get_first_free_bar(features);
	if (test->test_reg_bar < 0) {
		return -EINVAL;
	}

	ret = pcie_epf_test_alloc_space(epf);
	if (ret < 0) {
		return ret;
	}

	if (!features->core_init_notifier) {
		ret = pcie_epf_test_core_init(epf);
		if (ret < 0) {
			return ret;
		}
	}

	if (!features->linkup_notifier && !features->core_init_notifier) {
		k_work_schedule(&test->work, PCIE_EPF_TEST_WORK_PERIOD);
	}

	return 0;
}

static void pcie_epf_test_init(struct pcie_epf_device_s *epf,
			       struct pcie_epf_test_s *test)
{
	memset(epf, 0, sizeof(*epf));
	memset(test, 0, sizeof(*test));

	test->header.vendorid = 0x104c;
	test->header.deviceid = 0xb500;
	test->header.baseclass_code = PCI_CLASS_OTHERS;
	test->bar_size[0] = 1024;
	test->bar_size[1] = 512;
	test->bar_size[2] = 1024;
	test->bar_size[3] = 16384;
	test->bar_size[4] = 131072;
	test->bar_size[5] = 1048576;
	test->epf = epf;

	k_work_init_delayable(&test->work, pcie_epf_test_cmd_handler);

	epf->header = &test->header;
	epf->msi_interrupts = 1;
	epf->msix_interrupts = 32;
	epf->priv = test;
	epf->ops = &g_pcie_epf_test_ops;
	epf->event_ops = &g_pcie_epf_test_event_ops;
}

int pcie_epf_test_add(const struct device *epc)
{
	struct pcie_epf_device_s *epf = g_pcie_epf_test_device;
	int ret;
	int i;

	for (i = 0; i < CONFIG_PCIE_EPF_TEST_FUNC_COUNT; i++) {
		if (epf[i].epc != NULL) {
			ret = -EBUSY;
			goto err;
		}

		pcie_epf_test_init(&epf[i], &g_pcie_epf_test[i]);
		ret = pcie_epc_add_epf(epc, &epf[i]);
		if (ret < 0) {
			LOG_ERR("func %d bind error", i);
			goto err;
		}
	}

	return 0;

err:
	while (i-- > 0) {
		pcie_epc_remove_epf(epc, &epf[i]);
	}

	return ret;
}

void pcie_epf_test_remove(const struct device *epc)
{
	int i;

	for (i = 0; i < CONFIG_PCIE_EPF_TEST_FUNC_COUNT; i++) {
		pcie_epc_remove_epf(epc, &g_pcie_epf_test_device[i]);
	}
}
