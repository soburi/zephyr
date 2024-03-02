#pragma once
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>

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
public:
	InterruptIn(const struct gpio_dt_spec &pdt) : dt(pdt)
	{
	}
	void rise(void (*fn)())
	{
	}
	void fall(void (*fn)())
	{
	}
};
class PwmOut
{
	const struct pwm_dt_spec &dt;
	int period_usec;
public:
	PwmOut(const struct pwm_dt_spec &pdt) : dt(pdt)
	{
	}
	void write(float duty)
	{
		pwm_set_dt(dt, period_usec * 1000, period_usec * 1000 * duty);
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
	}
	float read()
	{
		uint16_t buf;
		struct adc_sequence seq = {
			.buffer = &buf,
			/* buffer size in bytes, not number of samples */
			.buffer_size = sizeof(buf),
		};
		(void)adc_sequence_init_dt(&dt, &seq);
		adc_read_dt(&dt, &seq);
		return 1.0f * buf / (2 << seq.resolution);
	}
};
class Timer
{
	const struct device *dev;
public:
	Timer(const struct device *d) : dev(d)
	{
	}
	void start()
	{
	}
	int read_us()
	{
		return 0;
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

static inline void wait_ms(int x)
{
	k_sleep(K_MSEC(x));
}

const struct gpio_dt_spec EN_1 = GPIO_DT_SPEC_GET(DT_NODELABEL(ul), gpios);
const struct gpio_dt_spec EN_2 = GPIO_DT_SPEC_GET(DT_NODELABEL(vl), gpios);
const struct gpio_dt_spec EN_3 = GPIO_DT_SPEC_GET(DT_NODELABEL(wl), gpios);


static const struct gpio_dt_spec PC_10 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 10,
	.flags = GPIO_ACTIVE_HIGH,
};

static const struct gpio_dt_spec PC_11 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 11,
	.flags = GPIO_ACTIVE_HIGH,
};

static const struct gpio_dt_spec PC_12 = { 
	.port = DEVICE_DT_GET(DT_NODELABEL(gpioc)),
	.pin = 12,
	.flags = GPIO_ACTIVE_HIGH,
};


static const struct gpio_dt_spec PA_7 = GPIO_DT_SPEC_GET(DT_NODELABEL(ul), gpios);
static const struct gpio_dt_spec PB_0 = GPIO_DT_SPEC_GET(DT_NODELABEL(vl), gpios);
static const struct gpio_dt_spec PB_1 = GPIO_DT_SPEC_GET(DT_NODELABEL(wl), gpios);
static const struct gpio_dt_spec PA_15 = GPIO_DT_SPEC_GET(DT_NODELABEL(u_in), gpios);
static const struct gpio_dt_spec PB_3 = GPIO_DT_SPEC_GET(DT_NODELABEL(v_in), gpios);
static const struct gpio_dt_spec PB_10 = GPIO_DT_SPEC_GET(DT_NODELABEL(w_in), gpios);
static const struct gpio_dt_spec PB_8 = GPIO_DT_SPEC_GET(DT_NODELABEL(direction), gpios);
static const struct gpio_dt_spec LED1 = GPIO_DT_SPEC_GET(DT_NODELABEL(direction), gpios);
static const struct adc_dt_spec PA_0 = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct adc_dt_spec PC_1 = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct adc_dt_spec PC_0 = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct adc_dt_spec PC_2 = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));
static const struct pwm_dt_spec PA_8 = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_u));
static const struct pwm_dt_spec PA_9 = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_v));
static const struct pwm_dt_spec PA_10 = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_w));
