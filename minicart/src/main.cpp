#include "mbed.h"

unsigned int q = 0, r = 0, s = 0, START = 8;

PwmOut mypwmA(PA_8);  // PWM_OUT
PwmOut mypwmB(PA_9);  // PWM_OUT
PwmOut mypwmC(PA_10); // PWM_OUT

DigitalOut EN1(PC_10);
DigitalOut EN2(PC_11);
DigitalOut EN3(PC_12);

const struct gpio_dt_spec H_A = GPIO_DT_SPEC_GET(DT_NODELABEL(u_in), gpios);
const struct gpio_dt_spec H_B = GPIO_DT_SPEC_GET(DT_NODELABEL(v_in), gpios);
const struct gpio_dt_spec H_C = GPIO_DT_SPEC_GET(DT_NODELABEL(w_in), gpios);

struct gpio_callback cb_H_A;
struct gpio_callback cb_H_B;
struct gpio_callback cb_H_C;
struct gpio_callback cb_button;

unsigned int UP, VP, WP;
AnalogIn V_adc(PC_2); // gaibu Volume
// AnalogIn V_adc(PB_1);   //Volume


DigitalOut myled(LED1);

float Vr_adc = 0.0f;

Timer uT;
float ut1 = 0, ut2 = 0, usi = 0;
float Speed = 0;

void HAH()
{

	s = r % 2;
	if (s == 0) {
		ut1 = uT.read_us();
		r++;
	}

	if (s == 1) {
		ut2 = uT.read_us();
		r++;
		uT.reset();
	}
	mypwmA.write(Vr_adc);
	mypwmB.write(0);
	mypwmC.write(0);
}

void HAL()
{

	mypwmA.write(0);
	mypwmC.write(0);
}
void HBH()
{

	mypwmA.write(0);
	mypwmB.write(Vr_adc);
	mypwmC.write(0);
}
void HBL()
{

	mypwmA.write(0);
	mypwmB.write(0);
}
void HCH()
{

	mypwmA.write(0);
	mypwmB.write(0);
	mypwmC.write(Vr_adc);
}

void HCL()
{
	mypwmB.write(0);
	mypwmC.write(0);
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

int main()
{
	// can1.frequency(500000);
	// can1.mode(mbed::interface::can::Normal);

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

	EN1 = 1;
	EN2 = 1;
	EN3 = 1;

	mypwmA.period_us(20); // PWM 50KHz

	mypwmB.period_us(20);

	mypwmC.period_us(20);

	while (1) {

		Vr_adc = V_adc.read();
		uT.start();

		if ((Vr_adc > 0.15f) && (q == 0)) {
			while (q < 50) {

				mypwmA.write(0);
				mypwmB.write(0.5f);
				mypwmC.write(0);
				wait_ms(START);

				mypwmA.write(0.5f);
				mypwmB.write(0);
				mypwmC.write(0);
				wait_ms(START);

				mypwmA.write(0);
				mypwmB.write(0);
				mypwmC.write(0.5f);
				wait_ms(START);
				q++;
			}
		}

		//  s=0;
		if (Vr_adc < 0.1f) {

			q = 0;
		}

		usi = abs(ut2 - ut1);
		Speed = 60 * (1 / (7.0 * usi * 1E-6));

		/*
		snprintf(message, sizeof(message), "%d.%03d , %d.%03d \r", (int)Speed,
			 (int)(Speed * 1000.0) % 1000, (int)Vr_adc, (int)(Vr_adc * 1000.0) % 1000);
		printf("%s", message);
		printf("%d  ,%d ,%d\r" ,gpio_pin_get_dt(&H_A),gpio_pin_get_dt(&H_B),gpio_pin_get_dt(&H_C));
		*/

		myled = !myled;
	}
}
