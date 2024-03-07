#include "mbed.h"
#include <zephyr/drivers/can.h>

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

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

Serial pc(USBTX, USBRX);

DigitalOut myled(LED1);

float Vr_adc = 0.0f;

Timer uT;
float ut1 = 0, ut2 = 0, usi = 0;
float Speed = 0;

static void tx_irq_callback(const struct device *dev, int error, void *arg)
{
	char *sender = (char *)arg;

	ARG_UNUSED(dev);

	if (error != 0) {
		printf("Callback! error-code: %d\nSender: %s\n",
		       error, sender);
	}
}

int dummy_speed;

static void send_engine_speed(const struct device *can_dev, struct can_frame *frame, int speed)
{
	speed = dummy_speed;
	dummy_speed++;

	memset(frame, 0, sizeof(struct can_frame));
            //# Engine speed
            //speed = self.simulator.get_engine_speed()
            //if self.verbose:
            //    print('engine speed = %d' % speed)
            //if speed > 16383.75:
            //    speed = 16383.75
            //# Message is 1 byte unknown, 1 byte fuel level, 2 bytes engine speed (4x), fuel low @ bit 55

       	//msg = [ 0, 0 ]
	frame->data[0] = 0;
	frame->data[1] = 0;
        //speed *= 4
        //msg.append(speed // 256)
        //msg.append(speed % 256)
	frame->data[2] = (speed * 4) / 256;
	frame->data[3] = (speed * 4) % 256;
        //# pad remaining bytes to make 8
        //msg.append(0)
        //msg.append(0)
        //msg.append(0)
        //msg.append(0)
	frame->data[4] = 0;
	frame->data[5] = 0;
	frame->data[6] = 0;
	frame->data[7] = 0;

	frame->id = 0x3d9;
	frame->flags = 0;
	frame->dlc = 8;

	can_send(can_dev, frame, K_FOREVER, tx_irq_callback, const_cast<char*>("send_engine_speed"));
}

static void send_vehicle_speed(const struct device *can_dev, struct can_frame *frame, int speed)
{
	memset(frame, 0, sizeof(struct can_frame));
        //# Vehicle speed
        //speed = int(self.simulator.get_vehicle_speed()) % 256
        /* Message is 15 bits speed (64x), left aligned */
        /* Note: extra 2x to yield required left-alignment */
	frame->data[0] = (speed * 128) / 256;
	frame->data[1] = (speed * 128) % 256;
        /* pad remaining bytes to make 8 */
	frame->data[2] = 0;
	frame->data[3] = 0;
	frame->data[4] = 0;
	frame->data[5] = 0;
	frame->data[6] = 0;
	frame->data[7] = 0;

	frame->id = 0x3e9;
	frame->flags = 0;
	frame->dlc = 8;

	can_send(can_dev, frame, K_FOREVER, tx_irq_callback, const_cast<char*>("send_vehicle_speed"));
}

void send_can_data(struct can_frame *frame)
{
	send_engine_speed(can_dev, frame, 0);
	//send_vehicle_speed(can_dev, frame, 0);
}

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
	struct can_frame change_led_frame;

	pc.baud(115200);

	EN1 = 1;
	EN2 = 1;
	EN3 = 1;

	mypwmA.period_us(20); // PWM 50KHz
	mypwmB.period_us(20);
	mypwmC.period_us(20);

	HA.rise(&HCH); // HAH
	HC.fall(&HBL); // HCL
	HB.rise(&HAH); // HBH
	HA.fall(&HCL); // HAL
	HC.rise(&HBH); // HCH
	HB.fall(&HAL); // HBL

	uT.start();

	while (1) {
		send_can_data(&change_led_frame);

		Vr_adc = V_adc.read();

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
		pc.printf("%.3f , %.3f \r", Speed, Vr_adc);
		// UP=HA; VP=HB; WP=HC;
		// pc.printf("%d  ,%d ,%d\r" ,UP,VP,WP);
		myled = !myled;
	}
}
