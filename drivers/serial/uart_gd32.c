/*
 * Copyright (c) 2016 Open-RnD Sp. z o.o.
 * Copyright (c) 2016 Linaro Limited.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @brief Driver for UART port on GD32 family processor.
 * @note  LPUART and U(S)ART have the same base and
 *        majority of operations are performed the same way.
 *        Please validate for newly added series.
 */

#include <kernel.h>
#include <arch/cpu.h>
#include <sys/__assert.h>
#include <soc.h>
#include <init.h>
#include <drivers/uart.h>
#include <drivers/clock_control.h>

#include "gd32vf103_usart.h"
#include "gd32vf103_gpio.h"

#include <linker/sections.h>
#include <clock_control/gd32_clock_control.h>
#include "uart_gd32.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(uart_gd32);

/* convenience defines */
#define DEV_CFG(dev)							\
	((const struct uart_gd32_config * const)(dev)->config->config_info)
#define DEV_DATA(dev)							\
	((struct uart_gd32_data * const)(dev)->driver_data)
#define DEV_REGS(dev) \
	(DEV_CFG(dev)->uconf.regs)


#define CLT2_HWFC(regval)             (BITS(8,9) & ((uint32_t)(regval) << 8))
#define USART_HWFC_NONE               CLT2_HWFC(0)                     /*!< HWFC disable */
#define USART_HWFC_RTS                CLT2_HWFC(1)                     /*!< RTS enable */
#define USART_HWFC_CTS                CLT2_HWFC(2)                     /*!< CTS enable */
#define USART_HWFC_RTSCTS             CLT2_HWFC(3)                     /*!< RTS&CTS enable */
#define USART_CTL2_HWFC               BITS(8,9)                        /*!< RTS&CTS enable */

static inline u32_t uart_gd32_cfg2ll_parity(enum uart_config_parity parity);
static inline enum uart_config_parity uart_gd32_ll2cfg_parity(u32_t parity);
static inline u32_t uart_gd32_cfg2ll_stopbits(enum uart_config_stop_bits sb);
static inline enum uart_config_stop_bits uart_gd32_ll2cfg_stopbits(u32_t sb);
static inline u32_t uart_gd32_cfg2ll_databits(enum uart_config_data_bits db);
static inline enum uart_config_data_bits uart_gd32_ll2cfg_databits(u32_t db);
static inline enum uart_config_flow_control uart_gd32_ll2cfg_hwctrl(u32_t fc);

static inline void uart_gd32_set_baudrate(struct device *dev, u32_t baud_rate)
{
	const struct uart_gd32_config *config = DEV_CFG(dev);
	struct uart_gd32_data *data = DEV_DATA(dev);
	u32_t regs = DEV_REGS(dev);


	usart_baudrate_set(regs, baud_rate);
}

static inline void uart_gd32_set_parity(struct device *dev, u32_t parity)
{
	u32_t regs = DEV_REGS(dev);

	usart_parity_config(regs, uart_gd32_cfg2ll_parity(parity));
}

static inline u32_t uart_gd32_get_parity(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return uart_gd32_ll2cfg_parity( (USART_CTL0(regs) & USART_CTL0_PM) >> 9);
}

static inline void uart_gd32_set_stopbits(struct device *dev, u32_t stopbits)
{
	u32_t regs = DEV_REGS(dev);

	usart_stop_bit_set(regs, stopbits);
}

static inline u32_t uart_gd32_get_stopbits(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return uart_gd32_ll2cfg_stopbits( (USART_CTL1(regs) & USART_CTL1_STB) >> 12);
}

static inline void uart_gd32_set_databits(struct device *dev, u32_t databits)
{
	u32_t regs = DEV_REGS(dev);

	usart_word_length_set(regs, databits);
}

static inline u32_t uart_gd32_get_databits(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return uart_gd32_ll2cfg_databits( (USART_CTL0(regs) & USART_CTL0_WL) >> 12);
}

static inline void uart_gd32_set_hwctrl(struct device *dev, u32_t hwctrl)
{
	u32_t regs = DEV_REGS(dev);
	usart_hardware_flow_rts_config(regs, (hwctrl>>0 & 0x1));
	usart_hardware_flow_cts_config(regs, (hwctrl>>1 & 0x1));
}

static inline u32_t uart_gd32_get_hwctrl(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return uart_gd32_ll2cfg_hwctrl( (USART_CTL0(regs) & USART_CTL2_HWFC) >> 12);
}

static inline u32_t uart_gd32_cfg2ll_parity(enum uart_config_parity parity)
{
	switch (parity) {
	case UART_CFG_PARITY_ODD:
		return USART_PM_ODD;
	case UART_CFG_PARITY_EVEN:
		return USART_PM_EVEN;
	case UART_CFG_PARITY_NONE:
	default:
		return USART_PM_NONE;
	}
}

static inline enum uart_config_parity uart_gd32_ll2cfg_parity(u32_t parity)
{
	switch (parity) {
	case USART_PM_ODD:
		return UART_CFG_PARITY_ODD;
	case USART_PM_EVEN:
		return UART_CFG_PARITY_EVEN;
	case USART_PM_NONE:
	default:
		return UART_CFG_PARITY_NONE;
	}
}

static inline u32_t uart_gd32_cfg2ll_stopbits(enum uart_config_stop_bits sb)
{
	switch (sb) {
/* Some MCU's don't support 0.5 stop bits */
#ifdef USART_STB_0_5BIT
	case UART_CFG_STOP_BITS_0_5:
		return USART_STB_0_5BIT;
#endif	/* USART_STB_0_5BIT */
	case UART_CFG_STOP_BITS_1:
		return USART_STB_1BIT;
/* Some MCU's don't support 1.5 stop bits */
#ifdef USART_STB_1_5BIT
	case UART_CFG_STOP_BITS_1_5:
		return USART_STB_1_5BIT;
#endif	/* USART_STB_1_5BIT */
	case UART_CFG_STOP_BITS_2:
	default:
		return USART_STB_2BIT;
	}
}

static inline enum uart_config_stop_bits uart_gd32_ll2cfg_stopbits(u32_t sb)
{
	switch (sb) {
/* Some MCU's don't support 0.5 stop bits */
#ifdef USART_STB_0_5BIT
	case USART_STB_0_5BIT:
		return UART_CFG_STOP_BITS_0_5;
#endif	/* USART_STB_0_5BIT */
	case USART_STB_1BIT:
		return UART_CFG_STOP_BITS_1;
/* Some MCU's don't support 1.5 stop bits */
#ifdef USART_STB_1_5BIT
	case USART_STB_1_5BIT:
		return UART_CFG_STOP_BITS_1_5;
#endif	/* USART_STB_1_5BIT */
	case USART_STB_2BIT:
	default:
		return UART_CFG_STOP_BITS_2;
	}
}

static inline u32_t uart_gd32_cfg2ll_databits(enum uart_config_data_bits db)
{
	switch (db) {
/* Some MCU's don't support 7B or 9B datawidth */
#ifdef USART_WL_7BIT
	case UART_CFG_DATA_BITS_7:
		return USART_WL_7BIT;
#endif	/* USART_WL_7BIT */
#ifdef USART_WL_9BIT
	case UART_CFG_DATA_BITS_9:
		return USART_WL_9BIT;
#endif	/* USART_WL_9BIT */
	case UART_CFG_DATA_BITS_8:
	default:
		return USART_WL_8BIT;
	}
}

static inline enum uart_config_data_bits uart_gd32_ll2cfg_databits(u32_t db)
{
	switch (db) {
/* Some MCU's don't support 7B or 9B datawidth */
#ifdef USART_WL_7BIT
	case USART_WL_7BIT:
		return UART_CFG_DATA_BITS_7;
#endif	/* USART_WL_7BIT */
#ifdef USART_WL_9BIT
	case USART_WL_9BIT:
		return UART_CFG_DATA_BITS_9;
#endif	/* USART_WL_9BIT */
	case USART_WL_8BIT:
	default:
		return UART_CFG_DATA_BITS_8;
	}
}

static inline u32_t uart_gd32_cfg2ll_hwctrl(enum uart_config_flow_control fc)
{
	if (fc == UART_CFG_FLOW_CTRL_RTS_CTS) {
		return USART_HWFC_RTSCTS;
	}

	return USART_HWFC_NONE;
}

static inline enum uart_config_flow_control uart_gd32_ll2cfg_hwctrl(u32_t fc)
{
	if (fc == USART_HWFC_RTSCTS) {
		return UART_CFG_FLOW_CTRL_RTS_CTS;
	}

	return UART_CFG_PARITY_NONE;
}

static int uart_gd32_configure(struct device *dev,
				const struct uart_config *cfg)
{
	struct uart_gd32_data *data = DEV_DATA(dev);
	u32_t regs = DEV_REGS(dev);
	const u32_t parity = uart_gd32_cfg2ll_parity(cfg->parity);
	const u32_t stopbits = uart_gd32_cfg2ll_stopbits(cfg->stop_bits);
	const u32_t databits = uart_gd32_cfg2ll_databits(cfg->data_bits);
	const u32_t flowctrl = uart_gd32_cfg2ll_hwctrl(cfg->flow_ctrl);

	/* Hardware doesn't support mark or space parity */
	if ((UART_CFG_PARITY_MARK == cfg->parity) ||
	    (UART_CFG_PARITY_SPACE == cfg->parity)) {
		return -ENOTSUP;
	}

	if (UART_CFG_STOP_BITS_0_5 == cfg->stop_bits) {
		return -ENOTSUP;
	}

	if (UART_CFG_STOP_BITS_1_5 == cfg->stop_bits) {
		return -ENOTSUP;
	}

	/* Driver doesn't support 5 or 6 databits and potentially 7 or 9 */
	if ((UART_CFG_DATA_BITS_5 == cfg->data_bits) ||
	    (UART_CFG_DATA_BITS_6 == cfg->data_bits)
#ifndef USART_WL_7BIT
	    || (UART_CFG_DATA_BITS_7 == cfg->data_bits)
#endif /* USART_WL_7BIT */
#ifndef USART_WL_9BIT
	    || (UART_CFG_DATA_BITS_9 == cfg->data_bits)
#endif /* USART_WL_9BIT */
		) {
		return -ENOTSUP;
	}

	/* Driver supports only RTS CTS flow control */
	if (UART_CFG_FLOW_CTRL_NONE != cfg->flow_ctrl) {
		if (UART_CFG_FLOW_CTRL_RTS_CTS != cfg->flow_ctrl) {
			return -ENOTSUP;
		}
	}

	usart_disable(regs);

	if (parity != uart_gd32_get_parity(dev)) {
		uart_gd32_set_parity(dev, parity);
	}

	if (stopbits != uart_gd32_get_stopbits(dev)) {
		uart_gd32_set_stopbits(dev, stopbits);
	}

	if (databits != uart_gd32_get_databits(dev)) {
		uart_gd32_set_databits(dev, databits);
	}

	if (flowctrl != uart_gd32_get_hwctrl(dev)) {
		uart_gd32_set_hwctrl(dev, flowctrl);
	}

	if (cfg->baudrate != data->baud_rate) {
		uart_gd32_set_baudrate(dev, cfg->baudrate);
		data->baud_rate = cfg->baudrate;
	}

	usart_enable(regs);
	return 0;
};

static int uart_gd32_config_get(struct device *dev, struct uart_config *cfg)
{
	struct uart_gd32_data *data = DEV_DATA(dev);

	cfg->baudrate = data->baud_rate;
	cfg->parity = uart_gd32_ll2cfg_parity(uart_gd32_get_parity(dev));
	cfg->stop_bits = uart_gd32_ll2cfg_stopbits(
		uart_gd32_get_stopbits(dev));
	cfg->data_bits = uart_gd32_ll2cfg_databits(
		uart_gd32_get_databits(dev));
	cfg->flow_ctrl = uart_gd32_ll2cfg_hwctrl(
		uart_gd32_get_hwctrl(dev));
	return 0;
}

static int uart_gd32_poll_in(struct device *dev, unsigned char *c)
{
	u32_t regs = DEV_REGS(dev);

	/* Clear overrun error flag */
	if (usart_flag_get(regs, USART_FLAG_ORERR) ) {
		usart_flag_clear(regs, USART_FLAG_ORERR);
	}

	if (!usart_flag_get(regs, USART_FLAG_RBNE) ) {
		return -1;
	}

	*c = (unsigned char)usart_data_receive(regs);

	return 0;
}

static void uart_gd32_poll_out(struct device *dev,
					unsigned char c)
{
	u32_t regs = DEV_REGS(dev);

	usart_flag_clear(regs, USART_FLAG_TC);

	usart_data_transmit(regs, (u8_t) c );

	/* Wait for TXE flag to be raised */
	while (!usart_flag_get(regs, USART_FLAG_TBE)) {
	}

}

static int uart_gd32_err_check(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);
	u32_t err = 0U;

	/* Check for errors, but don't clear them here.
	 * Some SoC clear all error flags when at least
	 * one is cleared. (e.g. F4X, F1X, and F2X)
	 */
	if (usart_flag_get(regs, USART_FLAG_ORERR)) {
		err |= UART_ERROR_OVERRUN;
	}

	if (usart_flag_get(regs, USART_FLAG_PERR)) {
		err |= UART_ERROR_PARITY;
	}

	if (usart_flag_get(regs, USART_FLAG_FERR)) {
		err |= UART_ERROR_FRAMING;
	}

	if (err & UART_ERROR_OVERRUN) {
		usart_flag_clear(regs, USART_FLAG_ORERR);
	}

	if (err & UART_ERROR_PARITY) {
		usart_flag_clear(regs, USART_FLAG_PERR);
	}

	if (err & UART_ERROR_FRAMING) {
		usart_flag_clear(regs, USART_FLAG_FERR);
	}

	/* Clear noise error as well,
	 * it is not represented by the errors enum
	 */
	usart_flag_clear(regs, USART_FLAG_NERR);

	return err;
}

static inline void __uart_gd32_get_clock(struct device *dev)
{
	struct uart_gd32_data *data = DEV_DATA(dev);
	struct device *clk =
		device_get_binding(GD32_CLOCK_CONTROL_NAME);

	__ASSERT_NO_MSG(clk);

	data->clock = clk;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_gd32_fifo_fill(struct device *dev, const u8_t *tx_data,
				  int size)
{
	u32_t regs = DEV_REGS(dev);
	u8_t num_tx = 0U;

	while ((size - num_tx > 0) &&
	       usart_interrupt_flag_get(regs, USART_INT_FLAG_TBE)) {
		/* TBE flag will be cleared with byte write to DATA register */

		/* Send a character (8bit , parity none) */
		usart_data_transmit(regs, tx_data[num_tx++]);
	}

	return num_tx;
}

static int uart_gd32_fifo_read(struct device *dev, u8_t *rx_data,
				  const int size)
{
	u32_t regs = DEV_REGS(dev);
	u8_t num_rx = 0U;

	while ((size - num_rx > 0) &&
	       usart_interrupt_flag_get(regs, USART_INT_FLAG_RBNE)) {
		/* RBNE flag will be cleared upon read from DATA register */

		/* Receive a character (8bit , parity none) */
		rx_data[num_rx++] = usart_data_receive(regs);

		/* Clear overrun error flag */
		if (usart_interrupt_flag_get(regs, USART_INT_FLAG_ERR_ORERR)) {
			usart_interrupt_flag_clear(regs, USART_INT_FLAG_ERR_ORERR);
		}
	}

	return num_rx;
}

static void uart_gd32_irq_tx_enable(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	usart_interrupt_enable(regs, USART_INT_TBE);
}

static void uart_gd32_irq_tx_disable(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	usart_interrupt_disable(regs, USART_INT_TBE);
}

static int uart_gd32_irq_tx_ready(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return usart_interrupt_flag_get(regs, USART_INT_FLAG_TBE);
}

static int uart_gd32_irq_tx_complete(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return usart_interrupt_flag_get(regs, USART_INT_FLAG_TC);
}

static void uart_gd32_irq_rx_enable(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	usart_interrupt_enable(regs, USART_INT_RBNE);
}

static void uart_gd32_irq_rx_disable(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	usart_interrupt_disable(regs, USART_INT_RBNE);
}

static int uart_gd32_irq_rx_ready(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return usart_interrupt_flag_get(regs, USART_INT_FLAG_RBNE);
}

static void uart_gd32_irq_err_enable(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	/* Enable Error interruptions */
	usart_interrupt_enable(regs, USART_INT_ERR);
	/* Enable Line break detection */
	usart_interrupt_enable(regs, USART_INT_LBD);
	/* Enable parity error interruption */
	usart_interrupt_enable(regs, USART_INT_PERR);
}

static void uart_gd32_irq_err_disable(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	/* Disable Error interruptions */
	usart_interrupt_disable(regs, USART_INT_ERR);
	/* Disable Line break detection */
	usart_interrupt_disable(regs, USART_INT_LBD);
	/* Disable parity error interruption */
	usart_interrupt_disable(regs, USART_INT_PERR);
}

static int usart_interrupt_flag_enabled(uint32_t usart_periph, uint32_t int_flag)
{
    return (USART_REG_VAL(usart_periph, int_flag) >> USART_BIT_POS(int_flag)) & 0x1;
}

static int uart_gd32_irq_is_pending(struct device *dev)
{
	u32_t regs = DEV_REGS(dev);

	return ((usart_interrupt_flag_enabled(regs, USART_INT_FLAG_RBNE) &&
		 usart_interrupt_flag_get(regs, USART_INT_FLAG_RBNE)) ||
		(usart_interrupt_flag_enabled(regs, USART_INT_FLAG_TC) &&
		 usart_interrupt_flag_get(regs, USART_INT_FLAG_TC)));
}

static int uart_gd32_irq_update(struct device *dev)
{
	return 1;
}

static void uart_gd32_irq_callback_set(struct device *dev,
					uart_irq_callback_user_data_t cb,
					void *cb_data)
{
	struct uart_gd32_data *data = DEV_DATA(dev);

	data->user_cb = cb;
	data->user_data = cb_data;
}

static void uart_gd32_isr(void *arg)
{
	struct device *dev = arg;
	struct uart_gd32_data *data = DEV_DATA(dev);

	if (data->user_cb) {
		data->user_cb(data->user_data);
	}
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static const struct uart_driver_api uart_gd32_driver_api = {
	.poll_in = uart_gd32_poll_in,
	.poll_out = uart_gd32_poll_out,
	.err_check = uart_gd32_err_check,
	.configure = uart_gd32_configure,
	.config_get = uart_gd32_config_get,
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_gd32_fifo_fill,
	.fifo_read = uart_gd32_fifo_read,
	.irq_tx_enable = uart_gd32_irq_tx_enable,
	.irq_tx_disable = uart_gd32_irq_tx_disable,
	.irq_tx_ready = uart_gd32_irq_tx_ready,
	.irq_tx_complete = uart_gd32_irq_tx_complete,
	.irq_rx_enable = uart_gd32_irq_rx_enable,
	.irq_rx_disable = uart_gd32_irq_rx_disable,
	.irq_rx_ready = uart_gd32_irq_rx_ready,
	.irq_err_enable = uart_gd32_irq_err_enable,
	.irq_err_disable = uart_gd32_irq_err_disable,
	.irq_is_pending = uart_gd32_irq_is_pending,
	.irq_update = uart_gd32_irq_update,
	.irq_callback_set = uart_gd32_irq_callback_set,
#endif	/* CONFIG_UART_INTERRUPT_DRIVEN */
};

/**
 * @brief Initialize UART channel
 *
 * This routine is called to reset the chip in a quiescent state.
 * It is assumed that this function is called only once per UART.
 *
 * @param dev UART device struct
 *
 * @return 0
 */
static int uart_gd32_init(struct device *dev)
{
	const struct uart_gd32_config *config = DEV_CFG(dev);
	struct uart_gd32_data *data = DEV_DATA(dev);
	u32_t regs = DEV_REGS(dev);

	__uart_gd32_get_clock(dev);
	/* enable clock */
	if (clock_control_on(data->clock,
			(clock_control_subsys_t *)&config->pclken) != 0) {
		return -EIO;
	}

	usart_deinit(regs);

	/* TX/RX direction */
	usart_transmit_config(regs, USART_TRANSMIT_ENABLE);
	usart_receive_config(regs, USART_RECEIVE_ENABLE);

	/* 8 data bit, 1 start bit, 1 stop bit, no parity */
	uart_gd32_set_databits(dev, USART_WL_8BIT);
	uart_gd32_set_stopbits(dev, USART_STB_1BIT);
	uart_gd32_set_parity(dev, USART_PM_NONE);

	if (config->hw_flow_control) {
		uart_gd32_set_hwctrl(dev, USART_HWFC_RTSCTS);
	}

	/* Set the default baudrate */
	uart_gd32_set_baudrate(dev, data->baud_rate);

	usart_enable(regs);

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	config->uconf.irq_config_func(dev);
#endif
	return 0;
}


#ifdef CONFIG_UART_INTERRUPT_DRIVEN
#define GD32_UART_IRQ_HANDLER_DECL(name)				\
	static void uart_gd32_irq_config_func_##name(struct device *dev)
#define GD32_UART_IRQ_HANDLER_FUNC(name)				\
	.irq_config_func = uart_gd32_irq_config_func_##name,
#define GD32_UART_IRQ_HANDLER(name)					\
static void uart_gd32_irq_config_func_##name(struct device *dev)	\
{									\
	IRQ_CONNECT(DT_##name##_IRQ,					\
		DT_UART_GD32_##name##_IRQ_PRI,			\
		uart_gd32_isr, DEVICE_GET(uart_gd32_##name),	\
		0);							\
	irq_enable(DT_##name##_IRQ);					\
}
#else
#define GD32_UART_IRQ_HANDLER_DECL(name)
#define GD32_UART_IRQ_HANDLER_FUNC(name)
#define GD32_UART_IRQ_HANDLER(name)
#endif

#define GD32_UART_INIT(name)						\
GD32_UART_IRQ_HANDLER_DECL(name);					\
									\
static const struct uart_gd32_config uart_gd32_cfg_##name = {		\
	.uconf = {							\
		.regs = DT_UART_GD32_##name##_BASE_ADDRESS,		\
		GD32_UART_IRQ_HANDLER_FUNC(name)			\
	},								\
	.pclken = { .bus = DT_UART_GD32_##name##_CLOCK_BUS,		\
		    .enr = DT_UART_GD32_##name##_CLOCK_BITS,		\
	},								\
	.hw_flow_control = DT_UART_GD32_##name##_HW_FLOW_CONTROL,	\
};									\
									\
static struct uart_gd32_data uart_gd32_data_##name = {			\
	.baud_rate = DT_UART_GD32_##name##_BAUD_RATE			\
};									\
									\
DEVICE_AND_API_INIT(uart_gd32_##name, DT_UART_GD32_##name##_NAME,	\
		    &uart_gd32_init,					\
		    &uart_gd32_data_##name, &uart_gd32_cfg_##name,	\
		    PRE_KERNEL_1, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,	\
		    &uart_gd32_driver_api);				\
									\
GD32_UART_IRQ_HANDLER(name)


#ifdef CONFIG_UART_0
GD32_UART_INIT(USART_0)
#endif	/* CONFIG_UART_0 */

#ifdef CONFIG_UART_1
GD32_UART_INIT(USART_1)
#endif	/* CONFIG_UART_1 */

#ifdef CONFIG_UART_2
GD32_UART_INIT(USART_2)
#endif	/* CONFIG_UART_2 */

#ifdef CONFIG_UART_3
GD32_UART_INIT(USART_3)
#endif	/* CONFIG_UART_3 */

#ifdef CONFIG_UART_4
GD32_UART_INIT(USART_4)
#endif /* CONFIG_UART_4 */

#ifdef CONFIG_UART_5
GD32_UART_INIT(USART_5)
#endif /* CONFIG_UART_5 */

#ifdef CONFIG_UART_6
GD32_UART_INIT(USART_6)
#endif /* CONFIG_UART_6 */

#ifdef CONFIG_UART_7
GD32_UART_INIT(USART_7)
#endif /* CONFIG_UART_7 */

#ifdef CONFIG_UART_8
GD32_UART_INIT(USART_8)
#endif /* CONFIG_UART_8 */

#ifdef CONFIG_UART_9
GD32_UART_INIT(UART_9)
#endif /* CONFIG_UART_9 */

