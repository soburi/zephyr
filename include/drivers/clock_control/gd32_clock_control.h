#ifndef GD32_CLOCK_CONTROL_H
#define GD32_CLOCK_CONTROL_H

#include <drivers/clock_control.h>

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
	int ret = device_usable_check(dev);

	if (ret != 0) {
		return ret;
	}

	const struct gd32_clock_control_driver_api *api =
		(const struct gd32_clock_control_driver_api *)dev->api;

	api->peripheral_enable(dev, func);
	return 0;
}

static inline int gd32_clock_control_peripheral_disable(const struct device *dev,
				   uint32_t func)
{
	int ret = device_usable_check(dev);

	if (ret != 0) {
		return ret;
	}

	const struct gd32_clock_control_driver_api *api =
		(const struct gd32_clock_control_driver_api *)dev->api;

	api->peripheral_enable(dev, func);
	return 0;
}

/* define the peripheral clock enable bit position and its register index offset */
#define RCU_REGIDX_BIT(regidx, bitpos)      (((uint32_t)(regidx) << 6) | (uint32_t)(bitpos))
#define RCU_BIT_POS(val)                    ((uint32_t)(val) & 0x1FU)

/* register offset */
/* peripherals enable */
#define AHBEN_REG_OFFSET                0x14U                     /*!< AHB enable register offset */
#define APB1EN_REG_OFFSET               0x1CU                     /*!< APB1 enable register offset */
#define APB2EN_REG_OFFSET               0x18U                     /*!< APB2 enable register offset */

/* peripherals reset */
#define AHBRST_REG_OFFSET               0x28U                     /*!< AHB reset register offset */
#define APB1RST_REG_OFFSET              0x10U                     /*!< APB1 reset register offset */
#define APB2RST_REG_OFFSET              0x0CU                     /*!< APB2 reset register offset */
#define RSTSCK_REG_OFFSET               0x24U                     /*!< reset source/clock register offset */

/* clock control */
#define CTL_REG_OFFSET                  0x00U                     /*!< control register offset */
#define BDCTL_REG_OFFSET                0x20U                     /*!< backup domain control register offset */

/* clock stabilization and stuck interrupt */
#define INT_REG_OFFSET                  0x08U                     /*!< clock interrupt register offset */

/* configuration register */
#define CFG0_REG_OFFSET                 0x04U                     /*!< clock configuration register 0 offset */
#define CFG1_REG_OFFSET                 0x2CU                     /*!< clock configuration register 1 offset */

/* peripheral clock enable */
typedef enum {
    /* AHB peripherals */
    RCU_DMA0 = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 0U),              /*!< DMA0 clock */
    RCU_DMA1 = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 1U),              /*!< DMA1 clock */
    RCU_CRC = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 6U),               /*!< CRC clock */
    RCU_EXMC = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 8U),              /*!< EXMC clock */
    RCU_USBFS = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 12U),            /*!< USBFS clock */
    /* APB1 peripherals */
    RCU_TIMER1 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 0U),           /*!< TIMER1 clock */
    RCU_TIMER2 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 1U),           /*!< TIMER2 clock */
    RCU_TIMER3 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 2U),           /*!< TIMER3 clock */
    RCU_TIMER4 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 3U),           /*!< TIMER4 clock */
    RCU_TIMER5 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 4U),           /*!< TIMER5 clock */
    RCU_TIMER6 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 5U),           /*!< TIMER6 clock */
    RCU_WWDGT = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 11U),           /*!< WWDGT clock */
    RCU_SPI1 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 14U),            /*!< SPI1 clock */
    RCU_SPI2 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 15U),            /*!< SPI2 clock */
    RCU_USART1 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 17U),          /*!< USART1 clock */
    RCU_USART2 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 18U),          /*!< USART2 clock */
    RCU_UART3 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 19U),           /*!< UART3 clock */
    RCU_UART4 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 20U),           /*!< UART4 clock */
    RCU_I2C0 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 21U),            /*!< I2C0 clock */
    RCU_I2C1 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 22U),            /*!< I2C1 clock */
    RCU_CAN0 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 25U),            /*!< CAN0 clock */
    RCU_CAN1 = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 26U),            /*!< CAN1 clock */
    RCU_BKPI = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 27U),            /*!< BKPI clock */
    RCU_PMU = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 28U),             /*!< PMU clock */
    RCU_DAC = RCU_REGIDX_BIT(APB1EN_REG_OFFSET, 29U),             /*!< DAC clock */
    RCU_RTC = RCU_REGIDX_BIT(BDCTL_REG_OFFSET, 15U),              /*!< RTC clock */
    /* APB2 peripherals */
    RCU_AF = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 0U),               /*!< alternate function clock */
    RCU_GPIOA = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 2U),            /*!< GPIOA clock */
    RCU_GPIOB = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 3U),            /*!< GPIOB clock */
    RCU_GPIOC = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 4U),            /*!< GPIOC clock */
    RCU_GPIOD = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 5U),            /*!< GPIOD clock */
    RCU_GPIOE = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 6U),            /*!< GPIOE clock */
    RCU_ADC0 = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 9U),             /*!< ADC0 clock */
    RCU_ADC1 = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 10U),            /*!< ADC1 clock */
    RCU_TIMER0 = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 11U),          /*!< TIMER0 clock */
    RCU_SPI0 = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 12U),            /*!< SPI0 clock */
    RCU_USART0 = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 14U),          /*!< USART0 clock */
} rcu_periph_enum;

/* peripheral clock enable when sleep mode*/
typedef enum {
    /* AHB peripherals */
    RCU_SRAM_SLP = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 2U),          /*!< SRAM clock */
    RCU_FMC_SLP = RCU_REGIDX_BIT(AHBEN_REG_OFFSET, 4U),           /*!< FMC clock */
} rcu_periph_sleep_enum;

/* peripherals reset */
typedef enum {
    /* AHB peripherals */
    RCU_USBFSRST = RCU_REGIDX_BIT(AHBRST_REG_OFFSET, 12U),        /*!< USBFS clock reset */
    /* APB1 peripherals */
    RCU_TIMER1RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 0U),       /*!< TIMER1 clock reset */
    RCU_TIMER2RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 1U),       /*!< TIMER2 clock reset */
    RCU_TIMER3RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 2U),       /*!< TIMER3 clock reset */
    RCU_TIMER4RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 3U),       /*!< TIMER4 clock reset */
    RCU_TIMER5RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 4U),       /*!< TIMER5 clock reset */
    RCU_TIMER6RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 5U),       /*!< TIMER6 clock reset */
    RCU_WWDGTRST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 11U),       /*!< WWDGT clock reset */
    RCU_SPI1RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 14U),        /*!< SPI1 clock reset */
    RCU_SPI2RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 15U),        /*!< SPI2 clock reset */
    RCU_USART1RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 17U),      /*!< USART1 clock reset */
    RCU_USART2RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 18U),      /*!< USART2 clock reset */
    RCU_UART3RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 19U),       /*!< UART3 clock reset */
    RCU_UART4RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 20U),       /*!< UART4 clock reset */
    RCU_I2C0RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 21U),        /*!< I2C0 clock reset */
    RCU_I2C1RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 22U),        /*!< I2C1 clock reset */
    RCU_CAN0RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 25U),        /*!< CAN0 clock reset */
    RCU_CAN1RST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 26U),        /*!< CAN1 clock reset */
    RCU_BKPIRST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 27U),        /*!< BKPI clock reset */
    RCU_PMURST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 28U),         /*!< PMU clock reset */
    RCU_DACRST = RCU_REGIDX_BIT(APB1RST_REG_OFFSET, 29U),         /*!< DAC clock reset */
    /* APB2 peripherals */
    RCU_AFRST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 0U),           /*!< alternate function clock reset */
    RCU_GPIOARST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 2U),        /*!< GPIOA clock reset */
    RCU_GPIOBRST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 3U),        /*!< GPIOB clock reset */
    RCU_GPIOCRST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 4U),        /*!< GPIOC clock reset */
    RCU_GPIODRST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 5U),        /*!< GPIOD clock reset */
    RCU_GPIOERST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 6U),        /*!< GPIOE clock reset */
    RCU_ADC0RST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 9U),         /*!< ADC0 clock reset */
    RCU_ADC1RST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 10U),        /*!< ADC1 clock reset */
    RCU_TIMER0RST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 11U),      /*!< TIMER0 clock reset */
    RCU_SPI0RST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 12U),        /*!< SPI0 clock reset */
    RCU_USART0RST = RCU_REGIDX_BIT(APB2RST_REG_OFFSET, 14U),      /*!< USART0 clock reset */
} rcu_periph_reset_enum;

/* clock stabilization and peripheral reset flags */
typedef enum {
    /* clock stabilization flags */
    RCU_FLAG_IRC8MSTB = RCU_REGIDX_BIT(CTL_REG_OFFSET, 1U),       /*!< IRC8M stabilization flags */
    RCU_FLAG_HXTALSTB = RCU_REGIDX_BIT(CTL_REG_OFFSET, 17U),      /*!< HXTAL stabilization flags */
    RCU_FLAG_PLLSTB = RCU_REGIDX_BIT(CTL_REG_OFFSET, 25U),        /*!< PLL stabilization flags */
    RCU_FLAG_PLL1STB = RCU_REGIDX_BIT(CTL_REG_OFFSET, 27U),       /*!< PLL1 stabilization flags */
    RCU_FLAG_PLL2STB = RCU_REGIDX_BIT(CTL_REG_OFFSET, 29U),       /*!< PLL2 stabilization flags */
    RCU_FLAG_LXTALSTB = RCU_REGIDX_BIT(BDCTL_REG_OFFSET, 1U),     /*!< LXTAL stabilization flags */
    RCU_FLAG_IRC40KSTB = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 1U),   /*!< IRC40K stabilization flags */
    /* reset source flags */
    RCU_FLAG_EPRST = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 26U),      /*!< external PIN reset flags */
    RCU_FLAG_PORRST = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 27U),     /*!< power reset flags */
    RCU_FLAG_SWRST = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 28U),      /*!< software reset flags */
    RCU_FLAG_FWDGTRST = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 29U),   /*!< FWDGT reset flags */
    RCU_FLAG_WWDGTRST = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 30U),   /*!< WWDGT reset flags */
    RCU_FLAG_LPRST = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 31U),      /*!< low-power reset flags */
} rcu_flag_enum;

/* clock stabilization and ckm interrupt flags */
typedef enum {
    RCU_INT_FLAG_IRC40KSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 0U),  /*!< IRC40K stabilization interrupt flag */
    RCU_INT_FLAG_LXTALSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 1U),   /*!< LXTAL stabilization interrupt flag */
    RCU_INT_FLAG_IRC8MSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 2U),   /*!< IRC8M stabilization interrupt flag */
    RCU_INT_FLAG_HXTALSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 3U),   /*!< HXTAL stabilization interrupt flag */
    RCU_INT_FLAG_PLLSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 4U),     /*!< PLL stabilization interrupt flag */
    RCU_INT_FLAG_PLL1STB = RCU_REGIDX_BIT(INT_REG_OFFSET, 5U),    /*!< PLL1 stabilization interrupt flag */
    RCU_INT_FLAG_PLL2STB = RCU_REGIDX_BIT(INT_REG_OFFSET, 6U),    /*!< PLL2 stabilization interrupt flag */
    RCU_INT_FLAG_CKM = RCU_REGIDX_BIT(INT_REG_OFFSET, 7U),        /*!< HXTAL clock stuck interrupt flag */
} rcu_int_flag_enum;

/* clock stabilization and stuck interrupt flags clear */
typedef enum {
    RCU_INT_FLAG_IRC40KSTB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 16U), /*!< IRC40K stabilization interrupt flags clear */
    RCU_INT_FLAG_LXTALSTB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 17U),  /*!< LXTAL stabilization interrupt flags clear */
    RCU_INT_FLAG_IRC8MSTB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 18U),  /*!< IRC8M stabilization interrupt flags clear */
    RCU_INT_FLAG_HXTALSTB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 19U),  /*!< HXTAL stabilization interrupt flags clear */
    RCU_INT_FLAG_PLLSTB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 20U),    /*!< PLL stabilization interrupt flags clear */
    RCU_INT_FLAG_PLL1STB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 21U),   /*!< PLL1 stabilization interrupt flags clear */
    RCU_INT_FLAG_PLL2STB_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 22U),   /*!< PLL2 stabilization interrupt flags clear */
    RCU_INT_FLAG_CKM_CLR = RCU_REGIDX_BIT(INT_REG_OFFSET, 23U),       /*!< CKM interrupt flags clear */
} rcu_int_flag_clear_enum;

/* clock stabilization interrupt enable or disable */
typedef enum {
    RCU_INT_IRC40KSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 8U),  /*!< IRC40K stabilization interrupt */
    RCU_INT_LXTALSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 9U),   /*!< LXTAL stabilization interrupt */
    RCU_INT_IRC8MSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 10U),  /*!< IRC8M stabilization interrupt */
    RCU_INT_HXTALSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 11U),  /*!< HXTAL stabilization interrupt */
    RCU_INT_PLLSTB = RCU_REGIDX_BIT(INT_REG_OFFSET, 12U),    /*!< PLL stabilization interrupt */
    RCU_INT_PLL1STB = RCU_REGIDX_BIT(INT_REG_OFFSET, 13U),   /*!< PLL1 stabilization interrupt */
    RCU_INT_PLL2STB = RCU_REGIDX_BIT(INT_REG_OFFSET, 14U),   /*!< PLL2 stabilization interrupt */
} rcu_int_enum;

/* oscillator types */
typedef enum {
    RCU_HXTAL = RCU_REGIDX_BIT(CTL_REG_OFFSET, 16U),         /*!< HXTAL */
    RCU_LXTAL = RCU_REGIDX_BIT(BDCTL_REG_OFFSET, 0U),        /*!< LXTAL */
    RCU_IRC8M = RCU_REGIDX_BIT(CTL_REG_OFFSET, 0U),          /*!< IRC8M */
    RCU_IRC40K = RCU_REGIDX_BIT(RSTSCK_REG_OFFSET, 0U),      /*!< IRC40K */
    RCU_PLL_CK = RCU_REGIDX_BIT(CTL_REG_OFFSET, 24U),        /*!< PLL */
    RCU_PLL1_CK = RCU_REGIDX_BIT(CTL_REG_OFFSET, 26U),       /*!< PLL1 */
    RCU_PLL2_CK = RCU_REGIDX_BIT(CTL_REG_OFFSET, 28U),       /*!< PLL2 */
} rcu_osci_type_enum;

/* rcu clock frequency */
typedef enum {
    CK_SYS = 0, /*!< system clock */
    CK_AHB, /*!< AHB clock */
    CK_APB1, /*!< APB1 clock */
    CK_APB2, /*!< APB2 clock */
} rcu_clock_freq_enum;


#endif //GD32_CLOCK_CONTROL_H
