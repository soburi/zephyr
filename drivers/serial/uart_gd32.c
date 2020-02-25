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

#include "gd32vf103.h"

#include <kernel.h>
#include <arch/cpu.h>
#include <sys/__assert.h>
#include <soc.h>
#include <init.h>
#include <drivers/uart.h>
#include <drivers/clock_control.h>

#include <linker/sections.h>
//#include <clock_control/gd32_clock_control.h>
#include "uart_gd32.h"

#include <logging/log.h>
LOG_MODULE_REGISTER(uart_gd32);

/* convenience defines */
#define DEV_CFG(dev)							\
	((const struct uart_gd32_config * const)(dev)->config->config_info)
#define DEV_DATA(dev)							\
	((struct uart_gd32_data * const)(dev)->driver_data)
#define UART_STRUCT(dev)					\
	((USART_TypeDef *)(DEV_CFG(dev))->uconf.base)

#define TIMEOUT 1000

#define DEV_BASE(dev)							\
	(*(DEV_CFG(dev)->uconf.base))

/* USART RTS configure */
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
//TODO	const struct uart_gd32_config *config = DEV_CFG(dev);
//TODO	struct uart_gd32_data *data = DEV_DATA(dev);
	u32_t base = DEV_BASE(dev);

//TODO	u32_t clock_rate;

	/* Get clock rate */
//TODO	if (clock_control_get_rate(data->clock,
//TODO			       (clock_control_subsys_t *)&config->pclken,
//TODO			       &clock_rate) < 0) {
//TODO		LOG_ERR("Failed call clock_control_get_rate");
//TODO		return;
//TODO	}

	usart_baudrate_set(base, baud_rate);
}

static inline void uart_gd32_set_parity(struct device *dev, u32_t parity)
{
	u32_t base = DEV_BASE(dev);

	usart_parity_config(base, uart_gd32_cfg2ll_parity(parity));
}

static inline u32_t uart_gd32_get_parity(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

	return uart_gd32_ll2cfg_parity( (USART_CTL0(base) & USART_CTL0_PM) >> 9);
}

static inline void uart_gd32_set_stopbits(struct device *dev, u32_t stopbits)
{
	u32_t base = DEV_BASE(dev);

	usart_stop_bit_set(base, stopbits);
}

static inline u32_t uart_gd32_get_stopbits(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

	return uart_gd32_ll2cfg_stopbits( (USART_CTL1(base) & USART_CTL1_STB) >> 12);
}

static inline void uart_gd32_set_databits(struct device *dev, u32_t databits)
{
	u32_t base = DEV_BASE(dev);

	usart_word_length_set(base, databits);
}

static inline u32_t uart_gd32_get_databits(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

	return uart_gd32_ll2cfg_databits( (USART_CTL0(base) & USART_CTL0_WL) >> 12);
}

static inline void uart_gd32_set_hwctrl(struct device *dev, u32_t hwctrl)
{
	u32_t base = DEV_BASE(dev);
	usart_hardware_flow_rts_config(base, (hwctrl>>0 & 0x1));
	usart_hardware_flow_cts_config(base, (hwctrl>>1 & 0x1));
}

static inline u32_t uart_gd32_get_hwctrl(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

	return uart_gd32_ll2cfg_hwctrl( (USART_CTL0(base) & USART_CTL2_HWFC) >> 12);
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

/**
 * @brief  Get LL hardware flow control define from
 *         Zephyr hardware flow control option.
 * @note   Supports only UART_CFG_FLOW_CTRL_RTS_CTS.
 * @param  fc: Zephyr hardware flow control option.
 * @retval LL_USART_HWCONTROL_RTS_CTS, or LL_USART_HWCONTROL_NONE.
 */
static inline u32_t uart_gd32_cfg2ll_hwctrl(enum uart_config_flow_control fc)
{
	if (fc == UART_CFG_FLOW_CTRL_RTS_CTS) {
		return USART_HWFC_RTSCTS;
	}

	return USART_HWFC_NONE;
}

/**
 * @brief  Get Zephyr hardware frlow control option from
 *         LL hardware flow control define.
 * @note   Supports only LL_USART_HWCONTROL_RTS_CTS.
 * @param  fc: LL hardware frlow control definition.
 * @retval UART_CFG_FLOW_CTRL_RTS_CTS, or UART_CFG_PARITY_NONE.
 */
static inline enum uart_config_flow_control uart_gd32_ll2cfg_hwctrl(u32_t fc)
{
	if (fc == USART_HWFC_RTSCTS) {
		return UART_CFG_FLOW_CTRL_RTS_CTS;
	}

	return UART_CFG_PARITY_NONE;
}


/* retarget the C library printf function to the USART */
int put_char(int ch)
{
    usart_data_transmit(USART0, (uint8_t) ch );
    while ( usart_flag_get(USART0, USART_FLAG_TBE)== RESET){
    }

    return ch;
}
static int uart_gd32_configure(struct device *dev,
				const struct uart_config *cfg)
{

#if 0
	struct uart_gd32_data *data = DEV_DATA(dev);
	u32_t base = DEV_BASE(dev);
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

	usart_disable(base);

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

	usart_enable(base);
#endif
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
	u32_t base = DEV_BASE(dev);

	/* Clear overrun error flag */
	if (usart_flag_get(base, USART_FLAG_ORERR) ) {
		usart_flag_clear(base, USART_FLAG_ORERR);
	}

	if (!usart_flag_get(base, USART_FLAG_RBNE) ) {
		return -1;
	}

	*c = (unsigned char)usart_data_receive(base);

	return 0;
}

static void uart_gd32_poll_out(struct device *dev,
					unsigned char c)
{
	put_char(c);
}

static int uart_gd32_err_check(struct device *dev)
{
	u32_t base = DEV_BASE(dev);
	u32_t err = 0U;

	/* Check for errors, but don't clear them here.
	 * Some SoC clear all error flags when at least
	 * one is cleared. (e.g. F4X, F1X, and F2X)
	 */
	if (usart_flag_get(base, USART_FLAG_ORERR)) {
		err |= UART_ERROR_OVERRUN;
	}

	if (usart_flag_get(base, USART_FLAG_PERR)) {
		err |= UART_ERROR_PARITY;
	}

	if (usart_flag_get(base, USART_FLAG_FERR)) {
		err |= UART_ERROR_FRAMING;
	}

	if (err & UART_ERROR_OVERRUN) {
		usart_flag_clear(base, USART_FLAG_ORERR);
	}

	if (err & UART_ERROR_PARITY) {
		usart_flag_clear(base, USART_FLAG_PERR);
	}

	if (err & UART_ERROR_FRAMING) {
		usart_flag_clear(base, USART_FLAG_FERR);
	}

	/* Clear noise error as well,
	 * it is not represented by the errors enum
	 */
	usart_flag_clear(base, USART_FLAG_NERR);

	return err;
}

static inline void __uart_gd32_get_clock(struct device *dev)
{
//TODO	struct uart_gd32_data *data = DEV_DATA(dev);
//TODO	struct device *clk =
//		device_get_binding(GD32_CLOCK_CONTROL_NAME);

//	__ASSERT_NO_MSG(clk);

//	data->clock = clk;
}

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_gd32_fifo_fill(struct device *dev, const u8_t *tx_data,
				  int size)
{
	USART_TypeDef *UartInstance = UART_STRUCT(dev);
	u8_t num_tx = 0U;

	while ((size - num_tx > 0) &&
	       LL_USART_IsActiveFlag_TXE(UartInstance)) {
		/* TXE flag will be cleared with byte write to DR|RDR register */

		/* Send a character (8bit , parity none) */
		LL_USART_TransmitData8(UartInstance, tx_data[num_tx++]);
	}

	return num_tx;
}

static int uart_gd32_fifo_read(struct device *dev, u8_t *rx_data,
				  const int size)
{
//TODO		USART_TypeDef *UartInstance = UART_STRUCT(dev);
	u8_t num_rx = 0U;

	while ((size - num_rx > 0) &&
	       LL_USART_IsActiveFlag_RXNE(UartInstance)) {
		/* RXNE flag will be cleared upon read from DR|RDR register */

		/* Receive a character (8bit , parity none) */
		rx_data[num_rx++] = LL_USART_ReceiveData8(UartInstance);

		/* Clear overrun error flag */
		if (LL_USART_IsActiveFlag_ORE(UartInstance)) {
			LL_USART_ClearFlag_ORE(UartInstance);
		}
	}

	return num_rx;
}

static void uart_gd32_irq_tx_enable(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	LL_USART_EnableIT_TC(UartInstance);
}

static void uart_gd32_irq_tx_disable(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	LL_USART_DisableIT_TC(UartInstance);
}

static int uart_gd32_irq_tx_ready(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	return LL_USART_IsActiveFlag_TXE(UartInstance);
}

static int uart_gd32_irq_tx_complete(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	return LL_USART_IsActiveFlag_TC(UartInstance);
}

static void uart_gd32_irq_rx_enable(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	LL_USART_EnableIT_RXNE(UartInstance);
}

static void uart_gd32_irq_rx_disable(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	LL_USART_DisableIT_RXNE(UartInstance);
}

static int uart_gd32_irq_rx_ready(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

//	return LL_USART_IsActiveFlag_RXNE(UartInstance);
}

static void uart_gd32_irq_err_enable(struct device *dev)
{
	u32_t base = DEV_BASE(dev);

	/* Enable FE, ORE interruptions */
	LL_USART_EnableIT_ERROR(UartInstance);
#if !defined(CONFIG_SOC_SERIES_GD32F0X) || defined(USART_LIN_SUPPORT)
	/* Enable Line break detection */
	if (IS_UART_LIN_INSTANCE(UartInstance)) {
		LL_USART_EnableIT_LBD(UartInstance);
	}
#endif
	/* Enable parity error interruption */
	LL_USART_EnableIT_PE(UartInstance);
}

static void uart_gd32_irq_err_disable(struct device *dev)
{
	USART_TypeDef *UartInstance = UART_STRUCT(dev);

	/* Disable FE, ORE interruptions */
	LL_USART_DisableIT_ERROR(UartInstance);
#if !defined(CONFIG_SOC_SERIES_GD32F0X) || defined(USART_LIN_SUPPORT)
	/* Disable Line break detection */
	if (IS_UART_LIN_INSTANCE(UartInstance)) {
		LL_USART_DisableIT_LBD(UartInstance);
	}
#endif
	/* Disable parity error interruption */
	LL_USART_DisableIT_PE(UartInstance);
}

static int uart_gd32_irq_is_pending(struct device *dev)
{
	USART_TypeDef *UartInstance = UART_STRUCT(dev);

	return ((LL_USART_IsActiveFlag_RXNE(UartInstance) &&
		 LL_USART_IsEnabledIT_RXNE(UartInstance)) ||
		(LL_USART_IsActiveFlag_TC(UartInstance) &&
		 LL_USART_IsEnabledIT_TC(UartInstance)));
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

    /* enable GPIO clock */
    rcu_periph_clock_enable(RCU_GPIOA);

    /* enable USART clock */
    rcu_periph_clock_enable(RCU_USART0);

    /* connect port to USARTx_Tx */
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    /* connect port to USARTx_Rx */
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_10);

    /* USART configure */
    usart_deinit(USART0);
    usart_baudrate_set(USART0, 115200U);
    usart_word_length_set(USART0, USART_WL_8BIT);
    usart_stop_bit_set(USART0, USART_STB_1BIT);
    usart_parity_config(USART0, USART_PM_NONE);
    usart_hardware_flow_rts_config(USART0, USART_RTS_DISABLE);
    usart_hardware_flow_cts_config(USART0, USART_CTS_DISABLE);
    usart_receive_config(USART0, USART_RECEIVE_ENABLE);
    usart_transmit_config(USART0, USART_TRANSMIT_ENABLE);
    usart_enable(USART0);

#if 0

	const struct uart_gd32_config *config = DEV_CFG(dev);
	struct uart_gd32_data *data = DEV_DATA(dev);
	u32_t base = DEV_BASE(dev);

	__uart_gd32_get_clock(dev);
	/* enable clock */
//TODO	if (clock_control_on(data->clock,
//TODO			(clock_control_subsys_t *)&config->pclken) != 0) {
//TODO		return -EIO;
//TODO	}

    /* enable USART clock */
    rcu_periph_clock_enable(RCU_USART0);

    /* connect port to USARTx_Tx */
    gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_9);

    /* connect port to USARTx_Rx */
    gpio_init(GPIOA, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_10);


	usart_deinit(base);
    usart_baudrate_set(USART0, 115200U);

	/* TX/RX direction */
	usart_transmit_config(base, USART_TRANSMIT_ENABLE);
	usart_receive_config(base, USART_RECEIVE_ENABLE);

	/* 8 data bit, 1 start bit, 1 stop bit, no parity */
	uart_gd32_set_databits(dev, USART_WL_8BIT);
	uart_gd32_set_stopbits(dev, USART_STB_1BIT);
	uart_gd32_set_parity(dev, USART_PM_NONE);


	if (config->hw_flow_control) {
		uart_gd32_set_hwctrl(dev, USART_HWFC_RTSCTS);
	}
	else {
		uart_gd32_set_hwctrl(dev, USART_HWFC_NONE);
	}

	/* Set the default baudrate */
	uart_gd32_set_baudrate(dev, data->baud_rate);

	usart_enable(base);

#ifdef USART_ISR_TEACK
	/* Wait until TEACK flag is set */
	while (usart_flag_get() == 0) {
	}
#endif /* !USART_ISR_TEACK */

#ifdef USART_ISR_REACK
	/* Wait until REACK flag is set */
	while (usart_flag_get() == 0) {
	}
#endif /* !USART_ISR_REACK */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	config->uconf.irq_config_func(dev);
#endif

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
		.base = (u8_t *)DT_UART_GD32_##name##_BASE_ADDRESS,\
		GD32_UART_IRQ_HANDLER_FUNC(name)			\
	},								\
	.hw_flow_control = DT_UART_GD32_##name##_HW_FLOW_CONTROL	\
};									\
									\
static struct uart_gd32_data uart_gd32_data_##name = {		\
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

#ifdef CONFIG_UART_6
GD32_UART_INIT(USART_6)
#endif /* CONFIG_UART_6 */

/*
 * GD32F0 and GD32L0 series differ from other GD32 series by some
 * peripheral names (UART vs USART).
 */
#if defined(CONFIG_SOC_SERIES_GD32F0X) || defined(CONFIG_SOC_SERIES_GD32L0X)

#ifdef CONFIG_UART_4
GD32_UART_INIT(USART_4)
#endif /* CONFIG_UART_4 */

#ifdef CONFIG_UART_5
GD32_UART_INIT(USART_5)
#endif /* CONFIG_UART_5 */

/* Following devices are not available in L0 series (for now)
 * But keeping them simplifies ifdefery and won't harm
 */

#ifdef CONFIG_UART_7
GD32_UART_INIT(USART_7)
#endif /* CONFIG_UART_7 */

#ifdef CONFIG_UART_8
GD32_UART_INIT(USART_8)
#endif /* CONFIG_UART_8 */

#else

#ifdef CONFIG_UART_4
GD32_UART_INIT(UART_4)
#endif /* CONFIG_UART_4 */

#ifdef CONFIG_UART_5
GD32_UART_INIT(UART_5)
#endif /* CONFIG_UART_5 */

#ifdef CONFIG_UART_7
GD32_UART_INIT(UART_7)
#endif /* CONFIG_UART_7 */

#ifdef CONFIG_UART_8
GD32_UART_INIT(UART_8)
#endif /* CONFIG_UART_8 */

#ifdef CONFIG_UART_9
GD32_UART_INIT(UART_9)
#endif /* CONFIG_UART_9 */

#ifdef CONFIG_UART_10
GD32_UART_INIT(UART_10)
#endif /* CONFIG_UART_10 */

#endif

#if defined(CONFIG_SOC_SERIES_GD32H7X) || \
	defined(CONFIG_SOC_SERIES_GD32L4X) || \
	defined(CONFIG_SOC_SERIES_GD32L0X) || \
	defined(CONFIG_SOC_SERIES_GD32WBX) || \
	defined(CONFIG_SOC_SERIES_GD32G4X)
#ifdef CONFIG_LPUART_1
GD32_UART_INIT(LPUART_1)
#endif /* CONFIG_LPUART_1 */
#endif
