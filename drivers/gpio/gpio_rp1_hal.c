#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>

#define DEV_CFG(port)  ((const struct gpio_rpi_config *)(port)->config)
#define DEV_DATA(port) ((struct gpio_rpi_data *)(port)->data)

/* pico-sdk includes */
#include "gpio_rp1_hal.h"

struct gpio_rpi_config {
	struct gpio_driver_config common;
	void (*bank_config_func)(void);
#if GPIO_RPI_HI_AVAILABLE
	const struct device *high_dev;
#endif

	DEVICE_MMIO_NAMED_ROM(gpio);
	DEVICE_MMIO_NAMED_ROM(rio);
	DEVICE_MMIO_NAMED_ROM(pads);

	uint8_t ngpios;
};

struct gpio_rpi_data {
	struct gpio_driver_data common;
	sys_slist_t callbacks;
	uint32_t single_ended_mask;
	uint32_t open_drain_mask;
	DEVICE_MMIO_NAMED_RAM(gpio);
	DEVICE_MMIO_NAMED_RAM(rio);
	DEVICE_MMIO_NAMED_RAM(pads);
};

bool gpio_get_out_level(uint32_t pin)
{
	const struct device *port;
	return !!(RIO_OUT(port) & BIT(pin));
}

bool gpio_get_dir(uint32_t pin)
{
	const struct device *port;
	return !!(RIO_OE(port) & BIT(pin));
}

void gpio_set_mask_n(uint32_t n, uint32_t mask)
{
	const struct device *port;
	RIO_OUT_SET(port, mask);
}

void gpio_clr_mask_n(uint32_t n, uint32_t mask)
{
	const struct device *port;
	RIO_OUT_CLR(port, mask);
}

void gpio_xor_mask_n(uint32_t n, uint32_t mask)
{
	const struct device *port;
	RIO_OUT_XOR(port, mask);
}

void gpio_put_masked_n(uint32_t n, uint32_t mask, uint32_t value)
{
	const struct device *port;
	RIO_OUT_XOR(port, (RIO_OUT(port) ^ value) & mask);
}

void gpio_put(uint32_t pin, bool value)
{
	const struct device *port;
	if (value) {
		RIO_OUT_SET(port, pin);
	} else {
		RIO_OUT_CLR(port, pin);
	}
}

void gpio_set_dir(uint32_t pin, bool value)
{
	const struct device *port;
	if (value == 1) { // GPIO_OUT) {
		RIO_OE_SET(port, BIT(pin));
		PADS_CTRL_CLR(port, pin, PADS_OUTPUT_DISABLE | PADS_INPUT_ENABLE);
	} else {
		RIO_OE_CLR(port, BIT(pin));
		PADS_CTRL_SET(port, pin, PADS_OUTPUT_DISABLE | PADS_INPUT_ENABLE);
	}
}

int gpio_is_pulled_up(uint32_t pin)
{
	const struct device *dev;
	return (sys_read32(PADS_CTRL(dev, pin) & GPIO_PADS_PULL_UP_ENABLE_MASK)) != 0;
}

int gpio_is_pulled_down(uint32_t pin)
{
	const struct device *dev;

	return (sys_read32(PADS_CTRL(dev, pin) & GPIO_PADS_PULL_DOWN_ENABLE_MASK)) != 0;
}

int gpio_set_irq_enabled(uint32_t pin, uint32_t events, bool value)
{
	const struct device *dev;

	if (value) {
		GPIO_INTR_RAW(dev, BIT(pin));
		GPIO_INTE_SET(dev, BIT(pin));
		GPIO_CTRL_SET(dev, pin, events);
	} else {
		GPIO_INTR_RAW(dev, BIT(pin));
		GPIO_INTE_CLR(dev, BIT(pin));
		GPIO_CTRL_CLR(dev, pin, ALL_EVENTS);
	}

	return 0;
}

void gpio_set_input_enabled(uint32_t pin, bool enabled)
{
	const struct device *port;
	if (enabled) {
		PADS_CTRL_SET(port, pin, PADS_INPUT_ENABLE);
	} else {
		PADS_CTRL_CLR(port, pin, PADS_INPUT_ENABLE);
	}
}

void gpio_set_pulls(uint32_t pin, bool up, bool down)
{
	const struct device *port;
	PADS_CTRL_XOR(port, pin,
		      (PADS_CTRL(port, pin) ^ ((up << GPIO_PADS_PULL_UP_ENABLE_SHIFT) |
					       (down << GPIO_PADS_PULL_DOWN_ENABLE_SHIFT))) &
			      (GPIO_PADS_PULL_UP_ENABLE_MASK | GPIO_PADS_PULL_DOWN_ENABLE_MASK));
}

void gpio_set_input_enabled_output_disabled(uint32_t pin, bool ie, bool od)
{
	const struct device *port;
	PADS_CTRL_XOR(port, pin,
		      (PADS_CTRL(port, pin) ^ ((ie << GPIO_PADS_INPUT_ENABLE_SHIFT) |
					       (od << GPIO_PADS_OUTPUT_DISABLE_SHIFT))) &
			      (GPIO_PADS_INPUT_ENABLE_MASK | GPIO_PADS_OUTPUT_DISABLE_MASK));
}

void gpio_set_function(uint32_t gpio, uint32_t fn)
{
	//	const uint32_t value = PADS_BANK0_GPIO0_IE_BITS & fn <<
	// IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB; 	const uint32_t mask =
	//                  PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS |
	//                  IO_BANK0_GPIO0_CTRL_FUNCSEL_MASK;
	//	GPIO_CTRL_XOR(port, (GPIO_CTRL(port) ^ value) & masks);
}

void gpio_set_dir_out_masked_n(uint32_t n, uint32_t mask)
{
	const struct device *port;
	struct gpio_rpi_data *data = port->data;

	RIO_OE_SET(port, mask & data->single_ended_mask & data->open_drain_mask);
}

void gpio_set_dir_in_masked_n(uint32_t n, uint32_t mask)
{
	const struct device *port;
	RIO_OE_CLR(port, mask);
}

void gpio_set_dir_masked_n(uint32_t n, uint32_t mask, uint32_t value)
{
	const struct device *port;
	RIO_OE_XOR(port, (RIO_OE(port) ^ value) & mask);
}

uint32_t gpio_get_all_n(uint32_t n)
{
	const struct device *port;
	return sys_read32(RIO_IN(port));
}

void gpio_toggle_dir_masked_n(uint32_t n, uint32_t mask)
{
	const struct device *port;
	RIO_OE_XOR(port, mask);
}

uint32_t gpio_get_dir_all_bits_n(uint32_t n)
{
	const struct device *port;
	return sys_read32(RIO_OE(port));
}

bool gpio_is_input_enabled(uint32_t pin)
{
	const struct device *port;
	return !!(sys_read32(PADS_CTRL(port, pin)) & GPIO_PADS_INPUT_ENABLE_MASK);
}

bool gpio_is_output_disabled(uint32_t pin)
{
	const struct device *port;
	return !!(sys_read32(PADS_CTRL(port, pin)) & GPIO_PADS_OUTPUT_DISABLE_MASK);
}

void gpio_acknowledge_irq(uint32_t pin, uint32_t event_mask)
{
	const struct device *port;
	GPIO_CTRL_CLR(port, pin, GPIO_CTRL_IRQRESET_MASK);
}

uint32_t gpio_get_irq_event_mask(uint32_t pin)
{
	const struct device *port;
	if (GPIO_INTR(port) & BIT(pin)) {
		return GPIO_CTRL(port, pin) & ALL_EVENTS >> GPIO_CTRL_IRQMASK_EDGE_LOW_SHIFT;
	}

	return 0;
}

bool gpio_has_pending_irq()
{
	const struct device *port;

	return !!GPIO_INTS(port);
}
