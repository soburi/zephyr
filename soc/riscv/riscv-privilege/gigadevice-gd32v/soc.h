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

#define ECLIC_NON_VECTOR_INTERRUPT             0x0                                      /*!< Non-Vector Interrupt Mode of ECLIC */
#define ECLIC_VECTOR_INTERRUPT                 0x1                                      /*!< Vector Interrupt Mode of ECLIC */

#define ECLIC_CFG_OFFSET            0x0
#define ECLIC_INFO_OFFSET           0x4
#define ECLIC_MTH_OFFSET            0xB

#define ECLIC_INT_IP_OFFSET            0x1000UL
#define ECLIC_INT_IE_OFFSET            0x1001UL
#define ECLIC_INT_ATTR_OFFSET          0x1002UL
#define ECLIC_INT_CTRL_OFFSET          0x1003UL

#define ECLIC_CFG_NLBITS_MASK          0x1EUL
#define ECLIC_CFG_NLBITS_LSB     (1u)

#define ECLIC_MAX_NLBITS                       8U                                       /*!< Max nlbit of the CLICINTCTLBITS */


/**\brief ECLIC Trigger Enum for different Trigger Type */
typedef enum ECLIC_TRIGGER {
    ECLIC_LEVEL_TRIGGER = 0x0,          /*!< Level Triggerred, trig[0] = 0 */
    ECLIC_POSTIVE_EDGE_TRIGGER = 0x1,   /*!< Postive/Rising Edge Triggered, trig[1] = 1, trig[0] = 0 */
    ECLIC_NEGTIVE_EDGE_TRIGGER = 0x3,   /*!< Negtive/Falling Edge Triggered, trig[1] = 1, trig[0] = 0 */
    ECLIC_MAX_TRIGGER = 0x3             /*!< MAX Supported Trigger Mode */
} ECLIC_TRIGGER_Type;

static inline void eclic_set_intattr (uint32_t source, uint8_t intattr){
  *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_ATTR_OFFSET+source*4) = intattr;
}

static inline uint8_t eclic_get_intattr  (uint32_t source){
  return *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_ATTR_OFFSET+source*4);
}

static inline void eclic_set_cliccfg (uint8_t cliccfg){
  *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_CFG_OFFSET) = cliccfg;
}

static inline uint8_t eclic_get_cliccfg (void){
  return *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_CFG_OFFSET);
}

static inline void ECLIC_SetMth(uint8_t mth)
{
  *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_MTH_OFFSET) = mth;
}

static inline void ECLIC_SetCtrlIRQ(IRQn_Type IRQn, uint8_t intctrl)
{
  *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_CTRL_OFFSET+IRQn*4) = intctrl;
}

static inline uint8_t ECLIC_GetCtrlIRQ(IRQn_Type IRQn)
{
  return *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_CTRL_OFFSET+IRQn*4);
}

static inline void ECLIC_EnableIRQ(IRQn_Type IRQn)
{
	*(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_IE_OFFSET+IRQn*4) = 1;
}

static inline uint32_t ECLIC_GetEnableIRQ(IRQn_Type IRQn)
{
	return *(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_IE_OFFSET+IRQn*4);
}

static inline void ECLIC_DisableIRQ(IRQn_Type IRQn)
{
	*(volatile uint8_t*)(__ECLIC_BASEADDR+ECLIC_INT_IE_OFFSET+IRQn*4) = 0;
}

static inline void ECLIC_SetCfgNlbits(uint32_t nlbits)
{
  /* shift nlbits to correct position */
  uint8_t nlbits_shifted = nlbits << ECLIC_CFG_NLBITS_LSB;

  /* read the current cliccfg */
  uint8_t old_cliccfg = eclic_get_cliccfg();
  uint8_t new_cliccfg = (old_cliccfg & (~ECLIC_CFG_NLBITS_MASK)) | (ECLIC_CFG_NLBITS_MASK & nlbits_shifted);

  eclic_set_cliccfg(new_cliccfg);
}

static inline uint32_t ECLIC_GetCfgNlbits()
{
  /* extract nlbits */
  uint8_t nlbits = eclic_get_cliccfg();
  nlbits = (nlbits & ECLIC_CFG_NLBITS_MASK) >> ECLIC_CFG_NLBITS_LSB;
  return nlbits;
}

static inline void ECLIC_SetShvIRQ(IRQn_Type IRQn, uint32_t shv)
{
  /* read the current attr */
  uint8_t old_intattr = eclic_get_intattr(IRQn);
      /*  Keep other bits unchanged and only set the LSB bit */
  uint8_t new_intattr = ((old_intattr & (~0x1)) | (shv &0x1));

  eclic_set_intattr(IRQn, new_intattr);
}

static inline void ECLIC_SetTrigIRQ(IRQn_Type IRQn, uint32_t trig)
{
  /* read the current attr */
  uint8_t old_intattr = eclic_get_intattr(IRQn);
      /* Keep other bits unchanged and only clear the bit 1 */
  uint8_t new_intattr = ((old_intattr & (~0x6)) | ((trig << 1) & 0x6));

  eclic_set_intattr(IRQn, new_intattr);
}

static inline void ECLIC_SetLevelIRQ(IRQn_Type IRQn, uint32_t lvl_abs)
{
    uint8_t nlbits = ECLIC_GetCfgNlbits();
    uint8_t intctlbits = (uint8_t)__ECLIC_INTCTLBITS;

    if (nlbits == 0) {
        return;
    }

    if (nlbits > intctlbits) {
        nlbits = intctlbits;
    }
    uint8_t maxlvl = ((1 << nlbits) - 1);
    if (lvl_abs > maxlvl) {
        lvl_abs = maxlvl;
    }
    uint8_t lvl = lvl_abs << (ECLIC_MAX_NLBITS - nlbits);
    uint8_t cur_ctrl = ECLIC_GetCtrlIRQ(IRQn);
    cur_ctrl = cur_ctrl << nlbits;
    cur_ctrl = cur_ctrl >> nlbits;
    ECLIC_SetCtrlIRQ(IRQn, (cur_ctrl | lvl));
}

static inline void ECLIC_SetPriorityIRQ(IRQn_Type IRQn, uint32_t pri)
{
    uint8_t nlbits = ECLIC_GetCfgNlbits();
    uint8_t intctlbits = (uint8_t)__ECLIC_INTCTLBITS;
    if (nlbits < intctlbits) {
        uint8_t maxpri = ((1 << (intctlbits - nlbits)) - 1);
        if (pri > maxpri) {
            pri = maxpri;
        }
        pri = pri << (ECLIC_MAX_NLBITS - intctlbits);
        uint8_t mask = ((uint8_t)(-1)) >> intctlbits;
        pri = pri | mask;
        uint8_t cur_ctrl = ECLIC_GetCtrlIRQ(IRQn);
        cur_ctrl = cur_ctrl >> (ECLIC_MAX_NLBITS - nlbits);
        cur_ctrl = cur_ctrl << (ECLIC_MAX_NLBITS - nlbits);
        ECLIC_SetCtrlIRQ(IRQn, (cur_ctrl | pri));
    }
}

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
