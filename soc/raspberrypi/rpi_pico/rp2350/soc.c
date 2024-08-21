/*
 * Copyright (c) 2024 Andrew Featherstone
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @brief System/hardware module for Raspberry Pi RP235xx MCUs
 *
 * This module provides routines to initialize and support board-level hardware
 * for the Raspberry Pi RP235xx (RP2350A, RP2350B, RP2354A, RP2354B).
 */

#if CONFIG_PLATFORM_SPECIFIC_INIT
#include <pico/runtime_init.h>


void z_arm_platform_init(void)
{
	runtime_init_per_core_enable_coprocessors();
}

#endif /* CONFIG_PLATFORM_SPECIFIC_INIT */
