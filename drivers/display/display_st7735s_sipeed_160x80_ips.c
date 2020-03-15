/*
 * Copyright (c) 2019 Marc Reilly
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include "display_st7735s.h"
#include <zephyr.h>
#include <stddef.h>

#define TRANSMIT_CMD(cmd, ...) { \
	uint8_t _cmd = cmd; \
	uint8_t _data[] = { __VA_ARGS__ }; \
	st7735s_transmit(data, _cmd, _data, sizeof(_data) ); }


void st7735s_lcd_init(struct st7735s_data *data)
{
	u16_t xoff = 0;
	u16_t yoff = 0;
#ifdef DT_INST_0_SITRONIX_ST7735S_X_OFFSET
	xoff = DT_INST_0_SITRONIX_ST7735S_X_OFFSET;
#endif
#ifdef DT_INST_0_SITRONIX_ST7735S_Y_OFFSET
	yoff = DT_INST_0_SITRONIX_ST7735S_Y_OFFSET;
#endif

	st7735s_set_lcd_margins(data, xoff, yoff);

	TRANSMIT_CMD(ST7735S_CMD_SLEEP_OUT);

	k_sleep(K_MSEC(100));

	// Set the frame frequency of the full colors normal mode
	// Frame rate=fosc/((RTNA x 2 + 40) x (LINE + FPA + BPA +2))
	// fosc = 850kHz
	TRANSMIT_CMD(ST7735S_CMD_FRAME_CTRL1, 0x05, 0x3A, 0x3A);
	
	// Set the frame frequency of the Idle mode
	// Frame rate=fosc/((RTNB x 2 + 40) x (LINE + FPB + BPB +2))
	// fosc = 850kHz
	TRANSMIT_CMD(ST7735S_CMD_FRAME_CTRL2, 0x05, 0x3A, 0x3A);

	// Set the frame frequency of the Partial mode/ full colors
	TRANSMIT_CMD(ST7735S_CMD_FRAME_CTRL3, 0x05, 0x3A, 0x3A, 0x05, 0x3A, 0x3A);

	TRANSMIT_CMD(ST7735S_CMD_POWER_CTRL1, 0x62, 0x02, 0x04);

	TRANSMIT_CMD(ST7735S_CMD_POWER_CTRL2, 0xC0);

	TRANSMIT_CMD(ST7735S_CMD_POWER_CTRL3, 0x0D, 0x00);

	TRANSMIT_CMD(ST7735S_CMD_POWER_CTRL4, 0x8D, 0x6A);

	TRANSMIT_CMD(ST7735S_CMD_POWER_CTRL5, 0x8D, 0xEE);

	TRANSMIT_CMD(ST7735S_CMD_VCOM_CTRL1,  0x0E);

	TRANSMIT_CMD(ST7735S_CMD_GAMMA_ADJUST_P,
		0x10, 0x0E, 0x02, 0x03, 0x0E, 0x07, 0x02, 0x07,
		0x0A, 0x12, 0x27, 0x37, 0x00, 0x0D, 0x0E, 0x10);

	TRANSMIT_CMD(ST7735S_CMD_GAMMA_ADJUST_N,
		0x10, 0x0E, 0x03, 0x03, 0x0F, 0x06, 0x02, 0x08,
		0x0A, 0x13, 0x26, 0x36, 0x00, 0x0D, 0x0E, 0x10);

	TRANSMIT_CMD(ST7735S_CMD_INVERT_ON);

	TRANSMIT_CMD(ST7735S_CMD_INVERSION_CTRL, 0x03);

	// define the format of RGB picture data 16-bit/pixel
	TRANSMIT_CMD(ST7735S_CMD_COLOR_MODE, 0x05);

	TRANSMIT_CMD(ST7735S_CMD_MEMORY_ACCESS_DATA_CTRL, 0x78);

	TRANSMIT_CMD(ST7735S_CMD_DISP_ON);

}
