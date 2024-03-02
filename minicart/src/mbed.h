#pragma once
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/counter.h>
#include <zephyr/drivers/uart.h>
#include <stdio.h>
#include <stdlib.h>

#define USBTX 0
#define USBRX 0

extern "C" {
	typedef void (*InterruptIn_fn)();

	struct InterruptIn_data {
		struct gpio_callback cb;
		const struct gpio_dt_spec *spec;
		InterruptIn_fn rise_fn;
		InterruptIn_fn fall_fn;
	};
}

class DigitalOut
{
	const struct gpio_dt_spec &dt;
public:
	DigitalOut(const struct gpio_dt_spec &pdt) : dt(pdt)
	{
		gpio_pin_configure_dt(&dt, GPIO_OUTPUT);
	}
	void operator=(int v)
	{
		gpio_pin_set_dt(&dt, v);
	}
	operator int()
	{
		return gpio_pin_get_dt(&dt);
	}
};

class DigitalIn
{
	const struct gpio_dt_spec &dt;
public:
	DigitalIn(const struct gpio_dt_spec &pdt) : dt(pdt)
	{
		gpio_pin_configure_dt(&dt, GPIO_INPUT);
	}
	bool operator==(const int &x)
	{
		return ((int)this == x);
	}
	operator int()
	{
		return gpio_pin_get_dt(&dt);
	}
};

class InterruptIn
{
	const struct gpio_dt_spec &dt;
	struct InterruptIn_data data;

	static void InterruptIn_Handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
	{
		struct InterruptIn_data *data = CONTAINER_OF(cb, struct InterruptIn_data, cb);
		if (gpio_pin_get_dt(data->spec)) {
			if (data->rise_fn) {
				data->rise_fn();
			}
		} else {
			if (data->fall_fn) {
				data->fall_fn();
			}
		}
	}
public:
	InterruptIn(const struct gpio_dt_spec &pdt) : dt(pdt)
	{
		gpio_pin_configure_dt(&dt, GPIO_INPUT);
		gpio_pin_interrupt_configure_dt(&dt, GPIO_INT_EDGE_BOTH);
		data.spec = &dt;
	       	gpio_init_callback(&data.cb, InterruptIn_Handler, BIT(dt.pin));
		gpio_add_callback_dt(&dt, &data.cb);
	}
	void rise(void (*fn)())
	{
		data.rise_fn = fn;
	}
	void fall(void (*fn)())
	{
		data.fall_fn = fn;
	}
};

class PwmOut
{
	const struct pwm_dt_spec &dt;
	int period_usec;
public:
	PwmOut(const struct pwm_dt_spec &pdt) : dt(pdt) , period_usec(20)
	{
	}
	void write(float duty)
	{
		pwm_set_dt(&dt, period_usec * 1000, period_usec * 1000 * duty);
	}
	void period_us(int us)
	{
		period_usec = us;
	}
};

class AnalogIn
{
	const struct adc_dt_spec &dt;
public:
	AnalogIn(const struct adc_dt_spec &pdt) : dt(pdt)
	{
		adc_channel_setup_dt(&pdt);
	}
	float read()
	{
		uint16_t buf;
		struct adc_sequence seq = {
			.buffer = &buf,
			.buffer_size = sizeof(buf),
		};
		(void)adc_sequence_init_dt(&dt, &seq);
		adc_read_dt(&dt, &seq);
		return (1.0f * buf) / BIT(seq.resolution);
	}
};

class Timer
{
	const struct device *dev;
	uint32_t top_cb_count;
	struct counter_top_cfg top_cfg;
	bool running;

	static void top_callback(const struct device *dev, void *user_data)
       	{
		uint32_t *cnt = static_cast<uint32_t*>(user_data);
		*cnt = *cnt + 1;
	}

public:
	Timer() : dev(DEVICE_DT_GET(DT_NODELABEL(counter2))), running(false)
	{
		top_cfg.ticks = 60000;
	       	top_cfg.callback = Timer::top_callback;
	       	top_cfg.user_data = &top_cb_count;
		counter_set_top_value(dev, &top_cfg);
	}

	void start()
	{
		running = true;
		counter_start(dev);
	}

	void stop()
	{
		running = false;
		counter_stop(dev);
	}

	void reset()
	{
		counter_stop(dev);
		counter_start(dev);
	}
	int read_us()
	{
		uint32_t tick = 0;
	       	counter_get_value(dev, &tick);
		return top_cb_count * top_cfg.ticks + tick;
	}
};

class Ticker
{
	const struct device *dev;
public:
	Ticker(const struct device *d) : dev(d)
	{
	}
	void attach_us(void (*fn)(), float)
	{
	}
};

class Serial
{
	const struct device *dev;
public:
	Serial(int tx, int rx)
	{
		if (tx == USBTX && rx == USBRX) {
			dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
		}
	}

	void baud(uint32_t baud)
	{
		struct uart_config cfg;

		if (dev) {
			uart_config_get(dev, &cfg);
			cfg.baudrate = baud;
			uart_configure(dev, &cfg);
		}
	}

	int printf(const char* fmt, ...)
	{
		va_list arg_ptr;
		int ret;

		va_start(arg_ptr, fmt);
		ret = vprintf(fmt, arg_ptr);
		va_end(arg_ptr);

		return ret;
	}
};

static inline void wait_ms(int x)
{
	k_sleep(K_MSEC(x));
}

static const struct gpio_dt_spec PC_10 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 10,
	.dt_flags = GPIO_ACTIVE_HIGH,
};

static const struct gpio_dt_spec PC_11 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 11,
	.dt_flags = GPIO_ACTIVE_HIGH,
};

static const struct gpio_dt_spec PC_12 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 12,
	.dt_flags = GPIO_ACTIVE_HIGH,
};

static const struct gpio_dt_spec PA_15 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioa)),
	.pin = 15,
	.dt_flags = (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN),
};

static const struct gpio_dt_spec PB_3 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 3,
	.dt_flags = (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN),
};

static const struct gpio_dt_spec PB_10 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 10,
	.dt_flags = (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN),
};

static const struct gpio_dt_spec LED1 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpiob)),
	.pin = 2,
	.dt_flags = (GPIO_ACTIVE_HIGH | GPIO_PULL_DOWN),
};

static const struct adc_dt_spec PC_2 = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

static const struct pwm_dt_spec PA_8 = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_u));
static const struct pwm_dt_spec PA_9 = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_v));
static const struct pwm_dt_spec PA_10 = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_w));
