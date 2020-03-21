/*
 * Copyright (c) 2018 Foundries.io
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <errno.h>
#include <soc.h>
#include <drivers/clock_control.h>

#include "gd32vf103_rcu.h"

#define LOG_LEVEL CONFIG_CLOCK_CONTROL_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(clock_control);

struct gd32_rcu_config {
	u32_t base_address;
};

enum gd32_rcu_reg {
	AHB_ENABLE =  RCU_BASE + AHBEN_REG_OFFSET,
	APB1_ENABLE = RCU_BASE + APB1EN_REG_OFFSET,
	APB2_ENABLE = RCU_BASE + APB2EN_REG_OFFSET,
	AHB_RESET =  RCU_BASE + AHBRST_REG_OFFSET,
	APB1_RESET = RCU_BASE + APB1RST_REG_OFFSET,
	APB2_RESET = RCU_BASE + APB2RST_REG_OFFSET,
};

enum gd32_rcu_peripherals {
	DMA0_TIMER1_AF  = (1<<0),
	DMA1_TIMER2_   = (1<<1),
	_TIMER3_GPIOA  = (1<<2),
	_TIMER4_GPIOB  = (1<<3),
	_TIMER5_GPIOC  = (1<<4),
	_TIMER5_GPIOD  = (1<<5),
	CRC_x_GPIOE     = (1<<6),
	EXMC_x_        = (1<<8),
	_x_ADC0        = (1<<9),
	_x_ADC1        = (1<<10),
	_WWDGT_TIMER0  = (1<<11),
	USBFS_x_SPI0    = (1<<12),
	_SPI1_USART0   = (1<<14),
	_SPI2_        = (1<<15),
	_USART1_      = (1<<17),
	_USART2_      = (1<<18),
	_UART3_       = (1<<19),
	_UART4_       = (1<<20),
	_I2C0_        = (1<<21),
	_I2C1_        = (1<<22),
	_CAN0_        = (1<<25),
	_CAN1_        = (1<<26),
	_BKPI_        = (1<<27),
	_PMU_         = (1<<28),
	_DAC_         = (1<<29),
};


#define DEV_CFG(dev)  ((struct gd32_rcu_config *)(dev->config->config_info))
#define DEV_BASE(dev) (DEV_CFG(dev)->base_address)

static inline void periph_clock_enable(enum gd32_rcu_reg addr, enum gd32_rcu_peripherals bit)
{
	*(volatile uint32_t*)(addr) |= bit;
}

static inline void periph_clock_disable(enum gd32_rcu_reg addr, enum gd32_rcu_peripherals bit)
{
	*(volatile uint32_t*)(addr) &= ~bit;
}


static int gd32_rcu_on(struct device *dev, clock_control_subsys_t sub_system)
{
	const uint32_t  en_offset[] = {0,  AHBEN_REG_OFFSET,  APB1EN_REG_OFFSET,  APB2EN_REG_OFFSET};
	struct gd32_pclken* pclken = (struct gd32_pclken*)sub_system;
	periph_clock_enable(((uint32_t)DEV_CFG(dev)->base_address) + en_offset[pclken->bus], pclken->enr);

	return 0;
}

static int gd32_rcu_off(struct device *dev, clock_control_subsys_t sub_system)
{
	const uint32_t rst_offset[] = {0, AHBRST_REG_OFFSET, APB1RST_REG_OFFSET, APB2RST_REG_OFFSET};
	struct gd32_pclken* pclken = (struct gd32_pclken*)sub_system;
	periph_clock_disable(((uint32_t)DEV_CFG(dev)->base_address) + rst_offset[pclken->bus], pclken->enr);
	return 0;
}

static uint32_t rcu_apb1_rate(uint32_t clksrc)
{
	uint32_t reg;

	reg = RCU_CFG0;
	/* reset the APB1PSC and set according to ck_apb1 */
	uint32_t cfg = (reg & RCU_CFG0_APB1PSC);
	switch(cfg) {
		case RCU_APB1_CKAHB_DIV1: return clksrc;
		case RCU_APB1_CKAHB_DIV2: return clksrc/2;
		case RCU_APB1_CKAHB_DIV4: return clksrc/4;
		case RCU_APB1_CKAHB_DIV8: return clksrc/8;
		case RCU_APB1_CKAHB_DIV16: return clksrc/16;
	}

	return 0;
}

static uint32_t rcu_apb2_rate(uint32_t clksrc)
{
	uint32_t reg;

	reg = RCU_CFG0;
	/* reset the APB1PSC and set according to ck_apb1 */
	uint32_t cfg = (reg & RCU_CFG0_APB2PSC);
	switch(cfg) {
		case RCU_APB2_CKAHB_DIV1: return clksrc;
		case RCU_APB2_CKAHB_DIV2: return clksrc/2;
		case RCU_APB2_CKAHB_DIV4: return clksrc/4;
		case RCU_APB2_CKAHB_DIV8: return clksrc/8;
		case RCU_APB2_CKAHB_DIV16: return clksrc/16;
	}

	return 0;
}

static int gd32_rcu_get_rate(struct device *dev, clock_control_subsys_t sub_system, u32_t* rate)
{
	const uint32_t rate_[] = {-ENOTSUP, SystemCoreClock,
		rcu_apb1_rate(SystemCoreClock), rcu_apb2_rate(SystemCoreClock)};
	struct gd32_pclken* pclken = (struct gd32_pclken*)sub_system;
	*rate = rate_[pclken->bus];
	return 0;
}

static int gd32_rcu_init(struct device *dev)
{
	return 0;
}

static const struct clock_control_driver_api gd32_rcu_api = {
	.on = gd32_rcu_on,
	.off = gd32_rcu_off,
	.get_rate = gd32_rcu_get_rate,
	//.get_status = gd32_rcu_get_status,
};

static struct gd32_rcu_config gd32_rcu_config = {
	.base_address = DT_INST_0_GIGADEVICE_GD32_RCU_BASE_ADDRESS,
};

DEVICE_AND_API_INIT(gd32_rcu, GD32_CLOCK_CONTROL_NAME,
		    &gd32_rcu_init,
		    NULL, &gd32_rcu_config,
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_OBJECTS,
		    &gd32_rcu_api);
