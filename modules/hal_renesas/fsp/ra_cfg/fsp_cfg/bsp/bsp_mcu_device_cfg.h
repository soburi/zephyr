#ifndef BSP_MCU_DEVICE_CFG_H_
#define BSP_MCU_DEVICE_CFG_H_

#if defined(CONFIGURE_SOC_SERIES_RA6M1) || defined(CONFIGURE_SOC_SERIES_RA6M2) ||                  \
	defined(CONFIGURE_SOC_SERIES_RA6M3) || defined(CONFIGURE_SOC_SERIES_RA6M4) ||              \
	defined(CONFIGURE_SOC_SERIES_RA6M5) || defined(CONFIGURE_SOC_SERIES_RA6E1) ||              \
	defined(CONFIGURE_SOC_SERIES_RA6E2) || defined(CONFIGURE_SOC_SERIES_RA6T1) ||              \
	defined(CONFIGURE_SOC_SERIES_RA6T2) || defined(CONFIGURE_SOC_SERIES_RA6T3)
#define BSP_CFG_MCU_PART_SERIES (6)
#endif

#if defined(CONFIGURE_SOC_SERIES_RA4M1) || defined(CONFIGURE_SOC_SERIES_RA4M2) ||                  \
	defined(CONFIGURE_SOC_SERIES_RA4M3) || defined(CONFIGURE_SOC_SERIES_RA4E1) ||              \
	defined(CONFIGURE_SOC_SERIES_RA4E2) || defined(CONFIGURE_SOC_SERIES_RA4W1) ||              \
	defined(CONFIGURE_SOC_SERIES_RA4T1)
#define BSP_CFG_MCU_PART_SERIES (4)
#endif

#if defined(CONFIGURE_SOC_SERIES_RA2L1) || defined(CONFIGURE_SOC_SERIES_RA2E1) ||                  \
	defined(CONFIGURE_SOC_SERIES_RA2E2) || defined(CONFIGURE_SOC_SERIES_RA2A1)
#define BSP_CFG_MCU_PART_SERIES(2)
#endif

#endif
