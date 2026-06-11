/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/drivers/pcie/epf/pci_epc.h>
#include <zephyr/drivers/pcie/epf/pci_epf.h>

/*
 * Fake EPC device: records the last operations performed on it so the
 * test can verify that the EPC API dispatches to the controller ops.
 */

struct fake_epc_data {
	struct pci_epf_header header;
	struct pci_epf_bar bar;
	int irqs_raised;
	bool started;
};

static struct fake_epc_data fake_data;

static int fake_write_header(const struct device *dev, uint8_t funcno,
			     const struct pci_epf_header *hdr)
{
	struct fake_epc_data *data = dev->data;

	data->header = *hdr;

	return 0;
}

static int fake_set_bar(const struct device *dev, uint8_t funcno, const struct pci_epf_bar *bar)
{
	struct fake_epc_data *data = dev->data;

	data->bar = *bar;

	return 0;
}

static int fake_raise_irq(const struct device *dev, uint8_t funcno, enum pci_epc_irq_type type,
			  uint16_t interrupt_num)
{
	struct fake_epc_data *data = dev->data;

	data->irqs_raised++;

	return 0;
}

static const struct pci_epc_features fake_features = {
	.msi_capable = true,
	.bar_reserved = BIT(1),
	.align = 256,
};

static const struct pci_epc_features *fake_get_features(const struct device *dev, uint8_t funcno)
{
	return &fake_features;
}

static int fake_start(const struct device *dev)
{
	struct fake_epc_data *data = dev->data;

	data->started = true;

	return 0;
}

static const struct pci_epc_ops fake_epc_ops = {
	.write_header = fake_write_header,
	.set_bar = fake_set_bar,
	.raise_irq = fake_raise_irq,
	.get_features = fake_get_features,
	.start = fake_start,
};

DEVICE_DEFINE(fake_epc, "FAKE_EPC", NULL, NULL, &fake_data, NULL, POST_KERNEL,
	      CONFIG_KERNEL_INIT_PRIORITY_DEVICE, &fake_epc_ops);

/* One 64 KiB outbound window with 4 KiB pages */
static uint8_t fake_window_mem[64 * 1024] __aligned(4096);
SYS_BITARRAY_DEFINE_STATIC(fake_window_map, 16);

static const struct pci_epc_mem_window fake_windows[] = {{
	.virt_base = fake_window_mem,
	.phys_base = (uintptr_t)fake_window_mem,
	.size = sizeof(fake_window_mem),
	.page_size = 4096,
}};

static sys_bitarray_t *fake_window_maps[] = {&fake_window_map};

static struct pci_epc fake_epc_ctrl = {
	.max_functions = 2,
	.windows = fake_windows,
	.num_windows = ARRAY_SIZE(fake_windows),
	.window_maps = fake_window_maps,
};

/* Test EPF device and driver */

static struct pci_epf_header test_header = {
	.vendorid = 0x1af4,
	.deviceid = 0x1044,
	.baseclass_code = 0xff,
};

static struct pci_epf_device test_epf = {
	.name = "pci_epf_test",
	.epc_name = "FAKE_EPC",
	.header = &test_header,
};

static int bound;

static int test_epf_bind(struct pci_epf_device *epf)
{
	int ret;

	ret = pci_epc_write_header(epf->epc, epf->funcno, epf->header);
	if (ret < 0) {
		return ret;
	}

	bound++;

	return 0;
}

static void test_epf_unbind(struct pci_epf_device *epf)
{
	bound--;
}

static const struct pci_epf_ops test_epf_ops = {
	.bind = test_epf_bind,
	.unbind = test_epf_unbind,
};

static const char *const test_epf_names[] = {"pci_epf_test", NULL};

static struct pci_epf_driver test_epf_driver = {
	.ops = &test_epf_ops,
	.names = test_epf_names,
};

static void *pci_epf_suite_setup(void)
{
	fake_epc_ctrl.dev = DEVICE_GET(fake_epc);

	zassert_equal(pci_epc_register(&fake_epc_ctrl), 0);
	zassert_equal(pci_epf_device_register(&test_epf), 0);
	zassert_equal(pci_epf_register_driver(&test_epf_driver), 0);

	return NULL;
}

ZTEST(pci_epf, test_epc_get)
{
	zassert_equal(pci_epc_get("FAKE_EPC"), &fake_epc_ctrl);
	zassert_is_null(pci_epc_get("NOPE"));
}

ZTEST(pci_epf, test_bind_flow)
{
	zassert_equal(bound, 1, "EPF driver bind not invoked");
	zassert_equal(test_epf.epc, &fake_epc_ctrl);
	zassert_equal(fake_data.header.vendorid, 0x1af4);
	zassert_equal(fake_data.header.deviceid, 0x1044);
}

ZTEST(pci_epf, test_features_and_bars)
{
	const struct pci_epc_features *features =
		pci_epc_get_features(&fake_epc_ctrl, test_epf.funcno);

	zassert_not_null(features);
	zassert_true(features->msi_capable);

	zassert_equal(pci_epc_get_first_free_bar(features), 0);
	zassert_equal(pci_epc_get_next_free_bar(features, 1), 2);
}

ZTEST(pci_epf, test_bar_setup)
{
	void *space = pci_epf_alloc_space(&test_epf, 0, 512, fake_features.align);

	zassert_not_null(space);
	zassert_equal(test_epf.bar[0].size, 512);

	zassert_equal(pci_epc_set_bar(test_epf.epc, test_epf.funcno, &test_epf.bar[0]), 0);
	zassert_equal(fake_data.bar.size, 512);

	pci_epf_free_space(&test_epf, 0);
	zassert_is_null(test_epf.bar[0].addr);
}

ZTEST(pci_epf, test_irq_and_start)
{
	zassert_equal(pci_epc_raise_irq(test_epf.epc, test_epf.funcno, PCI_EPC_IRQ_MSI, 1), 0);
	zassert_equal(fake_data.irqs_raised, 1);

	zassert_equal(pci_epc_start(test_epf.epc), 0);
	zassert_true(fake_data.started);

	/* Unimplemented op reports -ENOSYS */
	zassert_equal(pci_epc_set_msix(test_epf.epc, test_epf.funcno, 8, 0, 0), -ENOSYS);
}

ZTEST(pci_epf, test_outbound_window_alloc)
{
	uintptr_t phys = 0;
	void *virt = pci_epc_mem_alloc_addr(&fake_epc_ctrl, 8192, &phys);

	zassert_not_null(virt);
	zassert_equal(phys, (uintptr_t)virt);
	zassert_true(phys >= (uintptr_t)fake_window_mem);

	pci_epc_mem_free_addr(&fake_epc_ctrl, virt, 8192);
}

ZTEST_SUITE(pci_epf, NULL, pci_epf_suite_setup, NULL, NULL, NULL);
