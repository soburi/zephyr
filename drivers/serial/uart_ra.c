/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra_uart_sci

#include <zephyr/drivers/uart.h>

#include <r_sci_uart.h>
#include "bsp_api.h"
#include <r_ioport.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ra_uart_sci, CONFIG_UART_LOG_LEVEL);

struct uart_ra_config {
	uart_ctrl_t *p_ctrl;
};

struct uart_ra_data {
	sci_uart_instance_ctrl_t ctrl;
	uart_cfg_t cfg;
	sci_uart_extended_cfg_t cfg_ext;
	baud_setting_t baud;
};

static int uart_ra_poll_in(const struct device *dev, unsigned char *p_char)
{
	const struct uart_ra_config *config = dev->config;
	sci_uart_instance_ctrl_t *p_inst = (sci_uart_instance_ctrl_t *)config->p_ctrl;

	if (p_inst->p_reg->SSR_b.RDRF == 0) {
		return -1;
	}

	*p_char = p_inst->p_reg->RDR;

	return 0;
}

static void uart_ra_poll_out(const struct device *dev, unsigned char out_char)
{
	const struct uart_ra_config *config = dev->config;
	sci_uart_instance_ctrl_t *p_inst = (sci_uart_instance_ctrl_t *)config->p_ctrl;

	p_inst->p_reg->TDR = out_char;

	while (!p_inst->p_reg->SSR_b.TEND || !p_inst->p_reg->SSR_b.TDRE) {
		;
	}
}

static int uart_ra_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_ra_config *config = dev->config;
	struct uart_ra_data *data = dev->data;
	fsp_err_t err = FSP_SUCCESS;

	err = R_SCI_UART_Close(config->p_ctrl);
	err = R_SCI_UART_Open(config->p_ctrl, &data->cfg);

	data->ctrl.p_reg->SCR_b.RIE = 0;
	data->ctrl.p_reg->SCR_b.TIE = 0;
	data->ctrl.p_reg->SCR_b.TEIE = 0;

	return 0;
}

static int uart_ra_init(const struct device *dev)
{
	/* TODO:
	 * Since pinctrl function not implemented at the time of the first commit,
	 * I make a fixed configration.
	 * Will replace this code by pinctrl as soon as it implements.
	 */
	{
		static const ioport_pin_cfg_t g_uart_pin_cfg_data[] = {
			{.pin = BSP_IO_PORT_01_PIN_00,
			 .pin_cfg = ((uint32_t)IOPORT_CFG_DRIVE_HIGH |
				     (uint32_t)IOPORT_CFG_PERIPHERAL_PIN |
				     (uint32_t)IOPORT_PERIPHERAL_SCI0_2_4_6_8)},
			{.pin = BSP_IO_PORT_01_PIN_01,
			 .pin_cfg = ((uint32_t)IOPORT_CFG_DRIVE_HIGH |
				     (uint32_t)IOPORT_CFG_PERIPHERAL_PIN |
				     (uint32_t)IOPORT_PERIPHERAL_SCI0_2_4_6_8)},
			{.pin = BSP_IO_PORT_03_PIN_01,
			 .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN |
				     (uint32_t)IOPORT_PERIPHERAL_SCI0_2_4_6_8)},
			{.pin = BSP_IO_PORT_03_PIN_02,
			 .pin_cfg = ((uint32_t)IOPORT_CFG_PERIPHERAL_PIN |
				     (uint32_t)IOPORT_PERIPHERAL_SCI0_2_4_6_8)}
		};
		const ioport_cfg_t g_uart_pin_cfg = {
			.number_of_pins = sizeof(g_uart_pin_cfg_data) / sizeof(ioport_pin_cfg_t),
			.p_pin_cfg_data = &g_uart_pin_cfg_data[0],
		};
		static ioport_instance_ctrl_t g_uart_ioport_ctrl;

		R_IOPORT_Open(&g_uart_ioport_ctrl, &g_uart_pin_cfg);
	}

	struct uart_config cfg = {
		.baudrate = 115200, .parity = 0, .stop_bits = 1, .data_bits = 8, .flow_ctrl = 0};

	uart_ra_configure(dev, &cfg);

	return 0;
}

static const struct uart_driver_api uart_ra_api = {
	.poll_in = uart_ra_poll_in,
	.poll_out = uart_ra_poll_out,
	.configure = uart_ra_configure,
};

/** UART extended configuration for UARTonSCI HAL driver */

#define DEFINE_UART_RA(n)                                                                          \
	static struct uart_ra_data uart_ra_data_##n = {                                            \
		.baud = {.semr_baudrate_bits_b.abcse = 0,                                          \
			 .semr_baudrate_bits_b.abcs = 0,                                           \
			 .semr_baudrate_bits_b.bgdm = 1,                                           \
			 .cks = 0,                                                                 \
			 .brr = 38,                                                                \
			 .mddr = (uint8_t)184,                                                     \
			 .semr_baudrate_bits_b.brme = true},                                       \
		.cfg_ext = {.clock = SCI_UART_CLOCK_INT,                                           \
			    .rx_edge_start = SCI_UART_START_BIT_FALLING_EDGE,                      \
			    .noise_cancel = SCI_UART_NOISE_CANCELLATION_DISABLE,                   \
			    .rx_fifo_trigger = SCI_UART_RX_FIFO_TRIGGER_MAX,                       \
			    .p_baud_setting = &uart_ra_data_##n.baud,                              \
			    .flow_control = SCI_UART_FLOW_CONTROL_RTS,                             \
			    .flow_control_pin = (bsp_io_port_pin_t)UINT16_MAX,                     \
			    .rs485_setting =                                                       \
				    {                                                              \
					    .enable = SCI_UART_RS485_DISABLE,                      \
					    .polarity = SCI_UART_RS485_DE_POLARITY_HIGH,           \
					    .de_control_pin = (bsp_io_port_pin_t)UINT16_MAX,       \
				    }},                                                            \
		.cfg = {.channel = 0,                                                              \
			.data_bits = UART_DATA_BITS_8,                                             \
			.parity = UART_PARITY_OFF,                                                 \
			.stop_bits = UART_STOP_BITS_1,                                             \
			.p_extend = &uart_ra_data_##n.cfg_ext}};                                   \
	static struct uart_ra_config uart_ra_config_##n = {                                        \
		.p_ctrl = &uart_ra_data_##n.ctrl,                                                  \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, uart_ra_init, NULL, &uart_ra_data_##n, &uart_ra_config_##n,       \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_ra_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_UART_RA)
