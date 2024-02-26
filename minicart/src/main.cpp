#include "mbed.h"
#include <math.h>


unsigned int q = 0, r = 0, s = 0;
int START = 8;
static const struct gpio_dt_spec LED0 = GPIO_DT_SPEC_GET(DT_ALIAS(led0), gpios);
static const struct gpio_dt_spec EN_1 = GPIO_DT_SPEC_GET(DT_NODELABEL(ul), gpios);
static const struct gpio_dt_spec EN_2 = GPIO_DT_SPEC_GET(DT_NODELABEL(vl), gpios);
static const struct gpio_dt_spec EN_3 = GPIO_DT_SPEC_GET(DT_NODELABEL(wl), gpios);
static const struct gpio_dt_spec H_A = GPIO_DT_SPEC_GET(DT_NODELABEL(u_in), gpios);
static const struct gpio_dt_spec H_B = GPIO_DT_SPEC_GET(DT_NODELABEL(v_in), gpios);
static const struct gpio_dt_spec H_C = GPIO_DT_SPEC_GET(DT_NODELABEL(w_in), gpios);

static const struct adc_dt_spec V_adc = PA_0 = ADC_DT_SPEC_GET(DT_PATH(zephyr_user)); 

PwmOut mypwmA(PA_8);  // PWM_OUT
PwmOut mypwmB(PA_9);  // PWM_OUT
PwmOut mypwmC(PA_10); // PWM_OUT

//RawCAN can1(PA_11, PA_12);

unsigned int UP, VP, WP;
//AnalogIn V_adc(PC_2); // gaibu Volume
// AnalogIn V_adc(PB_1);   //Volume

//BufferedSerial pc(USBTX, USBRX);

float Vr_adc = 0.0f;

Timer uT;
float ut1 = 0, ut2 = 0, usi = 0;
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
void HAH() {

  s = r % 2;
  if (s == 0) {
    ut1 = uT.elapsed_time().count();
    r++;
  }

  if (s == 1) {
    ut2 = uT.elapsed_time().count();
    r++;
    uT.reset();
  }
  mypwmA.write(Vr_adc);
  mypwmB.write(0);
  mypwmC.write(0);
}

void HAL() {

  mypwmA.write(0);
  mypwmC.write(0);
}
void HBH() {

  mypwmA.write(0);
  mypwmB.write(Vr_adc);
  mypwmC.write(0);
}
void HBL() {

  mypwmA.write(0);
  mypwmB.write(0);
}
void HCH() {

  mypwmA.write(0);
  mypwmB.write(0);
  mypwmC.write(Vr_adc);
}

void HCL() {
  mypwmB.write(0);
  mypwmC.write(0);
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

struct gpio_init_callback cb_H_A;
struct gpio_init_callback cb_H_B;
struct gpio_init_callback cb_H_C;

float volume_read(const struct adc_dt_spec *dt)
{
	uint16_t buf;
	struct adc_sequence seq = {
		.buffer = &buf,
		.buffer_size = sizeof(buf),
	};
	(void)adc_sequence_init_dt(dt, &seq);
	adc_read_dt(dt, &seq);
	return 1.0f * buf / (2 << seq.resolution);
}

int main() {
  //pc.set_baud(9600);

  //can1.frequency(500000);
  //can1.mode(mbed::interface::can::Normal);
  gpio_pin_set_dt(&EN_1, 1);
  gpio_pin_set_dt(&EN_2, 1);
  gpio_pin_set_dt(&EN_3, 1);

  mypwmA.period_us(20); // PWM 50KHz
  mypwmB.period_us(20);
  mypwmC.period_us(20);

  gpio_pin_interrupt_configure_dt(&H_A, GPIO_INT_EDGE_BOTH);
  gpio_pin_interrupt_configure_dt(&H_B, GPIO_INT_EDGE_BOTH);
  gpio_pin_interrupt_configure_dt(&H_C, GPIO_INT_EDGE_BOTH);

  gpio_init_callback(&cb_H_A, H_A_handler, BIT(H_A.pin));
  gpio_init_callback(&cb_H_B, H_B_handler, BIT(H_B.pin));
  gpio_init_callback(&cb_H_C, H_B_handler, BIT(H_C.pin));

  gpio_add_callback_dt(&H_A, cb_H_A);
  gpio_add_callback_dt(&H_B, cb_H_B);
  gpio_add_callback_dt(&H_C, cb_H_C);



  while (1) {

    Vr_adc = volume_read(&V_adc);
    uT.start();

    if (loop_count == 1) {
      //send_can_msg();
      loop_count = 0;
    } else {
      loop_count++;
    }

    if ((Vr_adc > 0.15f) && (q == 0)) {
      while (q < 50) {

        mypwmA.write(0);
        mypwmB.write(0.5f);
        mypwmC.write(0);
	k_usleep(START);

        mypwmA.write(0.5f);
        mypwmB.write(0);
        mypwmC.write(0);
	k_usleep(START);

        mypwmA.write(0);
        mypwmB.write(0);
        mypwmC.write(0.5f);
	k_usleep(START);
        q++;
      }
    }

    //  s=0;
    if (Vr_adc < 0.1f) {
      q = 0;
    }

    usi = fabs(ut2 - ut1);
    Speed = 60 * (1.f / (7.0f * usi * float(1E-6)));
    snprintf(message, sizeof(message), "%d.%03d , %d.%03d \r", (int)Speed, int(Speed * 1000.0f) % 1000, 
                                                             (int)Vr_adc, int(Vr_adc * 1000.0f) % 1000);
    //pc.write(message, sizeof(message));
    // UP=HA; VP=HB; WP=HC;
    // pc.printf("%d  ,%d ,%d\r" ,UP,VP,WP);
    //myled = !myled;
    gpio_pin_toggle_dt(&LED0);
  }
}

