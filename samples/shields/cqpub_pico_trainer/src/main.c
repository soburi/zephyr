/*
 * Copyright (c) 2020 Libre Solar Technologies GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/storage/disk_access.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/fs/fs.h>
#include <ff.h>
#include <drivers/sensor.h>

#include <zephyr/zephyr.h>
#include <zephyr/device.h>
#include <zephyr/drivers/display.h>

static struct gpio_callback button_cb_data;
static struct gpio_callback irrecv_cb_data;
static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_ALIAS(sw0), gpios);
static const struct gpio_dt_spec irrecv = GPIO_DT_SPEC_GET(DT_ALIAS(irrecv0), gpios);
static struct gpio_dt_spec irled = GPIO_DT_SPEC_GET(DT_ALIAS(irled0), gpios);

static const uint32_t adc_channels[] = { 0, 1, 2 };
static const uint32_t gpio_pins[] = { 25, 6, 7, 8, 9 };

static uint32_t received = 0;


void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	printk("Button pressed at %" PRIu32 "\n", k_cycle_get_32());
}

void irrecv_received(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	gpio_port_value_t val;
	gpio_port_get(dev, &val);
	gpio_pin_set_dt(&irled, (val & pins) ? true : false );
	received++;
}

void main(void)
{
	const struct device* gpio_dev = DEVICE_DT_GET(DT_NODELABEL(gpio0));
	const struct device* adc_dev = DEVICE_DT_GET(DT_NODELABEL(adc));
	const struct device *accel_dev = NULL;
#if DT_NODE_HAS_STATUS(DT_ALIAS(accel0), okay)
	accel_dev = DEVICE_DT_GET(DT_ALIAS(accel0));
#endif

	int ret = 0;
	int16_t sample_buffer[1];
	struct adc_sequence sequence = {
		.buffer      = sample_buffer,
		/* buffer size in bytes, not number of samples */
		.buffer_size = sizeof(sample_buffer),
	};

	/* Configure channels individually prior to sampling. */
	for (uint8_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
		if (!device_is_ready(adc_dev)) {
			printk("ADC device not found\n");
			return;
		}

		struct adc_channel_cfg channel_cfg = {
			.channel_id       = adc_channels[i],
			.gain             = ADC_GAIN_1,
			.reference        = ADC_REF_INTERNAL,
			.acquisition_time = ADC_ACQ_TIME_DEFAULT,
		};

		adc_channel_setup(adc_dev, &channel_cfg);
	}

//Button

	if (!device_is_ready(button.port)) {
		printk("Error: button device %s is not ready\n", button.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n", ret, button.port->name, button.pin);
		return;
	}

	ret = gpio_pin_interrupt_configure_dt(&button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button.port->name, button.pin);
		return;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	gpio_add_callback(button.port, &button_cb_data);
	printk("Set up button at %s pin %d\n", button.port->name, button.pin);

//IR recv

        if (!device_is_ready(irrecv.port)) {
                printk("Error: irrecv device %s is not ready\n",
                       irrecv.port->name);
                return;
        }

        ret = gpio_pin_configure_dt(&irrecv, GPIO_INPUT);
        if (ret != 0) {
                printk("Error %d: failed to configure %s pin %d\n",
                       ret, irrecv.port->name, irrecv.pin);
                return;
        }

        ret = gpio_pin_interrupt_configure_dt(&irrecv, GPIO_INT_EDGE_BOTH);
        if (ret != 0) {
                printk("Error %d: failed to configure interrupt on %s pin %d\n",
                        ret, irrecv.port->name, irrecv.pin);
                return;
        }

        gpio_init_callback(&irrecv_cb_data, irrecv_received, BIT(irrecv.pin));
        gpio_add_callback(irrecv.port, &irrecv_cb_data);
        printk("Set up irrecv at %s pin %d\n", irrecv.port->name, irrecv.pin);

//IR LED

	if (!device_is_ready(irled.port)) {
		printk("Error %d: IR_LED device %s is not readyt\n", ret, irled.port->name);
		return;
	}

	ret = gpio_pin_configure_dt(&irled, GPIO_OUTPUT_INACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure IR_LED device %s pin %d\n",
		       ret, irled.port->name, irled.pin);
		return;
	}

// GPIO
	if (!device_is_ready(gpio_dev)) {
		printk("Error: GPIO device %s is not ready\n", gpio_dev->name);
		return;
	}

	for(uint32_t i=0; i<ARRAY_SIZE(gpio_pins);i++) {
		ret = gpio_pin_configure(gpio_dev, gpio_pins[i], GPIO_OUTPUT);
		if (ret != 0) {
			printk("Error %d: failed to configure LED device %s pin %d\n",
			       ret, gpio_dev->name, gpio_pins[i]);
			return;
		}
	}

#if IS_ENABLED(CONFIG_DISPLAY)
	const struct device *display_dev;
	struct display_capabilities capabilities;
	struct display_buffer_descriptor buf_desc;
	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		printk("Device %s not found. Aborting sample.\n",
			display_dev->name);
		return;//RETURN_FROM_MAIN(1);
	}

	printk("Display sample for %s\n", display_dev->name);
	display_get_capabilities(display_dev, &capabilities);

	buf_desc.buf_size = 64;
	buf_desc.pitch = 128;
	buf_desc.width = 8;
	buf_desc.height = 8;
	
	uint8_t box_8x8w[64] = { 0 };
	uint8_t box_8x8b[64] = { 1 };

	display_blanking_on(display_dev);

	for (int y = 0; y < capabilities.y_resolution; y+=8) {
		for (int x = 0; x < capabilities.y_resolution; x+=8) {
			if( x%2 ) {
				display_write(display_dev, x, y, &buf_desc, box_8x8w);
			}
			else {
				display_write(display_dev, x, y, &buf_desc, box_8x8b);

			}
		}
	}

	display_blanking_off(display_dev);
#endif

#if IS_ENABLED(CONFIG_DISK_ACCESS)
	/* raw disk i/o */
	do {
		static const char *disk_pdrv = "SD";
		uint64_t memory_size_mb;
		uint32_t block_count;
		uint32_t block_size;

		if (disk_access_init(disk_pdrv) != 0) {
			printk("Storage init ERROR!\n");
			break;
		}

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_GET_SECTOR_COUNT, &block_count)) {
			printk("Unable to get sector count\n");
			break;
		}
		printk("Block count %u\n", block_count);

		if (disk_access_ioctl(disk_pdrv,
				DISK_IOCTL_GET_SECTOR_SIZE, &block_size)) {
			printk("Unable to get sector size\n");
			break;
		}
		printk("Sector size %u\n", block_size);

		memory_size_mb = (uint64_t)block_count * block_size;
		printk("Memory Size(MB) %u\n", (uint32_t)(memory_size_mb >> 20));
	} while (0);
#endif

	int64_t time_stamp = k_uptime_get();
	while (1) {
		for(uint32_t i=0; i<ARRAY_SIZE(gpio_pins);i++) {
			/* If we have an LED, match its state to the button's. */
			int val = gpio_pin_get_dt(&button);

			if (val >= 0) {
				gpio_pin_set(gpio_dev, gpio_pins[i], val);
			}
			//k_msleep(1);
			k_yield();
		}

		int64_t now = k_uptime_get();
		if ((now - time_stamp) > 1000) {
			time_stamp = now;
			printk("ADC: ");
			for (uint8_t i = 0; i < ARRAY_SIZE(adc_channels); i++) {
				sequence.channels     = BIT(adc_channels[i]);
				sequence.resolution   = 12;
				sequence.oversampling = 0;

				ret = adc_read(adc_dev, &sequence);
				if (ret < 0) {
					printk("error:%d  ", ret);
				} else {
					printk("%d  ", sample_buffer[0]);
				}
			}
			printk("\n");

			if(received != 0) {
				printk("IR received %d\n", received);
				received = 0;
			}

			if(accel_dev) {
				struct sensor_value accel[3];
				static const enum sensor_channel channels[] = {
					SENSOR_CHAN_ACCEL_X,
					SENSOR_CHAN_ACCEL_Y,
					SENSOR_CHAN_ACCEL_Z,
				};

				ret = sensor_sample_fetch(accel_dev);
				if (ret < 0) {
					printk("sensor_Sample_fetch() failed: %d\n", ret);
					return;
				}

				for (size_t i = 0; i < ARRAY_SIZE(channels); i++) {
					ret = sensor_channel_get(accel_dev, channels[i], &accel[i]);
					if (ret < 0) {
						printk("sensor_channel_get(%d) failed: %d\n", channels[i], ret);
						return;
					}
				}

				printk("accel x:%d.%06d y:%d.%06d z:%d.%06d (m/s^2)\n",
				       accel[0].val1, accel[0].val2,
				       accel[1].val1, accel[1].val2,
				       accel[2].val1, accel[2].val2);
			}
		}
	}
}
