/*
    Copyright (c) 2018, GigaDevice Semiconductor Inc.
	Copyright (c) 2021, Tokita, Hiroshi <tokita.hiroshi@gmail.com>

    Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

    1. Redistributions of source code must retain the above copyright notice, this
       list of conditions and the following disclaimer.
    2. Redistributions in binary form must reproduce the above copyright notice,
       this list of conditions and the following disclaimer in the documentation
       and/or other materials provided with the distribution.
    3. Neither the name of the copyright holder nor the names of its contributors
       may be used to endorse or promote products derived from this software without
       specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
OF SUCH DAMAGE.
*/

#ifndef GD32_CLOCK_CONTROL_H
#define GD32_CLOCK_CONTROL_H

#include <drivers/clock_control.h>

struct gd32_pclken {
	uint32_t bus;
	uint32_t enr;
};

typedef void (*gd32_clock_control_peripheral_enable_fn)(
						    const struct device *dev,
						    uint32_t func);
typedef void (*gd32_clock_control_peripheral_disable_fn)(
						    const struct device *dev,
						    uint32_t sys);

struct gd32_clock_control_driver_api {
	struct clock_control_driver_api api;
	gd32_clock_control_peripheral_enable_fn peripheral_enable;
	gd32_clock_control_peripheral_disable_fn peripheral_disable;
};

static inline int gd32_clock_control_peripheral_enable(const struct device *dev,
				   uint32_t func)
{
	const struct gd32_clock_control_driver_api *api =
		(const struct gd32_clock_control_driver_api *)dev->api;

	api->peripheral_enable(dev, func);
	return 0;
}

static inline int gd32_clock_control_peripheral_disable(const struct device *dev,
				   uint32_t func)
{
	const struct gd32_clock_control_driver_api *api =
		(const struct gd32_clock_control_driver_api *)dev->api;

	api->peripheral_disable(dev, func);
	return 0;
}

/* define the peripheral clock enable bit position and its register index offset */
#define GD32_RCU_REGIDX_BIT(regidx, bitpos)      (((uint32_t)(regidx) << 6) | (uint32_t)(bitpos))
#define GD32_RCU_BIT_POS(val)                    ((uint32_t)(val) & 0x1FU)

/* register offset */
/* peripherals enable */
#define GD32_AHBEN_REG_OFFSET                0x14U                     /*!< AHB enable register offset */
#define GD32_APB1EN_REG_OFFSET               0x1CU                     /*!< APB1 enable register offset */
#define GD32_APB2EN_REG_OFFSET               0x18U                     /*!< APB2 enable register offset */

/* peripherals reset */
#define GD32_AHBRST_REG_OFFSET               0x28U                     /*!< AHB reset register offset */
#define GD32_APB1RST_REG_OFFSET              0x10U                     /*!< APB1 reset register offset */
#define GD32_APB2RST_REG_OFFSET              0x0CU                     /*!< APB2 reset register offset */
#define GD32_RSTSCK_REG_OFFSET               0x24U                     /*!< reset source/clock register offset */

/* clock control */
#define GD32_CTL_REG_OFFSET                  0x00U                     /*!< control register offset */
#define GD32_BDCTL_REG_OFFSET                0x20U                     /*!< backup domain control register offset */

/* clock stabilization and stuck interrupt */
#define GD32_INT_REG_OFFSET                  0x08U                     /*!< clock interrupt register offset */

/* configuration register */
#define GD32_CFG0_REG_OFFSET                 0x04U                     /*!< clock configuration register 0 offset */
#define GD32_CFG1_REG_OFFSET                 0x2CU                     /*!< clock configuration register 1 offset */

/* peripheral clock enable */
typedef enum {
    /* AHB peripherals */
    GD32_RCU_DMA0 = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 0U),              /*!< DMA0 clock */
    GD32_RCU_DMA1 = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 1U),              /*!< DMA1 clock */
    GD32_RCU_CRC = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 6U),               /*!< CRC clock */
    GD32_RCU_EXMC = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 8U),              /*!< EXMC clock */
    GD32_RCU_USBFS = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 12U),            /*!< USBFS clock */
    /* APB1 peripherals */
    GD32_RCU_TIMER1 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 0U),           /*!< TIMER1 clock */
    GD32_RCU_TIMER2 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 1U),           /*!< TIMER2 clock */
    GD32_RCU_TIMER3 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 2U),           /*!< TIMER3 clock */
    GD32_RCU_TIMER4 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 3U),           /*!< TIMER4 clock */
    GD32_RCU_TIMER5 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 4U),           /*!< TIMER5 clock */
    GD32_RCU_TIMER6 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 5U),           /*!< TIMER6 clock */
    GD32_RCU_WWDGT = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 11U),           /*!< WWDGT clock */
    GD32_RCU_SPI1 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 14U),            /*!< SPI1 clock */
    GD32_RCU_SPI2 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 15U),            /*!< SPI2 clock */
    GD32_RCU_USART1 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 17U),          /*!< USART1 clock */
    GD32_RCU_USART2 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 18U),          /*!< USART2 clock */
    GD32_RCU_UART3 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 19U),           /*!< UART3 clock */
    GD32_RCU_UART4 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 20U),           /*!< UART4 clock */
    GD32_RCU_I2C0 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 21U),            /*!< I2C0 clock */
    GD32_RCU_I2C1 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 22U),            /*!< I2C1 clock */
    GD32_RCU_CAN0 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 25U),            /*!< CAN0 clock */
    GD32_RCU_CAN1 = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 26U),            /*!< CAN1 clock */
    GD32_RCU_BKPI = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 27U),            /*!< BKPI clock */
    GD32_RCU_PMU = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 28U),             /*!< PMU clock */
    GD32_RCU_DAC = GD32_RCU_REGIDX_BIT(GD32_APB1EN_REG_OFFSET, 29U),             /*!< DAC clock */
    GD32_RCU_RTC = GD32_RCU_REGIDX_BIT(GD32_BDCTL_REG_OFFSET, 15U),              /*!< RTC clock */
    /* APB2 peripherals */
    GD32_RCU_AF = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 0U),               /*!< alternate function clock */
    GD32_RCU_GPIOA = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 2U),            /*!< GPIOA clock */
    GD32_RCU_GPIOB = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 3U),            /*!< GPIOB clock */
    GD32_RCU_GPIOC = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 4U),            /*!< GPIOC clock */
    GD32_RCU_GPIOD = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 5U),            /*!< GPIOD clock */
    GD32_RCU_GPIOE = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 6U),            /*!< GPIOE clock */
    GD32_RCU_ADC0 = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 9U),             /*!< ADC0 clock */
    GD32_RCU_ADC1 = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 10U),            /*!< ADC1 clock */
    GD32_RCU_TIMER0 = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 11U),          /*!< TIMER0 clock */
    GD32_RCU_SPI0 = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 12U),            /*!< SPI0 clock */
    GD32_RCU_USART0 = GD32_RCU_REGIDX_BIT(GD32_APB2EN_REG_OFFSET, 14U),          /*!< USART0 clock */
} gd32_rcu_periph_enum;

/* peripheral clock enable when sleep mode*/
typedef enum {
    /* AHB peripherals */
    GD32_RCU_SRAM_SLP = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 2U),          /*!< SRAM clock */
    GD32_RCU_FMC_SLP = GD32_RCU_REGIDX_BIT(GD32_AHBEN_REG_OFFSET, 4U),           /*!< FMC clock */
} gd32_rcu_periph_sleep_enum;

/* peripherals reset */
typedef enum {
    /* AHB peripherals */
    GD32_RCU_USBFSRST = GD32_RCU_REGIDX_BIT(GD32_AHBRST_REG_OFFSET, 12U),        /*!< USBFS clock reset */
    /* APB1 peripherals */
    GD32_RCU_TIMER1RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 0U),       /*!< TIMER1 clock reset */
    GD32_RCU_TIMER2RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 1U),       /*!< TIMER2 clock reset */
    GD32_RCU_TIMER3RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 2U),       /*!< TIMER3 clock reset */
    GD32_RCU_TIMER4RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 3U),       /*!< TIMER4 clock reset */
    GD32_RCU_TIMER5RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 4U),       /*!< TIMER5 clock reset */
    GD32_RCU_TIMER6RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 5U),       /*!< TIMER6 clock reset */
    GD32_RCU_WWDGTRST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 11U),       /*!< WWDGT clock reset */
    GD32_RCU_SPI1RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 14U),        /*!< SPI1 clock reset */
    GD32_RCU_SPI2RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 15U),        /*!< SPI2 clock reset */
    GD32_RCU_USART1RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 17U),      /*!< USART1 clock reset */
    GD32_RCU_USART2RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 18U),      /*!< USART2 clock reset */
    GD32_RCU_UART3RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 19U),       /*!< UART3 clock reset */
    GD32_RCU_UART4RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 20U),       /*!< UART4 clock reset */
    GD32_RCU_I2C0RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 21U),        /*!< I2C0 clock reset */
    GD32_RCU_I2C1RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 22U),        /*!< I2C1 clock reset */
    GD32_RCU_CAN0RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 25U),        /*!< CAN0 clock reset */
    GD32_RCU_CAN1RST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 26U),        /*!< CAN1 clock reset */
    GD32_RCU_BKPIRST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 27U),        /*!< BKPI clock reset */
    GD32_RCU_PMURST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 28U),         /*!< PMU clock reset */
    GD32_RCU_DACRST = GD32_RCU_REGIDX_BIT(GD32_APB1RST_REG_OFFSET, 29U),         /*!< DAC clock reset */
    /* APB2 peripherals */
    GD32_RCU_AFRST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 0U),           /*!< alternate function clock reset */
    GD32_RCU_GPIOARST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 2U),        /*!< GPIOA clock reset */
    GD32_RCU_GPIOBRST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 3U),        /*!< GPIOB clock reset */
    GD32_RCU_GPIOCRST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 4U),        /*!< GPIOC clock reset */
    GD32_RCU_GPIODRST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 5U),        /*!< GPIOD clock reset */
    GD32_RCU_GPIOERST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 6U),        /*!< GPIOE clock reset */
    GD32_RCU_ADC0RST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 9U),         /*!< ADC0 clock reset */
    GD32_RCU_ADC1RST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 10U),        /*!< ADC1 clock reset */
    GD32_RCU_TIMER0RST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 11U),      /*!< TIMER0 clock reset */
    GD32_RCU_SPI0RST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 12U),        /*!< SPI0 clock reset */
    GD32_RCU_USART0RST = GD32_RCU_REGIDX_BIT(GD32_APB2RST_REG_OFFSET, 14U),      /*!< USART0 clock reset */
} gd32_rcu_periph_reset_enum;

/* clock stabilization and peripheral reset flags */
typedef enum {
    /* clock stabilization flags */
    GD32_RCU_FLAG_IRC8MSTB = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 1U),       /*!< IRC8M stabilization flags */
    GD32_RCU_FLAG_HXTALSTB = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 17U),      /*!< HXTAL stabilization flags */
    GD32_RCU_FLAG_PLLSTB = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 25U),        /*!< PLL stabilization flags */
    GD32_RCU_FLAG_PLL1STB = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 27U),       /*!< PLL1 stabilization flags */
    GD32_RCU_FLAG_PLL2STB = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 29U),       /*!< PLL2 stabilization flags */
    GD32_RCU_FLAG_LXTALSTB = GD32_RCU_REGIDX_BIT(GD32_BDCTL_REG_OFFSET, 1U),     /*!< LXTAL stabilization flags */
    GD32_RCU_FLAG_IRC40KSTB = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 1U),   /*!< IRC40K stabilization flags */
    /* reset source flags */
    GD32_RCU_FLAG_EPRST = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 26U),      /*!< external PIN reset flags */
    GD32_RCU_FLAG_PORRST = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 27U),     /*!< power reset flags */
    GD32_RCU_FLAG_SWRST = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 28U),      /*!< software reset flags */
    GD32_RCU_FLAG_FWDGTRST = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 29U),   /*!< FWDGT reset flags */
    GD32_RCU_FLAG_WWDGTRST = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 30U),   /*!< WWDGT reset flags */
    GD32_RCU_FLAG_LPRST = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 31U),      /*!< low-power reset flags */
} gd32_rcu_flag_enum;

/* clock stabilization and ckm interrupt flags */
typedef enum {
    GD32_RCU_INT_FLAG_IRC40KSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 0U),  /*!< IRC40K stabilization interrupt flag */
    GD32_RCU_INT_FLAG_LXTALSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 1U),   /*!< LXTAL stabilization interrupt flag */
    GD32_RCU_INT_FLAG_IRC8MSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 2U),   /*!< IRC8M stabilization interrupt flag */
    GD32_RCU_INT_FLAG_HXTALSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 3U),   /*!< HXTAL stabilization interrupt flag */
    GD32_RCU_INT_FLAG_PLLSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 4U),     /*!< PLL stabilization interrupt flag */
    GD32_RCU_INT_FLAG_PLL1STB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 5U),    /*!< PLL1 stabilization interrupt flag */
    GD32_RCU_INT_FLAG_PLL2STB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 6U),    /*!< PLL2 stabilization interrupt flag */
    GD32_RCU_INT_FLAG_CKM = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 7U),        /*!< HXTAL clock stuck interrupt flag */
} gd32_rcu_int_flag_enum;

/* clock stabilization and stuck interrupt flags clear */
typedef enum {
    GD32_RCU_INT_FLAG_IRC40KSTB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 16U), /*!< IRC40K stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_LXTALSTB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 17U),  /*!< LXTAL stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_IRC8MSTB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 18U),  /*!< IRC8M stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_HXTALSTB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 19U),  /*!< HXTAL stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_PLLSTB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 20U),    /*!< PLL stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_PLL1STB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 21U),   /*!< PLL1 stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_PLL2STB_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 22U),   /*!< PLL2 stabilization interrupt flags clear */
    GD32_RCU_INT_FLAG_CKM_CLR = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 23U),       /*!< CKM interrupt flags clear */
} gd32_rcu_int_flag_clear_enum;

/* clock stabilization interrupt enable or disable */
typedef enum {
    GD32_RCU_INT_IRC40KSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 8U),  /*!< IRC40K stabilization interrupt */
    GD32_RCU_INT_LXTALSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 9U),   /*!< LXTAL stabilization interrupt */
    GD32_RCU_INT_IRC8MSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 10U),  /*!< IRC8M stabilization interrupt */
    GD32_RCU_INT_HXTALSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 11U),  /*!< HXTAL stabilization interrupt */
    GD32_RCU_INT_PLLSTB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 12U),    /*!< PLL stabilization interrupt */
    GD32_RCU_INT_PLL1STB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 13U),   /*!< PLL1 stabilization interrupt */
    GD32_RCU_INT_PLL2STB = GD32_RCU_REGIDX_BIT(GD32_INT_REG_OFFSET, 14U),   /*!< PLL2 stabilization interrupt */
} gd32_rcu_int_enum;

/* oscillator types */
typedef enum {
    GD32_RCU_HXTAL = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 16U),         /*!< HXTAL */
    GD32_RCU_LXTAL = GD32_RCU_REGIDX_BIT(GD32_BDCTL_REG_OFFSET, 0U),        /*!< LXTAL */
    GD32_RCU_IRC8M = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 0U),          /*!< IRC8M */
    GD32_RCU_IRC40K = GD32_RCU_REGIDX_BIT(GD32_RSTSCK_REG_OFFSET, 0U),      /*!< IRC40K */
    GD32_RCU_PLL_CK = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 24U),        /*!< PLL */
    GD32_RCU_PLL1_CK = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 26U),       /*!< PLL1 */
    GD32_RCU_PLL2_CK = GD32_RCU_REGIDX_BIT(GD32_CTL_REG_OFFSET, 28U),       /*!< PLL2 */
} gd32_rcu_osci_type_enum;

/* rcu clock frequency */
typedef enum {
    GD32_CK_SYS = 0, /*!< system clock */
    GD32_CK_AHB, /*!< AHB clock */
    GD32_CK_APB1, /*!< APB1 clock */
    GD32_CK_APB2, /*!< APB2 clock */
} gd32_rcu_clock_freq_enum;


#endif //GD32_CLOCK_CONTROL_H
