/*
 * Copyright (c) 2021 Tokita, Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file SoC configuration macros for the GigaDevice GD32V processor
 */

#ifndef __RISCV_GIGADEVICE_GD32V_SOC_H_
#define __RISCV_GIGADEVICE_GD32V_SOC_H_

#include <soc_common.h>
#include <devicetree.h>
#include <arch/riscv/csr.h>

/* Timer configuration */
#define RISCV_MTIME_BASE             (0xD1000000UL + 0)
#define RISCV_MTIMECMP_BASE          (0xD1000000UL + 0x8)

/* lib-c hooks required RAM defined variables */
#define RISCV_RAM_BASE               DT_SRAM_BASE_ADDRESS
#define RISCV_RAM_SIZE               KB(DT_SRAM_SIZE)

/* === CLIC CSR Registers === */
#define CSR_MTVT                0x307
#define CSR_MNXTI               0x345
#define CSR_MINTSTATUS          0x346
#define CSR_MSCRATCHCSW         0x348
#define CSR_MSCRATCHCSWL        0x349
#define CSR_MCLICBASE           0x350

/* === Nuclei custom CSR Registers === */
#define CSR_MCOUNTINHIBIT       0x320
#define CSR_MILM_CTL            0x7C0
#define CSR_MDLM_CTL            0x7C1
#define CSR_MNVEC               0x7C3
#define CSR_MSUBM               0x7C4
#define CSR_MDCAUSE             0x7C9
#define CSR_MCACHE_CTL          0x7CA
#define CSR_MMISC_CTL           0x7D0
#define CSR_MSAVESTATUS         0x7D6
#define CSR_MSAVEEPC1           0x7D7
#define CSR_MSAVECAUSE1         0x7D8
#define CSR_MSAVEEPC2           0x7D9
#define CSR_MSAVECAUSE2         0x7DA
#define CSR_MSAVEDCAUSE1        0x7DB
#define CSR_MSAVEDCAUSE2        0x7DC
#define CSR_PUSHMSUBM           0x7EB
#define CSR_MTVT2               0x7EC
#define CSR_JALMNXTI            0x7ED
#define CSR_PUSHMCAUSE          0x7EE
#define CSR_PUSHMEPC            0x7EF
#define CSR_MPPICFG_INFO        0x7F0
#define CSR_MFIOCFG_INFO        0x7F1
#define CSR_SLEEPVALUE          0x811
#define CSR_TXEVT               0x812
#define CSR_WFE                 0x810
#define CSR_MICFG_INFO          0xFC0
#define CSR_MDCFG_INFO          0xFC1
#define CSR_MCFG_INFO           0xFC2

#ifndef _ASMLANGUAGE
#include <toolchain.h>
#include <gd32vf103.h>
#include <core_feature_timer.h>

#define IS_UART_HWFLOW_INSTANCE(base) (base == USART0 || base == USART1 || base == USART2)
#define IS_UART_LIN_INSTANCE(base) (base == USART0 || base == USART1 || base == USART2)

/**
 * @brief structure to convey pinctrl information for stm32 soc
 * value
 */
struct soc_gpio_pinctrl {
	uint32_t pinmux;
	uint32_t pincfg;
};

#endif  /* !_ASMLANGUAGE */

#endif  /* __RISCV_GIGADEVICE_GD32VF103_SOC_H_ */
