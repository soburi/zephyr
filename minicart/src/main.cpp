#include "mbed.h"

unsigned int q = 0, r = 0, s = 0, START = 8;

PwmOut mypwmA(PA_8);  // PWM_OUT
PwmOut mypwmB(PA_9);  // PWM_OUT
PwmOut mypwmC(PA_10); // PWM_OUT

DigitalOut EN1(PC_10);
DigitalOut EN2(PC_11);
DigitalOut EN3(PC_12);

InterruptIn HA(PA_15);
InterruptIn HB(PB_3);
InterruptIn HC(PB_10);

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

int main()
{

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

		HA.rise(&HCH); // HAH
		HC.fall(&HBL); // HCL
		HB.rise(&HAH); // HBH
		HA.fall(&HCL); // HAL
		HC.rise(&HBH); // HCH
		HB.fall(&HAL); // HBL
		//  s=0;
		if (Vr_adc < 0.1f) {

			q = 0;
		}

		usi = abs(ut2 - ut1);
		Speed = 60 * (1 / (7.0 * usi * 1E-6));
		printf("%.3f , %.3f \r", Speed, Vr_adc);

		/*
		snprintf(message, sizeof(message), "%d.%03d , %d.%03d \r", (int)Speed,
			 (int)(Speed * 1000.0) % 1000, (int)Vr_adc, (int)(Vr_adc * 1000.0) % 1000);
		printf("%s", message);
		printf("%d  ,%d ,%d\r" ,gpio_pin_get_dt(&H_A),gpio_pin_get_dt(&H_B),gpio_pin_get_dt(&H_C));
		*/

		myled = !myled;
	}
}
