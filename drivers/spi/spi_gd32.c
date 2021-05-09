/*
 * Copyright (c) 2016 BayLibre, SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT gigadevice_gd32_spi

#define LOG_LEVEL CONFIG_SPI_LOG_LEVEL
#include <logging/log.h>
LOG_MODULE_REGISTER(spi_gd32);

#include <sys/util.h>
#include <kernel.h>
#include <soc.h>
#include <errno.h>
#include <drivers/spi.h>
#include <toolchain.h>
#include <drivers/clock_control.h>

#include "gd32vf103_spi.h"
#include "gd32vf103_gpio.h"
#include "spi_context.h"

#include <gigadevice_gd32_dt.h>

#define POLARITY_LOW  0
#define POLARITY_HIGH 1
#define PHASE_1EDGE 1
#define PHASE_2EDGE 1



#define DEV_CFG(dev)						\
(const struct spi_gd32_config * const)(dev->config)

#define DEV_DATA(dev)					\
(struct spi_gd32_data * const)(dev->data)

/*
 * Check for SPI_SR_FRE to determine support for TI mode frame format
 * error flag, because GD32F1 SoCs do not support it and  GD32CUBE
 * for F1 family defines an unused LL_SPI_SR_FRE.
 */

#define SPI_GD32_ERR_MSK (SPI_STAT_CRCERR | SPI_STAT_CONFERR | SPI_STAT_RXORERR)
#define BASE_ADDR(spi) (uint32_t)(spi)

typedef void (*irq_config_func_t)(struct device *port);

typedef uint32_t SPI_TypeDef;

struct spi_gd32_config {
	struct gd32_pclken pclken;
	SPI_TypeDef *spi;
        const struct soc_gpio_pinctrl *pinctrl_list;
        size_t pinctrl_list_size;
#ifdef CONFIG_SPI_GD32_INTERRUPT
	irq_config_func_t irq_config;
#endif
};

struct spi_gd32_data {
	struct spi_context ctx;
};

static inline FlagStatus spi_get_master_mode(uint32_t spi_periph)
{
	return (SPI_CTL0(spi_periph) & SPI_CTL0_MSTMOD) ? SET : RESET;
}

static inline void spi_i2s_flag_clear(uint32_t spi_periph, uint32_t flag)
{
	SPI_STAT(spi_periph) &= ~flag;
}


/* Value to shift out when no application data needs transmitting. */
#define SPI_GD32_TX_NOP 0x00

static bool spi_gd32_transfer_ongoing(struct spi_gd32_data *data)
{
	return spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx);
}

static int spi_gd32_get_err(SPI_TypeDef *spi)
{
	uint32_t sr = SPI_STAT(BASE_ADDR(spi));

	if (sr & SPI_GD32_ERR_MSK) {
		LOG_ERR("%s: err=%d", __func__,
			    sr & (uint32_t)SPI_GD32_ERR_MSK);

		/* OVR error must be explicitly cleared */
		if (spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_RXORERR)) {
			spi_i2s_flag_clear(BASE_ADDR(spi), SPI_FLAG_RXORERR);
		}

		return -EIO;
	}

	return 0;
}

/* Shift a SPI frame as master. */
static void spi_gd32_shift_m(SPI_TypeDef *spi, struct spi_gd32_data *data)
{
	uint16_t tx_frame = SPI_GD32_TX_NOP;
	uint16_t rx_frame;

	while (!spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_TBE) ) {
		/* NOP */
	}

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		if (spi_context_tx_buf_on(&data->ctx)) {
			tx_frame = UNALIGNED_GET((uint8_t *)(data->ctx.tx_buf));
		}
		spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
		/* The update is ignored if TX is off. */
		spi_context_update_tx(&data->ctx, 1, 1);
	} else {
		if (spi_context_tx_buf_on(&data->ctx)) {
			tx_frame = UNALIGNED_GET((uint16_t *)(data->ctx.tx_buf));
		}
		spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
		/* The update is ignored if TX is off. */
		spi_context_update_tx(&data->ctx, 2, 1);
	}

	while (!spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_RBNE)) {
		/* NOP */
	}

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
		if (spi_context_rx_buf_on(&data->ctx)) {
			UNALIGNED_PUT(rx_frame, (uint8_t *)data->ctx.rx_buf);
		}
		spi_context_update_rx(&data->ctx, 1, 1);
	} else {
		rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
		if (spi_context_rx_buf_on(&data->ctx)) {
			UNALIGNED_PUT(rx_frame, (uint16_t *)data->ctx.rx_buf);
		}
		spi_context_update_rx(&data->ctx, 2, 1);
	}
}

/* Shift a SPI frame as slave. */
static void spi_gd32_shift_s(SPI_TypeDef *spi, struct spi_gd32_data *data)
{
	if (spi_i2s_flag_get(BASE_ADDR(spi), I2S_FLAG_TBE) && spi_context_tx_on(&data->ctx)) {
		uint16_t tx_frame;

		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			tx_frame = UNALIGNED_GET((uint8_t *)(data->ctx.tx_buf));
			spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
			spi_context_update_tx(&data->ctx, 1, 1);
		} else {
			tx_frame = UNALIGNED_GET((uint16_t *)(data->ctx.tx_buf));
			spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
			spi_context_update_tx(&data->ctx, 2, 1);
		}
	} else {
		spi_i2s_interrupt_disable(BASE_ADDR(spi), SPI_I2S_INT_TBE);
	}

	if (spi_i2s_flag_get(BASE_ADDR(spi), SPI_I2S_INT_FLAG_RBNE) &&
	    spi_context_rx_buf_on(&data->ctx)) {
		uint16_t rx_frame;

		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
			UNALIGNED_PUT(rx_frame, (uint8_t *)data->ctx.rx_buf);
			spi_context_update_rx(&data->ctx, 1, 1);
		} else {
			rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
			UNALIGNED_PUT(rx_frame, (uint16_t *)data->ctx.rx_buf);
			spi_context_update_rx(&data->ctx, 2, 1);
		}
	}
}

/*
 * Without a FIFO, we can only shift out one frame's worth of SPI
 * data, and read the response back.
 *
 * TODO: support 16-bit data frames.
 */
static int spi_gd32_shift_frames(SPI_TypeDef *spi, struct spi_gd32_data *data)
{
	uint16_t operation = data->ctx.config->operation;

	if (SPI_OP_MODE_GET(operation) == SPI_OP_MODE_MASTER) {
		spi_gd32_shift_m(spi, data);
	} else {
		spi_gd32_shift_s(spi, data);
	}

	return spi_gd32_get_err(spi);
}

static void spi_gd32_complete(struct spi_gd32_data *data, SPI_TypeDef *spi,
			       int status)
{
#ifdef CONFIG_SPI_GD32_INTERRUPT
	spi_i2s_interrupt_disable(BASE_ADDR(spi), SPI_I2S_INT_TBE);
	spi_i2s_interrupt_disable(BASE_ADDR(spi), SPI_I2S_INT_RBNE);
	spi_i2s_interrupt_disable(BASE_ADDR(spi), SPI_I2S_INT_ERR);
#endif

	spi_context_cs_control(&data->ctx, false);


	if (spi_get_master_mode(BASE_ADDR(spi))) {
		while (spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_TRANS)) {
			/* NOP */
		}
	}
	/* BSY flag is cleared when MODF flag is raised */
	if (spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_CONFERR)) {
		spi_i2s_flag_clear(BASE_ADDR(spi), SPI_FLAG_CONFERR);
	}

	spi_disable(BASE_ADDR(spi));

#ifdef CONFIG_SPI_GD32_INTERRUPT
	spi_context_complete(&data->ctx, status);
#endif
}

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_isr(const struct device *dev)
{
	const struct spi_gd32_config *cfg = dev->config;
	struct spi_gd32_data *data = dev->data;
	SPI_TypeDef *spi = cfg->spi;
	int err;

	err = spi_gd32_get_err(spi);
	if (err) {
		spi_gd32_complete(data, spi, err);
		return;
	}

	if (spi_gd32_transfer_ongoing(data)) {
		err = spi_gd32_shift_frames(spi, data);
	}

	if (err || !spi_gd32_transfer_ongoing(data)) {
		spi_gd32_complete(data, spi, err);
	}
}
#endif

static int spi_gd32_configure(const struct device *dev,
			       const struct spi_config *config)
{
	const struct spi_gd32_config *cfg = DEV_CFG(dev);
	struct spi_gd32_data *data = DEV_DATA(dev);
	const uint32_t scaler[] = {
		SPI_PSC_2,
		SPI_PSC_4,
		SPI_PSC_8,
		SPI_PSC_16,
		SPI_PSC_32,
		SPI_PSC_64,
		SPI_PSC_128,
		SPI_PSC_256
	};
	SPI_TypeDef *spi = cfg->spi;
	uint32_t clock;
	int br;

	if (spi_context_configured(&data->ctx, config)) {
		/* Nothing to do */
		return 0;
	}

	if ((SPI_WORD_SIZE_GET(config->operation) != 8)
	    && (SPI_WORD_SIZE_GET(config->operation) != 16)) {
		return -ENOTSUP;
	}

	if (clock_control_get_rate(DEVICE_DT_GET(GD32_CLOCK_CONTROL_NODE),
			(clock_control_subsys_t) &cfg->pclken, &clock) < 0) {
		LOG_ERR("Failed call clock_control_get_rate");
		return -EIO;
	}

	for (br = 1 ; br <= ARRAY_SIZE(scaler) ; ++br) {
		uint32_t clk = clock >> br;

		if (clk <= config->frequency) {
			break;
		}
	}

	if (br > ARRAY_SIZE(scaler)) {
		LOG_ERR("Unsupported frequency %uHz, max %uHz, min %uHz",
			    config->frequency,
			    clock >> 1,
			    clock >> ARRAY_SIZE(scaler));
		return -EINVAL;
	}

	spi_i2s_deinit(BASE_ADDR(spi));
	spi_disable(BASE_ADDR(spi));

	spi_parameter_struct param;

	spi_struct_para_init(&param);

	param.prescale = scaler[br-1];


	if (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) {
		param.clock_polarity_phase  = (POLARITY_HIGH);
	} else {
		param.clock_polarity_phase  = (POLARITY_LOW);
	}

	if (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) {
		param.clock_polarity_phase |= (PHASE_2EDGE);
	} else {
		param.clock_polarity_phase |= (PHASE_1EDGE);
	}

	spi_bidirectional_transfer_config(BASE_ADDR(spi), SPI_TRANSMODE_FULLDUPLEX);

	if (config->operation & SPI_TRANSFER_LSB) {
		param.endian = SPI_ENDIAN_LSB;
	} else {
		param.endian = SPI_ENDIAN_MSB;
	}

	spi_crc_off(BASE_ADDR(spi));

	if (config->cs || !IS_ENABLED(CONFIG_SPI_GD32_USE_HW_SS)) {
		param.nss = SPI_NSS_SOFT;
	} else {
		param.nss = SPI_NSS_HARD;
	}

	if (config->operation & SPI_OP_MODE_SLAVE) {
		param.device_mode = SPI_SLAVE;
	} else {
		param.device_mode = SPI_MASTER;
	}

	if (SPI_WORD_SIZE_GET(config->operation) ==  8) {
		param.frame_size = SPI_FRAMESIZE_8BIT;
	} else {
		param.frame_size = SPI_FRAMESIZE_16BIT;
	}

	spi_init(BASE_ADDR(spi), &param);

	/* At this point, it's mandatory to set this on the context! */
	data->ctx.config = config;

	spi_context_cs_configure(&data->ctx);

	LOG_DBG("Installed config %p: freq %uHz (div = %u),"
		    " mode %u/%u/%u, slave %u",
		    config, clock >> br, 1 << br,
		    (SPI_MODE_GET(config->operation) & SPI_MODE_CPOL) ? 1 : 0,
		    (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) ? 1 : 0,
		    (SPI_MODE_GET(config->operation) & SPI_MODE_LOOP) ? 1 : 0,
		    config->slave);

	return 0;
}

static int spi_gd32_release(const struct device *dev,
			     const struct spi_config *config)
{
	struct spi_gd32_data *data = DEV_DATA(dev);

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static int transceive(const struct device *dev,
		      const struct spi_config *config,
		      const struct spi_buf_set *tx_bufs,
		      const struct spi_buf_set *rx_bufs,
		      bool asynchronous, struct k_poll_signal *signal)
{
	const struct spi_gd32_config *cfg = DEV_CFG(dev);
	struct spi_gd32_data *data = DEV_DATA(dev);
	SPI_TypeDef *spi = cfg->spi;
	int ret;

	if (!tx_bufs && !rx_bufs) {
		return 0;
	}

#ifndef CONFIG_SPI_GD32_INTERRUPT
	if (asynchronous) {
		return -ENOTSUP;
	}
#endif

	spi_context_lock(&data->ctx, asynchronous, signal, config);

	ret = spi_gd32_configure(dev, config);
	if (ret) {
		return ret;
	}

	/* Set buffers info */
	spi_context_buffers_setup(&data->ctx, tx_bufs, rx_bufs, 1);


	spi_enable(BASE_ADDR(spi));

	/* This is turned off in spi_gd32_complete(). */
	spi_context_cs_control(&data->ctx, true);

#ifdef CONFIG_SPI_GD32_INTERRUPT
	spi_i2s_interrupt_enable(BASE_ADDR(spi), SPI_I2S_INT_ERR);

	if (rx_bufs) {
		spi_i2s_interrupt_enable(BASE_ADDR(spi), SPI_I2S_INT_RBNE);
	}

	spi_i2s_interrupt_enable(BASE_ADDR(spi), SPI_I2S_INT_TBE);

	ret = spi_context_wait_for_completion(&data->ctx);
#else
	do {
		ret = spi_gd32_shift_frames(spi, data);
	} while (!ret && spi_gd32_transfer_ongoing(data));

	spi_gd32_complete(data, spi, ret);

#ifdef CONFIG_SPI_SLAVE
	if (spi_context_is_slave(&data->ctx) && !ret) {
		ret = data->ctx.recv_frames;
	}
#endif /* CONFIG_SPI_SLAVE */

#endif

	spi_context_release(&data->ctx, ret);

	return ret;
}

static int spi_gd32_transceive(const struct device *dev,
				const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	return transceive(dev, config, tx_bufs, rx_bufs, false, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_gd32_transceive_async(const struct device *dev,
				      const struct spi_config *config,
				      const struct spi_buf_set *tx_bufs,
				      const struct spi_buf_set *rx_bufs,
				      struct k_poll_signal *async)
{
	return transceive(dev, config, tx_bufs, rx_bufs, true, async);
}
#endif /* CONFIG_SPI_ASYNC */

static const struct spi_driver_api api_funcs = {
	.transceive = spi_gd32_transceive,
#ifdef CONFIG_SPI_ASYNC
	.transceive_async = spi_gd32_transceive_async,
#endif
	.release = spi_gd32_release,
};

static int spi_gd32_init(const struct device *dev)
{
	struct spi_gd32_data *data __attribute__((unused)) = dev->data;
	const struct spi_gd32_config *cfg = dev->config;
//	int err;

	if (clock_control_on(DEVICE_DT_GET(GD32_CLOCK_CONTROL_NODE),
			       (clock_control_subsys_t) &cfg->pclken) != 0) {
		LOG_ERR("Could not enable SPI clock");
		return -EIO;
	}

	/* Configure dt provided device signals when available */
//	err = gd32_dt_pinctrl_configure(cfg->pinctrl_list,
//					 cfg->pinctrl_list_size,
//					 (uint32_t)cfg->spi);
//	if (err < 0) {
//		LOG_ERR("SPI pinctrl setup failed (%d)", err);
//		return err;
//	}
	if((uint32_t)cfg->spi == SPI0) {
		rcu_periph_clock_enable(RCU_GPIOA);
		rcu_periph_clock_enable(RCU_GPIOB);

		rcu_periph_clock_enable(RCU_AF);
		rcu_periph_clock_enable(RCU_SPI0);
		/* SPI0 GPIO config: NSS/PA4, SCK/PA5, MOSI/PA7 */
		gpio_init(GPIOA, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_5 |GPIO_PIN_6| GPIO_PIN_7);
		gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_2);
	} else if((uint32_t)cfg->spi == SPI1) {
		rcu_periph_clock_enable(RCU_GPIOB);
		rcu_periph_clock_enable(RCU_AF);
		rcu_periph_clock_enable(RCU_SPI1);

		/* SPI1_SCK(PB13), SPI1_MISO(PB14) and SPI1_MOSI(PB15) GPIO pin configuration */
		gpio_init(GPIOB, GPIO_MODE_AF_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_13 | GPIO_PIN_15);
		gpio_init(GPIOB, GPIO_MODE_IN_FLOATING, GPIO_OSPEED_50MHZ, GPIO_PIN_14);
		/* SPI1_CS(PB12) GPIO pin configuration */
		gpio_init(GPIOB, GPIO_MODE_OUT_PP, GPIO_OSPEED_50MHZ, GPIO_PIN_12);
	}

#ifdef CONFIG_SPI_GD32_INTERRUPT
	cfg->irq_config(dev);
#endif

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

#ifdef CONFIG_SPI_GD32_INTERRUPT
#define GD32_SPI_IRQ_HANDLER_DECL(id)					\
	static void spi_gd32_irq_config_func_##id(const struct device *dev)
#define STM32_SPI_IRQ_HANDLER_FUNC(id)					\
	.irq_config = spi_stm32_irq_config_func_##id,
#define GD32_SPI_IRQ_HANDLER(id)					\
static void spi_gd32_irq_config_func_##id(const struct device *dev)		\
{									\
	IRQ_CONNECT(DT_INST_IRQN(id),					\
		    DT_INST_IRQ(id, priority),				\
		    spi_gd32_isr, DEVICE_DT_INST_GET(id), 0);		\
	irq_enable(DT_INST_IRQN(id));					\
}
#else
#define GD32_SPI_IRQ_HANDLER_DECL(id)
#define GD32_SPI_IRQ_HANDLER_FUNC(id)
#define GD32_SPI_IRQ_HANDLER(id)
#endif

#define SPI_DMA_CHANNEL(id, dir, DIR, src, dest)
#define SPI_DMA_STATUS_SEM(id)

#define GD32_SPI_INIT(id)						\
GD32_SPI_IRQ_HANDLER_DECL(id);						\
									\
static const struct soc_gpio_pinctrl spi_pins_##id[] =			\
				GIGADEVICE_GD32_DT_INST_PINCTRL(id, 0);	\
									\
static const struct spi_gd32_config spi_gd32_cfg_##id = {		\
	.spi = (SPI_TypeDef *) DT_INST_REG_ADDR(id),			\
	.pclken = {							\
		.enr = DT_INST_CLOCKS_CELL(id, bits),			\
		.bus = DT_INST_CLOCKS_CELL(id, bus)			\
	},								\
	.pinctrl_list = spi_pins_##id,					\
	.pinctrl_list_size = ARRAY_SIZE(spi_pins_##id),			\
	GD32_SPI_IRQ_HANDLER_FUNC(id)					\
};									\
									\
static struct spi_gd32_data spi_gd32_dev_data_##id = {		\
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_##id, ctx),		\
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_##id, ctx),		\
	SPI_DMA_CHANNEL(id, rx, RX, PERIPHERAL, MEMORY)			\
	SPI_DMA_CHANNEL(id, tx, TX, MEMORY, PERIPHERAL)			\
	SPI_DMA_STATUS_SEM(id)						\
};									\
									\
DEVICE_DT_INST_DEFINE(id, &spi_gd32_init, NULL,			\
		    &spi_gd32_dev_data_##id, &spi_gd32_cfg_##id,	\
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,		\
		    &api_funcs);					\
									\
GD32_SPI_IRQ_HANDLER(id)

DT_INST_FOREACH_STATUS_OKAY(GD32_SPI_INIT)
