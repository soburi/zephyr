#include <stdio.h>
#include <stdlib.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>
#include <zephyr/drivers/counter.h>

unsigned int q = 0, r = 0, s = 0;
int START = 8;

const struct gpio_dt_spec myled = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
//static const struct gpio_dt_spec button = GPIO_DT_SPEC_GET(DT_NODELABEL(user_button), gpios);

const struct gpio_dt_spec EN_1 = GPIO_DT_SPEC_GET(DT_NODELABEL(ul), gpios);
const struct gpio_dt_spec EN_2 = GPIO_DT_SPEC_GET(DT_NODELABEL(vl), gpios);
const struct gpio_dt_spec EN_3 = GPIO_DT_SPEC_GET(DT_NODELABEL(wl), gpios);
const struct gpio_dt_spec H_A = GPIO_DT_SPEC_GET(DT_NODELABEL(u_in), gpios);
const struct gpio_dt_spec H_B = GPIO_DT_SPEC_GET(DT_NODELABEL(v_in), gpios);
const struct gpio_dt_spec H_C = GPIO_DT_SPEC_GET(DT_NODELABEL(w_in), gpios);
const struct pwm_dt_spec mypwm_A = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_u));
const struct pwm_dt_spec mypwm_B = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_v));
const struct pwm_dt_spec mypwm_C = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_w));

const struct adc_dt_spec V_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user));

const struct device *uT = DEVICE_DT_GET(DT_NODELABEL(counter2));

struct gpio_callback cb_H_A;
struct gpio_callback cb_H_B;
struct gpio_callback cb_H_C;
struct gpio_callback cb_button;

// RawCAN can1(PA_11, PA_12);
//unsigned int UP, VP, WP;

float Vr_adc = 0.0f;

uint32_t top_cb_count;

static void top_callback(const struct device *dev, void *user_data);

static struct counter_top_cfg top_cfg = {
	.ticks =  60000,
	.callback = top_callback,
	.user_data = &top_cb_count,
};

int64_t ut1 = 0, ut2 = 0, usi = 0;
float Speed = 0;

char message[64];
int canerr;

uint32_t loop_count;

int dummy_speed;
/*
void send_can_msg()
{
    CANMessage msg;

    float spd = Speed;

    msg.id = 0x3d9;
    msg.len = 8U;
    msg.format = CANStandard;
    msg.type = CANData;
    msg.data[0] = 0;
    msg.data[1] = 0;
    msg.data[2] = (spd * 4) / 256;
    msg.data[3] = int(spd * 4) % 256;
    msg.data[4] = 0;
    msg.data[5] = 0;
    msg.data[6] = 0;
    msg.data[7] = 0;

    canerr = can1.write(msg);

    msg.id = 0x3e9;
    msg.len = 8U;
    msg.format = CANStandard;
    msg.type = CANData;
    msg.data[0] = (spd * 128) / 256;
    msg.data[1] = int(spd * 128) % 256;
    msg.data[2] = 0;
    msg.data[3] = 0;
    msg.data[4] = 0;
    msg.data[5] = 0;
    msg.data[6] = 0;
    msg.data[7] = 0;

    canerr = can1.write(msg);

    dummy_speed++;
}
*/

void top_callback(const struct device *dev, void *user_data)
{
	uint32_t* cnt = user_data;
	*cnt = *cnt+1;
}

void pwm_write(const struct pwm_dt_spec *dt, float ratio)
{
	pwm_set_dt(dt, dt->period, dt->period * ratio);
}

void HAH()
{
	uint32_t tick;
	s = r % 2;
	if (s == 0) {
		counter_get_value(uT, &tick);
		ut1 = top_cb_count * top_cfg.ticks + tick;
		r++;
	}

	if (s == 1) {
		counter_get_value(uT, &tick);
		ut2 = top_cb_count * top_cfg.ticks + tick;
		r++;
		counter_stop(uT);
		counter_start(uT);
	}
	pwm_write(&mypwm_A, Vr_adc);
	pwm_write(&mypwm_B, 0);
	pwm_write(&mypwm_C, 0);
}

void HAL()
{

	pwm_write(&mypwm_A, 0);
	pwm_write(&mypwm_C, 0);
}
void HBH()
{

	pwm_write(&mypwm_A, 0);
	pwm_write(&mypwm_B, Vr_adc);
	pwm_write(&mypwm_C, 0);
}
void HBL()
{

	pwm_write(&mypwm_A, 0);
	pwm_write(&mypwm_B, 0);
}
void HCH()
{

	pwm_write(&mypwm_A, 0);
	pwm_write(&mypwm_B, 0);
	pwm_write(&mypwm_C, Vr_adc);
}

void HCL()
{
	pwm_write(&mypwm_B, 0);
	pwm_write(&mypwm_C, 0);
}

void H_A_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&H_A)) {
		HCH();
	} else {
		HCL();
	}
}

void H_B_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&H_B)) {
		HAH();
	} else {
		HAL();
	}
}

void H_C_handler(const struct device *port, struct gpio_callback *cb, gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&H_C)) {
		HBH();
	} else {
		HBL();
	}
}

/*
void button_handler(const struct device *port,
					struct gpio_callback *cb,
					gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&button)) {
		printf("button_rise\n");
	} else {
		printf("button_fall\n");
	}
}
*/

float volume_read(const struct adc_dt_spec *dt)
{
	uint16_t buf;
	struct adc_sequence seq = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	(void)adc_sequence_init_dt(dt, &seq);
	adc_read_dt(dt, &seq);
	return (1.0f * buf) / BIT(seq.resolution);
}

int main()
{
	// can1.frequency(500000);
	// can1.mode(mbed::interface::can::Normal);
	gpio_pin_configure_dt(&myled, GPIO_OUTPUT);
	gpio_pin_configure_dt(&EN_1, GPIO_OUTPUT);
	gpio_pin_configure_dt(&EN_2, GPIO_OUTPUT);
	gpio_pin_configure_dt(&EN_3, GPIO_OUTPUT);

	gpio_pin_configure_dt(&H_A, GPIO_INPUT);
	gpio_pin_configure_dt(&H_B, GPIO_INPUT);
	gpio_pin_configure_dt(&H_C, GPIO_INPUT);

	gpio_pin_interrupt_configure_dt(&H_A, GPIO_INT_EDGE_BOTH);
	gpio_pin_interrupt_configure_dt(&H_B, GPIO_INT_EDGE_BOTH);
	gpio_pin_interrupt_configure_dt(&H_C, GPIO_INT_EDGE_BOTH);
	// gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);

	gpio_init_callback(&cb_H_A, H_A_handler, BIT(H_A.pin));
	gpio_init_callback(&cb_H_B, H_B_handler, BIT(H_B.pin));
	gpio_init_callback(&cb_H_C, H_C_handler, BIT(H_C.pin));
	// gpio_init_callback(&cb_button, button_handler, BIT(button.pin));

	gpio_add_callback_dt(&H_A, &cb_H_A);
	gpio_add_callback_dt(&H_B, &cb_H_B);
	gpio_add_callback_dt(&H_C, &cb_H_C);
	// gpio_add_callback_dt(&button, &cb_button);

	adc_channel_setup_dt(&V_adc);

	gpio_pin_set_dt(&EN_1, 1);
	gpio_pin_set_dt(&EN_2, 1);
	gpio_pin_set_dt(&EN_3, 1);

	counter_set_top_value(uT, &top_cfg);
	int er = counter_start(uT);

	while (1) {
		Vr_adc = volume_read(&V_adc);
		pwm_write(&mypwm_C, Vr_adc);

		// send_can_msg();

		if ((Vr_adc > 0.15f) && (q == 0)) {
			while (q < 50) {

				pwm_write(&mypwm_A, 0);
				pwm_write(&mypwm_B, 0.5f);
				pwm_write(&mypwm_C, 0);
				k_msleep(START);

				pwm_write(&mypwm_A, 0.5f);
				pwm_write(&mypwm_B, 0);
				pwm_write(&mypwm_C, 0);
				k_msleep(START);

				pwm_write(&mypwm_A, 0);
				pwm_write(&mypwm_B, 0);
				pwm_write(&mypwm_C, 0.5f);
				k_msleep(START);
				q++;
			}
		}

		//  s=0;
		if (Vr_adc < 0.1f) {
			q = 0;
		}

		usi = abs(ut2 - ut1);
		Speed = 60 * (1 / (7.0 * usi * 1E-6));
		//Speed = 60 * (1.f / (7.0f * usi * 1E-6f));
		int err = 0;
		//snprintf(message, sizeof(message), "%lld\n", counter_ticks_to_us(uT, usi));

		snprintf(message, sizeof(message), "%d.%03d , %d.%03d \r", (int)Speed, (int)(Speed * 1000.0) % 1000, 
                                                             (int)Vr_adc, (int)(Vr_adc * 1000.0) % 1000);
		//printf("%s", message);
		//printf("%d  ,%d ,%d\r" ,gpio_pin_get_dt(&H_A),gpio_pin_get_dt(&H_B),gpio_pin_get_dt(&H_C));

		gpio_pin_toggle_dt(&myled);
	}
}
