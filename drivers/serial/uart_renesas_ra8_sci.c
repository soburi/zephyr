/*
 * Copyright (c) 2024 Renesas Electronics Corporation
 * Copyright (c) 2024 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra8_uart_sci

#include <zephyr/kernel.h>
#include <zephyr/drivers/interrupt_controller/intc_ra_icu.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/drivers/pinctrl.h>
#include <zephyr/sys/util.h>
#include <zephyr/irq.h>
#include <zephyr/spinlock.h>
#include <soc.h>
#include "r_sci_uart.h"
#include "r_dtc.h"
#include "r_transfer_api.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(ra8_uart_sci);

struct uart_ra_sci_config {
	R_SCI0_Type * const regs;
	const struct pinctrl_dev_config *pcfg;
	unsigned int rxi_event;
	unsigned int txi_event;
	unsigned int tei_event;
	unsigned int eri_event;
};

struct uart_ra_sci_data {
	const struct device *dev;
	struct k_spinlock lock;
	struct st_sci_uart_instance_ctrl sci;
	struct uart_config uart_config;
	struct st_uart_cfg fsp_config;
	struct st_sci_uart_extended_cfg fsp_config_extend;
	struct st_baud_setting_t fsp_baud_setting;
#if defined(CONFIG_UART_INTERRUPT_DRIVEN)
	uart_irq_callback_user_data_t user_cb;
	void *user_cb_data;
#endif
};

static int uart_ra_sci_poll_in(const struct device *dev, unsigned char *c)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	if (cfg->regs->SSR_b.RDRF == 0U) {
		/* There are no characters available to read. */
		return -1;
	}

	/* got a character */
	*c = (unsigned char)cfg->regs->RDR;

	k_spin_unlock(&data->lock, key);

	return 0;
}

static void uart_ra_sci_poll_out(const struct device *dev, unsigned char c)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	while (cfg->regs->SSR_b.TEND == 0U) {
	}

	cfg->regs->TDR = c;

	k_spin_unlock(&data->lock, key);
}

static int uart_ra_sci_err_check(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;

	const uint32_t status = cfg->regs->SSR;
	int errors = 0;

	if ((status & BIT(R_SCI0_SSR_ORER_Pos)) != 0) {
		errors |= UART_ERROR_OVERRUN;
	}
	if ((status & BIT(R_SCI0_SSR_PER_Pos)) != 0) {
		errors |= UART_ERROR_PARITY;
	}
	if ((status & BIT(R_SCI0_SSR_FER_Pos)) != 0) {
		errors |= UART_ERROR_FRAMING;
	}

	return errors;
}

static int uart_ra_sci_apply_config(const struct uart_config *config,
				    struct st_uart_cfg *fsp_config,
				    struct st_sci_uart_extended_cfg *fsp_config_extend,
				    struct st_baud_setting_t *fsp_baud_setting)
{
	fsp_err_t fsp_err;

	fsp_err = R_SCI_UART_BaudCalculate(config->baudrate, false, 5000, fsp_baud_setting);
	__ASSERT(fsp_err == 0, "sci_uart: baud calculate error");

	switch (config->parity) {
	case UART_CFG_PARITY_NONE:
		fsp_config->parity = UART_PARITY_OFF;
		break;
	case UART_CFG_PARITY_ODD:
		fsp_config->parity = UART_PARITY_ODD;
		break;
	case UART_CFG_PARITY_EVEN:
		fsp_config->parity = UART_PARITY_EVEN;
		break;
	case UART_CFG_PARITY_MARK:
		return -ENOTSUP;
	case UART_CFG_PARITY_SPACE:
		return -ENOTSUP;
	default:
		return -EINVAL;
	}

	switch (config->stop_bits) {
	case UART_CFG_STOP_BITS_0_5:
		return -ENOTSUP;
	case UART_CFG_STOP_BITS_1:
		fsp_config->stop_bits = UART_STOP_BITS_1;
		break;
	case UART_CFG_STOP_BITS_1_5:
		return -ENOTSUP;
	case UART_CFG_STOP_BITS_2:
		fsp_config->stop_bits = UART_STOP_BITS_2;
		break;
	default:
		return -EINVAL;
	}

	switch (config->data_bits) {
	case UART_CFG_DATA_BITS_5:
		return -ENOTSUP;
	case UART_CFG_DATA_BITS_6:
		return -ENOTSUP;
	case UART_CFG_DATA_BITS_7:
		fsp_config->data_bits = UART_DATA_BITS_7;
		break;
	case UART_CFG_DATA_BITS_8:
		fsp_config->data_bits = UART_DATA_BITS_8;
		break;
	case UART_CFG_DATA_BITS_9:
		fsp_config->data_bits = UART_DATA_BITS_9;
		break;
	default:
		return -EINVAL;
	}

	fsp_config_extend->clock = SCI_UART_CLOCK_INT;
	fsp_config_extend->rx_edge_start = SCI_UART_START_BIT_FALLING_EDGE;
	fsp_config_extend->noise_cancel = SCI_UART_NOISE_CANCELLATION_DISABLE;
	fsp_config_extend->flow_control_pin = UINT16_MAX;

	switch (config->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		fsp_config_extend->flow_control = 0;
		fsp_config_extend->rs485_setting.enable = false;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		fsp_config_extend->flow_control = SCI_UART_FLOW_CONTROL_HARDWARE_CTSRTS;
		fsp_config_extend->rs485_setting.enable = false;
		break;
	case UART_CFG_FLOW_CTRL_DTR_DSR:
		return -ENOTSUP;
	case UART_CFG_FLOW_CTRL_RS485:
		/* TODO: implement this config */
		return -ENOTSUP;
	default:
		return -EINVAL;
	}

	return 0;
}

#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE

static int uart_ra_sci_configure(const struct device *dev, const struct uart_config *cfg)
{
	int err;
	fsp_err_t fsp_err;
	struct uart_ra_sci_data *data = dev->data;

	err = uart_ra_sci_apply_config(cfg, &data->fsp_config, &data->fsp_config_extend,
				       &data->fsp_baud_setting);
	if (err) {
		return err;
	}

	fsp_err = R_SCI_UART_Close(&data->sci);
	__ASSERT(fsp_err == 0, "sci_uart: configure: fsp close failed");

	fsp_err = R_SCI_UART_Open(&data->sci, &data->fsp_config);
	__ASSERT(fsp_err == 0, "sci_uart: configure: fsp open failed");
	memcpy(&data->uart_config, cfg, sizeof(struct uart_config));

	return err;
}

static int uart_ra_sci_config_get(const struct device *dev, struct uart_config *cfg)
{
	struct uart_ra_sci_data *data = dev->data;

	memcpy(cfg, &data->uart_config, sizeof(*cfg));
	return 0;
}

#endif /* CONFIG_UART_USE_RUNTIME_CONFIGURE */

#ifdef CONFIG_UART_INTERRUPT_DRIVEN

static int uart_ra_sci_fifo_fill(const struct device *dev, const uint8_t *tx_data, int size)
{
	struct uart_ra_sci_data *data = dev->data;
	const struct uart_ra_sci_config *cfg = dev->config;
	uint8_t num_tx = 0U;
	k_spinlock_key_t key;

	if (size <= 0 || tx_data == NULL) {
		return 0;
	}

	key = k_spin_lock(&data->lock);

	if (size > 0 && cfg->regs->SSR_b.TDRE) {
		/* TEND flag will be cleared with byte write to TDR register */

		/* Send a character (8bit , parity none) */
		cfg->regs->TDR = tx_data[num_tx++];
	}

	k_spin_unlock(&data->lock, key);

	return num_tx;
}

static int uart_ra_sci_fifo_read(const struct device *dev, uint8_t *rx_data, const int size)
{
	struct uart_ra_sci_data *data = dev->data;
	const struct uart_ra_sci_config *cfg = dev->config;
	uint8_t num_rx = 0U;
	k_spinlock_key_t key;

	if (size <= 0 || rx_data == NULL) {
		return 0;
	}

	key = k_spin_lock(&data->lock);

	if (size > 0 && cfg->regs->SSR_b.RDRF) {
		/* Receive a character (8bit , parity none) */
		rx_data[num_rx++] = cfg->regs->RDR;
	}

	/* Clear overrun error flag */
	cfg->regs->SSR_FIFO_b.ORER = 0U;

	k_spin_unlock(&data->lock, key);

	return num_rx;
}

static void uart_ra_sci_irq_tx_enable(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	cfg->regs->SCR |= (BIT(R_SCI0_SCR_TIE_Pos) | BIT(R_SCI0_SCR_TEIE_Pos));

	k_spin_unlock(&data->lock, key);
}

static void uart_ra_sci_irq_tx_disable(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	cfg->regs->SCR &= ~(BIT(R_SCI0_SCR_TIE_Pos) | BIT(R_SCI0_SCR_TEIE_Pos));

	k_spin_unlock(&data->lock, key);
}

static int uart_ra_sci_irq_tx_ready(const struct device *dev)
{
	struct uart_ra_sci_data *data = dev->data;
	const struct uart_ra_sci_config *cfg = dev->config;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	int ret;

	ret = (cfg->regs->SCR_b.TIE &&
	       (cfg->regs->SSR & (BIT(R_SCI0_SSR_TDRE_Pos) | BIT(R_SCI0_SSR_TEND_Pos))));

	k_spin_unlock(&data->lock, key);

	return ret;
}

static int uart_ra_sci_irq_tx_complete(const struct device *dev)
{
	struct uart_ra_sci_data *data = dev->data;
	const struct uart_ra_sci_config *cfg = dev->config;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	int ret;

	ret = (cfg->regs->SCR_b.TEIE && cfg->regs->SSR_b.TEND);

	k_spin_unlock(&data->lock, key);

	return ret;
}

static void uart_ra_sci_irq_rx_enable(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	cfg->regs->SCR_b.RIE = 1U;

	k_spin_unlock(&data->lock, key);
}

static void uart_ra_sci_irq_rx_disable(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	cfg->regs->SCR_b.RIE = 0U;

	k_spin_unlock(&data->lock, key);
}

static int uart_ra_sci_irq_rx_ready(const struct device *dev)
{
	struct uart_ra_sci_data *data = dev->data;
	const struct uart_ra_sci_config *cfg = dev->config;
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	int ret;

	ret = (cfg->regs->SCR_b.RIE && cfg->regs->SSR_b.RDRF);

	k_spin_unlock(&data->lock, key);

	return ret;
}

static void uart_ra_sci_irq_err_enable(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;

	irq_enable(cfg->eri_event);
}

static void uart_ra_sci_irq_err_disable(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;

	irq_disable(cfg->eri_event);
}

static int uart_ra_sci_irq_is_pending(const struct device *dev)
{
	const struct uart_ra_sci_config *cfg = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	k_spinlock_key_t key = k_spin_lock(&data->lock);

	const uint32_t scr = cfg->regs->SCR;
	const uint32_t ssr = cfg->regs->SSR;

	const bool tx_pending = ((scr & BIT(R_SCI0_SCR_TIE_Pos)) &&
				 (ssr & (BIT(R_SCI0_SSR_TEND_Pos) | BIT(R_SCI0_SSR_TDRE_Pos))));
	const bool rx_pending = ((scr & BIT(R_SCI0_SCR_RIE_Pos)) &&
				 ((ssr & (BIT(R_SCI0_SSR_RDRF_Pos) | BIT(R_SCI0_SSR_PER_Pos) |
					  BIT(R_SCI0_SSR_FER_Pos) | BIT(R_SCI0_SSR_ORER_Pos)))));

	k_spin_unlock(&data->lock, key);

	return tx_pending || rx_pending;
}

static int uart_ra_sci_irq_update(const struct device *dev)
{
	return 1;
}

static void uart_ra_sci_irq_callback_set(const struct device *dev, uart_irq_callback_user_data_t cb,
					 void *cb_data)
{
	struct uart_ra_sci_data *data = dev->data;

	data->user_cb = cb;
	data->user_cb_data = cb_data;
}

#endif /* CONFIG_UART_INTERRUPT_DRIVEN */

static const struct uart_driver_api uart_ra_sci_driver_api = {
	.poll_in = uart_ra_sci_poll_in,
	.poll_out = uart_ra_sci_poll_out,
	.err_check = uart_ra_sci_err_check,
#ifdef CONFIG_UART_USE_RUNTIME_CONFIGURE
	.configure = uart_ra_sci_configure,
	.config_get = uart_ra_sci_config_get,
#endif
#ifdef CONFIG_UART_INTERRUPT_DRIVEN
	.fifo_fill = uart_ra_sci_fifo_fill,
	.fifo_read = uart_ra_sci_fifo_read,
	.irq_tx_enable = uart_ra_sci_irq_tx_enable,
	.irq_tx_disable = uart_ra_sci_irq_tx_disable,
	.irq_tx_ready = uart_ra_sci_irq_tx_ready,
	.irq_rx_enable = uart_ra_sci_irq_rx_enable,
	.irq_rx_disable = uart_ra_sci_irq_rx_disable,
	.irq_tx_complete = uart_ra_sci_irq_tx_complete,
	.irq_rx_ready = uart_ra_sci_irq_rx_ready,
	.irq_err_enable = uart_ra_sci_irq_err_enable,
	.irq_err_disable = uart_ra_sci_irq_err_disable,
	.irq_is_pending = uart_ra_sci_irq_is_pending,
	.irq_update = uart_ra_sci_irq_update,
	.irq_callback_set = uart_ra_sci_irq_callback_set,
#endif /* CONFIG_UART_INTERRUPT_DRIVEN */
};

static int uart_ra_sci_init(const struct device *dev)
{
	const struct uart_ra_sci_config *config = dev->config;
	struct uart_ra_sci_data *data = dev->data;
	int ret;
	fsp_err_t fsp_err;

	/* Configure dt provided device signals when available */
	ret = pinctrl_apply_state(config->pcfg, PINCTRL_STATE_DEFAULT);
	if (ret < 0) {
		return ret;
	}

	/* Setup fsp sci_uart setting */
	ret = uart_ra_sci_apply_config(&data->uart_config, &data->fsp_config,
				       &data->fsp_config_extend, &data->fsp_baud_setting);
	if (ret != 0) {
		return ret;
	}

	data->fsp_config_extend.p_baud_setting = &data->fsp_baud_setting;
	data->fsp_config.p_extend = &data->fsp_config_extend;

	fsp_err = R_SCI_UART_Open(&data->sci, &data->fsp_config);
	__ASSERT(fsp_err == 0, "sci_uart: initialization: open failed");

	return 0;
}

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)

static void uart_ra_sci_rxi_isr(const struct device *dev)
{
	const struct uart_ra_sci_config *config = dev->config;
	struct uart_ra_sci_data *data = dev->data;

	if (data->user_cb != NULL) {
		data->user_cb(dev, data->user_cb_data);
	}

	ra_icu_clear_event_flag(config->rxi_event);
}

static void uart_ra_sci_txi_isr(const struct device *dev)
{
	const struct uart_ra_sci_config *config = dev->config;
	struct uart_ra_sci_data *data = dev->data;

	if (data->user_cb != NULL) {
		data->user_cb(dev, data->user_cb_data);
	}

	ra_icu_clear_event_flag(config->txi_event);
}

static void uart_ra_sci_tei_isr(const struct device *dev)
{
	const struct uart_ra_sci_config *config = dev->config;
	struct uart_ra_sci_data *data = dev->data;

	if (data->user_cb != NULL) {
		data->user_cb(dev, data->user_cb_data);
	}

	ra_icu_clear_event_flag(config->tei_event);
}

static void uart_ra_sci_eri_isr(const struct device *dev)
{
	const struct uart_ra_sci_config *config = dev->config;
	struct uart_ra_sci_data *data = dev->data;

	if (data->user_cb != NULL) {
		data->user_cb(dev, data->user_cb_data);
	}

	ra_icu_clear_event_flag(config->eri_event);
}

#endif /* defined(CONFIG_UART_INTERRUPT_DRIVEN) */

#define _ELC_EVENT_SCI_RXI(channel) ELC_EVENT_SCI##channel##_RXI
#define _ELC_EVENT_SCI_TXI(channel) ELC_EVENT_SCI##channel##_TXI
#define _ELC_EVENT_SCI_TEI(channel) ELC_EVENT_SCI##channel##_TEI
#define _ELC_EVENT_SCI_ERI(channel) ELC_EVENT_SCI##channel##_ERI

#define ELC_EVENT_SCI_RXI(channel) _ELC_EVENT_SCI_RXI(channel)
#define ELC_EVENT_SCI_TXI(channel) _ELC_EVENT_SCI_TXI(channel)
#define ELC_EVENT_SCI_TEI(channel) _ELC_EVENT_SCI_TEI(channel)
#define ELC_EVENT_SCI_ERI(channel) _ELC_EVENT_SCI_ERI(channel)

#if defined(CONFIG_UART_INTERRUPT_DRIVEN)

#define UART_RA_SCI_IRQ_CONFIG_INIT(index)                                                         \
	do {                                                                                       \
		IRQ_CONNECT(DT_IRQ_BY_NAME(DT_INST_PARENT(index), rxi, event),                     \
			    DT_IRQ_BY_NAME(DT_INST_PARENT(index), rxi, priority),                  \
			    uart_ra_sci_rxi_isr, DEVICE_DT_INST_GET(index), 0);                    \
		irq_enable(DT_IRQ_BY_NAME(DT_INST_PARENT(index), rxi, event));                     \
		IRQ_CONNECT(DT_IRQ_BY_NAME(DT_INST_PARENT(index), txi, event),                     \
			    DT_IRQ_BY_NAME(DT_INST_PARENT(index), txi, priority),                  \
			    uart_ra_sci_txi_isr, DEVICE_DT_INST_GET(index), 0);                    \
		irq_enable(DT_IRQ_BY_NAME(DT_INST_PARENT(index), txi, event));                     \
		IRQ_CONNECT(DT_IRQ_BY_NAME(DT_INST_PARENT(index), tei, event),                     \
			    DT_IRQ_BY_NAME(DT_INST_PARENT(index), tei, priority),                  \
			    uart_ra_sci_tei_isr, DEVICE_DT_INST_GET(index), 0);                    \
		irq_enable(DT_IRQ_BY_NAME(DT_INST_PARENT(index), tei, event));                     \
		IRQ_CONNECT(DT_IRQ_BY_NAME(DT_INST_PARENT(index), eri, event),                     \
			    DT_IRQ_BY_NAME(DT_INST_PARENT(index), eri, priority),                  \
			    uart_ra_sci_eri_isr, DEVICE_DT_INST_GET(index), 0);                    \
		irq_enable(DT_IRQ_BY_NAME(DT_INST_PARENT(index), eri, event));                     \
	} while (0)

#else

#define UART_RA_SCI_IRQ_CONFIG_INIT(index)

#endif

#define UART_RA_SCI_INIT(index)                                                                    \
	PINCTRL_DT_DEFINE(DT_INST_PARENT(index));                                                  \
                                                                                                   \
	static const struct uart_ra_sci_config uart_ra_sci_config_##index = {                      \
		.pcfg = PINCTRL_DT_DEV_CONFIG_GET(DT_INST_PARENT(index)),                          \
		.regs = (R_SCI0_Type *)DT_REG_ADDR(DT_INST_PARENT(index)),                         \
		.rxi_event = DT_IRQ_BY_NAME(DT_INST_PARENT(index), rxi, event),                    \
		.txi_event = DT_IRQ_BY_NAME(DT_INST_PARENT(index), txi, event),                    \
		.tei_event = DT_IRQ_BY_NAME(DT_INST_PARENT(index), tei, event),                    \
		.eri_event = DT_IRQ_BY_NAME(DT_INST_PARENT(index), eri, event),                    \
	};                                                                                         \
                                                                                                   \
	static struct uart_ra_sci_data uart_ra_sci_data_##index = {                                \
		.uart_config =                                                                     \
			{                                                                          \
				.baudrate = DT_INST_PROP(index, current_speed),                    \
				.parity = UART_CFG_PARITY_NONE,                                    \
				.stop_bits = UART_CFG_STOP_BITS_1,                                 \
				.data_bits = UART_CFG_DATA_BITS_8,                                 \
				.flow_ctrl = COND_CODE_1(DT_NODE_HAS_PROP(idx, hw_flow_control),   \
							 (UART_CFG_FLOW_CTRL_RTS_CTS),             \
							 (UART_CFG_FLOW_CTRL_NONE)),               \
			},                                                                         \
		.fsp_config = {},                                                                  \
		.fsp_config_extend = {},                                                           \
		.fsp_baud_setting = {},                                                            \
		.dev = DEVICE_DT_GET(DT_DRV_INST(index))};                                         \
                                                                                                   \
	static int uart_ra_sci_init_##index(const struct device *dev)                              \
	{                                                                                          \
		struct uart_ra_sci_data *data = dev->data;                                         \
		UART_RA_SCI_IRQ_CONFIG_INIT(index);                                                \
		data->fsp_config.channel = DT_INST_PROP(index, channel);                           \
		data->fsp_config.rxi_ipl = DT_IRQ_BY_NAME(DT_INST_PARENT(index), rxi, priority);   \
		data->fsp_config.rxi_irq = function_irq_table[DT_IRQ_BY_NAME(DT_INST_PARENT(index), rxi, event)];      \
		data->fsp_config.txi_ipl = DT_IRQ_BY_NAME(DT_INST_PARENT(index), txi, priority);   \
		data->fsp_config.txi_irq = function_irq_table[DT_IRQ_BY_NAME(DT_INST_PARENT(index), txi, event)];      \
		data->fsp_config.tei_ipl = DT_IRQ_BY_NAME(DT_INST_PARENT(index), tei, priority);   \
		data->fsp_config.tei_irq = function_irq_table[DT_IRQ_BY_NAME(DT_INST_PARENT(index), tei, event)];      \
		data->fsp_config.eri_ipl = DT_IRQ_BY_NAME(DT_INST_PARENT(index), eri, priority);   \
		data->fsp_config.eri_irq = function_irq_table[DT_IRQ_BY_NAME(DT_INST_PARENT(index), eri, event)];      \
		int err = uart_ra_sci_init(dev);                                                   \
		if (err != 0) {                                                                    \
			return err;                                                                \
		}                                                                                  \
		return 0;                                                                          \
	}                                                                                          \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(index, uart_ra_sci_init_##index, NULL,                               \
			      &uart_ra_sci_data_##index, &uart_ra_sci_config_##index,              \
			      PRE_KERNEL_1, CONFIG_SERIAL_INIT_PRIORITY, &uart_ra_sci_driver_api);

DT_INST_FOREACH_STATUS_OKAY(UART_RA_SCI_INIT)
