/*
 * Copyright (c) 2024 Nuvoton Technology Corporation.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_NUMAKER_M2L31X_CLOCK_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_NUMAKER_M2L31X_CLOCK_H_

#define NUMAKER_CLK_CLKSEL0_HCLKSEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL0_HCLKSEL_LXT 0x00000001
#define NUMAKER_CLK_CLKSEL0_HCLKSEL_PLL 0x00000002
#define NUMAKER_CLK_CLKSEL0_HCLKSEL_LIRC 0x00000003
#define NUMAKER_CLK_CLKSEL0_HCLKSEL_MIRC 0x00000005
#define NUMAKER_CLK_CLKSEL0_HCLKSEL_HIRC48M 0x00000006
#define NUMAKER_CLK_CLKSEL0_HCLKSEL_HIRC 0x00000007
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_LXT 0x00000001
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_PLL 0x00000002
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_LIRC 0x00000003
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_MIRC 0x00000005
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_HIRC48M 0x00000006
#define NUMAKER_CLK_CLKSEL0_HCLK0SEL_HIRC 0x00000007
#define NUMAKER_CLK_CLKSEL0_STCLKSEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL0_STCLKSEL_LXT 0x00000008
#define NUMAKER_CLK_CLKSEL0_STCLKSEL_HXT_DIV2 0x00000010
#define NUMAKER_CLK_CLKSEL0_STCLKSEL_HCLK_DIV2 0x00000018
#define NUMAKER_CLK_CLKSEL0_STCLKSEL_HIRC_DIV2 0x00000038
#define NUMAKER_CLK_CLKSEL0_HCLK1SEL_HIRC 0x00000000
#define NUMAKER_CLK_CLKSEL0_HCLK1SEL_MIRC 0x00001000
#define NUMAKER_CLK_CLKSEL0_HCLK1SEL_LXT 0x00002000
#define NUMAKER_CLK_CLKSEL0_HCLK1SEL_LIRC 0x00003000
#define NUMAKER_CLK_CLKSEL0_HCLK1SEL_HIRC48M_DIV2 0x00004000
#define NUMAKER_CLK_CLKSEL0_USBSEL_HIRC48M 0x00000000
#define NUMAKER_CLK_CLKSEL0_USBSEL_PLL 0x00000100
#define NUMAKER_CLK_CLKSEL0_EADC0SEL_PLL 0x00000400
#define NUMAKER_CLK_CLKSEL0_EADC0SEL_HCLK 0x00000800
#define NUMAKER_CLK_CLKSEL0_EADC0SEL_HCLK0 0x00000800
#define NUMAKER_CLK_CLKSEL0_EADC0SEL_HIRC 0x00000C00
#define NUMAKER_CLK_CLKSEL0_CANFD0SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL0_CANFD0SEL_HIRC48M 0x01000000
#define NUMAKER_CLK_CLKSEL0_CANFD0SEL_HCLK 0x02000000
#define NUMAKER_CLK_CLKSEL0_CANFD0SEL_HCLK0 0x02000000
#define NUMAKER_CLK_CLKSEL0_CANFD0SEL_HIRC 0x03000000
#define NUMAKER_CLK_CLKSEL0_CANFD1SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL0_CANFD1SEL_HIRC48M 0x04000000
#define NUMAKER_CLK_CLKSEL0_CANFD1SEL_HCLK 0x08000000
#define NUMAKER_CLK_CLKSEL0_CANFD1SEL_HCLK0 0x08000000
#define NUMAKER_CLK_CLKSEL0_CANFD1SEL_HIRC 0x0C000000
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_LXT 0x00000010
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_HCLK 0x00000020
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_HCLK0 0x00000020
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_HIRC 0x00000030
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_LIRC 0x00000040
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_HIRC48M 0x00000050
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_PLL 0x00000060
#define NUMAKER_CLK_CLKSEL1_CLKOSEL_MIRC 0x00000070
#define NUMAKER_CLK_CLKSEL1_TMR0SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL1_TMR0SEL_LXT 0x00000100
#define NUMAKER_CLK_CLKSEL1_TMR0SEL_PCLK0 0x00000200
#define NUMAKER_CLK_CLKSEL1_TMR0SEL_EXT 0x00000300
#define NUMAKER_CLK_CLKSEL1_TMR0SEL_LIRC 0x00000500
#define NUMAKER_CLK_CLKSEL1_TMR0SEL_HIRC 0x00000700
#define NUMAKER_CLK_CLKSEL1_TMR1SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL1_TMR1SEL_LXT 0x00001000
#define NUMAKER_CLK_CLKSEL1_TMR1SEL_PCLK0 0x00002000
#define NUMAKER_CLK_CLKSEL1_TMR1SEL_EXT 0x00003000
#define NUMAKER_CLK_CLKSEL1_TMR1SEL_LIRC 0x00005000
#define NUMAKER_CLK_CLKSEL1_TMR1SEL_HIRC 0x00007000
#define NUMAKER_CLK_CLKSEL1_TMR2SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL1_TMR2SEL_LXT 0x00010000
#define NUMAKER_CLK_CLKSEL1_TMR2SEL_PCLK1 0x00020000
#define NUMAKER_CLK_CLKSEL1_TMR2SEL_EXT 0x00030000
#define NUMAKER_CLK_CLKSEL1_TMR2SEL_LIRC 0x00050000
#define NUMAKER_CLK_CLKSEL1_TMR2SEL_HIRC 0x00070000
#define NUMAKER_CLK_CLKSEL1_TMR3SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL1_TMR3SEL_LXT 0x00100000
#define NUMAKER_CLK_CLKSEL1_TMR3SEL_PCLK1 0x00200000
#define NUMAKER_CLK_CLKSEL1_TMR3SEL_EXT 0x00300000
#define NUMAKER_CLK_CLKSEL1_TMR3SEL_LIRC 0x00500000
#define NUMAKER_CLK_CLKSEL1_TMR3SEL_HIRC 0x00700000
#define NUMAKER_CLK_CLKSEL1_WWDTSEL_HCLK_DIV2048 0x80000000
#define NUMAKER_CLK_CLKSEL1_WWDTSEL_HCLK0_DIV2048 0x80000000
#define NUMAKER_CLK_CLKSEL1_WWDTSEL_LIRC 0xC0000000
#define NUMAKER_CLK_CLKSEL2_EPWM0SEL_HCLK 0x00000000
#define NUMAKER_CLK_CLKSEL2_EPWM0SEL_HCLK0 0x00000000
#define NUMAKER_CLK_CLKSEL2_EPWM0SEL_PCLK0 0x00000001
#define NUMAKER_CLK_CLKSEL2_EPWM1SEL_HCLK 0x00000000
#define NUMAKER_CLK_CLKSEL2_EPWM1SEL_HCLK0 0x00000000
#define NUMAKER_CLK_CLKSEL2_EPWM1SEL_PCLK1 0x00000002
#define NUMAKER_CLK_CLKSEL2_QSPI0SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL2_QSPI0SEL_PLL 0x00000004
#define NUMAKER_CLK_CLKSEL2_QSPI0SEL_PCLK0 0x00000008
#define NUMAKER_CLK_CLKSEL2_QSPI0SEL_HIRC 0x0000000C
#define NUMAKER_CLK_CLKSEL2_SPI0SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL2_SPI0SEL_PLL 0x00000010
#define NUMAKER_CLK_CLKSEL2_SPI0SEL_PCLK1 0x00000020
#define NUMAKER_CLK_CLKSEL2_SPI0SEL_HIRC 0x00000030
#define NUMAKER_CLK_CLKSEL2_SPI0SEL_HIRC48M 0x00000040
#define NUMAKER_CLK_CLKSEL2_TKSEL_HIRC 0x00000000
#define NUMAKER_CLK_CLKSEL2_TKSEL_MIRC 0x00000080
#define NUMAKER_CLK_CLKSEL2_SPI1SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL2_SPI1SEL_PLL 0x00001000
#define NUMAKER_CLK_CLKSEL2_SPI1SEL_PCLK0 0x00002000
#define NUMAKER_CLK_CLKSEL2_SPI1SEL_HIRC 0x00003000
#define NUMAKER_CLK_CLKSEL2_SPI1SEL_HIRC48M 0x00004000
#define NUMAKER_CLK_CLKSEL3_PWM0SEL_HCLK 0x00000000
#define NUMAKER_CLK_CLKSEL3_PWM0SEL_HCLK0 0x00000000
#define NUMAKER_CLK_CLKSEL3_PWM0SEL_PCLK0 0x00000040
#define NUMAKER_CLK_CLKSEL3_PWM1SEL_HCLK 0x00000000
#define NUMAKER_CLK_CLKSEL3_PWM1SEL_HCLK0 0x00000000
#define NUMAKER_CLK_CLKSEL3_PWM1SEL_PCLK1 0x00000080
#define NUMAKER_CLK_CLKSEL3_SPI2SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL3_SPI2SEL_PLL 0x00000100
#define NUMAKER_CLK_CLKSEL3_SPI2SEL_PCLK1 0x00000200
#define NUMAKER_CLK_CLKSEL3_SPI2SEL_HIRC 0x00000300
#define NUMAKER_CLK_CLKSEL3_SPI2SEL_HIRC48M 0x00000400
#define NUMAKER_CLK_CLKSEL3_SPI3SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL3_SPI3SEL_PLL 0x00001000
#define NUMAKER_CLK_CLKSEL3_SPI3SEL_PCLK0 0x00002000
#define NUMAKER_CLK_CLKSEL3_SPI3SEL_HIRC 0x00003000
#define NUMAKER_CLK_CLKSEL3_SPI3SEL_HIRC48M 0x00004000
#define NUMAKER_CLK_CLKSEL4_UART0SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART0SEL_PLL 0x00000001
#define NUMAKER_CLK_CLKSEL4_UART0SEL_LXT 0x00000002
#define NUMAKER_CLK_CLKSEL4_UART0SEL_HIRC 0x00000003
#define NUMAKER_CLK_CLKSEL4_UART0SEL_MIRC 0x00000004
#define NUMAKER_CLK_CLKSEL4_UART0SEL_HIRC48M 0x00000005
#define NUMAKER_CLK_CLKSEL4_UART1SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART1SEL_PLL 0x00000010
#define NUMAKER_CLK_CLKSEL4_UART1SEL_LXT 0x00000020
#define NUMAKER_CLK_CLKSEL4_UART1SEL_HIRC 0x00000030
#define NUMAKER_CLK_CLKSEL4_UART1SEL_MIRC 0x00000040
#define NUMAKER_CLK_CLKSEL4_UART1SEL_HIRC48M 0x00000050
#define NUMAKER_CLK_CLKSEL4_UART2SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART2SEL_PLL 0x00000100
#define NUMAKER_CLK_CLKSEL4_UART2SEL_LXT 0x00000200
#define NUMAKER_CLK_CLKSEL4_UART2SEL_HIRC 0x00000300
#define NUMAKER_CLK_CLKSEL4_UART2SEL_MIRC 0x00000400
#define NUMAKER_CLK_CLKSEL4_UART2SEL_HIRC48M 0x00000500
#define NUMAKER_CLK_CLKSEL4_UART3SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART3SEL_PLL 0x00001000
#define NUMAKER_CLK_CLKSEL4_UART3SEL_LXT 0x00002000
#define NUMAKER_CLK_CLKSEL4_UART3SEL_HIRC 0x00003000
#define NUMAKER_CLK_CLKSEL4_UART3SEL_MIRC 0x00004000
#define NUMAKER_CLK_CLKSEL4_UART3SEL_HIRC48M 0x00005000
#define NUMAKER_CLK_CLKSEL4_UART4SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART4SEL_PLL 0x00010000
#define NUMAKER_CLK_CLKSEL4_UART4SEL_LXT 0x00020000
#define NUMAKER_CLK_CLKSEL4_UART4SEL_HIRC 0x00030000
#define NUMAKER_CLK_CLKSEL4_UART4SEL_MIRC 0x00040000
#define NUMAKER_CLK_CLKSEL4_UART4SEL_HIRC48M 0x00050000
#define NUMAKER_CLK_CLKSEL4_UART5SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART5SEL_PLL 0x00100000
#define NUMAKER_CLK_CLKSEL4_UART5SEL_LXT 0x00200000
#define NUMAKER_CLK_CLKSEL4_UART5SEL_HIRC 0x00300000
#define NUMAKER_CLK_CLKSEL4_UART5SEL_MIRC 0x00400000
#define NUMAKER_CLK_CLKSEL4_UART5SEL_HIRC48M 0x00500000
#define NUMAKER_CLK_CLKSEL4_UART6SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART6SEL_PLL 0x01000000
#define NUMAKER_CLK_CLKSEL4_UART6SEL_LXT 0x02000000
#define NUMAKER_CLK_CLKSEL4_UART6SEL_HIRC 0x03000000
#define NUMAKER_CLK_CLKSEL4_UART6SEL_MIRC 0x04000000
#define NUMAKER_CLK_CLKSEL4_UART6SEL_HIRC48M 0x05000000
#define NUMAKER_CLK_CLKSEL4_UART7SEL_HXT 0x00000000
#define NUMAKER_CLK_CLKSEL4_UART7SEL_PLL 0x10000000
#define NUMAKER_CLK_CLKSEL4_UART7SEL_LXT 0x20000000
#define NUMAKER_CLK_CLKSEL4_UART7SEL_HIRC 0x30000000
#define NUMAKER_CLK_CLKSEL4_UART7SEL_MIRC 0x40000000
#define NUMAKER_CLK_CLKSEL4_UART7SEL_HIRC48M 0x50000000
#define NUMAKER_CLK_CLKDIV0_HCLK(x) (((x) - 1UL) << (0))
#define NUMAKER_CLK_CLKDIV0_HCLK0(x) (((x) - 1UL) << (0))
#define NUMAKER_CLK_CLKDIV0_USB(x) (((x) - 1UL) << (4))
#define NUMAKER_CLK_CLKDIV0_UART0(x) (((x) - 1UL) << (8))
#define NUMAKER_CLK_CLKDIV0_UART1(x) (((x) - 1UL) << (12))
#define NUMAKER_CLK_CLKDIV0_EADC0(x) (((x) - 1UL) << (16))
#define NUMAKER_CLK_CLKDIV4_UART2(x) (((x) - 1UL) << (0))
#define NUMAKER_CLK_CLKDIV4_UART3(x) (((x) - 1UL) << (4))
#define NUMAKER_CLK_CLKDIV4_UART4(x) (((x) - 1UL) << (8))
#define NUMAKER_CLK_CLKDIV4_UART5(x) (((x) - 1UL) << (12))
#define NUMAKER_CLK_CLKDIV4_UART6(x) (((x) - 1UL) << (16))
#define NUMAKER_CLK_CLKDIV4_UART7(x) (((x) - 1UL) << (20))
#define NUMAKER_CLK_CLKDIV5_CANFD0(x) (((x) - 1UL) << (0))
#define NUMAKER_CLK_CLKDIV5_CANFD1(x) (((x) - 1UL) << (4))
#define NUMAKER_CLK_PCLKDIV_APB0DIV_DIV1 0x00000000
#define NUMAKER_CLK_PCLKDIV_APB0DIV_DIV2 0x00000001
#define NUMAKER_CLK_PCLKDIV_APB0DIV_DIV4 0x00000002
#define NUMAKER_CLK_PCLKDIV_APB0DIV_DIV8 0x00000003
#define NUMAKER_CLK_PCLKDIV_APB0DIV_DIV16 0x00000004
#define NUMAKER_CLK_PCLKDIV_APB1DIV_DIV1 0x00000000
#define NUMAKER_CLK_PCLKDIV_APB1DIV_DIV2 0x00000010
#define NUMAKER_CLK_PCLKDIV_APB1DIV_DIV4 0x00000020
#define NUMAKER_CLK_PCLKDIV_APB1DIV_DIV8 0x00000030
#define NUMAKER_CLK_PCLKDIV_APB1DIV_DIV16 0x00000040
#define NUMAKER_PDMA0_MODULE 0x00000001
#define NUMAKER_ISP_MODULE 0x00000002
#define NUMAKER_EBI_MODULE 0x00000003
#define NUMAKER_ST_MODULE 0x018C0004
#define NUMAKER_CRC_MODULE 0x00000007
#define NUMAKER_CRPT_MODULE 0x0000000C
#define NUMAKER_KS_MODULE 0x0000000D
#define NUMAKER_USBH_MODULE 0x00A01090
#define NUMAKER_GPA_MODULE 0x00000018
#define NUMAKER_GPB_MODULE 0x00000019
#define NUMAKER_GPC_MODULE 0x0000001A
#define NUMAKER_GPD_MODULE 0x0000001B
#define NUMAKER_GPE_MODULE 0x0000001C
#define NUMAKER_GPF_MODULE 0x0000001D
#define NUMAKER_GPG_MODULE 0x0000001E
#define NUMAKER_GPH_MODULE 0x0000001F
#define NUMAKER_RTC_MODULE 0x20000001
#define NUMAKER_TMR0_MODULE 0x25A00002
#define NUMAKER_TMR1_MODULE 0x25B00003
#define NUMAKER_TMR2_MODULE 0x25C00004
#define NUMAKER_TMR3_MODULE 0x25D00005
#define NUMAKER_CLKO_MODULE 0x26100006
#define NUMAKER_ACMP01_MODULE 0x20000007
#define NUMAKER_I2C0_MODULE 0x20000008
#define NUMAKER_I2C1_MODULE 0x20000009
#define NUMAKER_I2C2_MODULE 0x2000000A
#define NUMAKER_I2C3_MODULE 0x2000000B
#define NUMAKER_QSPI0_MODULE 0x2908000C
#define NUMAKER_SPI0_MODULE 0x2990000D
#define NUMAKER_SPI1_MODULE 0x29B0000E
#define NUMAKER_SPI2_MODULE 0x2DA0000F
#define NUMAKER_UART0_MODULE 0x31801110
#define NUMAKER_UART1_MODULE 0x31901191
#define NUMAKER_UART2_MODULE 0x31A11012
#define NUMAKER_UART3_MODULE 0x31B11093
#define NUMAKER_UART4_MODULE 0x31C11114
#define NUMAKER_UART5_MODULE 0x31D11195
#define NUMAKER_UART6_MODULE 0x31E11216
#define NUMAKER_UART7_MODULE 0x31F11297
#define NUMAKER_OTG_MODULE 0x2000001A
#define NUMAKER_USBD_MODULE 0x20A0109B
#define NUMAKER_EADC0_MODULE 0x2128221C
#define NUMAKER_TRNG_MODULE 0x2000001F
#define NUMAKER_SPI3_MODULE 0x4DB00006
#define NUMAKER_USCI0_MODULE 0x40000008
#define NUMAKER_USCI1_MODULE 0x40000009
#define NUMAKER_WWDT_MODULE 0x4578000B
#define NUMAKER_DAC_MODULE 0x4000000C
#define NUMAKER_EPWM0_MODULE 0x48800010
#define NUMAKER_EPWM1_MODULE 0x48840011
#define NUMAKER_EQEI0_MODULE 0x40000016
#define NUMAKER_EQEI1_MODULE 0x40000017
#define NUMAKER_TK_MODULE 0x489C0019
#define NUMAKER_ECAP0_MODULE 0x4000001A
#define NUMAKER_ECAP1_MODULE 0x4000001B
#define NUMAKER_ACMP2_MODULE 0x60000007
#define NUMAKER_PWM0_MODULE 0x6C980008
#define NUMAKER_PWM1_MODULE 0x6C9C0009
#define NUMAKER_UTCPD0_MODULE 0x6000000F
#define NUMAKER_CANRAM0_MODULE 0x80000010
#define NUMAKER_CANRAM1_MODULE 0x80000011
#define NUMAKER_CANFD0_MODULE 0x81621014
#define NUMAKER_CANFD1_MODULE 0x816A1095
#define NUMAKER_HCLK1_MODULE 0x81B3101C
#define NUMAKER_LPPDMA0_MODULE 0xA0000000
#define NUMAKER_LPGPIO_MODULE 0xA0000001
#define NUMAKER_LPSRAM_MODULE 0xA0000002
#define NUMAKER_WDT_MODULE 0xB5600010
#define NUMAKER_LPSPI0_MODULE 0xB5080011
#define NUMAKER_LPI2C0_MODULE 0xA0000012
#define NUMAKER_LPUART0_MODULE 0xB5031113
#define NUMAKER_LPTMR0_MODULE 0xB5A00014
#define NUMAKER_LPTMR1_MODULE 0xB5B00015
#define NUMAKER_TTMR0_MODULE 0xB5100016
#define NUMAKER_TTMR1_MODULE 0xB5180017
#define NUMAKER_LPADC0_MODULE 0xB5431218
#define NUMAKER_OPA_MODULE 0xA000001B

#define NUMAKER_CLK_PMUCTL_PDMSEL_PD 0x00000000
#define NUMAKER_CLK_PMUCTL_PDMSEL_NPD0 0x00000000
#define NUMAKER_CLK_PMUCTL_PDMSEL_NPD1 0x00000001
#define NUMAKER_CLK_PMUCTL_PDMSEL_NPD2 0x00000002
#define NUMAKER_CLK_PMUCTL_PDMSEL_NPD3 0x00000003
#define NUMAKER_CLK_PMUCTL_PDMSEL_NPD4 0x00000004
#define NUMAKER_CLK_PMUCTL_PDMSEL_NPD5 0x00000005
#define NUMAKER_CLK_PMUCTL_PDMSEL_SPD0 0x00000008
#define NUMAKER_CLK_PMUCTL_PDMSEL_SPD1 0x00000009
#define NUMAKER_CLK_PMUCTL_PDMSEL_SPD2 0x0000000A
#define NUMAKER_CLK_PMUCTL_PDMSEL_DPD0 0x0000000C
#define NUMAKER_CLK_PMUCTL_PDMSEL_DPD1 0x0000000D

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_CLOCK_NUMAKER_M2L31X_CLOCK_H_ */
