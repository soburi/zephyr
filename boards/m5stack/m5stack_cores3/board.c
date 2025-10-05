// SPDX-License-Identifier: Apache-2.0

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/sys/util.h>

#define AW9523_NODE DT_NODELABEL(aw9523b_gpio)

#if DT_NODE_HAS_STATUS(AW9523_NODE, okay)

struct aw_pin_init {
	uint8_t pin;
	gpio_flags_t flags;
};

static const struct aw_pin_init aw_init_table[] = {
	/* Port 0 */
	{ 0, GPIO_OUTPUT_HIGH },
	{ 1, GPIO_OUTPUT_HIGH },
	{ 2, GPIO_OUTPUT_HIGH },
	{ 3, GPIO_INPUT },
	{ 4, GPIO_INPUT },
	{ 5, GPIO_OUTPUT_LOW },
	{ 6, GPIO_OUTPUT_LOW },
	{ 7, GPIO_OUTPUT_LOW },
	/* Port 1 */
	{ 8, GPIO_OUTPUT_HIGH },
	{ 9, GPIO_OUTPUT_HIGH },
	{10, GPIO_INPUT },
	{11, GPIO_INPUT },
	{12, GPIO_OUTPUT_LOW },
	{13, GPIO_OUTPUT_HIGH },
	{14, GPIO_OUTPUT_LOW },
	{15, GPIO_OUTPUT_HIGH },
};

static int m5stack_cores3_aw9523_init(void)
{
	const struct device *aw = DEVICE_DT_GET(AW9523_NODE);

	if (!device_is_ready(aw)) {
		return 0;
	}

	for (size_t i = 0; i < ARRAY_SIZE(aw_init_table); i++) {
		int ret = gpio_pin_configure(aw, aw_init_table[i].pin,
					   aw_init_table[i].flags);
		if (ret < 0) {
			return ret;
		}
	}

	return 0;
}

SYS_INIT(m5stack_cores3_aw9523_init, POST_KERNEL, CONFIG_APPLICATION_INIT_PRIORITY);

#endif /* DT_NODE_HAS_STATUS(AW9523_NODE, okay) */
