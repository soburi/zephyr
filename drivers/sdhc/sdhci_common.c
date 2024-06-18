/*
 * Copyright (c) 2024 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/cache.h>
#include <zephyr/drivers/sdhc.h>
#include <zephyr/logging/log.h>

#include "sdhci_common.h"

LOG_MODULE_REGISTER(sdhci_common, CONFIG_SDHC_LOG_LEVEL);

#define SDHCI_DATA_MAX_TIMEOUT 0xE

#define SDHCI_RESET_TIMEOUT_MS 100
#define SDHCI_CLK_STABLE_TIMEOUT_MS 20
#define SDHCI_REQ_SD_BUSY_TIMEOUT_MS 100

static int sdhci_send_cmd(struct sdhci_common *sdhci_ctx, struct sdhc_command *cmd,
			  bool data_present);

static void sdhci_enable_sd_clock(struct sdhci_common *sdhci_ctx, bool on)
{
	uint16_t clk_reg = 0;
	int32_t timeout;

	clk_reg = sys_read16(sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL);
	if (!(clk_reg & SDHCI_CLOCK_INT_EN)) {
		/* clock not configured, nothing to do */
		return;
	}

	timeout = SDHCI_CLK_STABLE_TIMEOUT_MS * USEC_PER_MSEC;
	if (on) {
		/* wait for Internal Clock Stable */
		if (!WAIT_FOR(((sys_read16(sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL) &
				SDHCI_CLOCK_INT_STABLE)),
			      timeout, k_msleep(1))) {
			LOG_ERR("sdhc:clk_en: timeout for clk stable");
		}
	}

	if (on) {
		clk_reg |= SDHCI_CLOCK_CARD_EN;
	} else {
		clk_reg &= ~SDHCI_CLOCK_CARD_EN;
	}

	LOG_DBG("sdhc:clk_en: SDHCI_CLOCK_CONTROL:%04x", clk_reg);
	sys_write16(clk_reg, sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL);
}

static void sdhci_set_hspd(struct sdhci_common *sdhci_ctx, bool on)
{
	uint8_t ctrl;

	/* Set bus width */
	ctrl = sys_read8(sdhci_ctx->reg_base + SDHCI_HOST_CONTROL);
	if (on) {
		ctrl |= SDHCI_CTRL_HISPD;
	} else {
		ctrl &= ~SDHCI_CTRL_HISPD;
	}

	LOG_DBG("sdhc:hspd: SDHCI_HOST_CONTROL:%02x", ctrl);
	sys_write8(ctrl, sdhci_ctx->reg_base + SDHCI_HOST_CONTROL);
}

int sdhci_get_cd(struct sdhci_common *sdhci_ctx)
{
	uint32_t reg;

	reg = sys_read32(sdhci_ctx->reg_base + SDHCI_PRESENT_STATE);
	LOG_DBG("sdhc:get_cd: SDHCI_PRESENT_STATE:%x", reg);

	return reg & SDHCI_CARD_PRESENT ? 1 : 0;
}

int sdhci_is_card_busy(struct sdhci_common *sdhci_ctx)
{
	uint32_t reg;

	reg = sys_read32(sdhci_ctx->reg_base + SDHCI_PRESENT_STATE);
	LOG_DBG("sdhc:is_busy: SDHCI_PRESENT_STATE card_busy:%x", reg);

	return (reg & SDHCI_DATA_0_LVL_MASK) == SDHCI_DATA_0_LVL_MASK ? 0 : 1;
}

int sdhci_reset(struct sdhci_common *sdhci_ctx, uint8_t mask)
{
	int32_t timeout;

	timeout = SDHCI_RESET_TIMEOUT_MS * USEC_PER_MSEC;
	sys_write8(mask, sdhci_ctx->reg_base + SDHCI_SOFTWARE_RESET);

	if (!WAIT_FOR((!(sys_read8(sdhci_ctx->reg_base + SDHCI_SOFTWARE_RESET) & mask)), timeout,
		      k_msleep(1))) {
		LOG_ERR("sdhc:reset: timeout msk:%02x", mask);
		return -ETIMEDOUT;
	}

	return 0;
}

void sdhci_set_bus_power(struct sdhci_common *sdhci_ctx, enum sd_voltage bus_voltage)
{
	uint8_t pwr = 0;

	switch (bus_voltage) {
	case SD_VOL_3_3_V:
		pwr = SDHCI_POWER_330;
		break;
	case SD_VOL_3_0_V:
		pwr = SDHCI_POWER_300;
		break;
	case SD_VOL_1_8_V:
		pwr = SDHCI_POWER_180;
		break;
	default:
		LOG_DBG("power off");
	}

	if (!pwr) {
		sdhci_enable_sd_clock(sdhci_ctx, false);
		LOG_DBG("sdhc:bus_power: off");
		sys_write8(0, sdhci_ctx->reg_base + SDHCI_POWER_CONTROL);
		return;
	}

	sys_write8(pwr, sdhci_ctx->reg_base + SDHCI_POWER_CONTROL);

	pwr |= SDHCI_POWER_ON;
	LOG_DBG("sdhc:bus_power: SDHCI_POWER_CONTROL:%02x", pwr);
	sys_write8(pwr, sdhci_ctx->reg_base + SDHCI_POWER_CONTROL);

	sdhci_enable_sd_clock(sdhci_ctx, true);
}

int sdhci_set_clock(struct sdhci_common *sdhci_ctx, enum sdhc_clock_speed clock)
{
	uint16_t clk_reg = 0;
	uint16_t divider;
	int32_t timeout;

	sys_write16(0, sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL);

	if (clock == 0) {
		LOG_DBG("sdhc:clk_set: off");
		return 0;
	}

	/* Check if the Host Controller supports Programmable Clock */
	if (sdhci_ctx->clk_mul) {
		uint32_t max_clk = sdhci_ctx->max_clk *	sdhci_ctx->clk_mul;

		for (divider = 1; divider <= 1024; divider++) {
			if ((max_clk / divider) <= clock)
				break;
		}

		/* Set Programmable Clock Mode in the Clock Control register */
		clk_reg = SDHCI_PROG_CLOCK_MODE;
		divider--;
	} else {
		/* Version 3.00 divisors must be a multiple of 2. */
		if (sdhci_ctx->max_clk <= clock) {
			divider = 1;
		} else {
			for (divider = 2;
			     divider < SDHCI_MAX_DIV_SPEC_300;
			     divider += 2) {
				if ((sdhci_ctx->max_clk / divider) <= clock)
					break;
			}
		}
		divider >>= 1;
	}

	clk_reg |= FIELD_GET(SDHCI_DIV_MASK, divider) << SDHCI_DIVIDER_SHIFT;
	clk_reg |= FIELD_GET(SDHCI_DIV_HI_MASK, divider) << SDHCI_DIVIDER_HI_SHIFT;
	clk_reg |= SDHCI_CLOCK_INT_EN;
	LOG_DBG("sdhc:clk_set: SDHCI_CLOCK_CONTROL:%04x divider:%u", clk_reg, divider);
	sys_write16(clk_reg, sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL);


	timeout = SDHCI_CLK_STABLE_TIMEOUT_MS * USEC_PER_MSEC;
	/* wait for Internal Clock Stable */
	if (!WAIT_FOR(((sys_read16(sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL) &
			SDHCI_CLOCK_INT_STABLE)),
		      timeout, k_msleep(1))) {
		LOG_ERR("sdhc:clk_set: clk stable timeout");
		return -ETIMEDOUT;
	}

	clk_reg |= SDHCI_CLOCK_CARD_EN;
	LOG_DBG("sdhc:clk_set: stable SDHCI_CLOCK_CONTROL:%04x", clk_reg);
	sys_write16(clk_reg, sdhci_ctx->reg_base + SDHCI_CLOCK_CONTROL);

	return 0;
}

int sdhci_set_bus_width(struct sdhci_common *sdhci_ctx, enum sdhc_bus_width bus_width)
{
	uint8_t ctrl;

	/* Set bus width */
	ctrl = sys_read8(sdhci_ctx->reg_base + SDHCI_HOST_CONTROL);

	ctrl &= ~SDHCI_CTRL_8BITBUS;

	switch (bus_width) {
	case SDHC_BUS_WIDTH1BIT:
		ctrl &= ~SDHCI_CTRL_4BITBUS;
		break;
	case SDHC_BUS_WIDTH4BIT:
		ctrl |= SDHCI_CTRL_4BITBUS;
		break;
	case SDHC_BUS_WIDTH8BIT:
		ctrl |= SDHCI_CTRL_8BITBUS;
		break;
	default:
		return -ENOTSUP;
	}

	LOG_DBG("sdhc:bus_width: SDHCI_HOST_CONTROL:%02x", ctrl);
	sys_write8(ctrl, sdhci_ctx->reg_base + SDHCI_HOST_CONTROL);
	return 0;
}

int sdhci_set_voltage(struct sdhci_common *sdhci_ctx, enum sd_voltage signal_voltage)
{
	uint16_t ctrl;

	ctrl = sys_read16(sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);

	switch (signal_voltage) {
	case SD_VOL_3_3_V:
		ctrl &= ~SDHCI_CTRL_VDD_180;
		sys_write16(ctrl, sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);

		/* Wait for 5ms */
		k_msleep(5);

		/* 3.3V regulator output should be stable within 5 ms */
		ctrl = sys_read16(sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);
		LOG_DBG("sdhc:set_3_3: SDHCI_HOST_CONTROL2: %04x", ctrl);
		if (ctrl & SDHCI_CTRL_VDD_180) {
			LOG_ERR("sdhc: 3.3V regulator output did not become stable\n");
			return -EIO;
		}

		break;
	case SD_VOL_1_8_V:
		ctrl |= SDHCI_CTRL_VDD_180;
		sys_write16(ctrl, sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);

		/* Wait for 5 ms */
		k_msleep(5);

		/* 1.8V regulator output has to be stable within 5 ms */
		ctrl = sys_read16(sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);
		LOG_DBG("sdhc:set_1_8: SDHCI_HOST_CONTROL2: %04x", ctrl);
		if (!(ctrl & SDHCI_CTRL_VDD_180)) {
			LOG_ERR("sdhc: 1.8V regulator output did not become stable\n");
			return -EIO;
		}

		break;
	default:
		LOG_DBG("sdhc: signal voltage not supported %d", signal_voltage);
		return -ENOTSUP;
	}

	return 0;
}

int sdhci_set_uhs_timing(struct sdhci_common *sdhci_ctx, enum sdhc_timing_mode timing)
{
	bool hspd = false;
	uint16_t reg;

	reg = sys_read16(sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);
	reg &= ~SDHCI_CTRL_UHS_MASK;

	switch (timing) {
	case SDHC_TIMING_LEGACY:
	case SDHC_TIMING_SDR12:
		reg |= SDHCI_CTRL_UHS_SDR12;
		break;
	case SDHC_TIMING_SDR25:
		reg |= SDHCI_CTRL_UHS_SDR25;
		hspd = true;
		break;
	case SDHC_TIMING_SDR50:
		reg |= SDHCI_CTRL_UHS_SDR50;
		break;
	case SDHC_TIMING_DDR50:
		reg |= SDHCI_CTRL_UHS_DDR50;
		break;
	case SDHC_TIMING_SDR104:
		reg |= SDHCI_CTRL_UHS_SDR104;
		break;
	default:
		return -ENOTSUP;
	}

	sdhci_enable_sd_clock(sdhci_ctx, false);

	LOG_DBG("sdhc:timing: SDHCI_HOST_CONTROL2: %04x", reg);
	sys_write16(reg, sdhci_ctx->reg_base + SDHCI_HOST_CONTROL2);

	sdhci_set_hspd(sdhci_ctx, hspd);

	sdhci_enable_sd_clock(sdhci_ctx, true);
	return 0;
}

static int sdhc_stop_transfer(struct sdhci_common *sdhci_ctx)
{
	struct sdhc_command hdc_cmd = {0};

	hdc_cmd.opcode = SD_STOP_TRANSMISSION;
	hdc_cmd.arg = 0;
	hdc_cmd.response_type = SD_RSP_TYPE_R1;
	hdc_cmd.timeout_ms = 1000;

	return sdhci_send_cmd(sdhci_ctx, &hdc_cmd, false);
}

static bool sdhc_need_cmd12(struct sdhc_command *cmd)
{
	return (cmd->opcode == SD_READ_MULTIPLE_BLOCK || cmd->opcode == SD_WRITE_MULTIPLE_BLOCK);
}

static int sdhci_poll_data_complete(struct sdhci_common *sdhci_ctx, int32_t timeout)
{
	uint32_t reg_normal_int_stat = 0;
	int ret = -ETIMEDOUT;

	timeout = timeout ? timeout : 1;
	timeout = timeout * 100;

	while (timeout > 0) {
		reg_normal_int_stat = sys_read32(sdhci_ctx->reg_base + SDHCI_INT_STATUS);
		if (reg_normal_int_stat & SDHCI_INT_XFER_COMPLETE) {
			sys_write32(SDHCI_INT_XFER_COMPLETE,
				    sdhci_ctx->reg_base + SDHCI_INT_STATUS);
			ret = 0;
			break;
		}

		k_busy_wait(10);
		timeout--;
	}

	if (reg_normal_int_stat & SDHCI_INT_ERROR) {
		LOG_ERR("sdhc:req: data xfer error int_status:%08x", reg_normal_int_stat);
		sys_write32(SDHCI_INT_ERROR_MASK, sdhci_ctx->reg_base + SDHCI_INT_STATUS);
		ret = -EIO;
	}

	if (!timeout) {
		LOG_ERR("sdhc:req: data xfer timeout int_status:%08x", reg_normal_int_stat);
	}

	return ret;
}

static void sdhci_data_port_io(struct sdhci_common *sdhci_ctx, uint8_t *data, uint32_t size,
			       bool read)
{
	uint32_t *offs = (uint32_t *)data;
	int i;

	for (i = 0; i < size; i += 4, offs++) {
		if (read) {
			*offs = sys_read32(sdhci_ctx->reg_base + SDHCI_BUFFER);
		} else {
			sys_write32(*offs, sdhci_ctx->reg_base + SDHCI_BUFFER);
		}
	}
}

static int sdhci_transfer_data(struct sdhci_common *sdhci_ctx, struct sdhc_data *data,
			       bool is_read)
{
	uint32_t block = data->blocks;
	uint32_t reg_present_state;
	uint32_t int_status, rdy_mask;
	uint8_t *data_ptr = data->data;
	int32_t timeout;

	timeout = data->timeout_ms ? data->timeout_ms : 1;
	timeout = timeout * 100;

	if (is_read) {
		rdy_mask = SDHCI_BUF_RD_ENABLE;
	} else {
		rdy_mask = SDHCI_BUF_WR_ENABLE;
	}

	while (timeout && block) {
		int_status = sys_read32(sdhci_ctx->reg_base + SDHCI_INT_STATUS);
		if (int_status & SDHCI_INT_ERROR) {
			LOG_ERR("sdhc:req: data xfer error int_status:%08x", int_status);
			return -EIO;
		}

		reg_present_state = sys_read32(sdhci_ctx->reg_base + SDHCI_PRESENT_STATE);
		if (reg_present_state & rdy_mask) {
			sdhci_data_port_io(sdhci_ctx, data_ptr, data->block_size, is_read);
			data_ptr += data->block_size;
			block--;
		} else {
			k_busy_wait(10);
			timeout--;
		}
	};

	if (!timeout) {
		LOG_ERR("sdhc:req: transfer data timeout");
		return -ETIMEDOUT;
	}

	return sdhci_poll_data_complete(sdhci_ctx, timeout / 100);
}

static void sdhci_prepare_dma(struct sdhci_common *sdhci_ctx, struct sdhc_data *data,
			      bool is_read)
{
	uintptr_t dma_addr;

	sys_cache_data_flush_range(data->data, (data->blocks * data->block_size));

	dma_addr = z_mem_phys_addr(data->data);
	LOG_DBG("sdhc:req: SDHCI_DMA_ADDRESS %08lx", dma_addr);
	sys_write32(dma_addr, sdhci_ctx->reg_base + SDHCI_DMA_ADDRESS);
}

static void sdhci_prepare_data(struct sdhci_common *sdhci_ctx, struct sdhc_command *cmd,
			       struct sdhc_data *data, bool is_read)
{
	uint16_t transfer_mode = SDHCI_TRNS_BLK_CNT_EN;
	uint32_t trans_bytes;

	trans_bytes = data->blocks * data->block_size;
	if (data->blocks > 1) {
		transfer_mode |= SDHCI_TRNS_MULTI;
	}

	if (is_read) {
		transfer_mode |= SDHCI_TRNS_READ;
	}

	if (sdhci_ctx->f_auto_cmd12 && sdhc_need_cmd12(cmd)) {
		transfer_mode |= SDHCI_TRNS_ACMD12;
	}

	if (sdhci_ctx->f_use_dma) {
		transfer_mode |= SDHCI_TRNS_DMA;
		sdhci_prepare_dma(sdhci_ctx, data, is_read);
	}

	sys_write16(SDHCI_MAKE_BLKSZ(SDHCI_DEFAULT_BOUNDARY_ARG, data->block_size),
		    sdhci_ctx->reg_base + SDHCI_BLOCK_SIZE);
	sys_write16(data->blocks, sdhci_ctx->reg_base + SDHCI_BLOCK_COUNT);
	sys_write16(transfer_mode, sdhci_ctx->reg_base + SDHCI_TRANSFER_MODE);
}

static void sdhci_cmd_done(struct sdhci_common *sdhci_ctx, struct sdhc_command *cmd,
			   bool long_resp)
{
	cmd->response[0] = sys_read32(sdhci_ctx->reg_base + SDHCI_RESPONSE);
	if (!long_resp) {
		return;
	}
	cmd->response[1] = sys_read32(sdhci_ctx->reg_base + SDHCI_RESPONSE + 0x4);
	cmd->response[2] = sys_read32(sdhci_ctx->reg_base + SDHCI_RESPONSE + 0x8);
	cmd->response[3] = sys_read32(sdhci_ctx->reg_base + SDHCI_RESPONSE + 0xC);

	/* 136-bit: RTS=01b, Response field R[127:8] - RESP3[23:0],
	 * RESP2[31:0], RESP1[31:0], RESP0[31:0]
	 * Subsystem expects 128 bits response but SDHC sends
	 * 120 bits response from R[127:8].
	 */
	cmd->response[3] = ((cmd->response[3] << 8) | ((cmd->response[2] >> 24) & 0xFF));
	cmd->response[2] = ((cmd->response[2] << 8) | ((cmd->response[1] >> 24) & 0xFF));
	cmd->response[1] = ((cmd->response[1] << 8) | ((cmd->response[0] >> 24) & 0xFF));
	cmd->response[0] = (cmd->response[0] << 8);
}

static int sdhci_poll_cmd_complete(struct sdhci_common *sdhci_ctx, int32_t timeout)
{
	uint32_t reg_normal_int_stat = 0;
	int ret = -ETIMEDOUT;

	timeout = timeout ? timeout : 1;
	timeout = timeout * 100;
	while (timeout) {
		reg_normal_int_stat = sys_read32(sdhci_ctx->reg_base + SDHCI_INT_STATUS);
		if (reg_normal_int_stat & SDHCI_INT_CMD_COMPLETE) {
			sys_write32(SDHCI_INT_CMD_COMPLETE, sdhci_ctx->reg_base + SDHCI_INT_STATUS);
			ret = 0;
			break;
		}

		k_busy_wait(10);
		timeout--;
	}

	if (reg_normal_int_stat & SDHCI_INT_ERROR) {
		LOG_ERR("sdhc:req: cmd error int_status:%08x", reg_normal_int_stat);
		sys_write32(SDHCI_INT_ERROR_MASK, sdhci_ctx->reg_base + SDHCI_INT_STATUS);
		ret = -EIO;
	}

	if (!timeout) {
		LOG_ERR("sdhc:req: cmd timeout int_status:%08x", reg_normal_int_stat);
	}

	return ret;
}

static int sdhci_send_cmd(struct sdhci_common *sdhci_ctx, struct sdhc_command *cmd,
			  bool data_present)
{
	bool long_resp = false;
	uint16_t resp_type;
	uint16_t cmd_flags;
	int ret;

	resp_type = (cmd->response_type & SDHC_NATIVE_RESPONSE_MASK);

	switch (resp_type) {
	case SD_RSP_TYPE_NONE:
		cmd_flags = SDHCI_CMD_RESP_NONE;
		break;
	case SD_RSP_TYPE_R2:
		cmd_flags = SDHCI_CMD_RESP_LONG;
		cmd_flags |= SDHCI_CMD_CRC;
		long_resp = true;
		break;
	case SD_RSP_TYPE_R3:
	case SD_RSP_TYPE_R4:
		cmd_flags = SDHCI_CMD_RESP_SHORT;
		break;
	case SD_RSP_TYPE_R1:
	case SD_RSP_TYPE_R5:
	case SD_RSP_TYPE_R6:
	case SD_RSP_TYPE_R7:
		cmd_flags = SDHCI_CMD_RESP_SHORT;
		cmd_flags |= SDHCI_CMD_CRC;
		cmd_flags |= SDHCI_CMD_INDEX;
		break;
	case SD_RSP_TYPE_R1b:
	case SD_RSP_TYPE_R5b:
		cmd_flags = SDHCI_CMD_RESP_SHORT_BUSY;
		cmd_flags |= SDHCI_CMD_CRC;
		cmd_flags |= SDHCI_CMD_INDEX;
		break;
	default:
		LOG_ERR("sdhc:req: unknown response type 0x%08x", resp_type);
		return -EINVAL;
	}

	if (data_present) {
		cmd_flags |= SDHCI_CMD_DATA;
	}

	if (cmd->opcode == SD_STOP_TRANSMISSION) {
		cmd_flags |= (SDHCI_CMD_TYPE_ABORT << SDHCI_CMD_TYPE_SHIFT);
	}

	LOG_DBG("sdhc:req: op:%d resp_type:%d", cmd->opcode, resp_type);

	sys_write8(SDHCI_DATA_MAX_TIMEOUT, sdhci_ctx->reg_base + SDHCI_TIMEOUT_CONTROL);
	sys_write32(cmd->arg, sdhci_ctx->reg_base + SDHCI_ARGUMENT_1);
	sys_write16(SDHCI_MAKE_CMD(cmd->opcode, cmd_flags), sdhci_ctx->reg_base + SDHCI_COMMAND);

	if (cmd->opcode != SD_SEND_TUNING_BLOCK) {
		ret = sdhci_poll_cmd_complete(sdhci_ctx, cmd->timeout_ms);
	}
	if (!ret && resp_type != SD_RSP_TYPE_NONE) {
		sdhci_cmd_done(sdhci_ctx, cmd, long_resp);
	}

	return ret;
}

int sdhci_send_req(struct sdhci_common *sdhci_ctx, struct sdhc_command *cmd, struct sdhc_data *data)
{
	uint32_t reg_present_state;
	bool is_read = false;
	int ret = 0;

	reg_present_state = sys_read32(sdhci_ctx->reg_base + SDHCI_PRESENT_STATE);

	if (reg_present_state & SDHCI_CMD_INHIBIT) {
		LOG_ERR("sdhc:req: CMD line is not available");
		return -EBUSY;
	}

	if (data && (reg_present_state & SDHCI_DATA_INHIBIT)) {
		LOG_ERR("sdhc:req: Data line is not available");
		return -EBUSY;
	}

	sys_write32(SDHCI_INT_ALL_MASK, sdhci_ctx->reg_base + SDHCI_INT_STATUS);

	if (cmd->opcode != SD_WRITE_SINGLE_BLOCK && cmd->opcode != SD_WRITE_MULTIPLE_BLOCK) {
		is_read = true;
	}

	if (data) {
		sdhci_prepare_data(sdhci_ctx, cmd, data, is_read);
	}

	ret = sdhci_send_cmd(sdhci_ctx, cmd, !!data);
	if (ret) {
		goto req_error;
	}

	if (!data) {
		return 0;
	}

	LOG_DBG("sdhc:req:data: blocks:%d block_size:%d buf:%p blocknum:%u", data->blocks,
		data->block_size, data->data, data->block_addr);

	if (sdhci_ctx->f_use_dma) {
		ret = sdhci_poll_data_complete(sdhci_ctx, data->timeout_ms);

		if (!ret && is_read) {
			sys_cache_data_invd_range(data->data, data->blocks * data->block_size);
		}
	} else {
		ret = sdhci_transfer_data(sdhci_ctx, data, is_read);
		if (ret) {
			LOG_ERR("sdhc:req: data transfer failed (%d)", ret);
			goto req_error;
		}
	}

	/* Host need to send CMD12 (stop) at the end of Multiple-block read and write operation,
	 * if "Auto CMD12 Enable" not configured
	 */
	if (!sdhci_ctx->f_auto_cmd12 && sdhc_need_cmd12(cmd)) {
		ret = sdhc_stop_transfer(sdhci_ctx);
		if (ret) {
			LOG_ERR("sdhc:req: cmd12 (stop) failed (%d)", ret);
			goto req_error;
		}
	}

	return 0;

req_error:
	sdhci_reset(sdhci_ctx, SDHCI_RESET_CMD);
	sdhci_reset(sdhci_ctx, SDHCI_RESET_DATA);
	return ret;
}

static int sdhci_check_version(struct sdhci_common *sdhci_ctx)
{
	bool supported;

	sdhci_ctx->version = sys_read16(sdhci_ctx->reg_base + SDHCI_HOST_VERSION);

	supported = FIELD_GET(SDHCI_SPEC_VER_MASK, sdhci_ctx->version) >= SDHCI_SPEC_300;
	LOG_INF("sdhc: detected ver:%lu vendor_ver:%lu %s",
		FIELD_GET(SDHCI_SPEC_VER_MASK, sdhci_ctx->version),
		FIELD_GET(SDHCI_VENDOR_VER_MASK, sdhci_ctx->version),
		supported ? "supported" : "unsupported");

	if (!supported) {
		return -ENOTSUP;
	}

	sdhci_ctx->caps = sys_read32(sdhci_ctx->reg_base + SDHCI_CAPABILITIES);
	sdhci_ctx->caps1 = sys_read32(sdhci_ctx->reg_base + SDHCI_CAPABILITIES_1);

	LOG_INF("sdhc: capabilities caps0:%x caps1:%x", sdhci_ctx->caps, sdhci_ctx->caps1);

	return 0;
}

int sdhci_enable_dma(struct sdhci_common *sdhci_ctx, enum sdhc_dma_select dma_mode)
{
	if (dma_mode != SDHC_DMA_SDMA) {
		return -ENOTSUP;
	}

	sdhci_ctx->f_use_dma = true;
	sdhci_ctx->dma_mode = dma_mode;

	return 0;
}

int sdhci_init_caps(struct sdhci_common *sdhci_ctx, struct sdhc_host_props *props)
{
	struct sdhc_host_caps *host_caps = &props->host_caps;
	int ret;

	ret = sdhci_check_version(sdhci_ctx);
	if (ret) {
		return ret;
	}

	/*
	 * TODO: The struct sdhc_host_caps->timeout_clk_freq has incorrect size 5bit instead
	 * of 6bit as per SDHC standard. do not read it form now.
	 */

	sdhci_ctx->max_clk = FIELD_GET(SDHCI_CLOCK_BASE_MASK, sdhci_ctx->caps);
	sdhci_ctx->max_clk *= 1000000;
	LOG_DBG("sdhc:caps: Base Clock Frequency For SD Clock %uHz", sdhci_ctx->max_clk);

	if (!props->f_min) {
		props->f_min = props->f_max / SDHCI_MAX_DIV_SPEC_300;
		LOG_ERR("sdhc:caps: set min bus frequency to %u", props->f_min);
	}

	host_caps->max_blk_len = FIELD_GET(SDHCI_MAX_BLOCK_MASK, sdhci_ctx->caps);
	LOG_DBG("sdhc:caps: Max Block Length %u", host_caps->max_blk_len);

	host_caps->bus_8_bit_support = sdhci_ctx->caps & SDHCI_CAN_DO_8BIT ? 1 : 0;
	host_caps->adma_2_support = sdhci_ctx->caps & SDHCI_CAN_DO_ADMA2 ? 1 : 0;
	host_caps->high_spd_support = sdhci_ctx->caps & SDHCI_CAN_DO_HISPD ? 1 : 0;
	host_caps->sdma_support = sdhci_ctx->caps & SDHCI_CAN_DO_SDMA ? 1 : 0;

	host_caps->vol_330_support = sdhci_ctx->caps & SDHCI_CAN_VDD_330 ? 1 : 0;
	host_caps->vol_300_support = sdhci_ctx->caps & SDHCI_CAN_VDD_300 ? 1 : 0;
	host_caps->vol_180_support = sdhci_ctx->caps & SDHCI_CAN_VDD_180 ? 1 : 0;
	host_caps->address_64_bit_support_v3 = sdhci_ctx->caps & SDHCI_CAN_64BIT ? 1 : 0;

	LOG_DBG("sdhc:caps: %s %s %s %s %s %s %s %s",
		host_caps->bus_8_bit_support ? "SDHCI_CAN_DO_8BIT" : "",
		host_caps->adma_2_support ? "SDHCI_CAN_DO_ADMA2" : "",
		host_caps->high_spd_support ? "SDHCI_CAN_DO_HISPD" : "",
		host_caps->sdma_support ? "SDHCI_CAN_DO_SDMA" : "",
		host_caps->vol_330_support ? "SDHCI_CAN_VDD_330" : "",
		host_caps->vol_300_support ? "SDHCI_CAN_VDD_300" : "",
		host_caps->vol_180_support ? "SDHCI_CAN_VDD_180" : "",
		host_caps->address_64_bit_support_v3 ? "SDHCI_CAN_64BIT" : "");

	host_caps->slot_type = FIELD_GET(SDHCI_SLOT_TYPE_MASK, sdhci_ctx->caps);
	LOG_DBG("sdhc:caps: Slot Type %u", host_caps->slot_type);

	host_caps->sdr50_support = sdhci_ctx->caps1 & SDHCI_SUPPORT_SDR50 ? 1 : 0;
	host_caps->sdr104_support = sdhci_ctx->caps1 & SDHCI_SUPPORT_SDR104 ? 1 : 0;
	host_caps->ddr50_support = sdhci_ctx->caps1 & SDHCI_SUPPORT_DDR50 ? 1 : 0;
	host_caps->drv_type_a_support = sdhci_ctx->caps1 & SDHCI_SUPPORT_DRV_A ? 1 : 0;
	host_caps->drv_type_c_support = sdhci_ctx->caps1 & SDHCI_SUPPORT_DRV_C ? 1 : 0;
	host_caps->drv_type_d_support = sdhci_ctx->caps1 & SDHCI_SUPPORT_DRV_D ? 1 : 0;

	host_caps->retune_timer_count =
		FIELD_GET(SDHCI_RETUNING_TIMER_COUNT_MASK, sdhci_ctx->caps1);
	LOG_DBG("sdhc:caps1: Timer Count for Re-Tuning %u", host_caps->retune_timer_count);

	host_caps->sdr50_needs_tuning = sdhci_ctx->caps1 & SDHCI_USE_SDR50_TUNING ? 1 : 0;

	host_caps->retuning_mode = FIELD_GET(SDHCI_RETUNING_MODE_MASK, sdhci_ctx->caps1);
	LOG_DBG("sdhc:caps1: Re-Tuning Modes %u", host_caps->retuning_mode);

	host_caps->clk_multiplier = FIELD_GET(SDHCI_CLOCK_MUL_MASK, sdhci_ctx->caps1);
	sdhci_ctx->clk_mul = host_caps->clk_multiplier;
	LOG_DBG("sdhc:caps1: Clock Multiplier %u", host_caps->clk_multiplier);

	LOG_DBG("sdhc:caps1: %s %s %s %s %s %s %s",
		host_caps->sdr50_support ? "SDHCI_SUPPORT_SDR50" : "",
		host_caps->sdr104_support ? "SDHCI_SUPPORT_SDR104" : "",
		host_caps->ddr50_support ? "SDHCI_SUPPORT_DDR50" : "",
		host_caps->drv_type_a_support ? "SDHCI_SUPPORT_DRV_A" : "",
		host_caps->drv_type_c_support ? "SDHCI_SUPPORT_DRV_C" : "",
		host_caps->drv_type_d_support ? "SDHCI_SUPPORT_DRV_D" : "",
		host_caps->sdr50_needs_tuning ? "SDHCI_USE_SDR50_TUNING" : "");

	return 0;
}

int sdhci_init(struct sdhci_common *sdhci_ctx)
{
	sdhci_reset(sdhci_ctx, SDHCI_RESET_ALL);
	/* Enable only interrupts served by the SD controller */
	sys_write32(SDHCI_INT_DATA_MASK | SDHCI_INT_CMD_MASK,
		    sdhci_ctx->reg_base + SDHCI_INT_ENABLE);
	/* Mask all sdhci interrupt sources */
	sys_write32(0x0, sdhci_ctx->reg_base + SDHCI_SIGNAL_ENABLE);

	return 0;
}
