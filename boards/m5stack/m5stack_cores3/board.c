// SPDX-License-Identifier: Apache-2.0

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

#include <soc/soc.h>
#include <soc/spi_reg.h>
#include <soc/gpio_struct.h>
#include <soc/io_mux_reg.h>

#define AW9523_NODE DT_NODELABEL(aw9523b_gpio)

#if DT_NODE_HAS_STATUS(AW9523_NODE, okay)

LOG_MODULE_REGISTER(m5stack_cores3_board, CONFIG_LOG_DEFAULT_LEVEL);

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

static uint32_t read_io_mux_reg(uint8_t pin)
{
	switch (pin) {
	case 3:
		return REG_READ(IO_MUX_GPIO3_REG);
	case 35:
		return REG_READ(IO_MUX_GPIO35_REG);
	case 36:
		return REG_READ(IO_MUX_GPIO36_REG);
	case 37:
		return REG_READ(IO_MUX_GPIO37_REG);
	default:
		return 0;
	}
}

static void dump_spi2_state(void)
{
	uint32_t cmd = REG_READ(SPI_CMD_REG(2));
	uint32_t user = REG_READ(SPI_USER_REG(2));
	uint32_t user1 = REG_READ(SPI_USER1_REG(2));
	uint32_t user2 = REG_READ(SPI_USER2_REG(2));
	uint32_t ctrl = REG_READ(SPI_CTRL_REG(2));
	uint32_t clock = REG_READ(SPI_CLOCK_REG(2));
	uint32_t misc = REG_READ(SPI_MISC_REG(2));
	uint32_t ms_dlen = REG_READ(SPI_MS_DLEN_REG(2));
	uint32_t dma_conf = REG_READ(SPI_DMA_CONF_REG(2));

	LOG_INF("spi2: CMD=0x%08x USER=0x%08x USER1=0x%08x USER2=0x%08x",
		cmd, user, user1, user2);
	LOG_INF("spi2: CTRL=0x%08x CLOCK=0x%08x MISC=0x%08x", ctrl, clock, misc);
	LOG_INF("spi2: MS_DLEN=0x%08x DMA_CONF=0x%08x", ms_dlen, dma_conf);

	static const uint8_t pins[] = { 3, 35, 36, 37 };

	for (size_t i = 0; i < ARRAY_SIZE(pins); i++) {
		uint8_t pin = pins[i];
		uint32_t out_sel = GPIO.func_out_sel_cfg[pin].val;
		uint32_t in_sel = GPIO.func_in_sel_cfg[pin].val;
		uint32_t pin_cfg = GPIO.pin[pin].val;
		uint32_t io_mux = read_io_mux_reg(pin);

		LOG_INF("PIN%d: OUT_SEL=0x%08x IN_SEL=0x%08x PIN=0x%08x IO_MUX=0x%08x",
			pin, out_sel, in_sel, pin_cfg, io_mux);
	}
}

static int m5stack_cores3_aw9523_init(void)
{
	const struct device *aw = DEVICE_DT_GET(AW9523_NODE);

	if (!device_is_ready(aw)) {
		LOG_INF("AW9523B device not ready");
		return 0;
	}

	LOG_INF("AW9523B board setup start");
	for (size_t i = 0; i < ARRAY_SIZE(aw_init_table); i++) {
		int ret = gpio_pin_configure(aw, aw_init_table[i].pin,
					   aw_init_table[i].flags);
		if (ret < 0) {
			return ret;
		}
	}

	dump_spi2_state();
	LOG_INF("AW9523B board setup done");

	return 0;
}

SYS_INIT(m5stack_cores3_aw9523_init, POST_KERNEL,
	 CONFIG_M5STACK_CORES3_AW9523_INIT_PRIORITY);

#endif /* DT_NODE_HAS_STATUS(AW9523_NODE, okay) */
