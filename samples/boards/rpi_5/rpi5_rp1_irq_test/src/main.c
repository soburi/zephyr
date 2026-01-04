/*
 * RPi5 RP1 Interrupt Path Testing Sample
 *
 * This sample demonstrates and verifies the interrupt path from RP1 to bcm2712:
 * RP1 (MSI-X) -> MIP (MSI-X to SPI converter) -> GIC (SPI) -> Zephyr ISR
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/pcie/pcie.h>
#include <zephyr/irq.h>
#include <zephyr/kernel.h>
#include <zephyr/kernel/mm.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/device_mmio.h>
#include <zephyr/sys/printk.h>

#include "gic_monitor.h"
#include "mip_regs.h"
#include "rp1_irq_test_config.h"
#include "rp1_irq_trigger.h"
#include "rp1_pcie.h"

#define TEST_VECTOR RP1_MSIX_TEST_VECTOR
#define CFG_BAR_SENTINEL 0xFFU
#define RP1_BAR_MAX 6U
#define CFG_SWEEP_STEP 0x1000U

#define GIC_SCAN_REG_START 1U
#define GIC_SCAN_REG_COUNT 16U

#if (RP1_MIP_MSI_BASE_INTID != 0U) && (RP1_MIP_MSI_NUM_SPIS != 0U)
#define HAVE_TEST_INTID 1
#define TEST_INTID (RP1_MIP_MSI_BASE_INTID + TEST_VECTOR)
#else
#define HAVE_TEST_INTID 0
#define TEST_INTID 0U
#endif

#define GIC_NODE DT_NODELABEL(gic)
#define GICD_PHYS DT_REG_ADDR_BY_IDX(GIC_NODE, 0)
#define GICD_SIZE DT_REG_SIZE_BY_IDX(GIC_NODE, 0)

/* Global state */
static struct rp1_device rp1_dev;
static struct rp1_trigger rp1_trig;
static struct mip_state mip_state;
static struct gic_monitor gic_mon;
static struct rp1_bar_map rp1_bars[RP1_BAR_MAX];
static atomic_t isr_hits;

static mm_reg_t gicd_base;
static mm_reg_t mip_base;
static bool gic_ready;
static bool mip_ready;
static bool rp1_cfg_ready;
static bool rp1_bars_mapped;
static bool msix_configured;
static bool isr_installed;
static uint32_t rp1_cfg_bar_idx = 0xffffffffU;
static uint32_t rp1_cfg_offset;
static uint32_t gic_pend_before[GIC_SCAN_REG_COUNT];
static uint32_t gic_pend_after[GIC_SCAN_REG_COUNT];

static void print_banner(const char *title)
{
	printk("\n");
	printk("========================================\n");
	printk(" %s\n", title);
	printk("========================================\n");
}

static bool map_mmio(uintptr_t phys, size_t size, mm_reg_t *virt_out)
{
	if (phys == 0U || size == 0U) {
		return false;
	}

	device_map(virt_out, phys, size, K_MEM_CACHE_NONE);
	return true;
}

static void gic_snapshot_pending(uint32_t *buf)
{
	if (!gic_ready) {
		return;
	}

	for (uint32_t i = 0; i < GIC_SCAN_REG_COUNT; i++) {
		uint32_t reg = (GIC_SCAN_REG_START + i) * 4U;
		buf[i] = gicd_read32(&gic_mon, GICD_ISPENDR_OFF + reg);
	}
}

static void gic_dump_pending_diff(const uint32_t *before,
				  const uint32_t *after)
{
	bool any = false;

	for (uint32_t i = 0; i < GIC_SCAN_REG_COUNT; i++) {
		uint32_t reg_idx = GIC_SCAN_REG_START + i;
		uint32_t delta = after[i] & ~before[i];

		if (delta == 0U) {
			continue;
		}

		for (uint32_t bit = 0; bit < 32U; bit++) {
			if (delta & BIT(bit)) {
				uint32_t intid = (reg_idx * 32U) + bit;
				printk("  [NEW] INTID %u pending\n", intid);
				any = true;
			}
		}
	}

	if (!any) {
		printk("  No new pending bits detected\n");
	}
}

static bool mip_vector_is_set(const struct mip_state *state, uint32_t vector)
{
	if (vector < 32U) {
		return (state->statusl & BIT(vector)) != 0U;
	}
	if (vector < 64U) {
		return (state->statush & BIT(vector - 32U)) != 0U;
	}

	return false;
}

static void rp1_msi_isr(const void *arg)
{
	ARG_UNUSED(arg);
	atomic_inc(&isr_hits);
}

static void test_step_0_map_gic(void)
{
	print_banner("STEP 0: Map GIC Distributor");

	if (!map_mmio(GICD_PHYS, GICD_SIZE, &gicd_base)) {
		printk("FAILED: Could not map GIC distributor\n");
		printk("  GICD phys: 0x%llx, size: 0x%zx\n",
		       (unsigned long long)GICD_PHYS, (size_t)GICD_SIZE);
		gic_ready = false;
		return;
	}

	gic_monitor_init(&gic_mon, (uintptr_t)gicd_base);
	gic_ready = true;
	printk("SUCCESS: GICD mapped at 0x%llx\n",
	       (unsigned long long)gicd_base);
}

static void test_step_1_find_rp1(void)
{
	print_banner("STEP 1: Find RP1 PCIe Device");

	if (!rp1_find_device(&rp1_dev)) {
		printk("FAILED: RP1 device not found\n");
		printk("  Check that:\n");
		printk("    - PCIe is enabled in devicetree\n");
		printk("    - RP1 is properly connected\n");
		printk("    - CONFIG_PCIE is enabled\n");
		return;
	}

	printk("SUCCESS: RP1 found at BDF 0x%08x\n", rp1_dev.bdf);
}

static void test_step_2_find_msix(void)
{
	print_banner("STEP 2: Find MSI-X Capability");

	if (!rp1_find_msix_cap(&rp1_dev)) {
		printk("FAILED: MSI-X capability not found\n");
		printk("  This is unexpected - RP1 should have MSI-X\n");
		return;
	}

	printk("SUCCESS: MSI-X capability found\n");
	printk("  Vectors available: %u\n", rp1_dev.msix_count);
}

static void test_step_3_map_bars(void)
{
	print_banner("STEP 3: Map RP1 BARs (Full Scan)");

	rp1_bars_mapped = false;
	for (uint32_t i = 0; i < RP1_BAR_MAX; i++) {
		struct rp1_bar_map *map = &rp1_bars[i];

		if (!pcie_get_mbar(rp1_dev.bdf, i, &map->bar)) {
			map->mapped = false;
			continue;
		}

		device_map(&map->vaddr, map->bar.phys_addr, map->bar.size,
			   K_MEM_CACHE_NONE);
		map->mapped = true;
		rp1_bars_mapped = true;

		printk("  BAR%u: phys 0x%llx, size 0x%zx, virt 0x%llx\n",
		       i,
		       (unsigned long long)map->bar.phys_addr,
		       map->bar.size,
		       (unsigned long long)map->vaddr);
	}

	if (!rp1_bars_mapped) {
		printk("WARNING: No BARs mapped\n");
	}
}

static void test_step_4_bind_msix_table(void)
{
	print_banner("STEP 4: Bind MSI-X Table BAR");

	if (!rp1_bars_mapped ||
	    rp1_dev.msix_table_bar >= RP1_BAR_MAX ||
	    !rp1_bars[rp1_dev.msix_table_bar].mapped) {
		printk("FAILED: MSI-X table BAR not mapped\n");
		return;
	}

	rp1_dev.msix_bar = rp1_bars[rp1_dev.msix_table_bar];
	rp1_dev.msix_table_addr = rp1_dev.msix_bar.vaddr;

	size_t table_size = rp1_dev.msix_count * sizeof(struct msix_entry);
	if (rp1_dev.msix_bar.bar.size <
	    (rp1_dev.msix_table_offset + table_size)) {
		printk("WARNING: MSI-X table may exceed BAR size\n");
	}

	printk("SUCCESS: MSI-X table BAR%u bound\n", rp1_dev.msix_table_bar);
}

static void test_step_5_map_rp1_cfg_bar(void)
{
	print_banner("STEP 5: Map RP1 Config BAR");

	if (!rp1_bars_mapped) {
		printk("FAILED: BARs not mapped\n");
		rp1_cfg_ready = false;
		return;
	}

	uint32_t bar_idx = RP1_CFG_BAR_INDEX;
	if (bar_idx == CFG_BAR_SENTINEL) {
		bar_idx = rp1_dev.msix_table_bar;
	}

	if (bar_idx >= RP1_BAR_MAX || !rp1_bars[bar_idx].mapped) {
		printk("FAILED: RP1 config BAR%u not mapped\n", bar_idx);
		rp1_cfg_ready = false;
		return;
	}

	rp1_dev.cfg_bar = rp1_bars[bar_idx];
	rp1_cfg_ready = rp1_trigger_init(&rp1_trig, rp1_dev.cfg_bar.vaddr);
	rp1_cfg_bar_idx = bar_idx;
	rp1_cfg_offset = 0U;
	printk("SUCCESS: RP1 config BAR%u mapped (offset 0x0)\n", bar_idx);
}

static void test_step_5_enable_msix(void)
{
	print_banner("STEP 6: Enable MSI-X");

	rp1_enable_msix(&rp1_dev);
	printk("SUCCESS: MSI-X enabled\n");
}

static void test_step_6_setup_table(void)
{
	print_banner("STEP 7: Setup MSI-X Table Entry");

	if (RP1_MIP_MSG_ADDR == 0U) {
		printk("WARNING: MIP msg_addr is not configured\n");
		printk("  Set RP1_MIP_MSG_ADDR in rp1_irq_test_config.h\n");
		msix_configured = false;
		return;
	}

	uint32_t msg_data = RP1_MIP_MSI_OFFSET + TEST_VECTOR;
	printk("Configuring MSI-X vector %u:\n", TEST_VECTOR);
	printk("  Target msg_addr: 0x%llx\n", (unsigned long long)RP1_MIP_MSG_ADDR);
	printk("  Data: 0x%x\n", msg_data);

	if (RP1_MIP_MSI_BASE_INTID != 0U) {
		printk("  Expected INTID: %u\n",
		       RP1_MIP_MSI_BASE_INTID + TEST_VECTOR);
	}

	if (rp1_setup_msix_entry(&rp1_dev, TEST_VECTOR,
				 RP1_MIP_MSG_ADDR, msg_data)) {
		printk("SUCCESS: MSI-X table configured\n");
		msix_configured = true;
	} else {
		printk("FAILED: MSI-X table setup\n");
		msix_configured = false;
	}
}

static void test_step_7_init_mip(void)
{
	print_banner("STEP 8: Map and Initialize MIP");

	if (RP1_MIP_BASE_ADDR == 0U) {
		printk("WARNING: MIP base address is not configured\n");
		printk("  Set RP1_MIP_BASE_ADDR in rp1_irq_test_config.h\n");
		mip_ready = false;
		return;
	}

	if (!map_mmio(RP1_MIP_BASE_ADDR, RP1_MIP_REG_SIZE, &mip_base)) {
		printk("FAILED: Could not map MIP registers\n");
		mip_ready = false;
		return;
	}

	mip_ready = true;
	mip_init((uintptr_t)mip_base);
	printk("MIP initialization complete\n");
}

static void maybe_install_isr(void)
{
#if HAVE_TEST_INTID
	if (isr_installed) {
		return;
	}

	IRQ_CONNECT(TEST_INTID, 0, rp1_msi_isr, NULL, 0);
	irq_enable(TEST_INTID);
	isr_installed = true;

	printk("ISR connected to INTID %u\n", TEST_INTID);
#else
	ARG_UNUSED(isr_installed);
#endif
}

static void test_step_8_baseline_scan(void)
{
	print_banner("STEP 9: Baseline Scan (Before Trigger)");

	if (gic_ready && RP1_MIP_MSI_BASE_INTID && RP1_MIP_MSI_NUM_SPIS) {
		uint32_t start = RP1_MIP_MSI_BASE_INTID;
		uint32_t end = RP1_MIP_MSI_BASE_INTID + RP1_MIP_MSI_NUM_SPIS - 1;
		printk("Scanning GIC pending range %u-%u...\n", start, end);
		gic_scan_pending(&gic_mon, start, end);
	} else {
		printk("Skipping GIC scan (base/count not configured)\n");
	}

	if (gic_ready) {
		gic_snapshot_pending(gic_pend_before);
	}

	if (mip_ready) {
		printk("Reading MIP status...\n");
		mip_read_status((uintptr_t)mip_base, &mip_state);
		mip_dump_state(&mip_state);
	} else {
		printk("Skipping MIP status (not mapped)\n");
	}

	if (rp1_cfg_ready) {
		printk("Reading RP1 INTSTAT...\n");
		rp1_read_intstatus(&rp1_trig);
	} else {
		printk("Skipping RP1 INTSTAT (config BAR not mapped)\n");
	}
}

static bool sweep_cfg_bases(void)
{
	if (!mip_ready || !msix_configured || !rp1_bars_mapped) {
		return false;
	}

	uint32_t test_vec = RP1_MIP_MSI_OFFSET + TEST_VECTOR;
	uintptr_t saved_base = rp1_trig.rp1_base;
	bool saved_init = rp1_trig.initialized;

	printk("Sweeping RP1 config bases (step 0x%x)...\n", CFG_SWEEP_STEP);

	for (uint32_t bar = 0; bar < RP1_BAR_MAX; bar++) {
		if (!rp1_bars[bar].mapped) {
			continue;
		}

		for (uint32_t off = 0;
		     off + RP1_MSIX_CFG(TEST_VECTOR) + sizeof(uint32_t) <=
			     rp1_bars[bar].bar.size;
		     off += CFG_SWEEP_STEP) {
			struct mip_state before;
			struct mip_state after;

			rp1_trig.rp1_base = rp1_bars[bar].vaddr + off;
			rp1_trig.initialized = true;

			mip_read_status((uintptr_t)mip_base, &before);
			rp1_trigger_msix_test_quiet(&rp1_trig, TEST_VECTOR);
			k_msleep(5);
			mip_read_status((uintptr_t)mip_base, &after);
			rp1_clear_msix_test_quiet(&rp1_trig, TEST_VECTOR);

			if (!mip_vector_is_set(&before, test_vec) &&
			    mip_vector_is_set(&after, test_vec)) {
				printk("  HIT: BAR%u + 0x%x\n", bar, off);
				rp1_dev.cfg_bar = rp1_bars[bar];
				rp1_cfg_ready = true;
				rp1_cfg_bar_idx = bar;
				rp1_cfg_offset = off;
				return true;
			}

			if (test_vec < 64U) {
				mip_clear_vector((uintptr_t)mip_base, test_vec);
			}
		}
	}

	rp1_trig.rp1_base = saved_base;
	rp1_trig.initialized = saved_init;
	return false;
}

static void test_step_9_trigger(void)
{
	print_banner("STEP 10: Trigger Interrupt");

	if (!msix_configured) {
		printk("Skipping trigger (MSI-X table not configured)\n");
		return;
	}

	sweep_cfg_bases();
	if (!rp1_cfg_ready) {
		printk("Skipping trigger (RP1 config base not found)\n");
		return;
	}
	printk("Using RP1 cfg BAR%u + 0x%x\n", rp1_cfg_bar_idx, rp1_cfg_offset);

	/* Restore MSI-X table entry in case the sweep touched BAR0 */
	if (msix_configured) {
		uint32_t msg_data = RP1_MIP_MSI_OFFSET + TEST_VECTOR;
		rp1_setup_msix_entry(&rp1_dev, TEST_VECTOR,
				     RP1_MIP_MSG_ADDR, msg_data);
	}

	printk("Using MSI-X TEST bit to generate an interrupt\n");
	rp1_trigger_msix_test(&rp1_trig, TEST_VECTOR);

	printk("Waiting 100 ms for propagation...\n");
	k_msleep(100);
}

static void test_step_10_verify(void)
{
	print_banner("STEP 11: Verify Interrupt Path");

	if (rp1_cfg_ready) {
		printk("1. RP1 INTSTAT:\n");
		rp1_read_intstatus(&rp1_trig);
	} else {
		printk("1. RP1 INTSTAT: skipped\n");
	}

	if (mip_ready) {
		printk("2. MIP status:\n");
		mip_read_status((uintptr_t)mip_base, &mip_state);
		mip_dump_state(&mip_state);
	} else {
		printk("2. MIP status: skipped\n");
	}

	if (gic_ready && RP1_MIP_MSI_BASE_INTID && RP1_MIP_MSI_NUM_SPIS) {
		uint32_t start = RP1_MIP_MSI_BASE_INTID;
		uint32_t end = RP1_MIP_MSI_BASE_INTID +
			RP1_MIP_MSI_NUM_SPIS - 1;
		printk("3. GIC pending scan:\n");
		gic_scan_pending(&gic_mon, start, end);
	} else {
		printk("3. GIC pending scan: skipped\n");
	}

	if (gic_ready) {
		gic_snapshot_pending(gic_pend_after);
		printk("4. GIC pending diff:\n");
		gic_dump_pending_diff(gic_pend_before, gic_pend_after);
	}

	printk("\nDiagnosis:\n");

	uint32_t test_vec = RP1_MIP_MSI_OFFSET + TEST_VECTOR;
	bool mip_hit = mip_ready && mip_vector_is_set(&mip_state, test_vec);
	bool gic_ok = false;

#if HAVE_TEST_INTID
	if (gic_ready) {
		gic_ok = gic_is_intid_pending(&gic_mon, TEST_INTID);
	}
#endif

	if (atomic_get(&isr_hits) > 0) {
		gic_ok = true;
	}

	if (gic_ok) {
		printk("  SUCCESS: Interrupt reached GIC (INTID %u)\n", TEST_INTID);
		printk("  Path OK: RP1 -> MSI-X -> MIP -> GIC\n");
	} else if (mip_hit) {
		printk("  PARTIAL: Interrupt reached MIP but not GIC\n");
		printk("  Check: MIP base INTID, GIC enable/priority\n");
	} else {
		printk("  FAILED: Interrupt did not reach MIP\n");
		printk("  Check: MSI-X table, msg_addr, MIP base\n");
	}

	if (isr_installed) {
		printk("ISR hits: %ld\n", (long)atomic_get(&isr_hits));
	}
}

static void test_step_11_cleanup(void)
{
	print_banner("STEP 12: Cleanup");

	if (rp1_cfg_ready) {
		printk("Clearing MSI-X TEST bit...\n");
		rp1_clear_msix_test(&rp1_trig, TEST_VECTOR);
	}

	if (mip_ready) {
		printk("Clearing MIP status...\n");
		mip_clear_vector((uintptr_t)mip_base,
				 RP1_MIP_MSI_OFFSET + TEST_VECTOR);
	}

	printk("Cleanup complete\n");
}

static void print_summary(void)
{
	print_banner("Configuration Summary");

	printk("PCIe/RP1:\n");
	printk("  RP1 BDF:           0x%08x\n", rp1_dev.bdf);
	printk("  MSI-X vectors:     %u\n", rp1_dev.msix_count);
	printk("  MSI-X table:       BAR%u + 0x%x\n",
	       rp1_dev.msix_table_bar, rp1_dev.msix_table_offset);
	printk("  RP1 cfg BAR idx:   %u\n",
	       rp1_cfg_ready ? rp1_cfg_bar_idx :
	       ((RP1_CFG_BAR_INDEX == CFG_BAR_SENTINEL) ?
		rp1_dev.msix_table_bar : RP1_CFG_BAR_INDEX));
	printk("  RP1 cfg offset:    0x%x\n", rp1_cfg_offset);

	printk("\nMIP:\n");
	printk("  Base address:      0x%llx\n",
	       (unsigned long long)RP1_MIP_BASE_ADDR);
	printk("  Reg size:          0x%x\n", RP1_MIP_REG_SIZE);
	printk("  Msg address:       0x%llx\n",
	       (unsigned long long)RP1_MIP_MSG_ADDR);
	printk("  Base INTID:        %u\n", RP1_MIP_MSI_BASE_INTID);
	printk("  MSI count:         %u\n", RP1_MIP_MSI_NUM_SPIS);
	printk("  MSI offset:        %u\n", RP1_MIP_MSI_OFFSET);

	printk("\nTest vector:\n");
	printk("  Vector:            %u\n", TEST_VECTOR);
	printk("  Msg data:          0x%x\n",
	       RP1_MIP_MSI_OFFSET + TEST_VECTOR);
	if (RP1_MIP_MSI_BASE_INTID != 0U) {
		printk("  Expected INTID:    %u\n",
		       RP1_MIP_MSI_BASE_INTID + TEST_VECTOR);
	}

	printk("\nNOTE: Update rp1_irq_test_config.h with real values.\n");
}

static void print_next_steps(void)
{
	print_banner("Next Steps");

	printk("If the interrupt path is working:\n");
	printk("  1. Hook a real ISR to the INTID using IRQ_CONNECT()\n");
	printk("  2. Implement RP1 peripheral drivers (GPIO, UART, etc.)\n");
	printk("  3. Use a real interrupt source instead of TEST\n");

	printk("\nIf the path is NOT working:\n");
	printk("  1. Verify config values in rp1_irq_test_config.h\n");
	printk("  2. Confirm MSI-X table BAR and msg_addr\n");
	printk("  3. Check GIC enable/priority and MIP base INTID\n");

	printk("\nFor production:\n");
	printk("  - Implement MIP as an interrupt-controller in DT\n");
	printk("  - Use multi-level interrupts (CONFIG_MULTI_LEVEL_INTERRUPTS)\n");
	printk("  - Implement IACK for level-triggered sources\n");
}

int main(void)
{
	printk("\n\n");
	printk("========================================\n");
	printk(" RPi5 RP1 Interrupt Path Verification\n");
	printk(" RP1 -> MSI-X -> MIP -> GIC -> Zephyr\n");
	printk("========================================\n\n");

	printk("This sample will:\n");
	printk("  1. Enumerate RP1 on PCIe\n");
	printk("  2. Configure MSI-X and MIP\n");
	printk("  3. Trigger a test interrupt\n");
	printk("  4. Verify MIP and GIC state\n\n");

	k_msleep(500);

	test_step_0_map_gic();
	k_msleep(200);

	test_step_1_find_rp1();
	k_msleep(200);

	if (!rp1_dev.found) {
		printk("\nERROR: Cannot continue without RP1 device\n");
		return -1;
	}

	test_step_2_find_msix();
	k_msleep(200);

	test_step_3_map_bars();
	k_msleep(200);

	test_step_4_bind_msix_table();
	k_msleep(200);

	test_step_5_map_rp1_cfg_bar();
	k_msleep(200);

	test_step_5_enable_msix();
	k_msleep(200);

	test_step_6_setup_table();
	k_msleep(200);

	test_step_7_init_mip();
	k_msleep(200);

	maybe_install_isr();

	test_step_8_baseline_scan();
	k_msleep(200);

	test_step_9_trigger();

	test_step_10_verify();
	k_msleep(200);

	test_step_11_cleanup();

	printk("\n");
	print_summary();
	printk("\n");
	print_next_steps();

	printk("\n");
	print_banner("Test Complete");
	printk("\nCheck the output above to see where the path stops.\n");

	while (1) {
		k_msleep(10000);
	}

	return 0;
}
