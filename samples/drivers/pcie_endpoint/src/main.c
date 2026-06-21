/*
 * Copyright 2026
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/cache.h>
#include <zephyr/device.h>
#include <zephyr/drivers/pcie/endpoint/pcie_ep.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <string.h>

LOG_MODULE_REGISTER(pcie_endpoint_sample, LOG_LEVEL_INF);

#define PCIE_EP_NODE   DT_NODELABEL(pcie3x2_ep)
#define BAR0_SIZE      KB(64)
#define MAILBOX_OFFSET 0x1000
#define MAILBOX_MAGIC  0x524b4550U

struct ep_mailbox {
	uint32_t magic;
	uint32_t command;
	uint32_t argument;
	uint32_t response;
	uint32_t sequence;
	uint32_t link_up;
};

static uint8_t bar0_memory[BAR0_SIZE] __aligned(BAR0_SIZE);

int main(void)
{
	const struct device *ep = DEVICE_DT_GET(PCIE_EP_NODE);
	struct ep_mailbox *mailbox = (struct ep_mailbox *)(bar0_memory + MAILBOX_OFFSET);
	const struct pcie_ep_bar bar0 = {
		.phys_addr = (uintptr_t)bar0_memory,
		.size = sizeof(bar0_memory),
		.flags = 0,
		.bar = 0,
	};
	uint32_t last_sequence = 0;
	int ret;

	if (!device_is_ready(ep)) {
		LOG_ERR("PCIe endpoint device is not ready");
		return -ENODEV;
	}

	memset(mailbox, 0, sizeof(*mailbox));
	mailbox->magic = MAILBOX_MAGIC;
	sys_cache_data_flush_range(mailbox, sizeof(*mailbox));

	ret = pcie_ep_set_bar(ep, &bar0);
	if (ret != 0) {
		LOG_ERR("BAR0 setup failed: %d", ret);
		return ret;
	}

	ret = pcie_ep_start(ep);
	if (ret != 0) {
		LOG_ERR("link start failed: %d", ret);
		return ret;
	}

	LOG_INF("BAR0 mailbox ready at offset 0x%x", MAILBOX_OFFSET);

	while (true) {
		sys_cache_data_invd_range(mailbox, sizeof(*mailbox));
		mailbox->link_up = pcie_ep_is_link_up(ep);

		if (mailbox->sequence != last_sequence) {
			last_sequence = mailbox->sequence;
			mailbox->response = mailbox->command ^ mailbox->argument;
			sys_cache_data_flush_range(mailbox, sizeof(*mailbox));

			/*
			 * MSI vector 0 is generated only after the host enables
			 * MSI for this function.
			 */
			(void)pcie_ep_raise_irq(ep, PCIE_EP_IRQ_MSI, 0);
		} else {
			sys_cache_data_flush_range(&mailbox->link_up, sizeof(mailbox->link_up));
		}

		k_sleep(K_MSEC(10));
	}

	return 0;
}
