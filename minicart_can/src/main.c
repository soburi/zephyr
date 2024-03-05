/*
 * Copyright (c) 2018 Alexander Wachter
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/printk.h>
#include <zephyr/device.h>
#include <zephyr/drivers/can.h>

#define SLEEP_TIME K_MSEC(250)

static const struct device *const can_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_canbus));

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

	can_send(can_dev, frame, K_FOREVER, tx_irq_callback, "send_engine_speed");
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

	can_send(can_dev, frame, K_FOREVER, tx_irq_callback, "send_vehicle_speed");
}

void send_can_data(struct can_frame *frame)
{
	send_engine_speed(can_dev, frame, 0);
	//send_vehicle_speed(can_dev, frame, 0);
}

int main(void)
{
	struct can_frame change_led_frame;
	int ret;

	if (!device_is_ready(can_dev)) {
		printf("CAN: Device %s not ready.\n", can_dev->name);
		return 0;
	}

#ifdef CONFIG_LOOPBACK_MODE
	ret = can_set_mode(can_dev, CAN_MODE_LOOPBACK);
	if (ret != 0) {
		printf("Error setting CAN mode [%d]", ret);
		return 0;
	}
#endif
	ret = can_start(can_dev);
	if (ret != 0) {
		printf("Error starting CAN controller [%d]", ret);
		return 0;
	}

	printf("Finished init.\n");

	while (1) {
		send_can_data(&change_led_frame);

		k_sleep(SLEEP_TIME);
	}
}
