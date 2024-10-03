/*
 * Copyright (c) 2018 Jan Van Winkel <jan.van_winkel@dxplore.eu>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/display.h>
#include <zephyr/drivers/gpio.h>
#include <lvgl.h>
#include <stdio.h>
#include <string.h>
#include <zephyr/kernel.h>
#include <lvgl_input_device.h>

#define LOG_LEVEL CONFIG_LOG_DEFAULT_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(app);

int count = 0;
char count_str[20];
int main(void)
{
	const struct device *display_dev;
	//lv_obj_t *hello_world_label;
	lv_obj_t *label;

	display_dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
	if (!device_is_ready(display_dev)) {
		LOG_ERR("Device not ready, aborting test");
		return 0;
	}

	//hello_world_label = lv_label_create(lv_scr_act());
	//lv_label_set_text(hello_world_label, "Hello world!");
	//lv_obj_align(hello_world_label, LV_ALIGN_CENTER, 0, 0);
	//
	lv_style_t blue_bg_style;
       	lv_style_init(&blue_bg_style);
       	lv_style_set_bg_color(&blue_bg_style, lv_color_make(0x00, 0x00, 0x00));
       	lv_obj_add_style(lv_scr_act(), &blue_bg_style, 0);
	
	lv_task_handler();
	display_blanking_off(display_dev);
	//lv_label_set_text(count_label, "Japan Technical Jamboree Episode-2 #4");

	lv_obj_t * label2 = lv_label_create(lv_scr_act());
	lv_label_set_long_mode(label2, LV_LABEL_LONG_SCROLL_CIRCULAR);     /*Circular scroll*/
	lv_obj_set_width(label2, 150);
	lv_label_set_text(label2, "It is a circularly scrolling text. ");
	lv_obj_align(label2, LV_ALIGN_CENTER, 0, 40);

	while(true) {

		k_sleep(K_MSEC(1));
	}

        lv_label_set_text(label, "Hello world!");
        lv_obj_align(label, LV_ALIGN_CENTER, 0, 0);
        display_blanking_off(display_dev);

        while (1) {
                if ((count % 100) == 0U) {
                        sprintf(count_str, "%d", count/100U);
                        lv_label_set_text(label, count_str);
                }
                lv_task_handler();
                ++count;
                k_sleep(K_MSEC(10));
        }
}
