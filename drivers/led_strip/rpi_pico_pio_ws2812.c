/*
 * Copyright (c) 2023 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <zephyr/sys_clock.h>
#include <zephyr/kernel.h>

#define DT_DRV_COMPAT raspberrypi_pico_pio_ws2812

#define LOG_LEVEL CONFIG_LED_STRIP_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(rpi_pico_pio_ws2812);

struct rpi_pico_pio_ws2812_config {
	const struct device *piodev;
	const struct pinctrl_dev_config *const pcfg;
	struct pio_program program;
};

static int rpi_pico_pio_ws2812_init(const struct device *dev)
{
	const struct rpi_pico_pio_ws2812_config *config = dev->config;
	PIO pio;

	if (!device_is_ready(config->piodev)) {
		LOG_ERR("%s: PIO device not ready", dev->name);
		return -ENODEV;
	}

	pio = pio_rpi_pico_get_pio(config->piodev);

	pio_add_program(pio, &config->program);

	return pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
}

#define SET_DELAY(op, inst, i)                                                                     \
	(op | (((DT_INST_PROP_BY_IDX(inst, bit_waveform, i) - 1) & 0xF) << 8))

/*
 * This pio program runs [T0+T1+T2] cycles per 1 loop.
 * The first `out` instruction outputs 0 by [T2] times to the sideset pin.
 * These zeros are padding. Here is the start of actual data transmission.
 * The second `jmp` instruction output 1 by [T0] times to the sideset pin.
 * This `jmp` instruction jumps to line 3 if the value of register x is true.
 * Otherwise, jump to line 4.
 * The third `jmp` instruction outputs 1 by [T1] times to the sideset pin.
 * After output, return to the first line.
 * The fourth `jmp` instruction outputs 0 by [T1] times.
 * After output, return to the first line and output 0 by [T2] times.
 *
 * In the case of configuration, T0=3, T1=3, T2 =4,
 * the final output is 1110000000 in case register x is false.
 * It represents code 0, defined in the datasheet.
 * And outputs 1111110000 in case of x is true. It represents code 1.
 */
#define RASPBERRYPI_PICO_PIO_WS2812INIT(inst)                                                      \
	PINCTRL_DT_INST_DEFINE(inst);                                                              \
                                                                                                   \
	static const uint16_t rpi_pico_pio_ws2812_instructions_##inst[] = {                        \
		SET_DELAY(0x6021, inst, 2), /*  0: out    x, 1  side 0 [T2 - 1]  */                \
		SET_DELAY(0x1023, inst, 0), /*  1: jmp    !x, 3 side 1 [T0 - 1]  */                \
		SET_DELAY(0x1000, inst, 1), /*  2: jmp    0     side 1 [T1 - 1]  */                \
		SET_DELAY(0x0000, inst, 1), /*  3: jmp    0     side 0 [T1 - 1]  */                \
	};                                                                                         \
                                                                                                   \
	static const struct rpi_pico_pio_ws2812_config rpi_pico_pio_ws2812_##inst##_config = {     \
		.piodev = DEVICE_DT_GET(DT_INST_PARENT(inst)),                                     \
		.pcfg = PINCTRL_DT_INST_DEV_CONFIG_GET(inst),                                      \
		.program =                                                                         \
			{                                                                          \
				.instructions = rpi_pico_pio_ws2812_instructions_##inst,           \
				.length = ARRAY_SIZE(rpi_pico_pio_ws2812_instructions_##inst),     \
				.origin = -1,                                                      \
			},                                                                         \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(inst, &rpi_pico_pio_ws2812_init, NULL, NULL,                         \
			      &rpi_pico_pio_ws2812_##inst##_config, POST_KERNEL,                   \
			      CONFIG_LED_STRIP_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(RASPBERRYPI_PICO_PIO_WS2812INIT)
