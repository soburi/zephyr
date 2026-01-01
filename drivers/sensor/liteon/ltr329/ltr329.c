/*
 * Copyright (c) 2025 Konrad Sikora
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/sys/util.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/drivers/sensor.h>

LOG_MODULE_REGISTER(LTR329, CONFIG_SENSOR_LOG_LEVEL);

/* Register addresses */
#define LTR329_ALS_CONTR         0x80
#define LTR553_PS_CONTR          0x81
#define LTR553_PS_LED            0x82
#define LTR553_PS_N_PULSES       0x83
#define LTR553_PS_MEAS_RATE      0x84
#define LTR329_MEAS_RATE         0x85
#define LTR329_PART_ID           0x86
#define LTR329_MANUFAC_ID        0x87
#define LTR329_ALS_DATA_CH1_0    0x88
#define LTR329_ALS_DATA_CH1_1    0x89
#define LTR329_ALS_DATA_CH0_0    0x8A
#define LTR329_ALS_DATA_CH0_1    0x8B
#define LTR329_ALS_PS_STATUS     0x8C
#define LTR553_PS_DATA0          0x8D
#define LTR553_PS_DATA1          0x8E
#define LTR553_INTERRUPT         0x8F
#define LTR553_PS_THRES_UP_0     0x90
#define LTR553_PS_THRES_UP_1     0x91
#define LTR553_PS_THRES_LOW_0    0x92
#define LTR553_PS_THRES_LOW_1    0x93
#define LTR553_PS_OFFSET_0       0x94
#define LTR553_PS_OFFSET_1       0x95
#define LTR553_ALS_THRES_UP_0    0x97
#define LTR553_ALS_THRES_UP_1    0x98
#define LTR553_ALS_THRES_LOW_0   0x99
#define LTR553_ALS_THRES_LOW_1   0x9A
#define LTR553_INTERRUPT_PERSIST 0x9E

/* Bit masks and shifts for ALS_CONTR register */
#define LTR329_ALS_CONTR_MODE_MASK      BIT(0)
#define LTR329_ALS_CONTR_MODE_SHIFT     0
#define LTR329_ALS_CONTR_SW_RESET_MASK  BIT(1)
#define LTR329_ALS_CONTR_SW_RESET_SHIFT 1
#define LTR329_ALS_CONTR_GAIN_MASK      GENMASK(4, 2)
#define LTR329_ALS_CONTR_GAIN_SHIFT     2

/* Bit masks and shifts for MEAS_RATE register */
#define LTR329_MEAS_RATE_REPEAT_MASK    GENMASK(2, 0)
#define LTR329_MEAS_RATE_REPEAT_SHIFT   0
#define LTR329_MEAS_RATE_INT_TIME_MASK  GENMASK(5, 3)
#define LTR329_MEAS_RATE_INT_TIME_SHIFT 3

/* Bit masks and shift for PS_CONTR register */
#define LTR553_PS_CONTR_MODE_MASK     GENMASK(1, 0)
#define LTR553_PS_CONTR_MODE_SHIFT    0
#define LTR553_PS_CONTR_SAT_IND_MASK  BIT(5)
#define LTR553_PS_CONTR_SAT_IND_SHIFT 5

/* Bit masks and shift for PS_LED register */
#define LTR553_PS_LED_PULSE_FREQ_MASK  GENMASK(7, 5)
#define LTR553_PS_LED_PULSE_FREQ_SHIFT 5
#define LTR553_PS_LED_DUTY_CYCLE_MASK  GENMASK(4, 3)
#define LTR553_PS_LED_DUTY_CYCLE_SHIFT 3
#define LTR553_PS_LED_CURRENT_MASK     GENMASK(2, 0)
#define LTR553_PS_LED_CURRENT_SHIFT    0

/* Bit masks and shift for PS_N_PULSES register */
#define LTR553_PS_N_PULSES_COUNT_MASK  GENMASK(3, 0)
#define LTR553_PS_N_PULSES_COUNT_SHIFT 0

/* Bit masks and shift for PS_MEAS_RATE register */
#define LTR553_PS_MEAS_RATE_RATE_MASK  GENMASK(3, 0)
#define LTR553_PS_MEAS_RATE_RATE_SHIFT 0

/* Bit masks and shifts for PART_ID register */
#define LTR329_PART_ID_REVISION_MASK  GENMASK(3, 0)
#define LTR329_PART_ID_REVISION_SHIFT 0
#define LTR329_PART_ID_NUMBER_MASK    GENMASK(7, 4)
#define LTR329_PART_ID_NUMBER_SHIFT   4

/* Bit masks and shifts for MANUFAC_ID register */
#define LTR329_MANUFAC_ID_IDENTIFICATION_MASK  GENMASK(7, 0)
#define LTR329_MANUFAC_ID_IDENTIFICATION_SHIFT 0

/* Bit masks and shifts for ALS_STATUS register */
#define LTR329_ALS_PS_STATUS_PS_DATA_STATUS_MASK   BIT(0)
#define LTR329_ALS_PS_STATUS_PS_DATA_STATUS_SHIFT  0
#define LTR329_ALS_PS_STATUS_PS_INTR_STATUS_MASK   BIT(1)
#define LTR329_ALS_PS_STATUS_PS_INTR_STATUS_SHIFT  1
#define LTR329_ALS_PS_STATUS_ALS_DATA_STATUS_MASK  BIT(2)
#define LTR329_ALS_PS_STATUS_ALS_DATA_STATUS_SHIFT 2
#define LTR329_ALS_PS_STATUS_ALS_INTR_STATUS_MASK  BIT(3)
#define LTR329_ALS_PS_STATUS_ALS_INTR_STATUS_SHIFT 3
#define LTR329_ALS_PS_STATUS_ALS_GAIN_MASK         GENMASK(6, 4)
#define LTR329_ALS_PS_STATUS_ALS_GAIN_SHIFT        4
#define LTR329_ALS_PS_STATUS_ALS_DATA_VALID_MASK   BIT(7)
#define LTR329_ALS_PS_STATUS_ALS_DATA_VALID_SHIFT  7

/* Bit masks for LTR553-specific registers */
#define LTR553_INTERRUPT_PS_MASK        BIT(0)
#define LTR553_INTERRUPT_PS_SHIFT       0
#define LTR553_INTERRUPT_ALS_MASK       BIT(1)
#define LTR553_INTERRUPT_ALS_SHIFT      1
#define LTR553_INTERRUPT_POLARITY_MASK  BIT(2)
#define LTR553_INTERRUPT_POLARITY_SHIFT 2

#define LTR553_INTERRUPT_PERSIST_ALS_MASK  GENMASK(3, 0)
#define LTR553_INTERRUPT_PERSIST_ALS_SHIFT 0
#define LTR553_INTERRUPT_PERSIST_PS_MASK   GENMASK(7, 4)
#define LTR553_INTERRUPT_PERSIST_PS_SHIFT  4

#define LTR553_PS_DATA_MASK 0x07FF
#define LTR553_PS_DATA_MAX  LTR553_PS_DATA_MASK

#define LTR553_ALS_CONTR_MODE_ACTIVE 0x1
#define LTR553_PS_CONTR_MODE_ACTIVE  0x02

/* Expected sensor IDs */
#define LTR329_PART_ID_VALUE         0xA0
#define LTR329_MANUFACTURER_ID_VALUE 0x05
#define LTR553_PART_ID_VALUE         0x92
#define LTR553_MANUFACTURER_ID_VALUE 0x05

/* Timing definitions - refer to LTR-329ALS-01 datasheet */
#define LTR329_INIT_STARTUP_MS        100
#define LTR329_WAKEUP_FROM_STANDBY_MS 10

/* Macros to set and get register fields */
#define LTR329_REG_SET(reg, field, value)                                                          \
	(((value) << LTR329_##reg##_##field##_SHIFT) & LTR329_##reg##_##field##_MASK)
#define LTR329_REG_GET(reg, field, value)                                                          \
	(((value) & LTR329_##reg##_##field##_MASK) >> LTR329_##reg##_##field##_SHIFT)
#define LTR553_REG_SET(reg, field, value)                                                          \
	(((value) << LTR553_##reg##_##field##_SHIFT) & LTR553_##reg##_##field##_MASK)
#define LTR553_REG_GET(reg, field, value)                                                          \
	(((value) & LTR553_##reg##_##field##_MASK) >> LTR553_##reg##_##field##_SHIFT)

struct ltr329_config {
	const struct i2c_dt_spec bus;
	uint8_t part_id;
	uint8_t als_gain;
	uint8_t als_integration_time;
	uint8_t als_measurement_rate;
	uint8_t ps_led_pulse_freq;
	uint8_t ps_led_duty_cycle;
	uint8_t ps_led_current;
	uint8_t ps_n_pulses;
	uint8_t ps_measurement_rate;
	bool ps_saturation_indicator;
	bool ps_interrupt;
	bool als_interrupt;
	uint8_t ps_interrupt_persist;
	uint8_t als_interrupt_persist;
	uint16_t ps_upper_threshold;
	uint16_t ps_lower_threshold;
	uint16_t ps_offset;
	uint16_t als_upper_threshold;
	uint16_t als_lower_threshold;
};

struct ltr329_data {
	uint16_t als_ch0;
	uint16_t als_ch1;
	uint16_t ps_ch0;
	bool proximity_state;
};

static int ltr329_check_device_id(const struct ltr329_config *cfg)
{
	const struct i2c_dt_spec *bus = &cfg->bus;
	uint8_t id;
	int rc;

	rc = i2c_reg_read_byte_dt(bus, LTR329_PART_ID, &id);
	if (rc < 0) {
		LOG_ERR("Failed to read PART_ID");
		return rc;
	}

	if (id != cfg->part_id) {
		LOG_ERR("PART_ID mismatch: expected 0x%02X, got 0x%02X", cfg->part_id, id);
		return -ENODEV;
	}

	rc = i2c_reg_read_byte_dt(bus, LTR329_MANUFAC_ID, &id);
	if (rc < 0) {
		LOG_ERR("Failed to read MANUFAC_ID");
		return rc;
	}
	if (id != LTR329_MANUFACTURER_ID_VALUE) {
		LOG_ERR("MANUFAC_ID mismatch: expected 0x%02X, got 0x%02X",
			LTR329_MANUFACTURER_ID_VALUE, id);
		return -ENODEV;
	}

	return 0;
}

static int ltr553_init_interrupt_registers(const struct ltr329_config *cfg)
{
	const struct i2c_dt_spec *bus = &cfg->bus;
	const uint8_t interrupt = (cfg->ps_interrupt << LTR553_INTERRUPT_PS_SHIFT) |
				  (cfg->als_interrupt << LTR553_INTERRUPT_ALS_SHIFT);
	const uint8_t interrupt_persist =
		(cfg->ps_interrupt_persist << LTR553_INTERRUPT_PERSIST_PS_SHIFT) |
		(cfg->als_interrupt_persist << LTR553_INTERRUPT_PERSIST_ALS_SHIFT);
	uint8_t buf[7];
	int rc;

	buf[0] = interrupt;
	sys_put_le16(cfg->ps_upper_threshold, &buf[LTR553_PS_THRES_UP_0 - LTR553_INTERRUPT]);
	sys_put_le16(cfg->ps_lower_threshold, &buf[LTR553_PS_THRES_LOW_0 - LTR553_INTERRUPT]);
	sys_put_le16(cfg->ps_offset, &buf[LTR553_PS_OFFSET_0 - LTR553_INTERRUPT]);

	rc = i2c_burst_write_dt(bus, LTR553_INTERRUPT, buf, 7);
	if (rc < 0) {
		LOG_ERR("Failed to set interrupt and PS threshold/offset: %d", rc);
		return rc;
	}

	sys_put_le16(cfg->als_upper_threshold, &buf[0]);
	sys_put_le16(cfg->als_lower_threshold,
		     &buf[LTR553_ALS_THRES_LOW_0 - LTR553_ALS_THRES_UP_0]);

	rc = i2c_burst_write_dt(bus, LTR553_ALS_THRES_UP_0, buf, 4);
	if (rc < 0) {
		LOG_ERR("Failed to set ALS thresholds: %d", rc);
		return rc;
	}

	rc = i2c_reg_write_byte_dt(bus, LTR553_INTERRUPT_PERSIST, interrupt_persist);
	if (rc < 0) {
		LOG_ERR("Failed to set interrupt persistence");
		return rc;
	}

	return 0;
}

static int ltr553_init_ps_registers(const struct ltr329_config *cfg)
{
	const struct i2c_dt_spec *bus = &cfg->bus;
	const uint8_t ps_contr = LTR553_REG_SET(PS_CONTR, MODE, LTR553_PS_CONTR_MODE_ACTIVE) |
				 LTR553_REG_SET(PS_CONTR, SAT_IND, cfg->ps_saturation_indicator);
	const uint8_t ps_led = LTR553_REG_SET(PS_LED, PULSE_FREQ, cfg->ps_led_pulse_freq) |
			       LTR553_REG_SET(PS_LED, DUTY_CYCLE, cfg->ps_led_duty_cycle) |
			       LTR553_REG_SET(PS_LED, CURRENT, cfg->ps_led_current);
	const uint8_t ps_n_pulses = LTR553_REG_SET(PS_N_PULSES, COUNT, cfg->ps_n_pulses);
	const uint8_t ps_meas_rate = LTR553_REG_SET(PS_MEAS_RATE, RATE, cfg->ps_measurement_rate);
	const uint8_t buf[] = {ps_contr, ps_led, ps_n_pulses, ps_meas_rate};
	int rc;

	rc = i2c_burst_write_dt(bus, LTR553_PS_CONTR, buf, sizeof(buf));
	if (rc < 0) {
		LOG_ERR("Failed to set PS registers");
		return rc;
	}

	return 0;
}

static int ltr329_init_als_registers(const struct ltr329_config *cfg)
{
	const struct i2c_dt_spec *bus = &cfg->bus;
	const uint8_t control_reg = LTR329_REG_SET(ALS_CONTR, MODE, LTR553_ALS_CONTR_MODE_ACTIVE) |
				    LTR329_REG_SET(ALS_CONTR, GAIN, cfg->als_gain);
	const uint8_t meas_reg = LTR329_REG_SET(MEAS_RATE, REPEAT, cfg->als_measurement_rate) |
				 LTR329_REG_SET(MEAS_RATE, INT_TIME, cfg->als_integration_time);
	uint8_t buffer;
	int rc;

	rc = i2c_reg_write_byte_dt(bus, LTR329_ALS_CONTR, control_reg);
	if (rc < 0) {
		LOG_ERR("Failed to set ALS_CONTR register");
		return rc;
	}

	rc = i2c_reg_write_byte_dt(bus, LTR329_MEAS_RATE, meas_reg);
	if (rc < 0) {
		LOG_ERR("Failed to set MEAS_RATE register");
		return rc;
	}

	/* Read back the MEAS_RATE register to verify the settings */
	rc = i2c_reg_read_byte_dt(bus, LTR329_MEAS_RATE, &buffer);
	if (rc < 0) {
		LOG_ERR("Failed to read back MEAS_RATE register");
		return rc;
	}
	if (LTR329_REG_GET(MEAS_RATE, REPEAT, buffer) != cfg->als_measurement_rate) {
		LOG_ERR("Measurement rate mismatch: expected %u, got %u", cfg->als_measurement_rate,
			(uint8_t)LTR329_REG_GET(MEAS_RATE, REPEAT, buffer));
		return -ENODEV;
	}
	if (LTR329_REG_GET(MEAS_RATE, INT_TIME, buffer) != cfg->als_integration_time) {
		LOG_ERR("Integration time mismatch: expected %u, got %u", cfg->als_integration_time,
			(uint8_t)LTR329_REG_GET(MEAS_RATE, INT_TIME, buffer));
		return -ENODEV;
	}

	return 0;
}

static int ltr329_init(const struct device *dev)
{
	const struct ltr329_config *cfg = dev->config;
	int rc;

	if (!i2c_is_ready_dt(&cfg->bus)) {
		LOG_ERR("I2C bus not ready");
		return -ENODEV;
	}

	/* Wait for sensor startup */
	k_sleep(K_MSEC(LTR329_INIT_STARTUP_MS));

	rc = ltr329_check_device_id(cfg);
	if (rc < 0) {
		return rc;
	}

	/* Init register to enable sensor to active mode */
	rc = ltr329_init_als_registers(cfg);
	if (rc < 0) {
		return rc;
	}

	if (cfg->part_id == LTR553_PART_ID_VALUE) {
		rc = ltr553_init_ps_registers(cfg);
		if (rc < 0) {
			return rc;
		}

		rc = ltr553_init_interrupt_registers(cfg);
		if (rc < 0) {
			return rc;
		}
	}

	return 0;
}

static int ltr329_check_data_ready(const struct ltr329_config *cfg, enum sensor_channel chan)
{
	const struct i2c_dt_spec *bus = &cfg->bus;
	const bool need_als = (chan == SENSOR_CHAN_ALL) || (chan == SENSOR_CHAN_LIGHT);
	const bool need_ps = (cfg->part_id == LTR553_PART_ID_VALUE) &&
			     ((chan == SENSOR_CHAN_ALL) || (chan == SENSOR_CHAN_PROX));
	uint8_t status;
	int rc;

	rc = i2c_reg_read_byte_dt(bus, LTR329_ALS_PS_STATUS, &status);
	if (rc < 0) {
		LOG_ERR("Failed to read ALS_STATUS register");
		return rc;
	}

	if (need_als && !LTR329_REG_GET(ALS_PS_STATUS, ALS_DATA_STATUS, status)) {
		LOG_WRN("ALS data not ready");
		return -EBUSY;
	}

	if (need_ps && !LTR329_REG_GET(ALS_PS_STATUS, PS_DATA_STATUS, status)) {
		LOG_WRN("PS data not ready");
		return -EBUSY;
	}

	return 0;
}

static int ltr329_read_data(const struct ltr329_config *cfg, enum sensor_channel chan,
			    struct ltr329_data *data)
{
	const struct i2c_dt_spec *bus = &cfg->bus;
	const bool need_als = (chan == SENSOR_CHAN_ALL) || (chan == SENSOR_CHAN_LIGHT);
	const bool need_ps = (cfg->part_id == LTR553_PART_ID_VALUE) &&
			     ((chan == SENSOR_CHAN_ALL) || (chan == SENSOR_CHAN_PROX));
	const size_t read_als_ps = (LTR553_PS_DATA1 + 1) - LTR329_ALS_DATA_CH1_0;
	const size_t read_als_only = (LTR329_ALS_DATA_CH0_1 + 1) - LTR329_ALS_DATA_CH1_0;
	const size_t read_size =
		(cfg->part_id == LTR553_PART_ID_VALUE) ? read_als_ps : read_als_only;
	uint8_t reg = LTR329_ALS_DATA_CH1_0;
	uint8_t buff[read_als_ps];
	int rc;

	rc = i2c_write_read_dt(bus, &reg, sizeof(reg), buff, read_size);
	if (rc < 0) {
		LOG_ERR("Failed to read ALS data registers");
		return rc;
	}

	if (need_als) {
		data->als_ch1 = sys_get_le16(buff);
		data->als_ch0 = sys_get_le16(buff + 2);
	}

	if (need_ps) {
		data->ps_ch0 = sys_get_le16(buff + 5) & LTR553_PS_DATA_MASK;
	}

	return 0;
}

static bool ltr329_is_channel_supported(const struct ltr329_config *cfg, enum sensor_channel chan)
{
	if (cfg->part_id == LTR553_PART_ID_VALUE) {
		return (chan == SENSOR_CHAN_ALL) || (chan == SENSOR_CHAN_LIGHT) ||
		       (chan == SENSOR_CHAN_PROX);
	}

	return (chan == SENSOR_CHAN_ALL) || (chan == SENSOR_CHAN_LIGHT);
}

static int ltr329_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
	const struct ltr329_config *cfg = dev->config;
	struct ltr329_data *data = dev->data;
	int rc;

	if (!ltr329_is_channel_supported(cfg, chan)) {
		return -ENOTSUP;
	}

	rc = ltr329_check_data_ready(cfg, chan);
	if (rc < 0) {
		return rc;
	}

	rc = ltr329_read_data(cfg, chan, data);
	if (rc < 0) {
		return rc;
	}

	return 0;
}

static int ltr329_get_mapped_gain(const uint8_t reg_val, uint8_t *const output)
{
	/* Map the register value to the gain used in the lux calculation
	 * Indices 4 and 5 are invalid.
	 */
	static const uint8_t gain_lux_calc[] = {1, 2, 4, 8, 0, 0, 48, 96};

	if (reg_val < ARRAY_SIZE(gain_lux_calc)) {
		*output = gain_lux_calc[reg_val];
		/* 0 is not a valid value */
		return (*output == 0) ? -EINVAL : 0;
	}

	return -EINVAL;
}

static int ltr329_get_mapped_int_time(const uint8_t reg_val, uint8_t *const output)
{
	/* Map the register value to the integration time used in the lux calculation */
	static const uint8_t int_time_lux_calc[] = {10, 5, 20, 40, 15, 25, 30, 35};

	if (reg_val < ARRAY_SIZE(int_time_lux_calc)) {
		*output = int_time_lux_calc[reg_val];
		return 0;
	}

	*output = 0;
	return -EINVAL;
}

static int ltr329_channel_light_get(const struct device *dev, struct sensor_value *val)
{
	const struct ltr329_config *cfg = dev->config;
	struct ltr329_data *data = dev->data;
	uint8_t gain_value;
	uint8_t integration_time_value;

	if (ltr329_get_mapped_gain(cfg->als_gain, &gain_value) != 0) {
		LOG_ERR("Invalid gain configuration");
		return -EINVAL;
	}

	if (ltr329_get_mapped_int_time(cfg->als_integration_time, &integration_time_value) != 0) {
		LOG_ERR("Invalid integration time configuration");
		return -EINVAL;
	}

	if ((data->als_ch0 == 0) && (data->als_ch1 == 0)) {
		LOG_WRN("Both channels are zero; cannot compute ratio");
		return -EINVAL;
	}

	/* Calculate lux value according to the appendix A of the datasheet. */
	uint64_t lux;
	/* The calculation is scaled by 1000000 to avoid floating point. */
	uint64_t scaled_ratio =
		(data->als_ch1 * UINT64_C(1000000)) / (uint64_t)(data->als_ch0 + data->als_ch1);

	if (scaled_ratio < UINT64_C(450000)) {
		lux = (UINT64_C(1774300) * data->als_ch0 + UINT64_C(1105900) * data->als_ch1);
	} else if (scaled_ratio < UINT64_C(640000)) {
		lux = (UINT64_C(4278500) * data->als_ch0 - UINT64_C(1954800) * data->als_ch1);
	} else if (scaled_ratio < UINT64_C(850000)) {
		lux = (UINT64_C(592600) * data->als_ch0 + UINT64_C(118500) * data->als_ch1);
	} else {
		LOG_WRN("Invalid ratio: %llu", scaled_ratio);
		return -EINVAL;
	}

	/* Adjust lux value for gain and integration time.
	 * Multiply by 10 before to compensate for the integration time scaling.
	 */
	lux = (lux * 10) / (gain_value * integration_time_value);

	/* Get the integer and fractional parts from fixed point value */
	val->val1 = (int32_t)(lux / UINT64_C(1000000));
	val->val2 = (int32_t)(lux % UINT64_C(1000000));

	return 0;
}

static int ltr329_channel_proximity_get(const struct device *dev, struct sensor_value *val)
{
	const struct ltr329_config *cfg = dev->config;
	struct ltr329_data *data = dev->data;

	if (cfg->part_id != LTR553_PART_ID_VALUE) {
		return -ENOTSUP;
	}

	LOG_DBG("proximity: state=%d data: %d L-H: %d - %d", data->proximity_state,
		data->ps_ch0, cfg->ps_lower_threshold, cfg->ps_upper_threshold);

	if (data->proximity_state) {
		if (data->ps_ch0 <= cfg->ps_lower_threshold) {
			data->proximity_state = false;
		}
	} else {
		if (data->ps_ch0 >= cfg->ps_upper_threshold) {
			data->proximity_state = true;
		}
	}

	val->val1 = data->proximity_state ? 1 : 0;
	val->val2 = 0;

	return 0;
}

static int ltr329_channel_get(const struct device *dev, enum sensor_channel chan,
			      struct sensor_value *val)
{
	int ret = -ENOTSUP;

	if (chan == SENSOR_CHAN_LIGHT || chan == SENSOR_CHAN_ALL) {
		ret = ltr329_channel_light_get(dev, val);

		if (ret < 0) {
			return ret;
		}
	}

	if (chan == SENSOR_CHAN_PROX || chan == SENSOR_CHAN_ALL) {
		ret = ltr329_channel_proximity_get(dev, val);
		if (ret < 0) {
			return ret;
		}
	}

	return ret;
}

static DEVICE_API(sensor, ltr329_driver_api) = {
	.sample_fetch = ltr329_sample_fetch,
	.channel_get = ltr329_channel_get,
};

#define LTR329_ALS_GAIN_REG(n)                                                                     \
	COND_CODE_1(DT_NODE_HAS_PROP(n, gain), (DT_PROP(n, gain)),                                 \
		    (DT_ENUM_IDX_OR(n, als_gain, 0)))
#define LTR329_ALS_INT_TIME_REG(n)                                                                 \
	COND_CODE_1(DT_NODE_HAS_PROP(n, integration_time), (DT_PROP(n, integration_time)),         \
		    (DT_ENUM_IDX_OR(n, als_integration_time, 0)))
#define LTR329_ALS_MEAS_RATE_REG(n)                                                                \
	COND_CODE_1(DT_NODE_HAS_PROP(n, measurement_rate), (DT_PROP(n, measurement_rate)),         \
		    (DT_ENUM_IDX_OR(n, als_measurement_rate, 3)))

#define DEFINE_LTRXXX(_num, partid)                                                                \
	BUILD_ASSERT(DT_PROP_OR(_num, ps_upper_threshold, LTR553_PS_DATA_MASK) <=                  \
		     LTR553_PS_DATA_MASK);                                                         \
	BUILD_ASSERT(DT_PROP_OR(_num, ps_lower_threshold, 0) <= LTR553_PS_DATA_MAX);               \
	BUILD_ASSERT(DT_PROP_OR(_num, ps_lower_threshold, 0) <=                                    \
		     DT_PROP_OR(_num, ps_upper_threshold, LTR553_PS_DATA_MAX));                    \
	BUILD_ASSERT(DT_PROP_OR(_num, ps_offset, 0) <= LTR553_PS_DATA_MAX);                        \
	BUILD_ASSERT(DT_PROP_OR(_num, als_lower_threshold, 0) <=                                   \
		     DT_PROP_OR(_num, als_upper_threshold, UINT16_MAX));                           \
	static struct ltr329_data ltr329_data_##_num;                                              \
	static const struct ltr329_config ltr329_config_##_num = {                                 \
		.bus = I2C_DT_SPEC_GET(_num),                                                      \
		.part_id = partid,                                                                 \
		.als_gain = LTR329_ALS_GAIN_REG(_num),                                             \
		.als_integration_time = LTR329_ALS_INT_TIME_REG(_num),                             \
		.als_measurement_rate = LTR329_ALS_MEAS_RATE_REG(_num),                            \
		.ps_led_pulse_freq = DT_ENUM_IDX_OR(_num, ps_led_pulse_frequency, 3),              \
		.ps_led_duty_cycle = DT_ENUM_IDX_OR(_num, ps_led_duty_cycle, 3),                   \
		.ps_led_current = DT_ENUM_IDX_OR(_num, ps_led_current, 4),                         \
		.ps_n_pulses = DT_PROP_OR(_num, ps_n_pulses, 1),                                   \
		.ps_measurement_rate = DT_ENUM_IDX_OR(_num, ps_measurement_rate, 2),               \
		.ps_saturation_indicator = DT_PROP_OR(_num, ps_saturation_indicator, false),       \
		.ps_interrupt = DT_PROP_OR(_num, ps_interrupt, false),                             \
		.als_interrupt = DT_PROP_OR(_num, als_interrupt, false),                           \
		.ps_interrupt_persist = DT_PROP_OR(_num, ps_interrupt_persist, 0),                 \
		.als_interrupt_persist = DT_PROP_OR(_num, als_interrupt_persist, 0),               \
		.ps_upper_threshold = DT_PROP_OR(_num, ps_upper_threshold, LTR553_PS_DATA_MAX),    \
		.ps_lower_threshold = DT_PROP_OR(_num, ps_lower_threshold, 0),                     \
		.als_upper_threshold = DT_PROP_OR(_num, als_upper_threshold, UINT16_MAX),          \
		.als_lower_threshold = DT_PROP_OR(_num, als_lower_threshold, 0),                   \
		.ps_offset = DT_PROP_OR(_num, ps_offset, 0),                                       \
	};                                                                                         \
	SENSOR_DEVICE_DT_DEFINE(_num, ltr329_init, NULL, &ltr329_data_##_num,                      \
				&ltr329_config_##_num, POST_KERNEL, CONFIG_SENSOR_INIT_PRIORITY,   \
				&ltr329_driver_api);

#define DEFINE_LTR329(node_id) DEFINE_LTRXXX(node_id, LTR329_PART_ID_VALUE)
#define DEFINE_LTR553(node_id) DEFINE_LTRXXX(node_id, LTR553_PART_ID_VALUE)

DT_FOREACH_STATUS_OKAY(liteon_ltr329, DEFINE_LTR329)
DT_FOREACH_STATUS_OKAY(liteon_ltr553, DEFINE_LTR553)
