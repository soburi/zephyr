/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_ra_uart_sci

#include <zephyr/drivers/pinctrl.h>
#include <zephyr/drivers/uart.h>

#include <zephyr/drivers/clock_control.h>

#include <zephyr/logging/log.h>

#include <soc.h>

LOG_MODULE_REGISTER(ra_uart_sci, CONFIG_UART_LOG_LEVEL);

struct uart_ra_config {
	volatile struct sci_regs *regs;
	const struct device *clock_ctrl;
	const struct pinctrl_dev_config *pinconfig;
	const uint32_t channel;
	const uint32_t baudrate;
};

/**
 * @brief Serial Communications Interface (R_SCI0)
 */

struct sci_regs {
	volatile uint8_t SMR;
	volatile uint8_t BRR;
	volatile uint8_t SCR;
	volatile uint8_t TDR;
	volatile uint8_t SSR;
	volatile uint8_t RDR;
	volatile uint8_t SCMR;
	volatile uint8_t SEMR;
	volatile uint8_t SNFR;
	volatile uint8_t SIMR1;
	volatile uint8_t SIMR2;
	volatile uint8_t SIMR3;
	volatile uint8_t SISR;
	volatile uint8_t SPMR;
	volatile uint16_t TDRHL;
	volatile uint16_t RDRHL;
	volatile uint8_t MDDR;
	volatile uint8_t DCCR;
	volatile uint16_t FCR;
	volatile uint16_t FDR;
	volatile uint16_t LSR;
	volatile uint16_t CDR;
	volatile uint8_t SPTR;
};

#define SCI_UART_SCMR_DEFAULT_VALUE (0xF2U)

/* SCI SCR register bit masks */
#define SCI_SCR_TEIE_MASK (0x04U)
#define SCI_SCR_RE_MASK   (0x10U)
#define SCI_SCR_TE_MASK   (0x20U)
#define SCI_SCR_RIE_MASK  (0x40U)
#define SCI_SCR_TIE_MASK  (0x80U)

/* SCI SEMR register bit offsets */
#define SCI_UART_SEMR_BRME_OFFSET  (2U)
#define SCI_UART_SEMR_ABCSE_OFFSET (3U)
#define SCI_UART_SEMR_ABCS_OFFSET  (4U)
#define SCI_UART_SEMR_BGDM_OFFSET  (6U)
#define SCI_UART_SEMR_BAUD_SETTING_MASK                                                            \
	((1U << SCI_UART_SEMR_BRME_OFFSET) | (1U << SCI_UART_SEMR_ABCSE_OFFSET) |                  \
	 (1U << SCI_UART_SEMR_ABCS_OFFSET) | (1U << SCI_UART_SEMR_BGDM_OFFSET))
#define SPTR_OUTPUT_ENABLE_MASK (0x04U)
#define SPTR_SPB2D_BIT          (1U)

#define R_SCI0_SSR_RDRF_Msk (0x40UL)
#define R_SCI0_SSR_TEND_Msk (0x4UL)
#define R_SCI0_SSR_TDRE_Msk (0x80UL)

static int uart_ra_poll_in(const struct device *dev, unsigned char *p_char)
{
	const struct uart_ra_config *config = dev->config;

	if ((config->regs->SSR & R_SCI0_SSR_RDRF_Msk) == 0) {
		return -1;
	}

	*p_char = config->regs->RDR;

	return 0;
}

static void uart_ra_poll_out(const struct device *dev, unsigned char out_char)
{
	const struct uart_ra_config *config = dev->config;

	config->regs->TDR = out_char;

	while (!(config->regs->SSR & R_SCI0_SSR_TEND_Msk) ||
	       !(config->regs->SSR & R_SCI0_SSR_TDRE_Msk)) {
		;
	}
}

static int uart_ra_configure(const struct device *dev, const struct uart_config *cfg)
{
	const struct uart_ra_config *config = dev->config;
	volatile struct sci_regs *regs = config->regs;

	/* Configure parity and stop bits. */
	uint8_t smr = 0;
	uint8_t scmr = SCI_UART_SCMR_DEFAULT_VALUE;
	uint8_t semr = 0;
	uint8_t spmr = 0;
	uint8_t scr = 0;
	// uint8_t brr = 38;
	// uint8_t brr = 53;
	// uint8_t mddr = 184;
	uint8_t mddr = 128;

	const uint32_t pclk = 100000000;

	uint8_t brr = (pclk / (16 * config->baudrate)) - 1;

	switch (cfg->parity) {
	case UART_CFG_PARITY_NONE:
		smr |= 0;
		break;
	case UART_CFG_PARITY_ODD:
		smr |= (3 << 4);
		break;
	case UART_CFG_PARITY_EVEN:
		smr |= (2 << 4);
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->stop_bits) {
	case UART_CFG_STOP_BITS_1:
		smr |= (0 << 3);
		break;
	case UART_CFG_STOP_BITS_2:
		smr |= (1 << 3);
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->data_bits) {
	case UART_CFG_DATA_BITS_7:
		smr |= (1U << 6);
		break;
	case UART_CFG_DATA_BITS_8:
		break;
	case UART_CFG_DATA_BITS_9:
		scmr &= ~(1U << 4);
		break;
	default:
		return -ENOTSUP;
	}

	switch (cfg->flow_ctrl) {
	case UART_CFG_FLOW_CTRL_NONE:
		spmr = 0;
		break;
	case UART_CFG_FLOW_CTRL_RTS_CTS:
		spmr = (3 << 1);
		break;
	default:
		return -ENOTSUP;
	}

	/* Starts reception on falling edge of RXD if enabled in extension (otherwise reception
	 * starts at low level of RXD). */
	semr |= (1U) << 7;

	scr |= SCI_SCR_RE_MASK;
	scr |= SCI_SCR_RIE_MASK;
	scr |= SCI_SCR_TE_MASK;

	regs->SNFR = 0;

	regs->SMR = smr;
	regs->SCMR = scmr;
	regs->SEMR = semr;

	regs->BRR = brr;
	regs->MDDR = mddr;

	/* Set clock divisor settings. */
	regs->SEMR = (uint8_t)((regs->SEMR & ~(SCI_UART_SEMR_BAUD_SETTING_MASK)) |
			       ((BIT(SCI_UART_SEMR_BGDM_OFFSET))));

	regs->SCR = scr;

	regs->SCR &= ~(SCI_SCR_RIE_MASK);
	regs->SCR &= ~(SCI_SCR_TIE_MASK);
	regs->SCR &= ~(SCI_SCR_TEIE_MASK);

	return 0;
}

static int uart_ra_init(const struct device *dev)
{
	const struct uart_ra_config *config = dev->config;
	struct uart_config cfg = {.baudrate = 115200,
				  .parity = UART_CFG_PARITY_NONE,
				  .stop_bits = UART_CFG_STOP_BITS_1,
				  .data_bits = UART_CFG_DATA_BITS_8,
				  .flow_ctrl = 0};
	volatile struct sci_regs *regs = config->regs;
	struct ra_clock_config clockconf = {
		.channel = config->channel,
		.function = 0,
	};
	int ret;

	ret = pinctrl_apply_state(config->pinconfig, PINCTRL_STATE_DEFAULT);
	if (ret) {
		return ret;
	}

	ret = clock_control_on(config->clock_ctrl, &clockconf);
	if (ret) {
		return ret;
	}

	regs->SCR = 0U;
	regs->SSR = 0U;
	regs->SIMR1 = 0U;
	regs->SIMR2 = 0U;
	regs->SIMR3 = 0U;
	regs->CDR = 0U;
	regs->DCCR = 0U;
	regs->FCR = 0;

	/* Set the default level of the TX pin to 1. */
	regs->SPTR = (uint8_t)(1U << SPTR_SPB2D_BIT) | SPTR_OUTPUT_ENABLE_MASK;

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
	PINCTRL_DT_INST_DEFINE(n);                                                                 \
	static struct uart_ra_config uart_ra_config_##n = {                                        \
		.regs = (struct sci_regs *)DT_INST_REG_ADDR(n),                                    \
		.channel = DT_INST_PROP(n, channel),                                               \
		.pinconfig = PINCTRL_DT_INST_DEV_CONFIG_GET(n),                                    \
		.baudrate = 115200,                                                                \
		.clock_ctrl = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n))};                              \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, uart_ra_init, NULL, NULL, &uart_ra_config_##n, PRE_KERNEL_1,      \
			      CONFIG_SERIAL_INIT_PRIORITY, &uart_ra_api);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_UART_RA)
