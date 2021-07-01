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
//#include <init.h>
//#include <toolchain.h>
#include <soc.h>
#include <arch/riscv/csr.h>
//#define BIT(x)                       ((uint32_t)((uint32_t)0x01U<<(x)))
//#define GPIO_BASE             (APB2_BUS_BASE + 0x00000800U)  /*!< GPIO base address                */
//#define RCU_BASE              (AHB1_BUS_BASE + 0x00009000U)  /*!< RCU base address                 */
//#define AFIO_BASE             (APB2_BUS_BASE + 0x00000000U)  /*!< AFIO base address                */
//#define EXTI_BASE             (APB2_BUS_BASE + 0x00000400U)  /*!< EXTI base address                */
//#define csr_set set_csr
//extern void delay_1ms(uint32_t);
//static void k_msleep(uint32_t x) { delay_1ms(x); }
//#define __SYSTEM_CLOCK_108M_PLL_HXTAL           (uint32_t)(108000000)
/* define interrupt number */
//typedef enum IRQn
//{
//    EXTI10_15_IRQn               = 59,     /*!< EXTI[15:10] interrupts                                   */
//} IRQn_Type;

/* peripheral memory map */
#define APB1_BUS_BASE         ((uint32_t)0x40000000U)        /*!< apb1 base address                */
#define APB2_BUS_BASE         ((uint32_t)0x40010000U)        /*!< apb2 base address                */
#define AHB1_BUS_BASE         ((uint32_t)0x40018000U)        /*!< ahb1 base address                */
#define AHB3_BUS_BASE         ((uint32_t)0x60000000U)        /*!< ahb3 base address                */



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

/* GPIO mode values set */
#define GPIO_MODE_SET(n, mode)           ((uint32_t)((uint32_t)(mode) << (4U * (n))))
#define GPIO_MODE_MASK(n)                (0xFU << (4U * (n)))

const uint32_t	RCU_GPIOC = RCU_REGIDX_BIT(APB2EN_REG_OFFSET, 4U);

void __rcu_periph_clock_enable(uint32_t periph)
{
    RCU_REG_VAL(periph) |= BIT(RCU_BIT_POS(periph));
}

struct gd32_afio {
	volatile uint32_t EC;
	volatile uint32_t PCF0;
	volatile uint32_t EXTISS0;
	volatile uint32_t EXTISS1;
	volatile uint32_t EXTISS2;
	volatile uint32_t EXTISS3;
	volatile uint32_t PCF1;
};


struct gd32_exti {
	volatile uint32_t INTEN;
	volatile uint32_t EVEN;
	volatile uint32_t RTEN;
	volatile uint32_t FTEN;
	volatile uint32_t SWIEV;
	volatile uint32_t PD;
};


#define AFIO ((volatile struct gd32_afio *)(AFIO_BASE))
#define AFIO_EXTI_PORT_MASK     ((uint8_t)0x70)
#define AFIO_EXTI_SOURCE_FIELDS ((uint8_t)0x04U) /*!< select AFIO exti source registers */
#define AFIO_EXTI_SOURCE_MASK   ((uint8_t)0x03U)



#define GPIO_PIN_SOURCE(x) ((uint32_t)x)


#define AFIO ((volatile struct gd32_afio *)(AFIO_BASE))


#define AFIO_EXTI_PORT_MASK     ((uint8_t)0x70)
#define AFIO_EXTI_SOURCE_FIELDS ((uint8_t)0x04U) /*!< select AFIO exti source registers */
#define AFIO_EXTI_SOURCE_MASK   ((uint8_t)0x03U)

#define EXTI ((volatile struct gd32_exti *)(EXTI_BASE))

#define ECLIC_ADDR_BASE           0xd2000000
//  0x1000+4*i   1B/input    RW        eclicintip[i]
#define ECLIC_INT_IP_OFFSET            0x1000UL
//  0x1001+4*i   1B/input    RW        eclicintie[i]
#define ECLIC_INT_IE_OFFSET            0x1001UL
//  0x1002+4*i   1B/input    RW        eclicintattr[i]
#define ECLIC_INT_ATTR_OFFSET          0x1002UL
#define ECLICINTCTLBITS  4
#define ECLIC_CFG_NLBITS_LSB     (1u)
#define ECLIC_CFG_NLBITS_MASK          0x1EUL
#define ECLIC_CFG_OFFSET            0x0

//  0x1003+4*i   1B/input    RW        eclicintctl[i]
#define ECLIC_INT_CTRL_OFFSET          0x1003UL

void __eclic_enable_interrupt (uint32_t source) {
    *(volatile uint8_t*)(ECLIC_ADDR_BASE+ECLIC_INT_IE_OFFSET+source*4) = 1;
}

void __eclic_set_intctrl (uint32_t source, uint8_t intctrl){
  *(volatile uint8_t*)(ECLIC_ADDR_BASE+ECLIC_INT_CTRL_OFFSET+source*4) = intctrl;
}

uint8_t __eclic_get_intctrl  (uint32_t source){
  return *(volatile uint8_t*)(ECLIC_ADDR_BASE+ECLIC_INT_CTRL_OFFSET+source*4);
}

void __eclic_set_cliccfg (uint8_t cliccfg){
  *(volatile uint8_t*)(ECLIC_ADDR_BASE+ECLIC_CFG_OFFSET) = cliccfg;
}

uint8_t __eclic_get_cliccfg  (){
  return *(volatile uint8_t*)(ECLIC_ADDR_BASE+ECLIC_CFG_OFFSET);
}
//sets nlbits 
void __eclic_set_nlbits(uint8_t nlbits) {
  //shift nlbits to correct position
  uint8_t nlbits_shifted = nlbits << ECLIC_CFG_NLBITS_LSB;

  //read the current cliccfg 
  uint8_t old_cliccfg = __eclic_get_cliccfg();
  uint8_t new_cliccfg = (old_cliccfg & (~ECLIC_CFG_NLBITS_MASK)) | (ECLIC_CFG_NLBITS_MASK & nlbits_shifted); 

  __eclic_set_cliccfg(new_cliccfg);
}

//get nlbits 
uint8_t __eclic_get_nlbits(void) {
  //extract nlbits
  uint8_t nlbits = __eclic_get_cliccfg();
  nlbits = (nlbits & ECLIC_CFG_NLBITS_MASK) >> ECLIC_CFG_NLBITS_LSB;
  return nlbits;
}

//sets an interrupt priority based encoding of nlbits and ECLICINTCTLBITS
uint8_t __eclic_set_irq_priority(uint32_t source, uint8_t priority) {
	//extract nlbits
	uint8_t nlbits = __eclic_get_nlbits();
	if (nlbits >= ECLICINTCTLBITS) {
		nlbits = ECLICINTCTLBITS;
		return 0;
	}

	//shift priority into correct bit position
	priority = priority << (8 - ECLICINTCTLBITS);

	//write to eclicintctrl
	uint8_t current_intctrl = __eclic_get_intctrl(source);
	//shift intctrl right to mask off unused bits
	current_intctrl = current_intctrl >> (8-nlbits);
	//shift intctrl into correct bit position
	current_intctrl = current_intctrl << (8-nlbits);

	__eclic_set_intctrl(source, (current_intctrl | priority));

	return priority;
}

void __eclic_set_irq_lvl_abs(uint32_t source, uint8_t lvl_abs) {
  //extract nlbits
  uint8_t nlbits = __eclic_get_nlbits();
  if (nlbits > ECLICINTCTLBITS) {
    nlbits = ECLICINTCTLBITS;
  }

  //shift lvl_abs into correct bit position
  uint8_t lvl = lvl_abs << (8-nlbits);
 
  //write to clicintctrl
  uint8_t current_intctrl = __eclic_get_intctrl(source);
  //shift intctrl left to mask off unused bits
  current_intctrl = current_intctrl << nlbits;
  //shift intctrl into correct bit position
  current_intctrl = current_intctrl >> nlbits;

  __eclic_set_intctrl(source, (current_intctrl | lvl));
}

void exti_source_select(uint8_t output_port, uint8_t output_pin)
{
    uint32_t source = 0U;
    source = ((uint32_t) 0x0FU)
            << (AFIO_EXTI_SOURCE_FIELDS * (output_pin & AFIO_EXTI_SOURCE_MASK));

    /* select EXTI sources */
    if (GPIO_PIN_SOURCE(4) > output_pin) {
        /* select EXTI0/EXTI1/EXTI2/EXTI3 */
        AFIO->EXTISS0 &= ~source;
        AFIO->EXTISS0 |= (((uint32_t) output_port)
                << (AFIO_EXTI_SOURCE_FIELDS
                        * (output_pin & AFIO_EXTI_SOURCE_MASK)));
    } else if (GPIO_PIN_SOURCE(8) > output_pin) {
        /* select EXTI4/EXTI5/EXTI6/EXTI7 */
        AFIO->EXTISS1 &= ~source;
        AFIO->EXTISS1 |= (((uint32_t) output_port)
                << (AFIO_EXTI_SOURCE_FIELDS
                        * (output_pin & AFIO_EXTI_SOURCE_MASK)));
    } else if (GPIO_PIN_SOURCE(12) > output_pin) {
        /* select EXTI8/EXTI9/EXTI10/EXTI11 */
        AFIO->EXTISS2 &= ~source;
        AFIO->EXTISS2 |= (((uint32_t) output_port)
                << (AFIO_EXTI_SOURCE_FIELDS
                        * (output_pin & AFIO_EXTI_SOURCE_MASK)));
    } else {
        /* select EXTI12/EXTI13/EXTI14/EXTI15 */
        AFIO->EXTISS3 &= ~source;
        AFIO->EXTISS3 |= (((uint32_t) output_port)
                << (AFIO_EXTI_SOURCE_FIELDS
                        * (output_pin & AFIO_EXTI_SOURCE_MASK)));
    }
}

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
	init();
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
