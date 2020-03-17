/*
 * Copyright (c) 2016 BayLibre, SAS
 *
 * SPDX-License-Identifier: Apache-2.0
 */

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

#include "spi_context.h"

#include "gd32vf103_spi.h"

#define BASE_ADDR(spi) (uint32_t)(spi)

typedef void (*irq_config_func_t)(struct device *port);

typedef uint32_t SPI_TypeDef;

struct spi_gd32_config {
	struct gd32_pclken pclken;
	SPI_TypeDef *spi;
#ifdef CONFIG_SPI_GD32_INTERRUPT
	irq_config_func_t irq_config;
#endif
};

struct spi_gd32_data {
	struct spi_context ctx;
};


#define DEV_CFG(dev)						\
(const struct spi_gd32_config * const)(dev->config->config_info)

#define DEV_DATA(dev)					\
(struct spi_gd32_data * const)(dev->driver_data)

/*
 * Check for SPI_SR_FRE to determine support for TI mode frame format
 * error flag, because GD32F1 SoCs do not support it and  GD32CUBE
 * for F1 family defines an unused LL_SPI_SR_FRE.
 */
#define SPI_GD32_ERR_MSK (SPI_STAT_CRCERR | SPI_STAT_CONFERR | SPI_STAT_RXORERR)


static FlagStatus spi_get_master_mode(uint32_t spi_periph)
{
	return (SPI_CTL0(spi_periph) & SPI_CTL0_MSTMOD) ? SET : RESET;
}

static void spi_i2s_flag_clear(uint32_t spi_periph, uint32_t flag)
{
	SPI_STAT(spi_periph) &= ~flag;
}

static uint32_t spi_i2s_status_regs(uint32_t spi_periph)
{
	return SPI_STAT(spi_periph);
}

/* Value to shift out when no application data needs transmitting. */
#define SPI_GD32_TX_NOP 0x00

static bool spi_gd32_transfer_ongoing(struct spi_gd32_data *data)
{
	return spi_context_tx_on(&data->ctx) || spi_context_rx_on(&data->ctx);
}

static int spi_gd32_get_err(SPI_TypeDef *spi)
{
	u32_t sr = spi_i2s_status_regs(BASE_ADDR(spi));

	if (sr & SPI_GD32_ERR_MSK) {
		LOG_ERR("%s: err=%d", __func__,
			    sr & (u32_t)SPI_GD32_ERR_MSK);

		/* OVR error must be explicitly cleared */
		if (spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_RXORERR)) {
			spi_i2s_flag_clear(BASE_ADDR(spi), SPI_FLAG_RXORERR);
		}

		return -EIO;
	}

	return 0;
}

static inline u16_t spi_gd32_next_tx(struct spi_gd32_data *data)
{
	u16_t tx_frame = SPI_GD32_TX_NOP;

	if (spi_context_tx_buf_on(&data->ctx)) {
		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			tx_frame = UNALIGNED_GET((u8_t *)(data->ctx.tx_buf));
		} else {
			tx_frame = UNALIGNED_GET((u16_t *)(data->ctx.tx_buf));
		}
	}

	return tx_frame;
}

/* Shift a SPI frame as master. */
static void spi_gd32_shift_m(SPI_TypeDef *spi, struct spi_gd32_data *data)
{
	u16_t tx_frame;
	u16_t rx_frame;

	tx_frame = spi_gd32_next_tx(data);
	while (!spi_i2s_flag_get(BASE_ADDR(spi), SPI_FLAG_TBE) ) {
		/* NOP */
	}

	if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
		spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
		/* The update is ignored if TX is off. */
		spi_context_update_tx(&data->ctx, 1, 1);
	} else {
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
			UNALIGNED_PUT(rx_frame, (u8_t *)data->ctx.rx_buf);
		}
		spi_context_update_rx(&data->ctx, 1, 1);
	} else {
		rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
		if (spi_context_rx_buf_on(&data->ctx)) {
			UNALIGNED_PUT(rx_frame, (u16_t *)data->ctx.rx_buf);
		}
		spi_context_update_rx(&data->ctx, 2, 1);
	}
}

/* Shift a SPI frame as slave. */
static void spi_gd32_shift_s(SPI_TypeDef *spi, struct spi_gd32_data *data)
{
	if (spi_i2s_flag_get(BASE_ADDR(spi), I2S_FLAG_TBE) && spi_context_tx_on(&data->ctx)) {
		u16_t tx_frame = spi_gd32_next_tx(data);

		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
			spi_context_update_tx(&data->ctx, 1, 1);
		} else {
			spi_i2s_data_transmit(BASE_ADDR(spi), tx_frame);
			spi_context_update_tx(&data->ctx, 2, 1);
		}
	} else {
		spi_i2s_interrupt_disable(BASE_ADDR(spi), SPI_I2S_INT_TBE);
	}

	if (spi_i2s_flag_get(BASE_ADDR(spi), SPI_I2S_INT_FLAG_RBNE) &&
	    spi_context_rx_buf_on(&data->ctx)) {
		u16_t rx_frame;

		if (SPI_WORD_SIZE_GET(data->ctx.config->operation) == 8) {
			rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
			UNALIGNED_PUT(rx_frame, (u8_t *)data->ctx.rx_buf);
			spi_context_update_rx(&data->ctx, 1, 1);
		} else {
			rx_frame = spi_i2s_data_receive(BASE_ADDR(spi));
			UNALIGNED_PUT(rx_frame, (u16_t *)data->ctx.rx_buf);
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
	u16_t operation = data->ctx.config->operation;

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
static void spi_gd32_isr(void *arg)
{
	struct device * const dev = (struct device *) arg;
	const struct spi_gd32_config *cfg = dev->config->config_info;
	struct spi_gd32_data *data = dev->driver_data;
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

static int spi_gd32_configure(struct device *dev,
			       const struct spi_config *config)
{
	const struct spi_gd32_config *cfg = DEV_CFG(dev);
	struct spi_gd32_data *data = DEV_DATA(dev);
	const u32_t scaler[] = {
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
	u32_t clock;
	int br;

	if (spi_context_configured(&data->ctx, config)) {
		/* Nothing to do */
		return 0;
	}

	if ((SPI_WORD_SIZE_GET(config->operation) != 8)
	    && (SPI_WORD_SIZE_GET(config->operation) != 16)) {
		return -ENOTSUP;
	}

	if (clock_control_get_rate(device_get_binding(GD32_CLOCK_CONTROL_NAME),
			(clock_control_subsys_t) &cfg->pclken, &clock) < 0) {
		LOG_ERR("Failed call clock_control_get_rate");
		return -EIO;
	}

	for (br = 1 ; br <= ARRAY_SIZE(scaler) ; ++br) {
		u32_t clk = clock >> br;

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
		if (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) {
			param.clock_polarity_phase  = SPI_CK_PL_HIGH_PH_2EDGE;
		} else {
			param.clock_polarity_phase  = SPI_CK_PL_HIGH_PH_1EDGE;
		}

	} else {
		if (SPI_MODE_GET(config->operation) & SPI_MODE_CPHA) {
			param.clock_polarity_phase  = SPI_CK_PL_LOW_PH_2EDGE;
		} else {
			param.clock_polarity_phase  = SPI_CK_PL_LOW_PH_1EDGE;
		}

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

static int spi_gd32_release(struct device *dev,
			     const struct spi_config *config)
{
	struct spi_gd32_data *data = DEV_DATA(dev);

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

static int transceive(struct device *dev,
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

	spi_context_lock(&data->ctx, asynchronous, signal);

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

static int spi_gd32_transceive(struct device *dev,
				const struct spi_config *config,
				const struct spi_buf_set *tx_bufs,
				const struct spi_buf_set *rx_bufs)
{
	return transceive(dev, config, tx_bufs, rx_bufs, false, NULL);
}

#ifdef CONFIG_SPI_ASYNC
static int spi_gd32_transceive_async(struct device *dev,
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

static int spi_gd32_init(struct device *dev)
{
	struct spi_gd32_data *data __attribute__((unused)) = dev->driver_data;
	const struct spi_gd32_config *cfg = dev->config->config_info;

	__ASSERT_NO_MSG(device_get_binding(GD32_CLOCK_CONTROL_NAME));

	if (clock_control_on(device_get_binding(GD32_CLOCK_CONTROL_NAME),
			       (clock_control_subsys_t) &cfg->pclken) != 0) {
		LOG_ERR("Could not enable SPI clock");
		return -EIO;
	}

#ifdef CONFIG_SPI_GD32_INTERRUPT
	cfg->irq_config(dev);
#endif

	spi_context_unlock_unconditionally(&data->ctx);

	return 0;
}

#ifdef CONFIG_SPI_0

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_0(struct device *port);
#endif

static const struct spi_gd32_config spi_gd32_cfg_0 = {
	.spi = (SPI_TypeDef *) DT_SPI_0_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_0_CLOCK_BITS,
		.bus = DT_SPI_0_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_0,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_0 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_0, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_0, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_0, DT_SPI_0_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_0, &spi_gd32_cfg_0,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_0(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_0_IRQ, DT_SPI_0_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_0), 0);
	irq_enable(DT_SPI_0_IRQ);
}
#endif

#endif /* CONFIG_SPI_0 */

#ifdef CONFIG_SPI_1

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_1(struct device *port);
#endif

static const struct spi_gd32_config spi_gd32_cfg_1 = {
	.spi = (SPI_TypeDef *) DT_SPI_1_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_1_CLOCK_BITS,
		.bus = DT_SPI_1_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_1,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_1 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_1, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_1, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_1, DT_SPI_1_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_1, &spi_gd32_cfg_1,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_1(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_1_IRQ, DT_SPI_1_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_1), 0);
	irq_enable(DT_SPI_1_IRQ);
}
#endif

#endif /* CONFIG_SPI_1 */

#ifdef CONFIG_SPI_2

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_2(struct device *port);
#endif

static const struct spi_gd32_config spi_gd32_cfg_2 = {
	.spi = (SPI_TypeDef *) DT_SPI_2_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_2_CLOCK_BITS,
		.bus = DT_SPI_2_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_2,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_2 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_2, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_2, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_2, DT_SPI_2_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_2, &spi_gd32_cfg_2,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_2(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_2_IRQ, DT_SPI_2_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_2), 0);
	irq_enable(DT_SPI_2_IRQ);
}
#endif

#endif /* CONFIG_SPI_2 */

#ifdef CONFIG_SPI_3

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_3(struct device *port);
#endif

static const  struct spi_gd32_config spi_gd32_cfg_3 = {
	.spi = (SPI_TypeDef *) DT_SPI_3_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_3_CLOCK_BITS,
		.bus = DT_SPI_3_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_3,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_3 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_3, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_3, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_3, DT_SPI_3_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_3, &spi_gd32_cfg_3,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_3(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_3_IRQ, DT_SPI_3_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_3), 0);
	irq_enable(DT_SPI_3_IRQ);
}
#endif

#endif /* CONFIG_SPI_3 */

#ifdef CONFIG_SPI_4

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_4(struct device *port);
#endif

static const  struct spi_gd32_config spi_gd32_cfg_4 = {
	.spi = (SPI_TypeDef *) DT_SPI_4_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_4_CLOCK_BITS,
		.bus = DT_SPI_4_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_4,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_4 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_4, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_4, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_4, DT_SPI_4_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_4, &spi_gd32_cfg_4,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_4(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_4_IRQ, DT_SPI_4_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_4), 0);
	irq_enable(DT_SPI_4_IRQ);
}
#endif

#endif /* CONFIG_SPI_4 */

#ifdef CONFIG_SPI_5

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_5(struct device *port);
#endif

static const  struct spi_gd32_config spi_gd32_cfg_5 = {
	.spi = (SPI_TypeDef *) DT_SPI_5_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_5_CLOCK_BITS,
		.bus = DT_SPI_5_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_5,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_5 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_5, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_5, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_5, DT_SPI_5_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_5, &spi_gd32_cfg_5,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_5(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_5_IRQ, DT_SPI_5_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_5), 0);
	irq_enable(DT_SPI_5_IRQ);
}
#endif

#endif /* CONFIG_SPI_5 */

#ifdef CONFIG_SPI_6

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_6(struct device *port);
#endif

static const  struct spi_gd32_config spi_gd32_cfg_6 = {
	.spi = (SPI_TypeDef *) DT_SPI_6_BASE_ADDRESS,
	.pclken = {
		.enr = DT_SPI_6_CLOCK_BITS,
		.bus = DT_SPI_6_CLOCK_BUS
	},
#ifdef CONFIG_SPI_GD32_INTERRUPT
	.irq_config = spi_gd32_irq_config_func_6,
#endif
};

static struct spi_gd32_data spi_gd32_dev_data_6 = {
	SPI_CONTEXT_INIT_LOCK(spi_gd32_dev_data_6, ctx),
	SPI_CONTEXT_INIT_SYNC(spi_gd32_dev_data_6, ctx),
};

DEVICE_AND_API_INIT(spi_gd32_6, DT_SPI_6_NAME, &spi_gd32_init,
		    &spi_gd32_dev_data_6, &spi_gd32_cfg_6,
		    POST_KERNEL, CONFIG_SPI_INIT_PRIORITY,
		    &api_funcs);

#ifdef CONFIG_SPI_GD32_INTERRUPT
static void spi_gd32_irq_config_func_6(struct device *dev)
{
	IRQ_CONNECT(DT_SPI_6_IRQ, DT_SPI_6_IRQ_PRI,
		    spi_gd32_isr, DEVICE_GET(spi_gd32_6), 0);
	irq_enable(DT_SPI_6_IRQ);
}
#endif

#endif /* CONFIG_SPI_6 */
