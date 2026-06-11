/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Functional test of the vhost-pci-epf driver against a fake PCI
 * endpoint controller. The test plays the role of the host (root
 * complex): it pokes the legacy virtio-pci registers in the BAR0
 * backing memory and verifies the backend-facing VHost API reactions.
 */

#include <zephyr/ztest.h>
#include <zephyr/device.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/drivers/pcie/epf/pci_epc.h>
#include <zephyr/drivers/pcie/epf/pci_epf.h>
#include <zephyr/drivers/vhost.h>

/* Legacy virtio-pci register offsets (with MSI-X enabled) */
#define R_HOST_FEATURES 0x00
#define R_GUEST_FEATURES 0x04
#define R_QUEUE_PFN 0x08
#define R_QUEUE_SIZE 0x0c
#define R_QUEUE_SEL 0x0e
#define R_QUEUE_NOTIFY 0x10
#define R_STATUS 0x12
#define R_ISR 0x13
#define R_CONFIG_MSIX 0x14
#define R_QUEUE_MSIX 0x16
#define R_CONFIG 0x18

#define QUEUE_SIZE 4
#define RING_PCI_ADDR 0x10000000ULL
#define RING_SIZE 8192 /* legacy vring size for 4 entries, 4096 align */

#define SETTLE_TIMEOUT K_MSEC(500)

static const struct device *vhost_dev = DEVICE_DT_GET(DT_NODELABEL(vhost_epf));

/*
 * Fake EPC: records headers, BARs, mappings, MSI-X setup and raised
 * interrupts so the test can verify the EPF driver's behavior.
 */

struct fake_map {
	uintptr_t addr;
	uint64_t pci_addr;
	size_t size;
	bool active;
};

static struct fake_epc_data {
	struct pci_epf_header header;
	struct pci_epf_bar bars[PCI_STD_NUM_BARS];
	struct fake_map maps[16];
	uint16_t msix_nvec;
	int msix_bar;
	uint32_t msix_offset;
	enum pci_epc_irq_type last_irq_type;
	uint16_t last_irq_num;
	int irqs_raised;
	bool started;
} fake_data;

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

	data->bars[bar->barno] = *bar;

	return 0;
}

static void fake_clear_bar(const struct device *dev, uint8_t funcno, const struct pci_epf_bar *bar)
{
	struct fake_epc_data *data = dev->data;

	memset(&data->bars[bar->barno], 0, sizeof(data->bars[bar->barno]));
}

static int fake_map_addr(const struct device *dev, uint8_t funcno, uintptr_t addr,
			 uint64_t pci_addr, size_t size)
{
	struct fake_epc_data *data = dev->data;

	for (size_t i = 0; i < ARRAY_SIZE(data->maps); i++) {
		if (!data->maps[i].active) {
			data->maps[i] = (struct fake_map){
				.addr = addr,
				.pci_addr = pci_addr,
				.size = size,
				.active = true,
			};
			return 0;
		}
	}

	return -ENOMEM;
}

static void fake_unmap_addr(const struct device *dev, uint8_t funcno, uintptr_t addr)
{
	struct fake_epc_data *data = dev->data;

	for (size_t i = 0; i < ARRAY_SIZE(data->maps); i++) {
		if (data->maps[i].active && data->maps[i].addr == addr) {
			data->maps[i].active = false;
			return;
		}
	}
}

static int fake_set_msix(const struct device *dev, uint8_t funcno, uint16_t interrupts, int barno,
			 uint32_t offset)
{
	struct fake_epc_data *data = dev->data;

	data->msix_nvec = interrupts;
	data->msix_bar = barno;
	data->msix_offset = offset;

	return 0;
}

static int fake_raise_irq(const struct device *dev, uint8_t funcno, enum pci_epc_irq_type type,
			  uint16_t interrupt_num)
{
	struct fake_epc_data *data = dev->data;

	data->last_irq_type = type;
	data->last_irq_num = interrupt_num;
	data->irqs_raised++;

	return 0;
}

static const struct pci_epc_features fake_features = {
	.msi_capable = true,
	.msix_capable = true,
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
	.clear_bar = fake_clear_bar,
	.map_addr = fake_map_addr,
	.unmap_addr = fake_unmap_addr,
	.set_msix = fake_set_msix,
	.raise_irq = fake_raise_irq,
	.get_features = fake_get_features,
	.start = fake_start,
};

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
	.max_functions = 1,
	.windows = fake_windows,
	.num_windows = ARRAY_SIZE(fake_windows),
	.window_maps = fake_window_maps,
};

/*
 * The EPC must be registered before the vhost-pci-epf device
 * initializes (CONFIG_VHOST_PCI_EPF_INIT_PRIORITY), as binding only
 * happens at registration time.
 */
static int fake_epc_init(const struct device *dev)
{
	fake_epc_ctrl.dev = dev;

	return pci_epc_register(&fake_epc_ctrl);
}

DEVICE_DEFINE(fake_epc, "FAKE_EPC", fake_epc_init, NULL, &fake_data, NULL, POST_KERNEL, 50,
	      &fake_epc_ops);

/* Host-side register accessors into the BAR0 backing memory */

static volatile uint8_t *bar0;

#define REG8(off)  (*(volatile uint8_t *)(bar0 + (off)))
#define REG16(off) (*(volatile uint16_t *)(bar0 + (off)))
#define REG32(off) (*(volatile uint32_t *)(bar0 + (off)))

/*
 * Wait for the poll work to observe register writes. The poll period
 * is rounded up to a system tick, so wait several ticks worth of time.
 */
static void settle(void)
{
	k_sleep(K_MSEC(100));
}

static bool find_map(uint64_t pci_addr, size_t size)
{
	for (size_t i = 0; i < ARRAY_SIZE(fake_data.maps); i++) {
		if (fake_data.maps[i].active && fake_data.maps[i].pci_addr == pci_addr &&
		    fake_data.maps[i].size == size) {
			return true;
		}
	}

	return false;
}

static K_SEM_DEFINE(ready_sem, 0, 10);
static K_SEM_DEFINE(notify_sem, 0, 10);
static uint16_t last_ready_queue = UINT16_MAX;
static uint16_t last_notify_queue = UINT16_MAX;

static void ready_cb(const struct device *dev, uint16_t queue_id, void *user_data)
{
	last_ready_queue = queue_id;
	k_sem_give(&ready_sem);
}

static void notify_cb(const struct device *dev, uint16_t queue_id, void *user_data)
{
	last_notify_queue = queue_id;
	k_sem_give(&notify_sem);
}

static void *suite_setup(void)
{
	zassert_true(device_is_ready(vhost_dev));

	bar0 = fake_data.bars[0].addr;
	zassert_not_null((void *)bar0, "register BAR was not configured");

	return NULL;
}

ZTEST(vhost_pci_epf, test_function_setup)
{
	/* Transitional virtio-pci identity */
	zassert_equal(fake_data.header.vendorid, 0x1af4);
	zassert_equal(fake_data.header.deviceid, 0x1005);
	zassert_equal(fake_data.header.revid, 0);
	zassert_equal(fake_data.header.subsys_vendor_id, 0x1af4);
	zassert_equal(fake_data.header.subsys_id, 4);

	/* Legacy registers in an I/O space BAR */
	zassert_true(fake_data.bars[0].flags & PCI_EPF_BAR_SPACE_IO);
	zassert_true(fake_data.bars[0].size >= R_CONFIG + 4);

	/* MSI-X: one vector per queue plus the config vector, own BAR */
	zassert_equal(fake_data.msix_nvec, 3);
	zassert_equal(fake_data.msix_bar, 1);
	zassert_not_null(fake_data.bars[1].addr);

	zassert_true(fake_data.started);

	/* Initial register content */
	zassert_equal(REG32(R_HOST_FEATURES), 0x30);
	zassert_equal(REG16(R_QUEUE_SIZE), QUEUE_SIZE);
	zassert_equal(REG16(R_QUEUE_NOTIFY), 0xffff);
	zassert_equal(REG16(R_CONFIG_MSIX), 0xffff);
	zassert_equal(REG16(R_QUEUE_MSIX), 0xffff);
	zassert_equal(REG8(R_CONFIG), 0xaa);
	zassert_equal(REG8(R_CONFIG + 3), 0xdd);
}

ZTEST(vhost_pci_epf, test_legacy_flow)
{
	void *parts[3];
	size_t queue_size;
	uint64_t features;
	int ret;

	zassert_equal(vhost_register_virtq_ready_cb(vhost_dev, ready_cb, NULL), 0);

	/* Driver feature negotiation and status */
	REG32(R_GUEST_FEATURES) = 0x10;
	REG8(R_STATUS) = 0x07; /* ACKNOWLEDGE | DRIVER | DRIVER_OK */

	/* Queue 0 activation: select, then publish the ring PFN */
	REG16(R_QUEUE_SEL) = 0;
	REG32(R_QUEUE_PFN) = RING_PCI_ADDR >> 12;

	zassert_equal(k_sem_take(&ready_sem, SETTLE_TIMEOUT), 0, "queue ready not signalled");
	zassert_equal(last_ready_queue, 0);

	zassert_true(vhost_queue_ready(vhost_dev, 0));
	zassert_false(vhost_queue_ready(vhost_dev, 1));

	zassert_equal(vhost_get_driver_features(vhost_dev, &features), 0);
	zassert_equal(features, 0x10);

	/* The rings must be mapped through the outbound window */
	zassert_true(find_map(RING_PCI_ADDR, RING_SIZE), "ring mapping missing");

	zassert_equal(vhost_get_virtq(vhost_dev, 0, parts, &queue_size), 0);
	zassert_equal(queue_size, QUEUE_SIZE);
	/* Legacy contiguous layout: desc, avail, page-aligned used */
	zassert_equal((uint8_t *)parts[1] - (uint8_t *)parts[0], 16 * QUEUE_SIZE);
	zassert_equal((uint8_t *)parts[2] - (uint8_t *)parts[0], 4096);

	/* Doorbell via the notify sentinel */
	zassert_equal(vhost_register_virtq_notify_cb(vhost_dev, 0, notify_cb, NULL), 0);

	REG16(R_QUEUE_NOTIFY) = 0;
	zassert_equal(k_sem_take(&notify_sem, SETTLE_TIMEOUT), 0, "notify not signalled");
	zassert_equal(last_notify_queue, 0);
	zassert_equal(REG16(R_QUEUE_NOTIFY), 0xffff, "notify sentinel not restored");

	/* Kicking the same queue again must be detected as well */
	REG16(R_QUEUE_NOTIFY) = 0;
	zassert_equal(k_sem_take(&notify_sem, SETTLE_TIMEOUT), 0, "second notify lost");

	/* MSI-X vector assignment and guest notification */
	REG16(R_QUEUE_SEL) = 0;
	REG16(R_QUEUE_MSIX) = 1;
	REG16(R_CONFIG_MSIX) = 0;
	settle();

	int irqs_before = fake_data.irqs_raised;

	zassert_equal(vhost_notify_virtq(vhost_dev, 0), 0);
	zassert_equal(fake_data.irqs_raised, irqs_before + 1);
	zassert_equal(fake_data.last_irq_type, PCI_EPC_IRQ_MSIX);
	zassert_equal(fake_data.last_irq_num, 2, "MSI-X interrupts are counted from 1");
	zassert_true(REG8(R_ISR) & BIT(0));

	zassert_equal(vhost_set_device_status(vhost_dev, BIT(6)), 0); /* NEEDS_RESET */
	zassert_equal(fake_data.irqs_raised, irqs_before + 2);
	zassert_equal(fake_data.last_irq_num, 1);
	zassert_true(REG8(R_STATUS) & BIT(6));
	zassert_true(REG8(R_ISR) & BIT(1));

	/* Host buffer access through the outbound windows */
	const struct vhost_buf bufs[2] = {
		{.gpa = 0x20000010, .len = 32, .is_write = false},
		{.gpa = 0x20001000, .len = 64, .is_write = true},
	};
	struct vhost_iovec riov[2], wiov[2];
	size_t nread, nwrite;

	ret = vhost_prepare_iovec(vhost_dev, 0, 1, bufs, 2, riov, 2, wiov, 2, &nread, &nwrite);
	zassert_equal(ret, 0);
	zassert_equal(nread, 1);
	zassert_equal(nwrite, 1);
	zassert_equal(riov[0].iov_len, 32);
	zassert_equal(wiov[0].iov_len, 64);
	zassert_not_null(riov[0].iov_base);
	zassert_true(find_map(0x20000000, 4096), "read buffer mapping missing");
	zassert_true(find_map(0x20001000, 4096), "write buffer mapping missing");

	zassert_equal(vhost_release_iovec(vhost_dev, 0, 1), 0);
	zassert_equal(vhost_release_iovec(vhost_dev, 0, 1), -EINVAL, "double release");
	zassert_false(find_map(0x20000000, 4096), "read buffer not unmapped");

	/* Device reset by the host */
	REG8(R_STATUS) = 0;
	settle();

	zassert_false(vhost_queue_ready(vhost_dev, 0));
	zassert_equal(REG32(R_QUEUE_PFN), 0);
	zassert_equal(REG16(R_QUEUE_NOTIFY), 0xffff);
	zassert_equal(REG16(R_QUEUE_MSIX), 0xffff);
	zassert_equal(REG16(R_CONFIG_MSIX), 0xffff);
	zassert_equal(REG8(R_ISR), 0);
	zassert_false(find_map(RING_PCI_ADDR, RING_SIZE), "ring not unmapped on reset");
}

ZTEST_SUITE(vhost_pci_epf, NULL, suite_setup, NULL, NULL, NULL);
