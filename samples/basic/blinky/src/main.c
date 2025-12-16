/*
 * Copyright (c) 2016 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>
#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/uart.h>

/* 1000 msec = 1 sec */
#define SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/*
 * A build error on this line means your board is unsupported.
 * See the sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

void printx(const struct device* dev, char* msg, ...);

#include <zephyr/kernel.h>
#include <zephyr/sys/sys_io.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, CONFIG_UART_LOG_LEVEL);

#define CLK_UART_CTRL     0x00054
#define CLK_UART_DIV_INT  0x00058
#define CLK_UART_SEL      0x00060

static void dump_rp1_uartclk(uintptr_t clocks_base)
{
    uint32_t ctrl = sys_read32(clocks_base + CLK_UART_CTRL);
    uint32_t div  = sys_read32(clocks_base + CLK_UART_DIV_INT);
    uint32_t sel  = sys_read32(clocks_base + CLK_UART_SEL);

    printk("RP1 clocks_main: CTRL=0x%08x DIV_INT=%u SEL=0x%08x\n", ctrl, div, sel);
}

int main(void)
{
	int ret;
	bool led_state = true;

	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(rp1_uart0));
	const struct device *dev2 = DEVICE_DT_GET(DT_NODELABEL(uart10));

	printx(dev, "rp1_uart0x\n");
	printx(dev2, "uart10\n");
	if (dev) {
		printk("rp1_uart0 ready=%d name=%s\n", device_is_ready(dev), dev->name);
	}

	mm_reg_t virt_addr = 0;

	device_map(&virt_addr, 0x1f00018000, 0x100, 0);

	LOG_HEXDUMP_DBG((void*)virt_addr, 0x100, "clock");




	if (!gpio_is_ready_dt(&led)) {
		return 0;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return 0;
	}

	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		if (ret < 0) {
			return 0;
		}

		led_state = !led_state;
		printf("LED state: %s\n", led_state ? "ON" : "OFF");
		k_msleep(SLEEP_TIME_MS);

		uart_poll_out(dev, led_state ? '1' : '0');
	}
	return 0;
}
