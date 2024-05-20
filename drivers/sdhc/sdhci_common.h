/*
 * Copyright (c) 2024 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 */

#ifndef DRIVERS_SDHC_SDHCI_COMMON_H_
#define DRIVERS_SDHC_SDHCI_COMMON_H_

/**
 * @brief SD Host Controller (SDHC) generic implementation module according to
 * SD Host Controller Standard Specification Version 3.0
 * https://www.sdcard.org/developers/sd-standard-overview/host-controllers/
 *
 * It supports:
 * - Only SDHC Specification Version 3.0
 * - PIO transfers
 * - SDMA transfers
 * - SD card specification only
 * - SDHC reset
 * - SDCLK clock frequency configuration
 * - SD Bus Voltage Select
 * - v3.3 and v1.8 signal voltages switch
 * - Legacy, SDR12, SDR25, DDR50 timings configuration
 *
 * Limitations
 * - no tuning support
 * - SDR50 may work, without tuning
 * - no ADMA support
 * - only polling xfer verified
 * - no presets support
 */

/* Registers */
#define SDHCI_DMA_ADDRESS     0x00 /* 32bit */
#define SDHCI_BLOCK_SIZE      0x04 /* 16bit */
#define SDHCI_BLOCK_COUNT     0x06 /* 16bit */
#define SDHCI_ARGUMENT_1      0x08 /* 32bit */
#define SDHCI_TRANSFER_MODE   0x0C /* 16bit */
#define SDHCI_COMMAND         0x0E /* 16bit */
#define SDHCI_RESPONSE        0x10 /* 32bit */
#define SDHCI_RESPONSE1       0x14 /* 32bit */
#define SDHCI_RESPONSE2       0x18 /* 32bit */
#define SDHCI_RESPONSE3       0x1C /* 32bit */
#define SDHCI_BUFFER          0x20 /* 32bit */
#define SDHCI_PRESENT_STATE   0x24 /* 32bit */
#define SDHCI_HOST_CONTROL    0x28 /* 8bit */
#define SDHCI_POWER_CONTROL   0x29 /* 8bit */
#define SDHCI_CLOCK_CONTROL   0x2C /* 16bit */
#define SDHCI_TIMEOUT_CONTROL 0x2E /* 8bit */
#define SDHCI_SOFTWARE_RESET  0x2F /* 8bit */
#define SDHCI_INT_STATUS      0x30 /* 32bit Error[31,16] Normal[15,0] */
#define SDHCI_INT_ENABLE      0x34 /* 32bit Error[31,16] Normal[15,0] */
#define SDHCI_SIGNAL_ENABLE   0x38 /* 32bit Error[31,16] Normal[15,0]*/
#define SDHCI_HOST_CONTROL2   0x3E /* 16bit */
#define SDHCI_CAPABILITIES    0x40 /* 32bit */
#define SDHCI_CAPABILITIES_1  0x44 /* 32bit */
#define SDHCI_HOST_VERSION    0xFE /* 16bit */

/* SDHCI_BLOCK_SIZE */
#define SDHCI_DEFAULT_BOUNDARY_ARG             (0x7)
#define SDHCI_MAKE_BLKSZ(sdma_boundary, blksz) ((((sdma_boundary) & 0x7) << 12) | ((blksz) & 0xFFF))

/* SDHCI_TRANSFER_MODE */
#define SDHCI_TRNS_DMA        BIT(0)
#define SDHCI_TRNS_BLK_CNT_EN BIT(1)
#define SDHCI_TRNS_ACMD12     BIT(2)
#define SDHCI_TRNS_ACMD23     BIT(3)
#define SDHCI_TRNS_READ       BIT(4)
#define SDHCI_TRNS_MULTI      BIT(5)

/* SDHCI_COMMAND */
#define SDHCI_CMD_RESP_MASK       GENMASK(1, 0)
#define SDHCI_CMD_RESP_NONE       0x0
#define SDHCI_CMD_RESP_LONG       0x1
#define SDHCI_CMD_RESP_SHORT      0x2
#define SDHCI_CMD_RESP_SHORT_BUSY 0x3
#define SDHCI_CMD_CRC             BIT(3)
#define SDHCI_CMD_INDEX           BIT(4)
#define SDHCI_CMD_DATA            BIT(5)
#define SDHCI_CMD_TYPE_MASK       GENMASK(7, 6)
#define SDHCI_CMD_TYPE_SHIFT      6
#define SDHCI_CMD_TYPE_NORMAL     0x0
#define SDHCI_CMD_TYPE_RESUME     0x1
#define SDHCI_CMD_TYPE_SUSPEND    0x2
#define SDHCI_CMD_TYPE_ABORT      0x3
#define SDHCI_CMD_OP_MASK         GENMASK(13, 8)

#define SDHCI_MAKE_CMD(op, flags) ((((op) << 8) & SDHCI_CMD_OP_MASK) | ((flags) & 0xff))

/* SDHCI_PRESENT_STATE */
#define SDHCI_CMD_INHIBIT           BIT(0)
#define SDHCI_DATA_INHIBIT          BIT(1)
#define SDHCI_DAT_ACTIVE            BIT(2)
#define SDHCI_DOING_WRITE           BIT(8)
#define SDHCI_DOING_READ            BIT(9)
#define SDHCI_BUF_WR_ENABLE         BIT(10)
#define SDHCI_BUF_RD_ENABLE         BIT(11)
#define SDHCI_CARD_PRESENT          BIT(16)
#define SDHCI_CARD_STATE_STABLE     BIT(17)
#define SDHCI_CARD_DETECT_PIN_LEVEL BIT(18)
#define SDHCI_WRITE_PROTECT         BIT(19)
#define SDHCI_DATA_LVL_MASK         GENMASK(23, 20)
#define SDHCI_DATA_0_LVL_MASK       BIT(20)
#define SDHCI_CMD_LVL_MASK          BIT(24)

/* SDHCI_POWER_CONTROL */
#define SDHCI_POWER_ON  BIT(0)
#define SDHCI_POWER_180 0x0A
#define SDHCI_POWER_300 0x0C
#define SDHCI_POWER_330 0x0E

/* SDHCI_HOST_CONTROL */
#define SDHCI_CTRL_LED         BIT(0)
#define SDHCI_CTRL_4BITBUS     BIT(1)
#define SDHCI_CTRL_HISPD       BIT(2)
#define SDHCI_CTRL_DMA_MASK    GENMASK(4, 3)
#define SDHCI_CTRL_SDMA        0x0
#define SDHCI_CTRL_ADMA32      0x2
#define SDHCI_CTRL_8BITBUS     BIT(5)
#define SDHCI_CTRL_CD_TEST_INS BIT(6)
#define SDHCI_CTRL_CD_TEST     BIT(7)

/* SDHCI_CLOCK_CONTROL */
#define SDHCI_CLOCK_INT_EN     BIT(0)
#define SDHCI_CLOCK_INT_STABLE BIT(1)
#define SDHCI_CLOCK_CARD_EN    BIT(2)
#define SDHCI_PROG_CLOCK_MODE  BIT(5)
#define SDHCI_DIVIDER_SHIFT    8
#define SDHCI_DIVIDER_HI_SHIFT 6
#define SDHCI_DIV_MASK         GENMASK(7, 0)
#define SDHCI_DIV_HI_MASK      GENMASK(9, 8)
#define SDHCI_MAX_DIV_SPEC_300 2046

/* SDHCI_SOFTWARE_RESET */
#define SDHCI_RESET_ALL  BIT(0)
#define SDHCI_RESET_CMD  BIT(1)
#define SDHCI_RESET_DATA BIT(2)

/* SDHCI_INT_STATUS, SDHCI_INT_ENABLE, SDHCI_SIGNAL_ENABLE  */
#define SDHCI_INT_CMD_COMPLETE  BIT(0)
#define SDHCI_INT_XFER_COMPLETE BIT(1)
#define SDHCI_INT_DMA_END       BIT(3)
#define SDHCI_INT_BUF_WR_READY  BIT(4)
#define SDHCI_INT_BUF_RD_READY  BIT(5)
#define SDHCI_INT_CARD_INSERT   BIT(6)
#define SDHCI_INT_CARD_REMOVE   BIT(7)
#define SDHCI_INT_CARD_INT      BIT(8)
#define SDHCI_INT_ERROR         BIT(15)
#define SDHCI_INT_TIMEOUT       BIT(16)
#define SDHCI_INT_CRC           BIT(17)
#define SDHCI_INT_END_BIT       BIT(18)
#define SDHCI_INT_INDEX         BIT(19)
#define SDHCI_INT_DATA_TIMEOUT  BIT(20)
#define SDHCI_INT_DATA_CRC      BIT(21)
#define SDHCI_INT_DATA_END_BIT  BIT(22)
#define SDHCI_INT_BUS_POWER     BIT(23)
#define SDHCI_INT_ACMD12ERR     BIT(24)
#define SDHCI_INT_ADMA_ERROR    BIT(25)

#define SDHCI_INT_NORMAL_MASK GENMASK(14, 0)
#define SDHCI_INT_ERROR_MASK  GENMASK(31, 15)

#define SDHCI_INT_CMD_MASK                                                                         \
	(SDHCI_INT_CMD_COMPLETE | SDHCI_INT_TIMEOUT | SDHCI_INT_CRC | SDHCI_INT_END_BIT |          \
	 SDHCI_INT_INDEX)
#define SDHCI_INT_DATA_MASK                                                                        \
	(SDHCI_INT_XFER_COMPLETE | SDHCI_INT_DMA_END | SDHCI_INT_BUF_RD_READY |                    \
	 SDHCI_INT_BUF_WR_READY | SDHCI_INT_DATA_TIMEOUT | SDHCI_INT_DATA_CRC |                    \
	 SDHCI_INT_DATA_END_BIT | SDHCI_INT_ADMA_ERROR)
#define SDHCI_INT_ALL_MASK ((uint32_t)-1)

/* SDHCI_HOST_CONTROL2 */
#define SDHCI_CTRL_UHS_MASK          GENMASK(2, 0)
#define SDHCI_CTRL_UHS_SDR12         0x0
#define SDHCI_CTRL_UHS_SDR25         0x1
#define SDHCI_CTRL_UHS_SDR50         0x2
#define SDHCI_CTRL_UHS_SDR104        0x3
#define SDHCI_CTRL_UHS_DDR50         0x4
#define SDHCI_CTRL_HS400             0x5 /* Non-standard */
#define SDHCI_CTRL_VDD_180           BIT(3)
#define SDHCI_CTRL_DRV_TYPE_MASK     GENMASK(5, 4)
#define SDHCI_CTRL_DRV_TYPE_B        0x0
#define SDHCI_CTRL_DRV_TYPE_A        0x1
#define SDHCI_CTRL_DRV_TYPE_C        0x2
#define SDHCI_CTRL_DRV_TYPE_D        0x3
#define SDHCI_CTRL_EXEC_TUNING       BIT(6)
#define SDHCI_CTRL_TUNED_CLK         BIT(7)
#define SDHCI_CTRL_PRESET_VAL_ENABLE BIT(15)

/* SDHCI_CAPABILITIES */
#define SDHCI_TIMEOUT_CLK_MASK GENMASK(5, 0)
#define SDHCI_TIMEOUT_CLK_UNIT BIT(7)
#define SDHCI_CLOCK_BASE_MASK  GENMASK(15, 8)
#define SDHCI_MAX_BLOCK_MASK   GENMASK(17, 16)
#define SDHCI_CAN_DO_8BIT      BIT(18)
#define SDHCI_CAN_DO_ADMA2     BIT(19)
#define SDHCI_CAN_DO_HISPD     BIT(21)
#define SDHCI_CAN_DO_SDMA      BIT(22)
#define SDHCI_CAN_VDD_330      BIT(24)
#define SDHCI_CAN_VDD_300      BIT(25)
#define SDHCI_CAN_VDD_180      BIT(26)
#define SDHCI_CAN_64BIT        BIT(28)
#define SDHCI_SLOT_TYPE_MASK   GENMASK(31, 30)

/* SDHCI_CAPABILITIES_1 */
#define SDHCI_SUPPORT_SDR50             BIT(0)
#define SDHCI_SUPPORT_SDR104            BIT(1)
#define SDHCI_SUPPORT_DDR50             BIT(2)
#define SDHCI_SUPPORT_DRV_A             BIT(4)
#define SDHCI_SUPPORT_DRV_C             BIT(5)
#define SDHCI_SUPPORT_DRV_D             BIT(6)
#define SDHCI_RETUNING_TIMER_COUNT_MASK GENMASK(11, 8)
#define SDHCI_USE_SDR50_TUNING          BIT(13)
#define SDHCI_RETUNING_MODE_MASK        GENMASK(15, 14)
#define SDHCI_CLOCK_MUL_MASK            GENMASK(23, 16)

/* SDHCI_HOST_VERSION */
#define SDHCI_VENDOR_VER_MASK  GENMASK(15, 8)
#define SDHCI_VENDOR_VER_SHIFT 8
#define SDHCI_SPEC_VER_MASK    GENMASK(7, 0)
#define SDHCI_SPEC_100         0
#define SDHCI_SPEC_200         1
#define SDHCI_SPEC_300         2
#define SDHCI_SPEC_400         3

/**
 * @brief SDHC DMA type selection codes.
 *
 * SDHC v3.0 defines SDMA and ADMA2 modes.
 */
enum sdhc_dma_select {
	SDHC_DMA_SDMA = 0x0,
	SDHC_DMA_ADMA2 = 0x2,
};

/**
 * @brief SDHC common data structure.
 *
 * The SDHC drivers should configure @ref reg_base IO base address first and pass
 * pointer on @ref sdhci_common to all SDHC generic APIs.
 */
struct sdhci_common {
	mm_reg_t reg_base; /**< SDHC IO base address */

	/** @cond INTERNAL_HIDDEN */
	uint32_t version; /* SDHC version */
	uint32_t caps;    /* SDHC Capabilities register bits[31,0] */
	uint32_t caps1;   /* SDHC Capabilities register bits[63,32] */
	uint32_t max_clk; /* Maximum Base Clock frequency */
	uint32_t clk_mul; /* Clock Multiplier value */
	bool f_auto_cmd12; /* flag to Auto CMD12 Enable */
	bool f_use_dma; /* flag to enable DMA */
	enum sdhc_dma_select dma_mode; /* selected DMA mode */
	/** @endcond */
};

/**
 * @brief Initialize SDHC capabilities.
 *
 * Reads and checks SDHC HW version and capabilities. Fills @ref sdhc_host_props and
 * @ref sdhc_host_caps. The SDHC drivers should call this API first and then correct
 * @ref sdhc_host_props depending on SDHC HW specific implementation and SW configuration.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param props: generic SD host controller properties to fill.
 *
 * @retval 0 reset succeeded.
 * @retval -ENOTSUP: SDHC HW is not supported.
 */
int sdhci_init_caps(struct sdhci_common *sdhci_ctx, struct sdhc_host_props *props);

/**
 * @brief Enable SDHC DMA.
 *
 * Enables SDHC DMA and selects desired DMA mode.
 * The SDHC drivers should call this API after @ref sdhci_init_caps.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param dma_mode: SDHC DMA mode.
 *
 * @retval 0 reset succeeded.
 * @retval -ENOTSUP: SDHC HW is not supported.
 */
int sdhci_enable_dma(struct sdhci_common *sdhci_ctx, enum sdhc_dma_select dma_mode);

/**
 * @brief Enable SDHC auto CMD12 functionality.
 *
 * Enables SDHC auto CMD12 functionality in Transfer Mode Register for
 * multi-block read/write requests. By default the auto CMD12 is disabled.
 * The SDHC drivers should call this API after @ref sdhci_init_caps.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param enable: true to enable auto CMD12.
 *
 * @retval 0 reset succeeded.
 * @retval -ENOTSUP: SDHC HW is not supported.
 */
static inline void sdhci_enable_auto_cmd12(struct sdhci_common *sdhci_ctx, bool enable)
{
	sdhci_ctx->f_auto_cmd12 = enable;
}

/**
 * @brief Initialize SDHC hardware.
 *
 * Performs basic SDHC HW initialization, including "Software Reset For All" operation.
 *
 * @param sdhci_ctx: SDHC data context.
 *
 * @retval 0 reset succeeded.
 */
int sdhci_init(struct sdhci_common *sdhci_ctx);

/**
 * @brief Get CD status from SDHC hardware.
 *
 * Gets CD status from SDHCI_PRESENT_STATE "Card Inserted" field.
 *
 * @param sdhci_ctx: SDHC data context
 *
 * @retval 0 card no detected.
 * @retval 1 card detected.
 */
int sdhci_get_cd(struct sdhci_common *sdhci_ctx);

/**
 * @brief Software Reset SDHC hardware.
 *
 * Resets SDHC hardware depending @ref mask:
 *  bit 0 - Software Reset For All;
 *  bit 1 - Software Reset For CMD Line;
 *  bit 2 - Software Reset For DAT Line.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param mask: reset mask.
 *
 * @retval 0 reset succeeded.
 * @retval -ETIMEDOUT reset timed out.
 */
int sdhci_reset(struct sdhci_common *sdhci_ctx, uint8_t mask);

/**
 * @brief Get DAT0 line busy state from SDHC hardware.
 *
 * @param sdhci_ctx: SDHC data context.
 *
 * @retval 0 DAT0 free.
 * @retval 1 DAT0 busy.
 */
int sdhci_is_card_busy(struct sdhci_common *sdhci_ctx);

/**
 * @brief Configure SDCLK Frequency and enable Internal and SD clocks in SDHC hardware.
 *
 * Disables Internal and SD clocks If @ref clock is 0.
 * Configures SDCLK Frequency and enables Internal and SD clocks If @ref clock is not 0.
 * It is responsibility of the SDHC driver to check @ref clock valid range.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param clock: SDCLK Frequency, if 0 - disable clocks.
 *
 * @retval 0 on success.
 * @retval -ETIMEDOUT wait for "Internal Clock Stable" timeout.
 */
int sdhci_set_clock(struct sdhci_common *sdhci_ctx, enum sdhc_clock_speed clock);

/**
 * @brief Set SD Bus Voltage and power on SD bus in SDHC hardware.
 *
 * Disables SD bus power If @ref bus_voltage is 0. It also disables SD clock.
 * Sets SD Bus Voltage and power on SD bus If @ref bus_voltage is not 0. It also enables SD clock
 * if it was configured before.
 * It is responsibility of the SDHC driver to check @ref bus_voltage valid range.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param bus_voltage: SD Bus Voltage, if 0 - power off SD bus.
 */
void sdhci_set_bus_power(struct sdhci_common *sdhci_ctx, enum sd_voltage bus_voltage);

/**
 * @brief Set SD Bus width in SDHC hardware.
 *
 * It is responsibility of the SDHC driver to check @ref bus_width valid range.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param bus_width: SD Bus width.
 *
 * @retval 0 on success.
 * @retval -ENOTSUP if @ref bus_width is unsupported.
 */
int sdhci_set_bus_width(struct sdhci_common *sdhci_ctx, enum sdhc_bus_width bus_width);

/**
 * @brief Configure Signaling voltage in SDHC hardware.
 *
 * Configures Signaling voltage in SDHC hardware between @ref SD_VOL_3_3_V and @ref SD_VOL_1_8_V.
 * It is responsibility of the SDHC driver to check @ref signal_voltage valid range.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param signal_voltage: SD Bus Signaling voltage.
 *
 * @retval 0 on success.
 * @retval -ENOTSUP if @ref signal_voltage is unsupported.
 * @retval -EIO if voltage switch failed.
 */
int sdhci_set_voltage(struct sdhci_common *sdhci_ctx, enum sd_voltage signal_voltage);

/**
 * @brief Select UHS mode timings in SDHC hardware.
 *
 * Configures UHS mode timings in SDHCI_HOST_CONTROL2 "UHS Mode Select" field.
 * The SD clock is kept disabled while UHS timing mode is changed.
 * It is responsibility of the SDHC driver to check @ref signal_voltage valid range.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param timing: UHS timing mode.
 *
 * @retval 0 on success.
 * @retval -ENOTSUP if @ref timing is unsupported.
 */
int sdhci_set_uhs_timing(struct sdhci_common *sdhci_ctx, enum sdhc_timing_mode timing);

/**
 * @brief Send SD command to SDHC hardware.
 *
 * @param sdhci_ctx: SDHC data context.
 * @param cmd: SD command
 * @param cmd: SD command data, optional
 *
 * @retval 0 on success.
 * @retval -EBUSY if CMD or DAT lines busy
 * @retval -EINVAL if command response type in unknown
 * @retval -EIO if error detected during command or data processing
 * @retval -ETIMEDOUT reset timed out.
 */
int sdhci_send_req(struct sdhci_common *sdhci_ctx, struct sdhc_command *cmd,
		   struct sdhc_data *data);

#endif /* DRIVERS_SDHC_SDHCI_COMMON_H_ */
