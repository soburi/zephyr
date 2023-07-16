#include <string.h>
#include "bsp_api.h"
#include "bsp_clocks.h"

#define DT_DRV_COMPAT renesas_ra_clock

#include <zephyr/drivers/clock_control.h>
#include <zephyr/kernel.h>
#include <soc.h>

#if BSP_TZ_NONSECURE_BUILD
#include "bsp_guard.h"
#endif

uint32_t SystemCoreClock;

struct mstp_regs {
#if DT_INST_REG_SIZE_BY_NAME(0, mstp) != 16
	volatile uint32_t MSTPCRA;
#endif
	volatile uint32_t MSTPCRB;
	volatile uint32_t MSTPCRC;
	volatile uint32_t MSTPCRD;
	volatile uint32_t MSTPCRE;
};

static struct {
	struct mstp_regs *MSTP;
} regs = {(struct mstp_regs *)(DT_INST_REG_ADDR_BY_NAME(0, mstp))};

/*******************************************************************************************************************//**
 * Update SystemCoreClock variable based on current clock settings.
 **********************************************************************************************************************/
__attribute__((naked)) void bsp_prv_software_delay_loop (__attribute__(
                                                              (unused)) uint32_t loop_cnt)
{
    __asm volatile (
#if defined(RENESAS_CORTEX_M85) && (defined(__ARMCC_VERSION) || defined(__GNUC__))

        /* Optimize inner loop execution time on CM85 cores (Alignment allows for instruction fusion). */
        ".align 8\n"
#endif
        "sw_delay_loop:         \n"
#if defined(__ICCARM__) || defined(__ARMCC_VERSION)
        "   subs r0, #1         \n"    ///< 1 cycle
#elif defined(__GNUC__)
        "   sub r0, r0, #1      \n"    ///< 1 cycle
#endif

        "   cmp r0, #0          \n"    ///< 1 cycle

/* CM0 and CM23 have a different instruction set */
#if defined(__CORE_CM0PLUS_H_GENERIC) || defined(__CORE_CM23_H_GENERIC)
        "   bne sw_delay_loop   \n"    ///< 2 cycles
#else
        "   bne.n sw_delay_loop \n"    ///< 2 cycles
#endif
        "   bx lr               \n");  ///< 2 cycles
}
#define BSP_DELAY_NS_PER_SECOND    (1000000000)
#define BSP_DELAY_NS_PER_US        (1000)
void R_BSP_SoftwareDelay (uint32_t delay, bsp_delay_units_t units)
{
    uint32_t iclk_hz;
    uint32_t cycles_requested;
    uint32_t ns_per_cycle;
    uint32_t loops_required = 0;
    uint32_t total_us       = (delay * units);                        /** Convert the requested time to microseconds. */
    uint64_t ns_64bits;

    iclk_hz = SystemCoreClock;                                        /** Get the system clock frequency in Hz. */

    /* Running on the Sub-clock (32768 Hz) there are 30517 ns/cycle. This means one cycle takes 31 us. One execution
     * loop of the delay_loop takes 6 cycles which at 32768 Hz is 180 us. That does not include the overhead below prior to even getting
     * to the delay loop. Given this, at this frequency anything less then a delay request of 122 us will not even generate a single
     * pass through the delay loop.  For this reason small delays (<=~200 us) at this slow clock rate will not be possible and such a request
     * will generate a minimum delay of ~200 us.*/
    ns_per_cycle = BSP_DELAY_NS_PER_SECOND / iclk_hz;                 /** Get the # of nanoseconds/cycle. */

    /* We want to get the time in total nanoseconds but need to be conscious of overflowing 32 bits. We also do not want to do 64 bit */
    /* division as that pulls in a division library. */
    ns_64bits = (uint64_t) total_us * (uint64_t) BSP_DELAY_NS_PER_US; // Convert to ns.

    /* Have we overflowed 32 bits? */
    if (ns_64bits <= UINT32_MAX)
    {
        /* No, we will not overflow. */
        cycles_requested = ((uint32_t) ns_64bits / ns_per_cycle);
        loops_required   = cycles_requested / BSP_DELAY_LOOP_CYCLES;
    }
    else
    {
        /* We did overflow. Try dividing down first. */
        total_us  = (total_us / (ns_per_cycle * BSP_DELAY_LOOP_CYCLES));
        ns_64bits = (uint64_t) total_us * (uint64_t) BSP_DELAY_NS_PER_US; // Convert to ns.

        /* Have we overflowed 32 bits? */
        if (ns_64bits <= UINT32_MAX)
        {
            /* No, we will not overflow. */
            loops_required = (uint32_t) ns_64bits;
        }
        else
        {
            /* We still overflowed, use the max count for cycles */
            loops_required = UINT32_MAX;
        }
    }

    /** Only delay if the supplied parameters constitute a delay. */
    if (loops_required > (uint32_t) 0)
    {
        bsp_prv_software_delay_loop(loops_required);
    }
}
/***********************************************************************************************************************
 * Macro definitions
 **********************************************************************************************************************/

/* Key code for writing PRCR register. */
#define BSP_PRV_PRCR_KEY                        (0xA500U)
#define BSP_PRV_PRCR_UNLOCK                     ((BSP_PRV_PRCR_KEY) | 0x3U)
#define BSP_PRV_PRCR_LOCK                       ((BSP_PRV_PRCR_KEY) | 0x0U)

/* Wait state definitions for MEMWAIT. */
#define BSP_PRV_MEMWAIT_ZERO_WAIT_CYCLES        (0U)
#define BSP_PRV_MEMWAIT_TWO_WAIT_CYCLES         (1U)
#define BSP_PRV_MEMWAIT_MAX_ZERO_WAIT_FREQ      (32000000U)

/* Wait state definitions for FLDWAITR. */
#define BSP_PRV_FLDWAITR_ONE_WAIT_CYCLES        (0U)
#define BSP_PRV_FLDWAITR_TWO_WAIT_CYCLES        (1U)
#define BSP_PRV_FLDWAITR_MAX_ONE_WAIT_FREQ      (32000000U)

/* Temporary solution until R_FACI is added to renesas.h. */
#define BSP_PRV_FLDWAITR_REG_ACCESS             (*((volatile uint8_t *) (0x407EFFC4U)))

/* Wait state definitions for MCUS with SRAMWTSC and FLWT. */
#define BSP_PRV_SRAMWTSC_WAIT_CYCLES_DISABLE    (0U)
#define BSP_PRV_ROM_ZERO_WAIT_CYCLES            (0U)
#define BSP_PRV_ROM_ONE_WAIT_CYCLES             (1U)
#define BSP_PRV_ROM_TWO_WAIT_CYCLES             (2U)
#define BSP_PRV_ROM_THREE_WAIT_CYCLES           (3U)
#define BSP_PRV_ROM_FOUR_WAIT_CYCLES            (4U)
#define BSP_PRV_ROM_FIVE_WAIT_CYCLES            (5U)
#define BSP_PRV_SRAM_UNLOCK                     (((BSP_FEATURE_CGC_SRAMPRCR_KW_VALUE) << \
                                                  BSP_FEATURE_CGC_SRAMPRCR_KW_OFFSET) | 0x1U)
#define BSP_PRV_SRAM_LOCK                       (((BSP_FEATURE_CGC_SRAMPRCR_KW_VALUE) << \
                                                  BSP_FEATURE_CGC_SRAMPRCR_KW_OFFSET) | 0x0U)

/* Calculate value to write to MOMCR (MODRV controls main clock drive strength and MOSEL determines the source of the
 * main oscillator). */
#define BSP_PRV_MOMCR_MOSEL_BIT                 (6)
#define BSP_PRV_MODRV                           ((CGC_MAINCLOCK_DRIVE << BSP_FEATURE_CGC_MODRV_SHIFT) & \
                                                 BSP_FEATURE_CGC_MODRV_MASK)
#define BSP_PRV_MOSEL                           (BSP_CLOCK_CFG_MAIN_OSC_CLOCK_SOURCE << BSP_PRV_MOMCR_MOSEL_BIT)
#define BSP_PRV_MOMCR                           (BSP_PRV_MODRV | BSP_PRV_MOSEL)

/* Locations of bitfields used to configure CLKOUT. */
#define BSP_PRV_CKOCR_CKODIV_BIT                (4U)
#define BSP_PRV_CKOCR_CKOEN_BIT                 (7U)

/* Stop interval of at least 5 SOSC clock cycles between stop and restart of SOSC.
 * Calculated based on 8Mhz of MOCO clock. */
#define BSP_PRV_SUBCLOCK_STOP_INTERVAL_US       (1220U)

/* Locations of bitfields used to configure Peripheral Clocks. */
#define BSP_PRV_PERIPHERAL_CLK_REQ_BIT_POS      (6U)
#define BSP_PRV_PERIPHERAL_CLK_REQ_BIT_MASK     (1U << BSP_PRV_PERIPHERAL_CLK_REQ_BIT_POS)
#define BSP_PRV_PERIPHERAL_CLK_RDY_BIT_POS      (7U)
#define BSP_PRV_PERIPHERAL_CLK_RDY_BIT_MASK     (1U << BSP_PRV_PERIPHERAL_CLK_RDY_BIT_POS)

#ifdef BSP_CFG_UCK_DIV

/* If the MCU has SCKDIVCR2 for USBCK configuration. */
 #if !BSP_FEATURE_BSP_HAS_USBCKDIVCR

/* Location of bitfield used to configure USB clock divider. */
  #define BSP_PRV_SCKDIVCR2_UCK_BIT    (4U)
  #define BSP_PRV_UCK_DIV              (BSP_CFG_UCK_DIV)

/* If the MCU has USBCKDIVCR. */
 #elif BSP_FEATURE_BSP_HAS_USBCKDIVCR
  #if BSP_CLOCKS_USB_CLOCK_DIV_1 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (0U)
  #elif BSP_CLOCKS_USB_CLOCK_DIV_2 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (1U)
  #elif BSP_CLOCKS_USB_CLOCK_DIV_3 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (5U)
  #elif BSP_CLOCKS_USB_CLOCK_DIV_4 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (2U)
  #elif BSP_CLOCKS_USB_CLOCK_DIV_5 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (6U)
   //#error // fpb
  #elif BSP_CLOCKS_USB_CLOCK_DIV_6 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (3U)
  #elif BSP_CLOCKS_USB_CLOCK_DIV_8 == BSP_CFG_UCK_DIV
   #define BSP_PRV_UCK_DIV             (4U)
  #else

   #error "BSP_CFG_UCK_DIV not supported."

  #endif
 #endif
#endif
#if BSP_FEATURE_BSP_HAS_USB60_CLOCK_REQ
 #define BSP_CLOCKS_USB60_CLOCK_DIV_1            (0) // Divide USB source clock by 1
 #define BSP_CLOCKS_USB60_CLOCK_DIV_2            (1) // Divide USB source clock by 2
 #define BSP_CLOCKS_USB60_CLOCK_DIV_3            (5) // Divide USB source clock by 3
 #define BSP_CLOCKS_USB60_CLOCK_DIV_4            (2) // Divide USB source clock by 4
 #define BSP_CLOCKS_USB60_CLOCK_DIV_5            (6) // Divide USB source clock by 5
 #define BSP_CLOCKS_USB60_CLOCK_DIV_6            (3) // Divide USB source clock by 6
 #define BSP_CLOCKS_USB60_CLOCK_DIV_8            (4) // Divide USB source clock by 8
#endif /* BSP_FEATURE_BSP_HAS_USB60_CLOCK_REQ*/
/* Choose the value to write to FLLCR2 (if applicable). */
#if BSP_PRV_HOCO_USE_FLL
 #if 1U == BSP_CFG_HOCO_FREQUENCY
  #define BSP_PRV_FLL_FLLCR2                     (0x226U)
 #elif 2U == BSP_CFG_HOCO_FREQUENCY
  #define BSP_PRV_FLL_FLLCR2                     (0x263U)
 #elif 4U == BSP_CFG_HOCO_FREQUENCY
  #define BSP_PRV_FLL_FLLCR2                     (0x263U)
 #else

/* When BSP_CFG_HOCO_FREQUENCY is 0, 4, 7 */
  #define BSP_PRV_FLL_FLLCR2                     (0x1E9U)
 #endif
#endif

/* Calculate the value to write to SCKDIVCR. */
#define BSP_PRV_STARTUP_SCKDIVCR_ICLK_BITS       ((BSP_CFG_ICLK_DIV & 0xFU) << 24U)
#if BSP_FEATURE_CGC_HAS_PCLKE
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKE_BITS     ((BSP_CFG_PCLKE_DIV & 0xFU) << 20U)
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKE_BITS     (0U)
   //#error // fpb
#endif
#if BSP_FEATURE_CGC_HAS_PCLKD
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKD_BITS     (BSP_CFG_PCLKD_DIV & 0xFU)
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKD_BITS     (0U)
#endif
#if BSP_FEATURE_CGC_HAS_PCLKC
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKC_BITS     ((BSP_CFG_PCLKC_DIV & 0xFU) << 4U)
   //#error // fpb
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKC_BITS     (0U)
#endif
#if BSP_FEATURE_CGC_HAS_PCLKB
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKB_BITS     ((BSP_CFG_PCLKB_DIV & 0xFU) << 8U)
   //#error // fpb
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKB_BITS     (0U)
#endif
#if BSP_FEATURE_CGC_HAS_PCLKA
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKA_BITS     ((BSP_CFG_PCLKA_DIV & 0xFU) << 12U)
   //#error // fpb
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_PCLKA_BITS     (0U)
#endif
#if BSP_FEATURE_CGC_HAS_BCLK
 #define BSP_PRV_STARTUP_SCKDIVCR_BCLK_BITS      ((BSP_CFG_BCLK_DIV & 0xFU) << 16U)
#elif BSP_FEATURE_CGC_SCKDIVCR_BCLK_MATCHES_PCLKB

/* Some MCUs have a requirement that bits 18-16 be set to the same value as the bits for configuring the PCLKB divisor. */
 #define BSP_PRV_STARTUP_SCKDIVCR_BCLK_BITS      ((BSP_CFG_PCLKB_DIV & 0xFU) << 16U)
   //#error // fpb
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_BCLK_BITS      (0U)
#endif
#if BSP_FEATURE_CGC_HAS_FCLK
 #define BSP_PRV_STARTUP_SCKDIVCR_FCLK_BITS      ((BSP_CFG_FCLK_DIV & 0xFU) << 28U)
   //#error // fpb
#else
 #define BSP_PRV_STARTUP_SCKDIVCR_FCLK_BITS      (0U)
#endif
#define BSP_PRV_STARTUP_SCKDIVCR                 (BSP_PRV_STARTUP_SCKDIVCR_ICLK_BITS |  \
                                                  BSP_PRV_STARTUP_SCKDIVCR_PCLKE_BITS | \
                                                  BSP_PRV_STARTUP_SCKDIVCR_PCLKD_BITS | \
                                                  BSP_PRV_STARTUP_SCKDIVCR_PCLKC_BITS | \
                                                  BSP_PRV_STARTUP_SCKDIVCR_PCLKB_BITS | \
                                                  BSP_PRV_STARTUP_SCKDIVCR_PCLKA_BITS | \
                                                  BSP_PRV_STARTUP_SCKDIVCR_BCLK_BITS |  \
                                                  BSP_PRV_STARTUP_SCKDIVCR_FCLK_BITS)
#if BSP_FEATURE_CGC_HAS_CPUCLK
 #define BSP_PRV_STARTUP_SCKDIVCR2_CPUCK_BITS    (BSP_CFG_CPUCLK_DIV & 0xFU)
 #define BSP_PRV_STARTUP_SCKDIVCR2               (BSP_PRV_STARTUP_SCKDIVCR2_CPUCK_BITS)
#endif

/* The number of clocks is used to size the g_clock_freq array. */
#if BSP_PRV_PLL2_SUPPORTED
 #if 0 != BSP_FEATURE_NUM_PLL2_OUTPUT_CLOCKS
  #define BSP_PRV_NUM_CLOCKS    ((uint8_t) BSP_CLOCKS_SOURCE_CLOCK_PLL2 + (BSP_FEATURE_NUM_PLL1_OUTPUT_CLOCKS - 1) + \
                                 BSP_FEATURE_NUM_PLL2_OUTPUT_CLOCKS)
 #else
  #define BSP_PRV_NUM_CLOCKS    ((uint8_t) BSP_CLOCKS_SOURCE_CLOCK_PLL2 + 1U)
   //#error // fpb
 #endif
#elif BSP_PRV_PLL_SUPPORTED
 #if 0 != BSP_FEATURE_NUM_PLL1_OUTPUT_CLOCKS

/* Removed offset of 1 since the BSP_CLOCKS_SOURCE_CLOCK_PLL will be reused for BSP_CLOCKS_SOURCE_CLOCK_PLL1P which is included in BSP_FEATURE_NUM_PLL1_OUTPUT_CLOCKS count. */
  #define BSP_PRV_NUM_CLOCKS    ((uint8_t) BSP_CLOCKS_SOURCE_CLOCK_PLL + BSP_FEATURE_NUM_PLL1_OUTPUT_CLOCKS)
 #else
  #define BSP_PRV_NUM_CLOCKS    ((uint8_t) BSP_CLOCKS_SOURCE_CLOCK_PLL + 1U)
 #endif
#else
 #define BSP_PRV_NUM_CLOCKS     ((uint8_t) BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK + 1U)
#endif

/* Calculate PLLCCR value. */
#if BSP_PRV_PLL_SUPPORTED
 #if (1U == BSP_FEATURE_CGC_PLLCCR_TYPE)
  #if BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC == BSP_CFG_PLL_SOURCE
   #define BSP_PRV_PLSRCSEL                        (0)
   #define BSP_PRV_PLL_USED                        (1)
  #elif BSP_CLOCKS_SOURCE_CLOCK_HOCO == BSP_CFG_PLL_SOURCE
   #define BSP_PRV_PLSRCSEL                        (1)
   #define BSP_PRV_PLL_USED                        (1)
   //#error // fpb
  #else
   #define BSP_PRV_PLL_USED                        (0)
  #endif
  #define BSP_PRV_PLLCCR_PLLMUL_MASK               (0x3F) // PLLMUL in PLLCCR is 6 bits wide
  #define BSP_PRV_PLLCCR_PLLMUL_BIT                (8)    // PLLMUL in PLLCCR starts at bit 8
  #define BSP_PRV_PLLCCR_PLSRCSEL_BIT              (4)    // PLSRCSEL in PLLCCR starts at bit 4
  #define BSP_PRV_PLLCCR                           ((((BSP_CFG_PLL_MUL & BSP_PRV_PLLCCR_PLLMUL_MASK) <<   \
                                                      BSP_PRV_PLLCCR_PLLMUL_BIT) |                        \
                                                     (BSP_PRV_PLSRCSEL << BSP_PRV_PLLCCR_PLSRCSEL_BIT)) | \
                                                    BSP_CFG_PLL_DIV)
 #endif
 #if (2U == BSP_FEATURE_CGC_PLLCCR_TYPE)
  #if BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC == BSP_CFG_PLL_SOURCE
   #define BSP_PRV_PLSRCSEL                        (0)
   #define BSP_PRV_PLL_USED                        (1)
  #else
   #define BSP_PRV_PLL_USED                        (0)
  #endif
  #define BSP_PRV_PLLCCR2_PLLMUL_MASK              (0x1F) // PLLMUL in PLLCCR2 is 5 bits wide
  #define BSP_PRV_PLLCCR2_PLODIV_BIT               (6)    // PLODIV in PLLCCR2 starts at bit 6

  #define BSP_PRV_PLLCCR2_PLLMUL                   (BSP_CFG_PLL_MUL >> 1)
  #define BSP_PRV_PLLCCR                           (BSP_PRV_PLLCCR2_PLLMUL & BSP_PRV_PLLCCR2_PLLMUL_MASK) | \
    (BSP_CFG_PLL_DIV << BSP_PRV_PLLCCR2_PLODIV_BIT)
 #endif
 #if (3U == BSP_FEATURE_CGC_PLLCCR_TYPE)
  #if BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC == BSP_CFG_PLL_SOURCE
   #define BSP_PRV_PLSRCSEL                        (0)
   #define BSP_PRV_PLL_USED                        (1)
  #elif BSP_CLOCKS_SOURCE_CLOCK_HOCO == BSP_CFG_PLL_SOURCE
   #define BSP_PRV_PLSRCSEL                        (1)
   #define BSP_PRV_PLL_USED                        (1)
  #else
   #define BSP_PRV_PLL_USED                        (0)
  #endif

  #define BSP_PRV_PLL_MUL_CFG_MACRO_PLLMUL_MASK    (0x3FFU)
  #define BSP_PRV_PLLCCR_PLLMULNF_BIT              (6) // PLLMULNF in PLLCCR starts at bit 6
  #define BSP_PRV_PLLCCR_PLSRCSEL_BIT              (4) // PLSRCSEL in PLLCCR starts at bit 4
  #define BSP_PRV_PLLCCR                           ((((BSP_CFG_PLL_MUL & BSP_PRV_PLL_MUL_CFG_MACRO_PLLMUL_MASK) << \
                                                      BSP_PRV_PLLCCR_PLLMULNF_BIT) |                               \
                                                     (BSP_PRV_PLSRCSEL << BSP_PRV_PLLCCR_PLSRCSEL_BIT)) |          \
                                                    BSP_CFG_PLL_DIV)
  #define BSP_PRV_PLLCCR2_PLL_DIV_MASK             (0x0F) // PLL DIV in PLLCCR2/PLL2CCR2 is 4 bits wide
  #define BSP_PRV_PLLCCR2_PLL_DIV_Q_BIT            (4)    // PLL DIV Q in PLLCCR2/PLL2CCR2 starts at bit 4
  #define BSP_PRV_PLLCCR2_PLL_DIV_R_BIT            (8)    // PLL DIV R in PLLCCR2/PLL2CCR2 starts at bit 8
  #define BSP_PRV_PLLCCR2                          (((BSP_CFG_PLODIVR & BSP_PRV_PLLCCR2_PLL_DIV_MASK) << \
                                                     BSP_PRV_PLLCCR2_PLL_DIV_R_BIT) |                    \
                                                    ((BSP_CFG_PLODIVQ & BSP_PRV_PLLCCR2_PLL_DIV_MASK) << \
                                                     BSP_PRV_PLLCCR2_PLL_DIV_Q_BIT) |                    \
                                                    (BSP_CFG_PLODIVP & BSP_PRV_PLLCCR2_PLL_DIV_MASK))
 #endif
 #if (4U == BSP_FEATURE_CGC_PLLCCR_TYPE)
  #if BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK == BSP_CFG_PLL_SOURCE
   #define BSP_PRV_PLL_USED                        (1)
  #else
   #define BSP_PRV_PLL_USED                        (0)
  #endif

  #define BSP_PRV_PLLCCR_PLLMUL_MASK               (0xFFU)   // PLLMUL is 8 bits wide
  #define BSP_PRV_PLLCCR_PLLMUL_BIT                (8)       // PLLMUL starts at bit 8
  #define BSP_PRV_PLLCCR_RESET                     (0x0004U) // Bit 2 must be written as 1
  #define BSP_PRV_PLLCCR                           (((BSP_CFG_PLL_MUL & BSP_PRV_PLLCCR_PLLMUL_MASK) << \
                                                     BSP_PRV_PLLCCR_PLLMUL_BIT) |                      \
                                                    BSP_PRV_PLLCCR_RESET)
 #endif
#endif

#if BSP_FEATURE_CGC_HAS_PLL2
 #if BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC == BSP_CFG_PLL2_SOURCE
  #define BSP_PRV_PL2SRCSEL                           (0)
  #define BSP_PRV_PLL2_USED                           (1)
 #elif BSP_CLOCKS_SOURCE_CLOCK_HOCO == BSP_CFG_PLL2_SOURCE
  #define BSP_PRV_PL2SRCSEL                           (1)
  #define BSP_PRV_PLL2_USED                           (1)
   //#error // fpb
 #else
  #define BSP_PRV_PLL2_USED                           (0)
 #endif

 #if (3U == BSP_FEATURE_CGC_PLLCCR_TYPE)
  #define BSP_PRV_PLL2_MUL_CFG_MACRO_PLLMUL_MASK      (0x3FF)
  #define BSP_PRV_PLL2_MUL_CFG_MACRO_PLLMULNF_MASK    (0x003U)
  #define BSP_PRV_PLL2CCR_PLLMULNF_BIT                (6) // PLLMULNF in PLLCCR starts at bit 6
  #define BSP_PRV_PLL2CCR_PLSRCSEL_BIT                (4) // PLSRCSEL in PLLCCR starts at bit 4
  #define BSP_PRV_PLL2CCR                             ((((BSP_CFG_PLL2_MUL & BSP_PRV_PLL2_MUL_CFG_MACRO_PLLMUL_MASK) << \
                                                         BSP_PRV_PLL2CCR_PLLMULNF_BIT) |                                \
                                                        (BSP_PRV_PL2SRCSEL << BSP_PRV_PLL2CCR_PLSRCSEL_BIT)) |          \
                                                       BSP_CFG_PLL2_DIV)
  #define BSP_PRV_PLL2CCR2_PLL_DIV_MASK               (0x0F) // PLL DIV in PLL2CCR2 is 4 bits wide
  #define BSP_PRV_PLL2CCR2_PLL_DIV_Q_BIT              (4)    // PLL DIV Q in PLL2CCR2 starts at bit 4
  #define BSP_PRV_PLL2CCR2_PLL_DIV_R_BIT              (8)    // PLL DIV R in PLL2CCR2 starts at bit 8
  #define BSP_PRV_PLL2CCR2                            (((BSP_CFG_PL2ODIVR & BSP_PRV_PLL2CCR2_PLL_DIV_MASK) << \
                                                        BSP_PRV_PLL2CCR2_PLL_DIV_R_BIT) |                     \
                                                       ((BSP_CFG_PL2ODIVQ & BSP_PRV_PLL2CCR2_PLL_DIV_MASK) << \
                                                        BSP_PRV_PLL2CCR2_PLL_DIV_Q_BIT) |                     \
                                                       (BSP_CFG_PL2ODIVP & BSP_PRV_PLL2CCR2_PLL_DIV_MASK))
 #else
  #define BSP_PRV_PLL2CCR                             ((BSP_CFG_PLL2_MUL << R_SYSTEM_PLL2CCR_PLL2MUL_Pos) | \
                                                       (BSP_CFG_PLL2_DIV << R_SYSTEM_PLL2CCR_PL2IDIV_Pos) | \
                                                       (BSP_PRV_PL2SRCSEL << R_SYSTEM_PLL2CCR_PL2SRCSEL_Pos))
   //#error // fpb
 #endif
#endif

/* All clocks with configurable source except PLL and CLKOUT can use PLL. */
#if (BSP_CFG_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_PLL)
 #define BSP_PRV_STABILIZE_PLL                    (1)
   //#error // fpb
#endif

/* All clocks with configurable source can use the main oscillator. */
#if (BSP_CFG_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
 #define BSP_PRV_STABILIZE_MAIN_OSC               (1)
#elif defined(BSP_CFG_UCK_SOURCE) && BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ && \
    (BSP_CFG_UCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_CANFDCLK_SOURCE) && (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_PLL_SOURCE) && (BSP_CFG_PLL_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC) && BSP_PRV_PLL_USED
 #define BSP_PRV_MAIN_OSC_USED                    (1)
 #define BSP_PRV_STABILIZE_MAIN_OSC               (1)
#elif defined(BSP_CFG_PLL2_SOURCE) && (BSP_CFG_PLL2_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC) && BSP_PRV_PLL2_USED
 #define BSP_PRV_MAIN_OSC_USED                    (1)
 #define BSP_PRV_STABILIZE_MAIN_OSC               (1)
#elif defined(BSP_CFG_CLKOUT_SOURCE) && (BSP_CFG_CLKOUT_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_SCISPICLK_SOURCE) && (BSP_CFG_SCISPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_SPICLK_SOURCE) && (BSP_CFG_SPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_SCICLK_SOURCE) && (BSP_CFG_SCICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_CANFDCLK_SOURCE) && (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_GPTCLK_SOURCE) && (BSP_CFG_GPTCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_IICCLK_SOURCE) && (BSP_CFG_IICCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_CECCLK_SOURCE) && (BSP_CFG_CECCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_I3CCLK_SOURCE) && (BSP_CFG_I3CCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_ADCCLK_SOURCE) && (BSP_CFG_ADCCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_LCDCLK_SOURCE) && (BSP_CFG_LCDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_U60CLK_SOURCE) && (BSP_CFG_U60CLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_OCTA_SOURCE) && (BSP_CFG_OCTA_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#elif defined(BSP_CFG_SDADC_CLOCK_SOURCE) && (BSP_CFG_SDADC_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_MAIN_OSC)
 #define BSP_PRV_MAIN_OSC_USED                    (1)
#else
 #define BSP_PRV_MAIN_OSC_USED                    (0)
   //#error // fpb
#endif

/* All clocks with configurable source can use HOCO except the CECCLK and I3CCLK. */
#if (BSP_CFG_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
 #define BSP_PRV_STABILIZE_HOCO                   (1)
#elif defined(BSP_CFG_PLL_SOURCE) && (BSP_CFG_PLL_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO) && BSP_PRV_PLL_USED
 #define BSP_PRV_HOCO_USED                        (1)
 #define BSP_PRV_STABILIZE_HOCO                   (1)
   //#error // fpb
#elif defined(BSP_CFG_PLL2_SOURCE) && (BSP_CFG_PLL2_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO) && BSP_PRV_PLL2_USED
 #define BSP_PRV_HOCO_USED                        (1)
 #define BSP_PRV_STABILIZE_HOCO                   (1)
#elif defined(BSP_CFG_UCK_SOURCE) && BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ && \
    (BSP_CFG_UCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_CLKOUT_SOURCE) && (BSP_CFG_CLKOUT_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_SCISPICLK_SOURCE) && (BSP_CFG_SCISPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_SPICLK_SOURCE) && (BSP_CFG_SPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_SCICLK_SOURCE) && (BSP_CFG_SCICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_CANFDCLK_SOURCE) && (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_GPTCLK_SOURCE) && (BSP_CFG_GPTCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_IICCLK_SOURCE) && (BSP_CFG_IICCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_ADCCLK_SOURCE) && (BSP_CFG_ADCCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_LCDCLK_SOURCE) && (BSP_CFG_LCDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_U60CLK_SOURCE) && (BSP_CFG_U60CLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_OCTA_SOURCE) && (BSP_CFG_OCTA_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#elif defined(BSP_CFG_SDADC_CLOCK_SOURCE) && (BSP_CFG_SDADC_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_HOCO)
 #define BSP_PRV_HOCO_USED                        (1)
#else
 #define BSP_PRV_HOCO_USED                        (0)
#endif

/* All clocks with configurable source except PLL can use MOCO. */
#if (BSP_CFG_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
 #define BSP_PRV_STABILIZE_MOCO                   (1)
#elif defined(BSP_CFG_CLKOUT_SOURCE) && (BSP_CFG_CLKOUT_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_UCK_SOURCE) && BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ && \
    (BSP_CFG_UCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_SCISPICLK_SOURCE) && (BSP_CFG_SCISPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_SPICLK_SOURCE) && (BSP_CFG_SPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_SCICLK_SOURCE) && (BSP_CFG_SCICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_CANFDCLK_SOURCE) && (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_GPTCLK_SOURCE) && (BSP_CFG_GPTCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_IICCLK_SOURCE) && (BSP_CFG_IICCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_I3CCLK_SOURCE) && (BSP_CFG_I3CCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_ADCCLK_SOURCE) && (BSP_CFG_ADCCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_LCDCLK_SOURCE) && (BSP_CFG_LCDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_U60CLK_SOURCE) && (BSP_CFG_U60CLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#elif defined(BSP_CFG_OCTA_SOURCE) && (BSP_CFG_OCTA_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_MOCO)
 #define BSP_PRV_MOCO_USED                        (1)
#else
 #define BSP_PRV_MOCO_USED                        (0)
   //#error // fpb
#endif

/* All clocks with configurable source except UCK, CANFD, LCDCLK, USBHSCLK, I3CCLK and PLL can use LOCO. */
#if (BSP_CFG_CLOCK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
 #define BSP_PRV_STABILIZE_LOCO                   (1)
#elif defined(BSP_CFG_CLKOUT_SOURCE) && (BSP_CFG_CLKOUT_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_SCISPICLK_SOURCE) && (BSP_CFG_SCISPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_SPICLK_SOURCE) && (BSP_CFG_SPICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_SCICLK_SOURCE) && (BSP_CFG_SCICLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_CANFDCLK_SOURCE) && (BSP_CFG_CANFDCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_GPTCLK_SOURCE) && (BSP_CFG_GPTCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_IICCLK_SOURCE) && (BSP_CFG_IICCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_ADCCLK_SOURCE) && (BSP_CFG_ADCCLK_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#elif defined(BSP_CFG_OCTA_SOURCE) && (BSP_CFG_OCTA_SOURCE == BSP_CLOCKS_SOURCE_CLOCK_LOCO)
 #define BSP_PRV_LOCO_USED                        (1)
#else
 #define BSP_PRV_LOCO_USED                        (0)
   //#error // fpb
#endif

/* Determine the optimal operating speed mode to apply after clock configuration based on the startup clock
 * frequency. */
#if BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_CGC_LOW_SPEED_MAX_FREQ_HZ && \
    !BSP_PRV_PLL_USED && !BSP_PRV_PLL2_USED
 #define BSP_PRV_STARTUP_OPERATING_MODE           (BSP_PRV_OPERATING_MODE_LOW_SPEED)
#elif BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_CGC_MIDDLE_SPEED_MAX_FREQ_HZ
 #define BSP_PRV_STARTUP_OPERATING_MODE           (BSP_PRV_OPERATING_MODE_MIDDLE_SPEED)
#else
 #define BSP_PRV_STARTUP_OPERATING_MODE           (BSP_PRV_OPERATING_MODE_HIGH_SPEED)
#endif

#if BSP_FEATURE_BSP_HAS_CLOCK_SUPPLY_TYPEB
 #define BSP_PRV_CLOCK_SUPPLY_TYPE_B              (0 == BSP_CFG_ROM_REG_OFS1_ICSATS)
#else
 #define BSP_PRV_CLOCK_SUPPLY_TYPE_B              (0)
   //#error // fpb
#endif

#if (BSP_FEATURE_BSP_HAS_CANFD_CLOCK && (BSP_CFG_CANFDCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED) &&    \
    (BSP_CFG_CANFDCLK_SOURCE != BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)) ||                                \
    (BSP_FEATURE_BSP_HAS_SCISPI_CLOCK && (BSP_CFG_SCISPICLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) || \
    (BSP_FEATURE_BSP_HAS_SCI_CLOCK && (BSP_CFG_SCICLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_SPI_CLOCK && (BSP_CFG_SPICLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_GPT_CLOCK && (BSP_CFG_GPTCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_IIC_CLOCK && (BSP_CFG_IICCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_CEC_CLOCK && (BSP_CFG_CECCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_I3C_CLOCK && (BSP_CFG_I3CCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_ADC_CLOCK && (BSP_CFG_ADCCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||       \
    (BSP_FEATURE_BSP_HAS_USB60_CLOCK_REQ && (BSP_CFG_U60CK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)) ||  \
    (BSP_FEATURE_BSP_HAS_LCD_CLOCK && (BSP_CFG_LCDCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED))

 #define BSP_PRV_HAS_ENABLED_PERIPHERAL_CLOCKS    (1U)
#else
 #define BSP_PRV_HAS_ENABLED_PERIPHERAL_CLOCKS    (0U)
   //#error // fpb
#endif

static uint32_t g_clock_freq[BSP_PRV_NUM_CLOCKS]  BSP_PLACE_IN_SECTION(BSP_SECTION_NOINIT);

static void bsp_prv_clock_set_hard_reset (void)
{
    /* Wait states in SRAMWTSC are set after hard reset. No change required here. */

    /* Calculate the wait states for ROM */
 #if BSP_FEATURE_CGC_HAS_FLWT
  #if BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_BSP_SYS_CLOCK_FREQ_ONE_ROM_WAITS

    /* Do nothing. Default setting in FLWT is correct. */
  #elif BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_BSP_SYS_CLOCK_FREQ_TWO_ROM_WAITS || \
    BSP_FEATURE_BSP_SYS_CLOCK_FREQ_TWO_ROM_WAITS == 0
    R_FCACHE->FLWT = BSP_PRV_ROM_ONE_WAIT_CYCLES;
  #elif 0 == BSP_FEATURE_BSP_SYS_CLOCK_FREQ_THREE_ROM_WAITS || \
    (BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_BSP_SYS_CLOCK_FREQ_THREE_ROM_WAITS)
    R_FCACHE->FLWT = BSP_PRV_ROM_TWO_WAIT_CYCLES;
  #elif 0 == BSP_FEATURE_BSP_SYS_CLOCK_FREQ_FOUR_ROM_WAITS || \
    (BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_BSP_SYS_CLOCK_FREQ_FOUR_ROM_WAITS)
    R_FCACHE->FLWT = BSP_PRV_ROM_THREE_WAIT_CYCLES;
  #elif 0 == BSP_FEATURE_BSP_SYS_CLOCK_FREQ_FIVE_ROM_WAITS || \
    (BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_BSP_SYS_CLOCK_FREQ_FIVE_ROM_WAITS)
    R_FCACHE->FLWT = BSP_PRV_ROM_FOUR_WAIT_CYCLES;
  #else
    R_FCACHE->FLWT = BSP_PRV_ROM_FIVE_WAIT_CYCLES;
  #endif
 #endif

 #if BSP_FEATURE_CGC_HAS_MEMWAIT && !BSP_PRV_CLOCK_SUPPLY_TYPE_B
  #if BSP_STARTUP_ICLK_HZ > BSP_PRV_MEMWAIT_MAX_ZERO_WAIT_FREQ

    /* The MCU must be in high speed mode to set wait states to 2. High speed mode is the default out of reset. */
    R_SYSTEM->MEMWAIT = BSP_PRV_MEMWAIT_TWO_WAIT_CYCLES;
  #endif
 #endif

 #if BSP_FEATURE_CGC_HAS_FLDWAITR && !BSP_PRV_CLOCK_SUPPLY_TYPE_B
  #if BSP_STARTUP_ICLK_HZ > BSP_PRV_FLDWAITR_MAX_ONE_WAIT_FREQ

    /* The MCU must be in high speed mode to set wait states to 2. High speed mode is the default out of reset. */
    BSP_PRV_FLDWAITR_REG_ACCESS = BSP_PRV_FLDWAITR_TWO_WAIT_CYCLES;
  #endif
 #endif

    /* In order to avoid a system clock (momentarily) higher than expected, the order of switching the clock and
     * dividers must be so that the frequency of the clock goes lower, instead of higher, before being correct. */

    /* MOCO is the source clock after reset. If the new source clock is faster than the current source clock,
     * then set the clock dividers before switching to the new source clock. */
 #if BSP_MOCO_FREQ_HZ <= BSP_STARTUP_SOURCE_CLOCK_HZ
  #if BSP_FEATURE_CGC_HAS_CPUCLK
   #if BSP_PRV_ICLK_DIV_VALUE >= BSP_PRV_SCKDIVCR_DIV_VALUE(BSP_FEATURE_CGC_ICLK_DIV_RESET)

    /* If the requested ICLK divider is greater than or equal to the current ICLK divider, then writing to
     * SCKDIVCR first will always satisfy the constraint: CPUCLK frequency >= ICLK frequency. */
    R_SYSTEM->SCKDIVCR  = BSP_PRV_STARTUP_SCKDIVCR;
    R_SYSTEM->SCKDIVCR2 = BSP_PRV_STARTUP_SCKDIVCR2;
   #else

    /* If the requested ICLK divider is less than the current ICLK divider, then writing to SCKDIVCR2 first
     * will always satisfy the constraint: CPUCLK frequency >= ICLK frequency. */
    R_SYSTEM->SCKDIVCR2 = BSP_PRV_STARTUP_SCKDIVCR2;
    R_SYSTEM->SCKDIVCR  = BSP_PRV_STARTUP_SCKDIVCR;
   #endif
  #else
    R_SYSTEM->SCKDIVCR = BSP_PRV_STARTUP_SCKDIVCR;
  #endif
 #endif

    /* Set the system source clock */
    R_SYSTEM->SCKSCR = BSP_CFG_CLOCK_SOURCE;

    /* MOCO is the source clock after reset. If the new source clock is slower than the current source clock,
     * then set the clock dividers after switching to the new source clock. */
 #if BSP_MOCO_FREQ_HZ > BSP_STARTUP_SOURCE_CLOCK_HZ
  #if BSP_FEATURE_CGC_HAS_CPUCLK
   #if BSP_PRV_ICLK_DIV_VALUE >= BSP_PRV_SCKDIVCR_DIV_VALUE(BSP_FEATURE_CGC_ICLK_DIV_RESET)

    /* If the requested ICLK divider is greater than or equal to the current ICLK divider, then writing to
     * SCKDIVCR first will always satisfy the constraint: CPUCLK frequency >= ICLK frequency. */
    R_SYSTEM->SCKDIVCR  = BSP_PRV_STARTUP_SCKDIVCR;
    R_SYSTEM->SCKDIVCR2 = BSP_PRV_STARTUP_SCKDIVCR2;
   #else

    /* If the requested ICLK divider is less than the current ICLK divider, then writing to SCKDIVCR2 first
     * will always satisfy the constraint: CPUCLK frequency >= ICLK frequency. */
    R_SYSTEM->SCKDIVCR2 = BSP_PRV_STARTUP_SCKDIVCR2;
    R_SYSTEM->SCKDIVCR  = BSP_PRV_STARTUP_SCKDIVCR;
   #endif
  #else
    R_SYSTEM->SCKDIVCR = BSP_PRV_STARTUP_SCKDIVCR;
  #endif
 #endif

    /* Update the CMSIS core clock variable so that it reflects the new ICLK frequency. */
    SystemCoreClockUpdate();

    /* Clocks are now at requested frequencies. */

    /* Adjust the MCU specific wait state soon after the system clock is set, if the system clock frequency to be
     * set is lower than previous. */
 #if BSP_FEATURE_CGC_HAS_SRAMWTSC
  #if BSP_STARTUP_ICLK_HZ <= BSP_FEATURE_BSP_SYS_CLOCK_FREQ_NO_RAM_WAITS
   #if BSP_FEATURE_CGC_HAS_SRAMPRCR2 == 1
    R_SRAM->SRAMPRCR2 = BSP_PRV_SRAM_UNLOCK;
    R_SRAM->SRAMWTSC  = BSP_PRV_SRAMWTSC_WAIT_CYCLES_DISABLE;
    R_SRAM->SRAMPRCR2 = BSP_PRV_SRAM_LOCK;
   #else
    R_SRAM->SRAMPRCR = BSP_PRV_SRAM_UNLOCK;

    /* Execute data memory barrier before and after setting the wait states (MREF_INTERNAL_000). */
    __DMB();
    R_SRAM->SRAMWTSC = BSP_PRV_SRAMWTSC_WAIT_CYCLES_DISABLE;
    __DMB();

    R_SRAM->SRAMPRCR = BSP_PRV_SRAM_LOCK;
   #endif
  #endif
 #endif

    /* ROM wait states are 0 by default.  No change required here. */
}
/*******************************************************************************************************************//**
 * Initializes variable to store system clock frequencies.
 **********************************************************************************************************************/
#if BSP_TZ_NONSECURE_BUILD
void bsp_clock_freq_var_init (void)
#else
static void bsp_clock_freq_var_init (void)
#endif
{
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_HOCO] = BSP_HOCO_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_MOCO] = BSP_MOCO_FREQ_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_LOCO] = BSP_LOCO_FREQ_HZ;
#if BSP_CLOCK_CFG_MAIN_OSC_POPULATED
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC] = BSP_CFG_XTAL_HZ;
#else
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC] = 0U;
#endif
#if BSP_CLOCK_CFG_SUBCLOCK_POPULATED
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK] = BSP_SUBCLOCK_FREQ_HZ;
#else
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK] = 0U;
#endif
#if BSP_PRV_PLL_SUPPORTED
 #if BSP_CLOCKS_SOURCE_CLOCK_PLL == BSP_CFG_CLOCK_SOURCE
  #if (3U != BSP_FEATURE_CGC_PLLCCR_TYPE)

    /* The PLL Is the startup clock. */
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL] = BSP_STARTUP_SOURCE_CLOCK_HZ;
  #else
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL]   = BSP_CFG_PLL1P_FREQUENCY_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL1Q] = BSP_CFG_PLL1Q_FREQUENCY_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL1R] = BSP_CFG_PLL1R_FREQUENCY_HZ;
  #endif
 #else

    /* The PLL value will be calculated at initialization. */
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL] = BSP_CFG_XTAL_HZ;
 #endif
#endif

#if BSP_TZ_NONSECURE_BUILD && BSP_TZ_CFG_CGFSAR != 0xFFFFFFFFU

    /* If the CGC is secure and this is a non secure project, register a callback for getting clock settings. */
    R_BSP_ClockUpdateCallbackSet(g_bsp_clock_update_callback, &g_callback_memory);
#endif

    /* Update PLL Clock Frequency based on BSP Configuration. */
#if BSP_PRV_PLL_SUPPORTED && BSP_CLOCKS_SOURCE_CLOCK_PLL != BSP_CFG_CLOCK_SOURCE && BSP_PRV_PLL_USED
 #if (1U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL] = ((g_clock_freq[BSP_CFG_PLL_SOURCE] * (BSP_CFG_PLL_MUL + 1U)) >> 1U) /
                                                (BSP_CFG_PLL_DIV + 1U);
 #elif (3U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL]   = BSP_CFG_PLL1P_FREQUENCY_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL1Q] = BSP_CFG_PLL1Q_FREQUENCY_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL1R] = BSP_CFG_PLL1R_FREQUENCY_HZ;
 #elif (4U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL] = (g_clock_freq[BSP_CFG_PLL_SOURCE] * (BSP_CFG_PLL_MUL + 1U)) >> 1U;
 #else
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL] = ((g_clock_freq[BSP_CFG_PLL_SOURCE] * (BSP_CFG_PLL_MUL + 1U)) >> 1U) >>
                                                BSP_CFG_PLL_DIV;
 #endif
#endif

    /* Update PLL2 Clock Frequency based on BSP Configuration. */
#if BSP_PRV_PLL2_SUPPORTED && BSP_PRV_PLL2_USED
 #if (1U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL2] = ((g_clock_freq[BSP_CFG_PLL2_SOURCE] * (BSP_CFG_PLL2_MUL + 1U)) >> 1U) /
                                                 (BSP_CFG_PLL2_DIV + 1U);
 #elif (3U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL2]  = BSP_CFG_PLL2P_FREQUENCY_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL2Q] = BSP_CFG_PLL2Q_FREQUENCY_HZ;
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL2R] = BSP_CFG_PLL2R_FREQUENCY_HZ;
 #else
    g_clock_freq[BSP_CLOCKS_SOURCE_CLOCK_PLL2] =
        ((g_clock_freq[BSP_CFG_PLL2_SOURCE] * (BSP_CFG_PLL2_MUL + 1U)) >> 1U) >> BSP_CFG_PLL2_DIV;
 #endif
#endif

    /* The SystemCoreClock needs to be updated before calling R_BSP_SoftwareDelay. */
    SystemCoreClockUpdate();
}
/*******************************************************************************************************************//**
 * Initializes system clocks.  Makes no assumptions about current register settings.
 **********************************************************************************************************************/
void bsp_clock_init2(void)
{
    /* Unlock CGC and LPM protection registers. */
    R_SYSTEM->PRCR = (uint16_t) BSP_PRV_PRCR_UNLOCK;

#if BSP_FEATURE_BSP_FLASH_CACHE
 #if !BSP_CFG_USE_LOW_VOLTAGE_MODE && BSP_FEATURE_BSP_FLASH_CACHE_DISABLE_OPM

    /* Disable flash cache before modifying MEMWAIT, SOPCCR, or OPCCR. */
    R_BSP_FlashCacheDisable();
 #else

    /* Enable the flash cache and don't disable it while running from flash. On these MCUs, the flash cache does not
     * need to be disabled when adjusting the operating power mode. */
    R_BSP_FlashCacheEnable();
 #endif
#endif

#if BSP_FEATURE_BSP_FLASH_PREFETCH_BUFFER

    /* Disable the flash prefetch buffer. */
    R_FACI_LP->PFBER = 0;
#endif

    bsp_clock_freq_var_init();

#if BSP_CLOCK_CFG_MAIN_OSC_POPULATED
 #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET

    /* Update the main oscillator drive, source, and wait states if the main oscillator is stopped.  If the main
     * oscillator is running, the drive, source, and wait states are assumed to be already set appropriately. */
    if (R_SYSTEM->MOSCCR)
    {
        /* Don't write to MOSCWTCR unless MOSTP is 1 and MOSCSF = 0. */
        FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.MOSCSF, 0U);

        /* Configure main oscillator drive. */
        R_SYSTEM->MOMCR = BSP_PRV_MOMCR;

        /* Set the main oscillator wait time. */
        R_SYSTEM->MOSCWTCR = (uint8_t) BSP_CLOCK_CFG_MAIN_OSC_WAIT;
    }

 #else

    /* Configure main oscillator drive. */
    R_SYSTEM->MOMCR = BSP_PRV_MOMCR;

    /* Set the main oscillator wait time. */
    R_SYSTEM->MOSCWTCR = (uint8_t) BSP_CLOCK_CFG_MAIN_OSC_WAIT;
 #endif
#endif

#if BSP_FEATURE_CGC_HAS_SOSC
 #if BSP_CLOCK_CFG_SUBCLOCK_POPULATED

    /* If Sub-Clock Oscillator is started at reset, stop it before configuring the subclock drive. */
    if (0U == R_SYSTEM->SOSCCR)
    {
        /* Stop the Sub-Clock Oscillator to update the SOMCR register. */
        R_SYSTEM->SOSCCR = 1U;

        /* Allow a stop interval of at least 5 SOSC clock cycles before configuring the drive capacity
         * and restarting Sub-Clock Oscillator. */
        R_BSP_SoftwareDelay(BSP_PRV_SUBCLOCK_STOP_INTERVAL_US, BSP_DELAY_UNITS_MICROSECONDS);
        //k_busy_wait(BSP_PRV_SUBCLOCK_STOP_INTERVAL_US);
    }

    /* Configure the subclock drive as subclock is not running. */
    R_SYSTEM->SOMCR = ((BSP_CLOCK_CFG_SUBCLOCK_DRIVE << BSP_FEATURE_CGC_SODRV_SHIFT) & BSP_FEATURE_CGC_SODRV_MASK);

    /* Restart the Sub-Clock Oscillator. */
    R_SYSTEM->SOSCCR = 0U;
  #if (BSP_CLOCKS_SOURCE_CLOCK_SUBCLOCK == BSP_CFG_CLOCK_SOURCE) || (BSP_PRV_HOCO_USE_FLL)

    /* If the subclock is the system clock source OR if FLL is used, wait for stabilization. */
    R_BSP_SubClockStabilizeWait(BSP_CLOCK_CFG_SUBCLOCK_STABILIZATION_MS);
  #endif
 #else
    R_SYSTEM->SOSCCR = 1U;
 #endif
#endif

#if BSP_FEATURE_CGC_HAS_HOCOWTCR
 #if BSP_FEATURE_CGC_HOCOWTCR_64MHZ_ONLY

    /* These MCUs only require writes to HOCOWTCR if HOCO is set to 64 MHz. */
  #if 64000000 == BSP_HOCO_HZ
   #if BSP_CFG_USE_LOW_VOLTAGE_MODE

    /* Wait for HOCO to stabilize before writing to HOCOWTCR. */
    FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.HOCOSF, 1U);
   #else

    /* HOCO is assumed to be stable because these MCUs also require the HOCO to be stable before changing the operating
     * power control mode. */
   #endif
    R_SYSTEM->HOCOWTCR = BSP_FEATURE_CGC_HOCOWTCR_VALUE;
  #endif
 #else

    /* These MCUs require HOCOWTCR to be set to the maximum value except in snooze mode.  There is no restriction to
     * writing this register. */
    R_SYSTEM->HOCOWTCR = BSP_FEATURE_CGC_HOCOWTCR_VALUE;
 #endif
#endif

#if !BSP_CFG_USE_LOW_VOLTAGE_MODE
 #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET

    /* Switch to high-speed to prevent any issues with the subsequent clock configurations. */
    bsp_prv_operating_mode_set(BSP_PRV_OPERATING_MODE_HIGH_SPEED);
 #elif BSP_FEATURE_CGC_LOW_VOLTAGE_MAX_FREQ_HZ > 0U

    /* MCUs that support low voltage mode start up in low voltage mode. */
    bsp_prv_operating_mode_opccr_set(BSP_PRV_OPERATING_MODE_HIGH_SPEED);

  #if !BSP_PRV_HOCO_USED

    /* HOCO must be running during startup in low voltage mode. If HOCO is not used, turn it off after exiting low
     * voltage mode. */
    R_SYSTEM->HOCOCR = 1U;
  #endif
 #elif BSP_FEATURE_CGC_STARTUP_OPCCR_MODE != BSP_PRV_OPERATING_MODE_HIGH_SPEED

    /* Some MCUs do not start in high speed mode. */
    bsp_prv_operating_mode_opccr_set(BSP_PRV_OPERATING_MODE_HIGH_SPEED);
 #endif
#endif

    /* The FLL function can only be used when the subclock is running. */
#if BSP_PRV_HOCO_USE_FLL

    /* If FLL is to be used configure FLLCR1 and FLLCR2 before starting HOCO. */
    R_SYSTEM->FLLCR2 = BSP_PRV_FLL_FLLCR2;
    R_SYSTEM->FLLCR1 = 1U;
#endif

    /* Start all clocks used by other clocks first. */
#if BSP_PRV_HOCO_USED
    R_SYSTEM->HOCOCR = 0U;

 #if BSP_PRV_HOCO_USE_FLL && (BSP_CLOCKS_SOURCE_CLOCK_HOCO != BSP_CFG_PLL_SOURCE)

    /* If FLL is enabled, wait for the FLL stabilization delay (1.8 ms) */
    R_BSP_SoftwareDelay(BSP_PRV_FLL_STABILIZATION_TIME_US, BSP_DELAY_UNITS_MICROSECONDS);
    //k_busy_wait(BSP_PRV_FLL_STABILIZATION_TIME_US);
 #endif

 #if BSP_PRV_STABILIZE_HOCO

    /* Wait for HOCO to stabilize. */
    FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.HOCOSF, 1U);
 #endif
#endif
#if BSP_PRV_MOCO_USED
 #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET

    /* If the MOCO is not running, start it and wait for it to stabilize using a software delay. */
    if (0U != R_SYSTEM->MOCOCR)
    {
        R_SYSTEM->MOCOCR = 0U;
  #if BSP_PRV_STABILIZE_MOCO
        R_BSP_SoftwareDelay(BSP_FEATURE_CGC_MOCO_STABILIZATION_MAX_US, BSP_DELAY_UNITS_MICROSECONDS);
        //k_busy_wait(BSP_FEATURE_CGC_MOCO_STABILIZATION_MAX_US);
  #endif
    }
 #endif
#endif
#if BSP_PRV_LOCO_USED
 #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET

    /* If the LOCO is not running, start it and wait for it to stabilize using a software delay. */
    if (0U != R_SYSTEM->LOCOCR)
    {
        R_SYSTEM->LOCOCR = 0U;
  #if BSP_PRV_STABILIZE_LOCO
        R_BSP_SoftwareDelay(BSP_FEATURE_CGC_LOCO_STABILIZATION_MAX_US, BSP_DELAY_UNITS_MICROSECONDS);
  #endif
    }

 #else
    R_SYSTEM->LOCOCR = 0U;
  #if BSP_PRV_STABILIZE_LOCO
    R_BSP_SoftwareDelay(BSP_FEATURE_CGC_LOCO_STABILIZATION_MAX_US, BSP_DELAY_UNITS_MICROSECONDS);
  #endif
 #endif
#endif
#if BSP_PRV_MAIN_OSC_USED
    R_SYSTEM->MOSCCR = 0U;

 #if BSP_PRV_STABILIZE_MAIN_OSC

    /* Wait for main oscillator to stabilize. */
    FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.MOSCSF, 1U);
 #endif
#endif

    /* Start clocks that require other clocks. At this point, all dependent clocks are running and stable if needed. */

#if BSP_PRV_STARTUP_OPERATING_MODE != BSP_PRV_OPERATING_MODE_LOW_SPEED
 #if BSP_FEATURE_CGC_HAS_PLL2 && BSP_CFG_PLL2_SOURCE != BSP_CLOCKS_CLOCK_DISABLED
    R_SYSTEM->PLL2CCR = BSP_PRV_PLL2CCR;
  #if (3U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    R_SYSTEM->PLL2CCR2 = BSP_PRV_PLL2CCR2;
  #endif

    /* Start PLL2. */
    R_SYSTEM->PLL2CR = 0U;
 #endif                                /* BSP_FEATURE_CGC_HAS_PLL2 && BSP_CFG_PLL2_ENABLE */
#endif

#if BSP_PRV_PLL_SUPPORTED && BSP_PRV_PLL_USED

    /* Configure the PLL registers. */
 #if (1U == BSP_FEATURE_CGC_PLLCCR_TYPE) || (4U == BSP_FEATURE_CGC_PLLCCR_TYPE)
    R_SYSTEM->PLLCCR = (uint16_t) BSP_PRV_PLLCCR;
 #elif 2U == BSP_FEATURE_CGC_PLLCCR_TYPE
    R_SYSTEM->PLLCCR2 = (uint8_t) BSP_PRV_PLLCCR;
 #elif 3U == BSP_FEATURE_CGC_PLLCCR_TYPE
    R_SYSTEM->PLLCCR  = (uint16_t) BSP_PRV_PLLCCR;
    R_SYSTEM->PLLCCR2 = (uint16_t) BSP_PRV_PLLCCR2;
 #endif

 #if BSP_FEATURE_CGC_PLLCCR_WAIT_US > 0

    /* This loop is provided to ensure at least 1 us passes between setting PLLMUL and clearing PLLSTP on some
     * MCUs (see PLLSTP notes in Section 8.2.4 "PLL Control Register (PLLCR)" of the RA4M1 manual R01UH0887EJ0100).
     * Five loops are needed here to ensure the most efficient path takes at least 1 us from the setting of
     * PLLMUL to the clearing of PLLSTP. HOCO is the fastest clock we can be using here since PLL cannot be running
     * while setting PLLCCR. */
    bsp_prv_software_delay_loop(BSP_DELAY_LOOPS_CALCULATE(BSP_PRV_MAX_HOCO_CYCLES_PER_US));
 #endif

    /* Start the PLL. */
    R_SYSTEM->PLLCR = 0U;

 #if BSP_PRV_STABILIZE_PLL

    /* Wait for PLL to stabilize. */
    FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.PLLSF, 1U);
 #endif
#endif

    /* Set source clock and dividers. */
#if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET
 #if BSP_TZ_SECURE_BUILD

    /* In case of soft reset, make sure callback pointer is NULL initially. */
    g_bsp_clock_update_callback = NULL;
 #endif

 #if BSP_FEATURE_CGC_HAS_CPUCLK
    bsp_prv_clock_set(BSP_CFG_CLOCK_SOURCE, BSP_PRV_STARTUP_SCKDIVCR, BSP_PRV_STARTUP_SCKDIVCR2);
 #else
    bsp_prv_clock_set(BSP_CFG_CLOCK_SOURCE, BSP_PRV_STARTUP_SCKDIVCR, 0);
 #endif
#else
    bsp_prv_clock_set_hard_reset();
#endif

    /* If the MCU can run in a lower power mode, apply the optimal operating speed mode. */
#if !BSP_CFG_USE_LOW_VOLTAGE_MODE
 #if BSP_PRV_STARTUP_OPERATING_MODE != BSP_PRV_OPERATING_MODE_HIGH_SPEED
  #if BSP_PRV_PLL_SUPPORTED
   #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET
    if (BSP_PRV_OPERATING_MODE_LOW_SPEED == BSP_PRV_STARTUP_OPERATING_MODE)
    {
        /* If the MCU has a PLL, ensure PLL is stopped and stable before entering low speed mode. */
        R_SYSTEM->PLLCR = 1U;

        /* Wait for PLL to stabilize. */
        FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.PLLSF, 0U);

    #if BSP_FEATURE_CGC_HAS_PLL2

        /* If the MCU has a PLL2, ensure PLL2 is stopped and stable before entering low speed mode. */
        R_SYSTEM->PLL2CR = 1U;

        /* Wait for PLL to stabilize. */
        FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->OSCSF_b.PLL2SF, 0U);
    #endif
    }
   #endif
  #endif
    bsp_prv_operating_mode_set(BSP_PRV_STARTUP_OPERATING_MODE);
 #endif
#endif

#if defined(BSP_PRV_POWER_USE_DCDC) && (BSP_PRV_POWER_USE_DCDC == BSP_PRV_POWER_DCDC_STARTUP) && \
    (BSP_PRV_STARTUP_OPERATING_MODE <= BSP_PRV_OPERATING_MODE_MIDDLE_SPEED)

    /* Start DCDC as part of BSP startup when configured (BSP_CFG_DCDC_ENABLE == 2). */
    R_BSP_PowerModeSet(BSP_CFG_DCDC_VOLTAGE_RANGE);
#endif

    /* Configure BCLK if it exists on the MCU. */
#ifdef BSP_CFG_BCLK_OUTPUT
 #if BSP_CFG_BCLK_OUTPUT > 0U
    R_SYSTEM->BCKCR   = BSP_CFG_BCLK_OUTPUT - 1U;
    R_SYSTEM->EBCKOCR = 1U;
 #else
  #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET
    R_SYSTEM->EBCKOCR = 0U;
  #endif
 #endif
#endif

    /* Configure SDRAM clock if it exists on the MCU. */
#ifdef BSP_CFG_SDCLK_OUTPUT
    R_SYSTEM->SDCKOCR = BSP_CFG_SDCLK_OUTPUT;
#endif

    /* Configure CLKOUT. */
#if BSP_CFG_CLKOUT_SOURCE == BSP_CLOCKS_CLOCK_DISABLED
 #if BSP_CFG_STARTUP_CLOCK_REG_NOT_RESET
    R_SYSTEM->CKOCR = 0U;
 #endif
#else
    uint8_t ckocr = BSP_CFG_CLKOUT_SOURCE | (BSP_CFG_CLKOUT_DIV << BSP_PRV_CKOCR_CKODIV_BIT);
    R_SYSTEM->CKOCR = ckocr;
    ckocr          |= (1U << BSP_PRV_CKOCR_CKOEN_BIT);
    R_SYSTEM->CKOCR = ckocr;
#endif

#if BSP_PRV_STARTUP_OPERATING_MODE != BSP_PRV_OPERATING_MODE_LOW_SPEED
 #if BSP_CFG_UCK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED

    /* If the USB clock has a divider setting in SCKDIVCR2. */
  #if BSP_FEATURE_BSP_HAS_USB_CLOCK_DIV && !BSP_FEATURE_BSP_HAS_USBCKDIVCR
    R_SYSTEM->SCKDIVCR2 = BSP_PRV_UCK_DIV << BSP_PRV_SCKDIVCR2_UCK_BIT;
  #endif                               /* BSP_FEATURE_BSP_HAS_USB_CLOCK_DIV && !BSP_FEATURE_BSP_HAS_USBCKDIVCR */

    /* If there is a REQ bit in USBCKCR than follow sequence from section 8.2.29 in RA6M4 hardware manual R01UH0890EJ0050. */
  #if BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ

    /* Request to change the USB Clock. */
    R_SYSTEM->USBCKCR_b.USBCKSREQ = 1;

    /* Wait for the clock to be stopped. */
    FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->USBCKCR_b.USBCKSRDY, 1U);

    /* Write the settings. */
    R_SYSTEM->USBCKDIVCR = BSP_PRV_UCK_DIV;

    /* Select the USB Clock without enabling it. */
    R_SYSTEM->USBCKCR = BSP_CFG_UCK_SOURCE | R_SYSTEM_USBCKCR_USBCKSREQ_Msk;
  #endif                               /* BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ */

  #if BSP_FEATURE_BSP_HAS_USB_CLOCK_SEL

    /* Some MCUs use an alternate register for selecting the USB clock source. */
   #if BSP_FEATURE_BSP_HAS_USB_CLOCK_SEL_ALT
    #if BSP_CLOCKS_SOURCE_CLOCK_PLL == BSP_CFG_UCK_SOURCE

    /* Write to USBCKCR to select the PLL. */
    R_SYSTEM->USBCKCR_ALT = 0;
    #elif BSP_CLOCKS_SOURCE_CLOCK_HOCO == BSP_CFG_UCK_SOURCE

    /* Write to USBCKCR to select the HOCO. */
    R_SYSTEM->USBCKCR_ALT = 1;
    #endif
   #else

    /* Select the USB Clock. */
    R_SYSTEM->USBCKCR = BSP_CFG_UCK_SOURCE;
   #endif
  #endif                               /* BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ */

  #if BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ

    /* Wait for the USB Clock to be started. */
    FSP_HARDWARE_REGISTER_WAIT(R_SYSTEM->USBCKCR_b.USBCKSRDY, 0U);
  #endif                               /* BSP_FEATURE_BSP_HAS_USB_CLOCK_REQ */
 #endif                                /* BSP_CFG_USB_ENABLE */
#endif                                 /* BSP_PRV_STARTUP_OPERATING_MODE != BSP_PRV_OPERATING_MODE_LOW_SPEED */

    /* Set the OCTASPI clock if it exists on the MCU (See section 8.2.30 of the RA6M4 hardware manual R01UH0890EJ0050). */
#if BSP_FEATURE_BSP_HAS_OCTASPI_CLOCK && BSP_CFG_OCTA_SOURCE != BSP_CLOCKS_CLOCK_DISABLED
    bsp_octaclk_settings_t octaclk_settings =
    {
        .source_clock = (bsp_clocks_source_t) BSP_CFG_OCTA_SOURCE,
        .divider      = (bsp_clocks_octaclk_div_t) BSP_CFG_OCTA_DIV
    };
    R_BSP_OctaclkUpdate(&octaclk_settings);
#endif                                 /* BSP_FEATURE_BSP_HAS_OCTASPI_CLOCK && BSP_CFG_OCTASPI_CLOCK_ENABLE */

    /* Set the SDADC clock if it exists on the MCU. */
#if BSP_FEATURE_BSP_HAS_SDADC_CLOCK && (BSP_CFG_SDADC_CLOCK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)

    /* SDADC isn't controlled like the other peripheral clocks so we cannot use the generic setter. */
    R_SYSTEM->SDADCCKCR = BSP_CFG_SDADC_CLOCK_SOURCE & R_SYSTEM_SDADCCKCR_SDADCCKSEL_Msk;
#endif

    /* Lock CGC and LPM protection registers. */
    R_SYSTEM->PRCR = (uint16_t) BSP_PRV_PRCR_LOCK;

#if BSP_FEATURE_BSP_FLASH_CACHE && BSP_FEATURE_BSP_FLASH_CACHE_DISABLE_OPM
    R_BSP_FlashCacheEnable();
#endif

#if BSP_FEATURE_BSP_FLASH_PREFETCH_BUFFER
    R_FACI_LP->PFBER = 1;
#endif
}


static int clock_control_ra_on(const struct device *dev, clock_control_subsys_t subsys)
{
	struct ra_clock_config *clockconf = (struct ra_clock_config *)subsys;

	int lock = irq_lock();
	regs.MSTP->MSTPCRB &= ~(1U << (31U - clockconf->channel));
	irq_unlock(lock);

    /* Set the CANFD clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_CANFD_CLOCK && (BSP_CFG_CANFDCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED) && \
    (BSP_CFG_CANFDCLK_SOURCE != BSP_CLOCKS_SOURCE_CLOCK_MAIN_OSC)

    bsp_peripheral_clock_set(&R_SYSTEM->CANFDCKCR,
                             &R_SYSTEM->CANFDCKDIVCR,
                             BSP_CFG_CANFDCLK_DIV,
                             BSP_CFG_CANFDCLK_SOURCE);
#endif

    /* Set the SCISPI clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_SCISPI_CLOCK && (BSP_CFG_SCISPICLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->SCISPICKCR,
                             &R_SYSTEM->SCISPICKDIVCR,
                             BSP_CFG_SCISPICLK_DIV,
                             BSP_CFG_SCISPICLK_SOURCE);
#endif

    /* Set the SCI clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_SCI_CLOCK && (BSP_CFG_SCICLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->SCICKCR, &R_SYSTEM->SCICKDIVCR, BSP_CFG_SCICLK_DIV, BSP_CFG_SCICLK_SOURCE);
#endif

    /* Set the SPI clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_SPI_CLOCK && (BSP_CFG_SPICLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->SPICKCR, &R_SYSTEM->SPICKDIVCR, BSP_CFG_SPICLK_DIV, BSP_CFG_SPICLK_SOURCE);
#endif

    /* Set the GPT clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_GPT_CLOCK && (BSP_CFG_GPTCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->GPTCKCR, &R_SYSTEM->GPTCKDIVCR, BSP_CFG_GPTCLK_DIV, BSP_CFG_GPTCLK_SOURCE);
#endif

    /* Set the IIC clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_IIC_CLOCK && (BSP_CFG_IICCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->IICCKCR, &R_SYSTEM->IICCKDIVCR, BSP_CFG_IICCLK_DIV, BSP_CFG_IICCLK_SOURCE);
#endif

    /* Set the CEC clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_CEC_CLOCK && (BSP_CFG_CECCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->CECCKCR, &R_SYSTEM->CECCKDIVCR, BSP_CFG_CECCLK_DIV, BSP_CFG_CECCLK_SOURCE);
#endif

    /* Set the I3C clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_I3C_CLOCK && (BSP_CFG_I3CCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->I3CCKCR, &R_SYSTEM->I3CCKDIVCR, BSP_CFG_I3CCLK_DIV, BSP_CFG_I3CCLK_SOURCE);
#endif

    /* Set the ADC clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_ADC_CLOCK && (BSP_CFG_ADCCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->ADCCKCR, &R_SYSTEM->ADCCKDIVCR, BSP_CFG_ADCCLK_DIV, BSP_CFG_ADCCLK_SOURCE);
#endif

    /* Set the LCD clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_LCD_CLOCK && (BSP_CFG_LCDCLK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->LCDCKCR, &R_SYSTEM->LCDCKDIVCR, BSP_CFG_LCDCLK_DIV, BSP_CFG_LCDCLK_SOURCE);
#endif

    /* Set the USB-HS clock if it exists on the MCU */
#if BSP_FEATURE_BSP_HAS_USB60_CLOCK_REQ && (BSP_CFG_U60CK_SOURCE != BSP_CLOCKS_CLOCK_DISABLED)
    bsp_peripheral_clock_set(&R_SYSTEM->USB60CKCR, &R_SYSTEM->USB60CKDIVCR, BSP_CFG_U60CK_DIV, BSP_CFG_U60CK_SOURCE);
#endif
	return 0;
}

void SystemCoreClockUpdate (void)
{
    uint32_t clock_index = R_SYSTEM->SCKSCR;
#if !BSP_FEATURE_CGC_HAS_CPUCLK
    SystemCoreClock = g_clock_freq[clock_index] >> R_SYSTEM->SCKDIVCR_b.ICK;
#else
    uint8_t cpuclk_div = R_SYSTEM->SCKDIVCR2_b.CPUCK;
    if (8U == cpuclk_div)
    {
        SystemCoreClock = g_clock_freq[clock_index] / 3U;
    }
    else if (9U == cpuclk_div)
    {
        SystemCoreClock = g_clock_freq[clock_index] / 6U;
    }
    else if (10U == cpuclk_div)
    {
        SystemCoreClock = g_clock_freq[clock_index] / 12U;
    }
    else
    {
        SystemCoreClock = g_clock_freq[clock_index] >> cpuclk_div;
    }
#endif
}



static const struct clock_control_driver_api ra_clock_control_driver_api = {
	.on = clock_control_ra_on,
	//.off = nxp_s32_clock_off,
	//.get_rate = nxp_s32_clock_get_rate,
};

static int ra_clock_control_init(const struct device *dev)
{
	bsp_clock_init2();
	return 0;
}

DEVICE_DT_INST_DEFINE(0, &ra_clock_control_init, NULL, NULL, NULL, PRE_KERNEL_1,
		      CONFIG_CLOCK_CONTROL_INIT_PRIORITY, &ra_clock_control_driver_api);
