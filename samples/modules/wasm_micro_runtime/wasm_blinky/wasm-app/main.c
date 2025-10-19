/*
 * Copyright (c) 2020 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include "wit-bindgen/blink.h"

void exports_blink_run(void)
{
        int32_t ret;

        ret = zephyr_led_driver_configure();
        if (ret != 0) {
                return;
        }

        for (int i = 0; i < 10; ++i) {
                zephyr_led_driver_set(true);
                zephyr_led_driver_sleep_ms(250);
                zephyr_led_driver_set(false);
                zephyr_led_driver_sleep_ms(250);
        }
}

int main(void)
{
        exports_blink_run();

        return 0;
}
