# Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
# SPDX-License-Identifier: Apache-2.0

if (CONFIG_SOC_FAMILY_SMARTBOND)

zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/sdk/bsp/include)
zephyr_include_directories(${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/da1469x_hal)

zephyr_library_sources(
  ${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/da1469x_hal/da1469x_clock.c
  ${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/da1469x_hal/da1469x_otp.c
  ${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/da1469x_hal/da1469x_pd.c
  ${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/da1469x_hal/da1469x_pdc.c
  ${ZEPHYR_HAL_RENESAS_MODULE_DIR}/smartbond/da1469x_hal/da1469x_trimv.c
)

endif()
