/*
 * SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pcie/endpoint/pcie_epc.h>
#include <zephyr/ztest.h>

struct test_function {
	struct pcie_epf_device_s epf;
	struct k_work_delayable work;
	struct k_sem running;
	struct k_sem release;
	int lock_result;
	uint32_t doorbells;
	uint32_t accesses;
	uint32_t unbinds;
};

static struct pcie_epc_ctrl_s controller;
static const struct pcie_epc_config config = {.max_functions = 1U};

static int controller_start(const struct device *dev)
{
	ARG_UNUSED(dev);
	return 0;
}

static int stop_result;

static int controller_stop(const struct device *dev)
{
	ARG_UNUSED(dev);
	return stop_result;
}

static int controller_init(const struct device *dev)
{
	return pcie_epc_init(dev, NULL);
}

static DEVICE_API(pcie_epc, controller_api) = {
	.start = controller_start,
	.stop = controller_stop,
};

DEVICE_DEFINE(test_epc, "test_epc", controller_init, NULL, &controller, &config,
	      POST_KERNEL, CONFIG_PCIE_EPC_INIT_PRIORITY, &controller_api);

static void function_work(struct k_work *work)
{
	struct test_function *function = CONTAINER_OF(k_work_delayable_from_work(work),
						     struct test_function, work);
	struct pcie_epc_ctrl_s *epc = function->epf.epc->data;

	k_sem_give(&function->running);
	zassert_ok(k_sem_take(&function->release, K_SECONDS(2)));
	/* Bound the wait so a lock inversion fails instead of hanging the suite. */
	function->lock_result = k_mutex_lock(&epc->lock, K_MSEC(100));
	if (function->lock_result == 0) {
		k_mutex_unlock(&epc->lock);
	}

	/* Match EPF workers that periodically schedule themselves. */
	k_work_schedule(&function->work, K_SECONDS(1));
}

static int function_bind(struct pcie_epf_device_s *epf)
{
	struct test_function *function = CONTAINER_OF(epf, struct test_function, epf);

	pcie_epc_doorbell_notify(epf->epc, epf->funcno, 0U);
	k_work_init_delayable(&function->work, function_work);
	k_sem_init(&function->running, 0, 1);
	k_sem_init(&function->release, 0, 1);
	k_work_schedule(&function->work, K_NO_WAIT);
	return 0;
}

static void function_unbind(struct pcie_epf_device_s *epf)
{
	struct test_function *function = CONTAINER_OF(epf, struct test_function, epf);
	struct k_work_sync sync;

	function->unbinds++;
	/* Detached functions must no longer receive notifications. */
	pcie_epc_doorbell_notify(epf->epc, epf->funcno, 0U);
	k_sem_give(&function->release);
	k_work_cancel_delayable_sync(&function->work, &sync);
}

static void function_doorbell(struct pcie_epf_device_s *epf, uint8_t index)
{
	struct test_function *function = CONTAINER_OF(epf, struct test_function, epf);

	ARG_UNUSED(index);
	function->doorbells++;
}

static void function_bar_access(struct pcie_epf_device_s *epf)
{
	struct test_function *function = CONTAINER_OF(epf, struct test_function, epf);

	function->accesses++;
}

ZTEST(pcie_epc, test_remove_lifecycle)
{
	static const struct pcie_epf_ops_s ops = {
		.bind = function_bind,
		.unbind = function_unbind,
	};
	static const struct pcie_epc_event_ops_s events = {
		.doorbell = function_doorbell,
		.bar_access = function_bar_access,
	};
	struct test_function function = {.epf = {.ops = &ops, .event_ops = &events}};
	const struct device *dev = DEVICE_GET(test_epc);

	zassert_true(device_is_ready(dev));
	for (uint32_t i = 0U; i < 2U; i++) {
		zassert_ok(pcie_epc_add_epf(dev, &function.epf));
		zassert_equal(function.doorbells, i);
		zassert_equal(function.epf.funcno, 0U);
		zassert_ok(k_sem_take(&function.running, K_SECONDS(1)));
		pcie_epc_doorbell_notify(dev, 0U, 0U);
		zassert_equal(function.doorbells, i + 1U);
		pcie_epc_bar_access_notify(dev, 0U);
		zassert_equal(function.accesses, i + 1U);
		/* Other functions have no target. */
		pcie_epc_bar_access_notify(dev, 1U);
		zassert_equal(function.accesses, i + 1U);
		zassert_ok(pcie_epc_start(dev));
		zassert_equal(pcie_epc_remove_epf(dev, &function.epf), -EBUSY);
		zassert_equal(function.unbinds, i);
		zassert_equal(function.epf.epc, dev);
		zassert_true(function.epf.is_bound);

		stop_result = -ENOTSUP;
		zassert_equal(pcie_epc_stop(dev), -ENOTSUP);
		zassert_equal(pcie_epc_remove_epf(dev, &function.epf), -EBUSY);
		stop_result = 0;
		zassert_ok(pcie_epc_stop(dev));
		zassert_ok(pcie_epc_remove_epf(dev, &function.epf));
		zassert_equal(function.unbinds, i + 1U);
		zassert_equal(function.doorbells, i + 1U);
		pcie_epc_bar_access_notify(dev, 0U);
		zassert_equal(function.accesses, i + 1U);
		zassert_ok(function.lock_result);
		zassert_equal(k_work_delayable_busy_get(&function.work), 0);
		zassert_is_null(function.epf.epc);
		zassert_false(function.epf.is_bound);
	}
}

ZTEST(pcie_epc, test_fixed_header)
{
	struct pcie_epf_header_s header = {.vendorid = 0x1234U};
	const struct device *dev = DEVICE_GET(test_epc);

	/* A controller without the operation has a header fixed in hardware. */
	zassert_ok(pcie_epc_write_header(dev, 0U, &header));
	zassert_equal(pcie_epc_write_header(dev, 1U, &header), -EINVAL);
}

ZTEST(pcie_epc, test_window_bitmap_bounds)
{
	SYS_BITARRAY_DEFINE_STATIC(bitmap, 32);
	uint8_t backing[48];
	struct pcie_epc_mem_s mem = {.bitmap = &bitmap};
	const struct device *dev = DEVICE_GET(test_epc);
	uintptr_t phys;

	zassert_ok(pcie_epc_mem_init(dev, &mem, backing, 0x1000U, sizeof(backing), 16U));
	for (size_t i = 0U; i < 3U; i++) {
		zassert_equal(pcie_epc_mem_alloc_addr(dev, &phys, 16U), backing + 16U * i);
		zassert_equal(phys, 0x1000U + 16U * i);
	}
	/* Padding bits in the bitmap must not extend the outbound window. */
	zassert_is_null(pcie_epc_mem_alloc_addr(dev, &phys, 16U));
	pcie_epc_mem_free_addr(dev, 0x1010U, 16U);
	zassert_equal(pcie_epc_mem_alloc_addr(dev, &phys, 16U), backing + 16U);
	zassert_equal(phys, 0x1010U);
	zassert_is_null(pcie_epc_mem_alloc_addr(dev, &phys, 16U));
	pcie_epc_mem_free_addr(dev, 0x1000U, sizeof(backing));
	pcie_epc_mem_exit(dev);
}

ZTEST_SUITE(pcie_epc, NULL, NULL, NULL, NULL, NULL);
