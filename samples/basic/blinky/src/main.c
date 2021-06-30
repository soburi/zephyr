/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr.h>
#include <device.h>
#include <devicetree.h>
#include <drivers/gpio.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

#if DT_NODE_HAS_STATUS(LED0_NODE, okay)
#define LED0	DT_GPIO_LABEL(LED0_NODE, gpios)
#define PIN	DT_GPIO_PIN(LED0_NODE, gpios)
#define FLAGS	DT_GPIO_FLAGS(LED0_NODE, gpios)
#else
/* A build error here means your board isn't set up to blink an LED. */
#error "Unsupported board: led0 devicetree alias is not defined"
#define LED0	""
#define PIN	0
#define FLAGS	0
#endif
#define csr_set set_csr

/* RCU definitions */
#define RCU                             RCU_BASE

#define GPIO_CTL0(gpiox)           REG32((gpiox) + 0x00U)    /*!< GPIO port control register 0 */
#define GPIO_CTL1(gpiox)           REG32((gpiox) + 0x04U)    /*!< GPIO port control register 1 */
#define GPIO_ISTAT(gpiox)          REG32((gpiox) + 0x08U)    /*!< GPIO port input status register */
#define GPIO_OCTL(gpiox)           REG32((gpiox) + 0x0CU)    /*!< GPIO port output control register */
#define GPIO_BC(gpiox)             REG32((gpiox) + 0x14U)    /*!< GPIO bit clear register */
#define GPIO_BOP(gpiox)            REG32((gpiox) + 0x10U)    /*!< GPIO port bit operation register */
#define GPIO_PIN_SOURCE_13               ((uint8_t)0x0DU)          /*!< GPIO pin source 13 */
#define ECLIC_PRIGROUP_LEVEL4_PRIO0        4    /*!< 4 bits for level 0 bits for priority */
#define GPIO_PORT_SOURCE_GPIOC           ((uint8_t)0x02U)          /*!< output port source C */

#define GPIOC                      (GPIO_BASE + 0x00000800U)
#define GPIO_PIN_13                      BIT(13)                   /*!< GPIO pin 13 */

/* GPIO mode definitions */
#define GPIO_MODE_AIN                    ((uint8_t)0x00U)          /*!< analog input mode */
#define GPIO_MODE_IN_FLOATING            ((uint8_t)0x04U)          /*!< floating input mode */
#define GPIO_MODE_IPD                    ((uint8_t)0x28U)          /*!< pull-down input mode */
#define GPIO_MODE_IPU                    ((uint8_t)0x48U)          /*!< pull-up input mode */
#define GPIO_MODE_OUT_OD                 ((uint8_t)0x14U)          /*!< GPIO output with open-drain */
#define GPIO_MODE_OUT_PP                 ((uint8_t)0x10U)          /*!< GPIO output with push-pull */
#define GPIO_MODE_AF_OD                  ((uint8_t)0x1CU)          /*!< AFIO output with open-drain */
#define GPIO_MODE_AF_PP                  ((uint8_t)0x18U)          /*!< AFIO output with push-pull */

/* GPIO output max speed value */
#define GPIO_OSPEED_10MHZ                ((uint8_t)0x01U)          /*!< output max speed 10MHz */
#define GPIO_OSPEED_2MHZ                 ((uint8_t)0x02U)          /*!< output max speed 2MHz */
#define GPIO_OSPEED_50MHZ                ((uint8_t)0x03U)          /*!< output max speed 50MHz */

#define REG32(addr)                  (*(volatile uint32_t *)(uint32_t)(addr))

/* BUILTIN LED OF LONGAN BOARDS IS PIN PC13 */
#define LED_PIN GPIO_PIN_13
#define LED_GPIO_PORT GPIOC
#define LED_GPIO_CLK RCU_GPIOC

/* constants definitions */
/* define the peripheral clock enable bit position and its register index offset */
#define RCU_REGIDX_BIT(regidx, bitpos)      (((uint32_t)(regidx) << 6) | (uint32_t)(bitpos))
#define RCU_REG_VAL(periph)                 (REG32(RCU + ((uint32_t)(periph) >> 6)))
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

const uint32_t	RCU_GPIOC = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 4U);

void __rcu_periph_clock_enable(uint32_t periph)
{
    RCU_REG_VAL(periph) |= BIT(RCU_BIT_POS(periph));
}

/* GPIO mode values set */
#define GPIO_MODE_SET(n, mode)           ((uint32_t)((uint32_t)(mode) << (4U * (n))))
#define GPIO_MODE_MASK(n)                (0xFU << (4U * (n)))



void __gpio_init(uint32_t gpio_periph, uint32_t mode, uint32_t speed,
        uint32_t pin)
{
    uint16_t i;
    uint32_t temp_mode = 0U;
    uint32_t reg = 0U;

    /* GPIO mode configuration */
    temp_mode = (uint32_t) (mode & ((uint32_t) 0x0FU));

    /* GPIO speed configuration */
    if (((uint32_t) 0x00U) != ((uint32_t) mode & ((uint32_t) 0x10U))) {
        /* output mode max speed:10MHz,2MHz,50MHz */
        temp_mode |= (uint32_t) speed;
    }

    /* configure the eight low port pins with GPIO_CTL0 */
    for (i = 0U; i < 8U; i++) {
        if ((1U << i) & pin) {
            reg = GPIO_CTL0(gpio_periph);

            /* clear the specified pin mode bits */
            reg &= ~GPIO_MODE_MASK(i);
            /* set the specified pin mode bits */
            reg |= GPIO_MODE_SET(i, temp_mode);

            /* set IPD or IPU */
            if (GPIO_MODE_IPD == mode) {
                /* reset the corresponding OCTL bit */
                GPIO_BC(gpio_periph) = (uint32_t) ((1U << i) & pin);
            } else {
                /* set the corresponding OCTL bit */
                if (GPIO_MODE_IPU == mode) {
                    GPIO_BOP(gpio_periph) = (uint32_t) ((1U << i) & pin);
                }
            }
            /* set GPIO_CTL0 register */
            GPIO_CTL0(gpio_periph) = reg;
        }
    }
    /* configure the eight high port pins with GPIO_CTL1 */
    for (i = 8U; i < 16U; i++) {
        if ((1U << i) & pin) {
            reg = GPIO_CTL1(gpio_periph);

            /* clear the specified pin mode bits */
            reg &= ~GPIO_MODE_MASK(i - 8U);
            /* set the specified pin mode bits */
            reg |= GPIO_MODE_SET(i - 8U, temp_mode);

            /* set IPD or IPU */
            if (GPIO_MODE_IPD == mode) {
                /* reset the corresponding OCTL bit */
                GPIO_BC(gpio_periph) = (uint32_t) ((1U << i) & pin);
            } else {
                /* set the corresponding OCTL bit */
                if (GPIO_MODE_IPU == mode) {
                    GPIO_BOP(gpio_periph) = (uint32_t) ((1U << i) & pin);
                }
            }
            /* set GPIO_CTL1 register */
            GPIO_CTL1(gpio_periph) = reg;
        }
    }
}

void longan_led_init()
{
    /* enable the led clock */
    __rcu_periph_clock_enable(LED_GPIO_CLK);
    /* configure led GPIO port */ 
    __gpio_init(LED_GPIO_PORT, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, LED_PIN);

    GPIO_BC(LED_GPIO_PORT) = LED_PIN;
}
#if 0
void init()
{
	//Enable the interrupts. From now on interrupt handlers can be executed
	//eclic_global_interrupt_enable();	
    csr_set(mstatus, MSTATUS_MIE);
    __eclic_set_nlbits(ECLIC_PRIGROUP_LEVEL4_PRIO0);

	exti_source_select(GPIO_PORT_SOURCE_GPIOC, GPIO_PIN_SOURCE_13);
	//exti_init(EXTI_13, EXTI_INTERRUPT, EXTI_TRIG_BOTH);

    /* reset the EXTI line x */
    EXTI->EVEN  &= ~BIT(13);
    EXTI->INTEN &= ~BIT(13);
    EXTI->INTEN |= BIT(13);
    
    EXTI->RTEN |= BIT(13);
    EXTI->FTEN |= BIT(13);

    EXTI->PD = BIT(13);


    __eclic_enable_interrupt(EXTI10_15_IRQn);
    __eclic_set_irq_lvl_abs(EXTI10_15_IRQn, 1);
    __eclic_set_irq_priority(EXTI10_15_IRQn, 1);

	return;
}
#endif
void longan_led_on()
{
    /*
     * LED is hardwired with 3.3V on the anode, we control the cathode
     * (negative side) so we need to use reversed logic: bit clear is on.
     */
    GPIO_BC(LED_GPIO_PORT) = LED_PIN;
}

void longan_led_off()
{
    GPIO_BOP(LED_GPIO_PORT) = LED_PIN;
}

void main(void)
{
	const struct device *dev;
	bool led_is_on = true;
	int ret;

	longan_led_init();
	//dev = device_get_binding(LED0);
	//if (dev == NULL) {
	//	return;
	//}

	//ret = gpio_pin_configure(dev, PIN, GPIO_OUTPUT_ACTIVE | FLAGS);
	//if (ret < 0) {
	//	return;
	//}

	int count = 0;
	while (1) {
		//gpio_pin_set(dev, PIN, (int)led_is_on);
		if(led_is_on) longan_led_on();
		else longan_led_off();
		led_is_on = !led_is_on;
		k_msleep(SLEEP_TIME_MS);
		printk("%d\n", count++);
	}
}
