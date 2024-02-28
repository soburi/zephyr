#include <stdio.h>
#include <stdlib.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/pwm.h>

unsigned int q = 0, r = 0, s = 0;
int START = 8;
static const struct gpio_dt_spec LED0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec EN_1 = GPIO_DT_SPEC_GET(DT_NODELABEL(ul), gpios);
static const struct gpio_dt_spec EN_2 = GPIO_DT_SPEC_GET(DT_NODELABEL(vl), gpios);
static const struct gpio_dt_spec EN_3 = GPIO_DT_SPEC_GET(DT_NODELABEL(wl), gpios);
static const struct gpio_dt_spec H_A = GPIO_DT_SPEC_GET(DT_NODELABEL(u_in), gpios);
static const struct gpio_dt_spec H_B = GPIO_DT_SPEC_GET(DT_NODELABEL(v_in), gpios);
static const struct gpio_dt_spec H_C = GPIO_DT_SPEC_GET(DT_NODELABEL(w_in), gpios);

static const struct adc_dt_spec V_adc = ADC_DT_SPEC_GET(DT_PATH(zephyr_user)); 

static const struct pwm_dt_spec mypwm_A = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_u));
static const struct pwm_dt_spec mypwm_B = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_v));
static const struct pwm_dt_spec mypwm_C = PWM_DT_SPEC_GET(DT_NODELABEL(pwm_w));

struct gpio_callback cb_H_A;
struct gpio_callback cb_H_B;
struct gpio_callback cb_H_C;

//RawCAN can1(PA_11, PA_12);

unsigned int UP, VP, WP;
//AnalogIn V_adc(PC_2); // gaibu Volume
// AnalogIn V_adc(PB_1);   //Volume

//BufferedSerial pc(USBTX, USBRX);

float Vr_adc = 0.0f;

//Timer uT;
int64_t ut0 = 0, ut1 = 0, ut2 = 0;
float Speed = 0;

char message[64];
int canerr;

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

int64_t delta_T()
{
	int64_t cur  = k_uptime_ticks();
	if (ut0 > cur) {
		return (INT64_MAX - ut0) + cur;
	}

	return cur - ut0;
}

void reset_T()
{
	ut0 = k_uptime_ticks();
}

void HAH() {

  s = r % 2;
  if (s == 0) {
    ut1 = delta_T();
    r++;
  }

  if (s == 1) {
    ut2 = delta_T();
    r++;
    reset_T();
  }
  pwm_set_dt(&mypwm_A, 20000, 20000 * Vr_adc);
  pwm_set_dt(&mypwm_B, 20000, 0);
  pwm_set_dt(&mypwm_C, 20000, 0);
}

void HAL() {

  pwm_set_dt(&mypwm_A, 20000, 0);
  pwm_set_dt(&mypwm_C, 20000, 0);
}
void HBH() {

  pwm_set_dt(&mypwm_A, 20000, 0);
  pwm_set_dt(&mypwm_B, 20000, 20000 * Vr_adc);
  pwm_set_dt(&mypwm_C, 20000, 0);
}
void HBL() {

  pwm_set_dt(&mypwm_A, 20000, 0);
  pwm_set_dt(&mypwm_B, 20000, 0);
}
void HCH() {

  pwm_set_dt(&mypwm_A, 20000, 0);
  pwm_set_dt(&mypwm_B, 20000, 0);
  pwm_set_dt(&mypwm_C, 20000, 20000 * Vr_adc);
}

void HCL() {
  pwm_set_dt(&mypwm_B, 20000, 0);
  pwm_set_dt(&mypwm_C, 20000, 0);
}

uint32_t loop_count;

void H_A_handler(const struct device *port,
                                        struct gpio_callback *cb,
                                        gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&H_A)) {
		HCH();
	} else {
		HCL();
	}
}

void H_B_handler(const struct device *port,
                                        struct gpio_callback *cb,
                                        gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&H_B)) {
		HAH();
	} else {
		HAL();
	}
}

void H_C_handler(const struct device *port,
                                        struct gpio_callback *cb,
                                        gpio_port_pins_t pins)
{
	if (gpio_pin_get_dt(&H_C)) {
		HBH();
	} else {
		HBL();
	}
}

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

int main() {
  //pc.set_baud(9600);

  //can1.frequency(500000);
  //can1.mode(mbed::interface::can::Normal);
  gpio_pin_configure_dt(&LED0, GPIO_OUTPUT);

  gpio_pin_configure_dt(&EN_1, GPIO_OUTPUT);
  gpio_pin_configure_dt(&EN_2, GPIO_OUTPUT);
  gpio_pin_configure_dt(&EN_3, GPIO_OUTPUT);

  gpio_pin_configure_dt(&H_A, GPIO_INPUT);
  gpio_pin_configure_dt(&H_B, GPIO_INPUT);
  gpio_pin_configure_dt(&H_C, GPIO_INPUT);

  gpio_pin_interrupt_configure_dt(&H_A, GPIO_INT_EDGE_BOTH);
  gpio_pin_interrupt_configure_dt(&H_B, GPIO_INT_EDGE_BOTH);
  gpio_pin_interrupt_configure_dt(&H_C, GPIO_INT_EDGE_BOTH);

  gpio_init_callback(&cb_H_A, H_A_handler, BIT(H_A.pin));
  gpio_init_callback(&cb_H_B, H_B_handler, BIT(H_B.pin));
  gpio_init_callback(&cb_H_C, H_B_handler, BIT(H_C.pin));

  //gpio_add_callback_dt(&H_A, &cb_H_A);
  //gpio_add_callback_dt(&H_B, &cb_H_B);
  //gpio_add_callback_dt(&H_C, &cb_H_C);

  adc_channel_setup_dt(&V_adc);

  gpio_pin_set_dt(&EN_1, 1);
  gpio_pin_set_dt(&EN_2, 1);
  gpio_pin_set_dt(&EN_3, 1);

  reset_T();

	pwm_set_dt(&mypwm_A, 20000000, 10000000);
	pwm_set_dt(&mypwm_B, 20000000, 10000000);
	pwm_set_dt(&mypwm_C, 20000000, 10000000);

  while (1) {
#if 0
    Vr_adc = volume_read(&V_adc);

    if (loop_count == 1) {
      //send_can_msg();
      loop_count = 0;
    } else {
      loop_count++;
    }

    if ((Vr_adc > 0.15f) && (q == 0)) {
      while (q < 50) {

	pwm_set_dt(&mypwm_A, 20000, 0);
	pwm_set_dt(&mypwm_B, 20000, 10000);
	pwm_set_dt(&mypwm_C, 20000, 0);
	k_msleep(START);

	pwm_set_dt(&mypwm_A, 20000, 10000);
	pwm_set_dt(&mypwm_B, 20000, 0);
	pwm_set_dt(&mypwm_C, 20000, 0);
	k_msleep(START);

	pwm_set_dt(&mypwm_A, 20000, 0);
	pwm_set_dt(&mypwm_B, 20000, 0);
	pwm_set_dt(&mypwm_C, 20000, 10000);
	k_msleep(START);
        q++;
      }
    }

    //  s=0;
    if (Vr_adc < 0.1f) {
      q = 0;
    }

    Speed = 60 * (1.f / (7.0f * k_cyc_to_us_floor64(abs(ut2 - ut1)) * 1E-6f));
    printf("%d.%03d , %d.%03d \r", (int)Speed, (int)(Speed * 1000.0f) % 1000, 
                                                             (int)Vr_adc, (int)(Vr_adc * 1000.0f) % 1000);
    //pc.write(message, sizeof(message));
    // UP=HA; VP=HB; WP=HC;
    // pc.printf("%d  ,%d ,%d\r" ,UP,VP,WP);
    //myled = !myled;
#endif
    gpio_pin_toggle_dt(&LED0);
  }
}

