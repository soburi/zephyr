/*
 * Copyright (c) 2024 espros photonics Co.
 * Copyright (c) 2024 Espressif Systems (Shanghai) CO LTD.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT espressif_esp32_lcd_cam_dvp

#include <soc/gdma_channel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/clock_control/esp32_clock_control.h>
#include <zephyr/drivers/dma.h>
#include <zephyr/drivers/dma/dma_esp32.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/interrupt_controller/intc_esp32.h>
#include <zephyr/kernel.h>
#include <hal/cam_hal.h>
#include <hal/cam_ll.h>
#include <hal/dma_types.h>
#include <hal/mmu_hal.h>
#include <hal/cache_hal.h>
#include <hal/cache_types.h>

#include <zephyr/logging/log.h>
#include <zephyr/cache.h>
#include <zephyr/sys/util.h>
#include <zephyr/spinlock.h>

#include "video_device.h"

LOG_MODULE_REGISTER(video_esp32_lcd_cam, CONFIG_VIDEO_LOG_LEVEL);

#define VIDEO_ESP32_DMA_BUFFER_MAX_SIZE DMA_DESCRIPTOR_BUFFER_MAX_SIZE_4B_ALIGNED
#define VIDEO_ESP32_VSYNC_MASK          0x04
#define VIDEO_ESP32_WATCHDOG_PERIOD_MS  500U
#define VIDEO_ESP32_STALL_TIMEOUT_MS    2000U

#ifdef CONFIG_POLL
#define VIDEO_ESP32_RAISE_OUT_SIG_IF_ENABLED(result)                                               \
	if (data->signal_out) {                                                                    \
		k_poll_signal_raise(data->signal_out, result);                                     \
	}
#else
#define VIDEO_ESP32_RAISE_OUT_SIG_IF_ENABLED(result)
#endif

enum video_esp32_cam_clk_sel_values {
	VIDEO_ESP32_CAM_CLK_SEL_NONE = 0,
	VIDEO_ESP32_CAM_CLK_SEL_XTAL = 1,
	VIDEO_ESP32_CAM_CLK_SEL_PLL_DIV2 = 2,
	VIDEO_ESP32_CAM_CLK_SEL_PLL_F160M = 3,
};

struct video_esp32_config {
	const struct device *dma_dev;
	const struct device *source_dev;
	uint32_t cam_clk;
	uint8_t rx_dma_channel;
	uint8_t data_width;
	uint8_t vsync_filter_thres;
	uint8_t invert_de;
	uint8_t invert_byte_order;
	uint8_t invert_bit_order;
	uint8_t invert_pclk;
	uint8_t invert_hsync;
	uint8_t invert_vsync;
};

struct video_esp32_data {
	cam_hal_context_t hal;
	const struct video_esp32_config *config;
	struct video_format video_format;
	struct video_buffer *active_vbuf;
	bool is_streaming;
	bool capture_paused;
	uint8_t dma_block_count;
	uint32_t last_rx_done_ms;
	uint32_t last_recovery_log_ms;
	struct k_work_delayable watchdog_work;
	struct k_spinlock lock;
	struct k_fifo fifo_in;
	struct k_fifo fifo_out;
	struct dma_block_config dma_blocks[CONFIG_DMA_ESP32_MAX_DESCRIPTOR_NUM];
#ifdef CONFIG_POLL
	struct k_poll_signal *signal_out;
#endif
};

static void video_esp32_watchdog_handler(struct k_work *work);
static void video_esp32_dma_rx_done(const struct device *dev, void *user_data, uint32_t channel,
				    int status);

static void video_esp32_invalidate_ext_dcache(void *addr, size_t size)
{
#if defined(CONFIG_ESP_SPIRAM)
	if (size == 0U) {
		return;
	}

	uint32_t line_size = cache_hal_get_cache_line_size(CACHE_TYPE_DATA);
	if (line_size == 0U) {
		line_size = 32U;
	}

	uintptr_t start = (uintptr_t)addr;
	uintptr_t end = start + size;
	uintptr_t aligned_start = ROUND_DOWN(start, line_size);
	uintptr_t aligned_end = ROUND_UP(end, line_size);
	size_t aligned_size = aligned_end - aligned_start;

	if (!mmu_hal_check_valid_ext_vaddr_region(0, (uint32_t)aligned_start, aligned_size,
						  MMU_VADDR_DATA)) {
		return;
	}

	/*
	 * Invalidate in chunks to avoid very long critical sections while
	 * invalidating large PSRAM buffers (full frame buffers).
	 */
	const size_t chunk_max = 4U * 1024U;
	uintptr_t cur = aligned_start;
	size_t remaining = aligned_size;

	while (remaining > 0U) {
		size_t chunk = MIN(remaining, chunk_max);

		cache_hal_invalidate_addr((uint32_t)cur, chunk);
		cur += chunk;
		remaining -= chunk;
	}
#else
	ARG_UNUSED(addr);
	ARG_UNUSED(size);
#endif
}

static int video_esp32_dma_configure(struct video_esp32_data *data)
{
	const struct video_esp32_config *cfg = data->config;
	struct dma_config dma_cfg = {0};

	if (data->dma_block_count == 0U) {
		return -EINVAL;
	}

	dma_cfg.channel_direction = PERIPHERAL_TO_MEMORY;
	dma_cfg.dma_callback = video_esp32_dma_rx_done;
	dma_cfg.user_data = data;
	dma_cfg.dma_slot = SOC_GDMA_TRIG_PERIPH_CAM0;
	dma_cfg.source_burst_length = 4;
	dma_cfg.dest_burst_length = 4;
	dma_cfg.complete_callback_en = 1;
	dma_cfg.block_count = data->dma_block_count;
	dma_cfg.head_block = &data->dma_blocks[0];

	return dma_config(cfg->dma_dev, cfg->rx_dma_channel, &dma_cfg);
}

static int video_esp32_prepare_dma_blocks(struct video_esp32_data *data)
{
	uint32_t buffer_size;

	if (data->active_vbuf == NULL) {
		return -EINVAL;
	}

	buffer_size = data->active_vbuf->bytesused;
	memset(data->dma_blocks, 0, sizeof(data->dma_blocks));
	for (int i = 0; i < CONFIG_DMA_ESP32_MAX_DESCRIPTOR_NUM; ++i) {
		struct dma_block_config *blk = &data->dma_blocks[i];

		blk->dest_address =
			(uint32_t)data->active_vbuf->buffer + (i * VIDEO_ESP32_DMA_BUFFER_MAX_SIZE);
		if (buffer_size < VIDEO_ESP32_DMA_BUFFER_MAX_SIZE) {
			blk->block_size = buffer_size;
			blk->next_block = NULL;
			data->dma_block_count = i + 1;
			return 0;
		}
		blk->block_size = VIDEO_ESP32_DMA_BUFFER_MAX_SIZE;
		if (i == (CONFIG_DMA_ESP32_MAX_DESCRIPTOR_NUM - 1)) {
			return -ENOBUFS;
		}
		blk->next_block = &data->dma_blocks[i + 1];
		buffer_size -= VIDEO_ESP32_DMA_BUFFER_MAX_SIZE;
	}

	return -EINVAL;
}

static void video_esp32_update_dma_block_addresses(struct video_esp32_data *data)
{
	if (data->active_vbuf == NULL || data->dma_block_count == 0U) {
		return;
	}

	for (uint8_t i = 0; i < data->dma_block_count; i++) {
		data->dma_blocks[i].dest_address =
			(uint32_t)data->active_vbuf->buffer + (i * VIDEO_ESP32_DMA_BUFFER_MAX_SIZE);
	}
}

static int video_esp32_reload_dma(struct video_esp32_data *data)
{
	const struct video_esp32_config *cfg = data->config;
	int ret = 0;

	if (data->active_vbuf == NULL) {
		LOG_ERR("No video buffer available. Enqueue some buffers first.");
		return -EAGAIN;
	}

	/* Keep descriptor destination addresses in sync with the active buffer. */
	video_esp32_update_dma_block_addresses(data);

	ret = dma_reload(cfg->dma_dev, cfg->rx_dma_channel, 0, (uint32_t)data->active_vbuf->buffer,
			 data->active_vbuf->bytesused);
	if (ret < 0) {
		LOG_ERR("Unable to reload DMA (%d)", ret);
		return ret;
	}

	ret = dma_start(cfg->dma_dev, cfg->rx_dma_channel);
	if (ret < 0) {
		LOG_ERR("Unable to start DMA (%d)", ret);
		return ret;
	}

	return 0;
}

void video_esp32_dma_rx_done(const struct device *dev, void *user_data, uint32_t channel,
			     int status)
{
	struct video_esp32_data *data = user_data;

	if (status == DMA_STATUS_BLOCK) {
		LOG_DBG("received block");
		return;
	}

	if (status != DMA_STATUS_COMPLETE) {
		VIDEO_ESP32_RAISE_OUT_SIG_IF_ENABLED(VIDEO_BUF_ERROR)
		LOG_ERR("DMA error: %d", status);
		/* Trigger recovery in thread context. */
		k_work_schedule(&data->watchdog_work, K_NO_WAIT);
		return;
	}

	k_spinlock_key_t key = k_spin_lock(&data->lock);
	data->last_rx_done_ms = k_uptime_get_32();
	k_spin_unlock(&data->lock, key);

	if (data->active_vbuf == NULL) {
		VIDEO_ESP32_RAISE_OUT_SIG_IF_ENABLED(VIDEO_BUF_ERROR)
		LOG_ERR("No video buffer available. Enque some buffers first.");
		return;
	}

	data->active_vbuf->timestamp = data->last_rx_done_ms;

#if defined(CONFIG_CACHE_MANAGEMENT)
	{
		void *buf = data->active_vbuf->buffer;

#if defined(CONFIG_CACHE_CAN_SAY_MEM_COHERENCE)
		if (!sys_cache_is_mem_coherent(buf)) {
			sys_cache_data_invd_range(buf, data->active_vbuf->bytesused);
		}
#else
		sys_cache_data_invd_range(buf, data->active_vbuf->bytesused);
#endif
	}
#endif

	k_fifo_put(&data->fifo_out, data->active_vbuf);
	VIDEO_ESP32_RAISE_OUT_SIG_IF_ENABLED(VIDEO_BUF_DONE)
	data->active_vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);

	if (data->active_vbuf == NULL) {
		LOG_DBG("Capture paused. No buffer available");
		/*
		 * Stop CAM when no buffers are available. Otherwise the peripheral keeps
		 * sampling and its FIFO can overflow while DMA is idle, which can
		 * manifest as vertical drift or color corruption when capture resumes.
		 */
		cam_hal_stop_streaming(&data->hal);
		data->capture_paused = true;
		return;
	}
	if (video_esp32_reload_dma(data) != 0) {
		/*
		 * DMA reload/start can fail transiently. Pause capture and put the
		 * buffer back so the application can recover by enqueuing again.
		 */
		cam_hal_stop_streaming(&data->hal);
		data->capture_paused = true;
		k_fifo_put(&data->fifo_in, data->active_vbuf);
		data->active_vbuf = NULL;
		VIDEO_ESP32_RAISE_OUT_SIG_IF_ENABLED(VIDEO_BUF_ERROR)
	}
}

static void video_esp32_watchdog_handler(struct k_work *work)
{
	struct video_esp32_data *data =
		CONTAINER_OF(work, struct video_esp32_data, watchdog_work.work);
	const struct video_esp32_config *cfg = data->config;
	uint32_t now_ms = k_uptime_get_32();
	bool should_recover = false;
	uint32_t last_rx_ms;

	/*
	 * The watchdog is meant to recover from a stuck DMA/capture path where
	 * frames stop arriving while the application still runs.
	 */
	k_spinlock_key_t key = k_spin_lock(&data->lock);
	last_rx_ms = data->last_rx_done_ms;
	if (data->is_streaming && (now_ms - last_rx_ms) > VIDEO_ESP32_STALL_TIMEOUT_MS &&
	    !(data->capture_paused && (data->active_vbuf == NULL))) {
		should_recover = true;
	}
	k_spin_unlock(&data->lock, key);

	if (should_recover) {
		(void)dma_stop(cfg->dma_dev, cfg->rx_dma_channel);
		cam_hal_stop_streaming(&data->hal);

		key = k_spin_lock(&data->lock);
		if (data->active_vbuf == NULL) {
			data->active_vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
		}
		k_spin_unlock(&data->lock, key);

		if (data->active_vbuf != NULL) {
			int ret;

			ret = video_esp32_prepare_dma_blocks(data);
			if (ret == 0) {
				ret = video_esp32_dma_configure(data);
			}
			if (ret == 0) {
				ret = video_esp32_reload_dma(data);
			}
			if (ret == 0) {
				cam_hal_start_streaming(&data->hal);

				key = k_spin_lock(&data->lock);
				data->capture_paused = false;
				data->last_rx_done_ms = now_ms;
				k_spin_unlock(&data->lock, key);

				if ((now_ms - data->last_recovery_log_ms) > 5000U) {
					LOG_WRN("Capture stalled, recovered (no frame for %u ms)",
						now_ms - last_rx_ms);
					data->last_recovery_log_ms = now_ms;
				}
			}
		}
	}

	/* Keep running while streaming to allow future recovery attempts. */
	if (data->is_streaming) {
		k_work_schedule(&data->watchdog_work, K_MSEC(VIDEO_ESP32_WATCHDOG_PERIOD_MS));
	}
}

static int video_esp32_set_stream(const struct device *dev, bool enable, enum video_buf_type type)
{
	const struct video_esp32_config *cfg = dev->config;
	struct video_esp32_data *data = dev->data;
	struct dma_status dma_status = {0};
	int error = 0;

	if (!enable) {
		LOG_DBG("Stop streaming");

		if (video_stream_stop(cfg->source_dev, type)) {
			return -EIO;
		}

		data->is_streaming = false;
		data->capture_paused = false;
		(void)k_work_cancel_delayable(&data->watchdog_work);
		error = dma_stop(cfg->dma_dev, cfg->rx_dma_channel);
		if (error) {
			LOG_ERR("Unable to stop DMA (%d)", error);
			return error;
		}

		cam_hal_stop_streaming(&data->hal);

		return 0;
	}

	if (data->is_streaming) {
		return -EBUSY;
	}

	LOG_DBG("Start streaming");

	error = dma_get_status(cfg->dma_dev, cfg->rx_dma_channel, &dma_status);

	if (error) {
		LOG_ERR("Unable to get Rx status (%d)", error);
		return error;
	}

	if (dma_status.busy) {
		LOG_ERR("Rx DMA Channel %d is busy", cfg->rx_dma_channel);
		return -EBUSY;
	}

	data->active_vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT);
	if (!data->active_vbuf) {
		LOG_ERR("No enqueued video buffers available.");
		return -EAGAIN;
	}
	data->capture_paused = false;

	error = video_esp32_prepare_dma_blocks(data);
	if (error == -ENOBUFS) {
		LOG_ERR("Not enough descriptors available. Increase "
			"CONFIG_DMA_ESP32_MAX_DESCRIPTOR_NUM");
		return -ENOBUFS;
	}
	if (error) {
		return error;
	}

	error = video_esp32_dma_configure(data);
	if (error) {
		LOG_ERR("Unable to configure DMA (%d)", error);
		return error;
	}

	error = dma_start(cfg->dma_dev, cfg->rx_dma_channel);
	if (error) {
		LOG_ERR("Unable to start DMA (%d)", error);
		return error;
	}

	cam_hal_start_streaming(&data->hal);

	if (video_stream_start(cfg->source_dev, type)) {
		return -EIO;
	}
	data->is_streaming = true;
	data->last_rx_done_ms = k_uptime_get_32();
	k_work_schedule(&data->watchdog_work, K_MSEC(VIDEO_ESP32_WATCHDOG_PERIOD_MS));

	return 0;
}

static int video_esp32_get_caps(const struct device *dev, struct video_caps *caps)
{
	const struct video_esp32_config *config = dev->config;

	/* Two buffers are needed to perform transfers */
	caps->min_vbuf_count = 2;

	/* Forward the message to the source device */
	return video_get_caps(config->source_dev, caps);
}

static int video_esp32_get_fmt(const struct device *dev, struct video_format *fmt)
{
	const struct video_esp32_config *cfg = dev->config;
	int ret = 0;

	LOG_DBG("Get format");

	ret = video_get_format(cfg->source_dev, fmt);
	if (ret < 0) {
		LOG_ERR("Failed to get format from source");
		return ret;
	}

	ret = video_estimate_fmt_size(fmt);
	if (ret < 0) {
		return ret;
	}

	return 0;
}

static int video_esp32_set_fmt(const struct device *dev, struct video_format *fmt)
{
	const struct video_esp32_config *cfg = dev->config;
	struct video_esp32_data *data = dev->data;
	int ret;

	ret = video_set_format(cfg->source_dev, fmt);
	if (ret < 0) {
		return ret;
	}

	ret = video_estimate_fmt_size(fmt);
	if (ret < 0) {
		return ret;
	}

	data->video_format = *fmt;

	return 0;
}

static int video_esp32_enqueue(const struct device *dev, struct video_buffer *vbuf)
{
	struct video_esp32_data *data = dev->data;
	int ret;

	vbuf->bytesused = data->video_format.pitch * data->video_format.height;
	vbuf->line_offset = 0;

	if (data->is_streaming && data->active_vbuf == NULL) {
		data->active_vbuf = vbuf;
		ret = video_esp32_reload_dma(data);
		if (ret == 0) {
			if (data->capture_paused) {
				cam_hal_start_streaming(&data->hal);
				data->capture_paused = false;
			}
			return 0;
		}
		data->active_vbuf = NULL;
	}

	k_fifo_put(&data->fifo_in, vbuf);

	return 0;
}

static int video_esp32_dequeue(const struct device *dev, struct video_buffer **vbuf,
			       k_timeout_t timeout)
{
	struct video_esp32_data *data = dev->data;

	*vbuf = k_fifo_get(&data->fifo_out, timeout);
	LOG_DBG("Dequeue done, vbuf = %p", *vbuf);
	if (*vbuf == NULL) {
		return -EAGAIN;
	}

	video_esp32_invalidate_ext_dcache((*vbuf)->buffer, (*vbuf)->bytesused);

	return 0;
}

static int video_esp32_flush(const struct device *dev, bool cancel)
{
	struct video_esp32_data *data = dev->data;
	struct video_buffer *vbuf = NULL;

	if (cancel) {
		if (data->active_vbuf) {
			k_fifo_put(&data->fifo_out, data->active_vbuf);
			data->active_vbuf = NULL;
		}
		while ((vbuf = k_fifo_get(&data->fifo_in, K_NO_WAIT)) != NULL) {
			k_fifo_put(&data->fifo_out, vbuf);
#ifdef CONFIG_POLL
			if (data->signal_out) {
				k_poll_signal_raise(data->signal_out, VIDEO_BUF_ABORTED);
			}
#endif
		}
	} else {
		while (!k_fifo_is_empty(&data->fifo_in)) {
			k_sleep(K_MSEC(1));
		}
	}

	return 0;
}

#ifdef CONFIG_POLL
int video_esp32_set_signal(const struct device *dev, struct k_poll_signal *sig)
{
	struct video_esp32_data *data = dev->data;

	data->signal_out = sig;
	return 0;
}
#endif

static void video_esp32_cam_ctrl_init(const struct device *dev)
{
	const struct video_esp32_config *cfg = dev->config;
	struct video_esp32_data *data = dev->data;

	const cam_hal_config_t hal_cfg = {
		.port = 0,
		.byte_swap_en = cfg->invert_byte_order,
	};

	cam_hal_init(&data->hal, &hal_cfg);

	/* Avoid data corruption on FIFO/DMA backpressure (e.g. slow consumers). */
	cam_ll_enable_stop_signal(data->hal.hw, true);

	/* Default byte order setting (may be overridden by negotiated format). */
	cam_ll_swap_dma_data_byte_order(data->hal.hw, cfg->invert_byte_order);

	cam_ll_reverse_dma_data_bit_order(data->hal.hw, cfg->invert_bit_order);
	cam_ll_enable_invert_pclk(data->hal.hw, cfg->invert_pclk);
	cam_ll_set_input_data_width(data->hal.hw, cfg->data_width);
	cam_ll_enable_invert_de(data->hal.hw, cfg->invert_de);
	cam_ll_enable_invert_vsync(data->hal.hw, cfg->invert_vsync);
	cam_ll_enable_invert_hsync(data->hal.hw, cfg->invert_hsync);

	if (cfg->vsync_filter_thres > 0U) {
		cam_ll_enable_vsync_filter(data->hal.hw, true);
		cam_ll_set_vsync_filter_thres(data->hal.hw, cfg->vsync_filter_thres);
	} else {
		cam_ll_enable_vsync_filter(data->hal.hw, false);
		cam_ll_set_vsync_filter_thres(data->hal.hw, 0);
	}
}

static int video_esp32_set_frmival(const struct device *dev, struct video_frmival *frmival)
{
	const struct video_esp32_config *cfg = dev->config;

	return video_set_frmival(cfg->source_dev, frmival);
}

static int video_esp32_get_frmival(const struct device *dev, struct video_frmival *frmival)
{
	const struct video_esp32_config *cfg = dev->config;

	return video_get_frmival(cfg->source_dev, frmival);
}

static int video_esp32_init(const struct device *dev)
{
	const struct video_esp32_config *cfg = dev->config;
	struct video_esp32_data *data = dev->data;

	if (!cfg->cam_clk) {
		LOG_ERR("No cam_clk specified\n");
		return -EINVAL;
	}

	if (ESP32_CLK_CPU_PLL_160M % cfg->cam_clk) {
		LOG_ERR("Invalid cam_clk value. It must be a divisor of 160M\n");
		return -EINVAL;
	}

	/* Enable camera main clock output */
	cam_ll_select_clk_src(0, LCD_CLK_SRC_PLL160M);
	cam_ll_set_group_clock_coeff(0, ESP32_CLK_CPU_PLL_160M / cfg->cam_clk, 0, 0);

	k_fifo_init(&data->fifo_in);
	k_fifo_init(&data->fifo_out);
	data->config = cfg;
	data->dma_block_count = 0U;
	data->last_rx_done_ms = k_uptime_get_32();
	data->last_recovery_log_ms = 0U;
	k_work_init_delayable(&data->watchdog_work, video_esp32_watchdog_handler);
	video_esp32_cam_ctrl_init(dev);

	if (!device_is_ready(cfg->dma_dev)) {
		LOG_ERR("DMA device not ready");
		return -ENODEV;
	}

	return 0;
}

int video_esp32_set_selection(const struct device *dev, struct video_selection *sel)
{
	struct video_esp32_data *data = dev->data;
	const struct video_esp32_config *cfg = dev->config;
	int ret;

	ret = video_set_selection(cfg->source_dev, sel);
	if (ret == -ENOSYS || ret == -ENOTSUP) {
		return ret;
	}
	if (ret < 0) {
		LOG_ERR("Failed to set selection on source device");
		return ret;
	}

	ret = video_get_format(cfg->source_dev, &data->video_format);
	if (ret < 0) {
		LOG_ERR("Failed to get format from source device");
		return ret;
	}

	return 0;
}

int video_esp32_get_selection(const struct device *dev, struct video_selection *sel)
{
	const struct video_esp32_config *cfg = dev->config;

	return video_get_selection(cfg->source_dev, sel);
}

static DEVICE_API(video, esp32_driver_api) = {
	/* mandatory callbacks */
	.set_format = video_esp32_set_fmt,
	.get_format = video_esp32_get_fmt,
	.set_stream = video_esp32_set_stream,
	.get_caps = video_esp32_get_caps,
	/* optional callbacks */
	.enqueue = video_esp32_enqueue,
	.dequeue = video_esp32_dequeue,
	.flush = video_esp32_flush,
	.set_selection = video_esp32_set_selection,
	.get_selection = video_esp32_get_selection,
	.set_frmival = video_esp32_set_frmival,
	.get_frmival = video_esp32_get_frmival,
#ifdef CONFIG_POLL
	.set_signal = video_esp32_set_signal,
#endif
};

#define SOURCE_DEV(n) DEVICE_DT_GET(DT_INST_PHANDLE(n, source))

static const struct video_esp32_config esp32_config = {
	.source_dev = SOURCE_DEV(0),
	.dma_dev = DEVICE_DT_GET_OR_NULL(DT_DMAS_CTLR_BY_NAME(DT_INST_PARENT(0), rx)),
	.rx_dma_channel = DT_DMAS_CELL_BY_NAME(DT_INST_PARENT(0), rx, channel),
	.data_width = DT_INST_PROP_OR(0, data_width, 8),
	.vsync_filter_thres = DT_INST_PROP_OR(0, vsync_filter_thres, 0),
	.invert_bit_order = DT_INST_PROP(0, invert_bit_order),
	.invert_byte_order = DT_INST_PROP(0, invert_byte_order),
	.invert_pclk = DT_INST_PROP(0, invert_pclk),
	.invert_de = DT_INST_PROP(0, invert_de),
	.invert_hsync = DT_INST_PROP(0, invert_hsync),
	.invert_vsync = DT_INST_PROP(0, invert_vsync),
	.cam_clk = DT_INST_PROP_OR(0, cam_clk, 0),
};

static struct video_esp32_data esp32_data = {0};

DEVICE_DT_INST_DEFINE(0, video_esp32_init, NULL, &esp32_data, &esp32_config, POST_KERNEL,
		      CONFIG_VIDEO_INIT_PRIORITY, &esp32_driver_api);

VIDEO_DEVICE_DEFINE(esp32, DEVICE_DT_INST_GET(0), SOURCE_DEV(0));
