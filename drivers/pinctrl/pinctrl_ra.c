/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/pinctrl.h>
#include <string.h>

#define DT_DRV_COMPAT renesas_ra_pinctrl

#define PmnPFS_PMR_POS (16UL)
#define PWPR_PFSWE_POS (6UL)
#define PWPR_B0WI_POS  (7UL)

struct pfs_regs {
	struct {
		struct {
			struct {
				volatile uint32_t PmnPFS;
			};
		} PIN[16];
	} PORT[15];
};

struct pmisc_pwpr_regs {
	volatile uint8_t PWPR;
};

static struct {
	struct pfs_regs *PFS;
	struct pmisc_pwpr_regs *PMISC;
} regs = {(struct pfs_regs *)(DT_INST_REG_ADDR_BY_NAME(0, pfs)),
	  (struct pmisc_pwpr_regs *)(DT_INST_REG_ADDR_BY_NAME(0, pmisc_pwpr))};

static void configure_pfs(const pinctrl_soc_pin_t *pinc)
{
	pinctrl_soc_pin_t pincfg;

	memcpy(&pincfg, pinc, sizeof(pinctrl_soc_pin_t));
	pincfg.pin = 0;
	pincfg.port = 0;

	/* Clear PMR bits before configuring */
	if ((pincfg.config & PmnPFS_PMR_POS)) {
		regs.PFS->PORT[pinc->port].PIN[pinc->pin].PmnPFS &= ~(BIT(PmnPFS_PMR_POS));
		regs.PFS->PORT[pinc->port].PIN[pinc->pin].PmnPFS =
			(pincfg.config & ~PmnPFS_PMR_POS);
	}

	regs.PFS->PORT[pinc->port].PIN[pinc->pin].PmnPFS = pincfg.config;
}

int pinctrl_configure_pins(const pinctrl_soc_pin_t *pins, uint8_t pin_cnt, uintptr_t reg)
{
	regs.PMISC->PWPR = 0;
	regs.PMISC->PWPR = BIT(PWPR_PFSWE_POS);

	for (int i = 0; i < pin_cnt; i++) {
		configure_pfs(&pins[i]);
	}

	regs.PMISC->PWPR = 0;
	regs.PMISC->PWPR = BIT(PWPR_B0WI_POS);

	return 0;
}
