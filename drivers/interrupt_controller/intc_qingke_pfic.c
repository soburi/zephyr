/*
 * Copyright (c) 2023 Chen Xingyu <hi@xingrz.me>
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT wch_qingke_pfic

#include <zephyr/arch/cpu.h>
#include <zephyr/arch/riscv/irq.h>
#include <zephyr/device.h>

#define PFIC_BASE DT_INST_REG_ADDR(0)

#define PFIC_ISR(n)      (PFIC_BASE + 0x000 + 0x4 * n)
#define PFIC_IPR(n)      (PFIC_BASE + 0x020 + 0x4 * n)
#define PFIC_ITHRESDR    (PFIC_BASE + 0x040)
#define PFIC_VTFBADDRR   (PFIC_BASE + 0x044)
#define PFIC_CFGR        (PFIC_BASE + 0x048)
#define PFIC_GISR        (PFIC_BASE + 0x04C)
#define PFIC_IDCFGR      (PFIC_BASE + 0x050)
#define PFIC_VTFADDRR(n) (PFIC_BASE + 0x060 + 0x4 * n)
#define PFIC_IENR(n)     (PFIC_BASE + 0x100 + 0x4 * n)
#define PFIC_IRER(n)     (PFIC_BASE + 0x180 + 0x4 * n)
#define PFIC_IPSR(n)     (PFIC_BASE + 0x200 + 0x4 * n)
#define PFIC_IPRR(n)     (PFIC_BASE + 0x280 + 0x4 * n)
#define PFIC_IACTR(n)    (PFIC_BASE + 0x300 + 0x4 * n)
#define PFIC_IPRIOR(n)   (PFIC_BASE + 0x400 + n)
#define PFIC_SCTLR       (PFIC_BASE + 0xD10)

/* PFIC_ITHRESDR Register */
#define PFIC_ITHRESDR_THRESHOLD(n) ((n & BIT_MASK(4)) << 4)

#define PFIC_IRQN_GROUP(irqn) (irqn >> 5)
#define PFIC_IRQN_SHIFT(irqn) (irqn & BIT_MASK(5))

void riscv_clic_irq_enable(unsigned int irq)
{
	sys_write32(BIT(PFIC_IRQN_SHIFT(irq)), PFIC_IENR(PFIC_IRQN_GROUP(irq)));
}

void riscv_clic_irq_disable(unsigned int irq)
{
	uint32_t regval;

	/* Temporarily mask ISRs with priority lower than SYSTICK (1) */
	regval = sys_read32(PFIC_ITHRESDR);
	sys_write32(PFIC_ITHRESDR_THRESHOLD(1), PFIC_ITHRESDR);

	sys_write32(BIT(PFIC_IRQN_SHIFT(irq)), PFIC_IRER(PFIC_IRQN_GROUP(irq)));

	/* Restore the original threshold */
	sys_write32(regval, PFIC_ITHRESDR);
}

int riscv_clic_irq_is_enabled(unsigned int irq)
{
	return sys_read32(PFIC_ISR(PFIC_IRQN_GROUP(irq))) & BIT(PFIC_IRQN_SHIFT(irq));
}

void riscv_clic_irq_priority_set(unsigned int irq, unsigned int prio, uint32_t flags)
{
	sys_write8(prio, PFIC_IPRIOR(irq));
}
