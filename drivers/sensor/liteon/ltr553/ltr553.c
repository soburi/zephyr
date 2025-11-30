/*
 * Copyright (c) 2025 The Zephyr Project Developers
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT liteon_ltr553

#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(ltr553, CONFIG_SENSOR_LOG_LEVEL);

/*
 * Register map according to the LiteOn LTR-553ALS-01 datasheet.
 * 7-bit I2C address: 0x23
 */
#define LTR553_REG_ALS_CONTROL           0x80
#define LTR553_REG_PS_CONTROL            0x81
#define LTR553_REG_PS_LED                0x82
#define LTR553_REG_PS_N_PULSES           0x83
#define LTR553_REG_PS_MEAS_RATE          0x84
#define LTR553_REG_ALS_MEAS_RATE         0x85
#define LTR553_REG_PART_ID               0x86
#define LTR553_REG_MANUFACTURER_ID       0x87
#define LTR553_REG_ALS_DATA_CH1_L        0x88
#define LTR553_REG_ALS_DATA_CH1_H        0x89
#define LTR553_REG_ALS_DATA_CH0_L        0x8A
#define LTR553_REG_ALS_DATA_CH0_H        0x8B
#define LTR553_REG_ALS_PS_STATUS         0x8C
#define LTR553_REG_PS_DATA_L             0x8D
#define LTR553_REG_PS_DATA_H             0x8E
#define LTR553_REG_INTERRUPT             0x8F

/* Control bits */
#define LTR553_ALS_CONTROL_MODE_ACTIVE   BIT(0)
#define LTR553_ALS_CONTROL_SW_RESET      BIT(1)
#define LTR553_ALS_CONTROL_GAIN_SHIFT    2

#define LTR553_PS_CONTROL_SAT_EN         BIT(5)
#define LTR553_PS_CONTROL_ACTIVE_MASK    BIT_MASK(2)

#define LTR553_ALS_STATUS_ALS_DATA       BIT(2)
#define LTR553_ALS_STATUS_PS_DATA        BIT(0)

/* Default configuration values */
#define LTR553_ALS_GAIN                  4U
#define LTR553_ALS_GAIN_CODE             0x02
#define LTR553_ALS_INT_TIME_MS           50U
#define LTR553_ALS_INT_CODE              (0x01 << 3)
#define LTR553_ALS_REPEAT_CODE           0x00
#define LTR553_PS_LED_CURRENT_CODE       0x02
#define LTR553_PS_LED_DUTY_CODE          (0x01 << 3)
#define LTR553_PS_LED_FREQ_CODE          (0x02 << 5)
#define LTR553_PS_NUM_PULSES             0x01
#define LTR553_PS_MEAS_RATE_CODE         0x02

static const int32_t ltr553_ch0_coeff[4] = { 17743, 42785, 5926, 0 };
static const int32_t ltr553_ch1_coeff[4] = { -11059, 19548, -1185, 0 };

struct ltr553_data {
        uint16_t als_ch0;
        uint16_t als_ch1;
        uint16_t ps_data;
};

struct ltr553_config {
        struct i2c_dt_spec bus;
};

static int ltr553_read_als(const struct ltr553_config *config, struct ltr553_data *data)
{
        uint8_t buf[4];
        int ret = i2c_burst_read_dt(&config->bus, LTR553_REG_ALS_DATA_CH1_L, buf, sizeof(buf));

        if (ret < 0) {
                LOG_ERR("Failed to read ALS data (%d)", ret);
                return ret;
        }

        data->als_ch1 = ((uint16_t)buf[1] << 8) | buf[0];
        data->als_ch0 = ((uint16_t)buf[3] << 8) | buf[2];

        return 0;
}

static int ltr553_read_prox(const struct ltr553_config *config, struct ltr553_data *data)
{
        uint8_t buf[2];
        int ret = i2c_burst_read_dt(&config->bus, LTR553_REG_PS_DATA_L, buf, sizeof(buf));

        if (ret < 0) {
                LOG_ERR("Failed to read proximity data (%d)", ret);
                return ret;
        }

        data->ps_data = ((uint16_t)(buf[1] & 0x07) << 8) | buf[0];

        return 0;
}

static int ltr553_sample_fetch(const struct device *dev, enum sensor_channel chan)
{
        struct ltr553_data *data = dev->data;
        const struct ltr553_config *config = dev->config;
        uint8_t status;
        int ret;

        if (!(chan == SENSOR_CHAN_ALL || chan == SENSOR_CHAN_LIGHT || chan == SENSOR_CHAN_PROX)) {
                return -ENOTSUP;
        }

        ret = i2c_reg_read_byte_dt(&config->bus, LTR553_REG_ALS_PS_STATUS, &status);
        if (ret < 0) {
                LOG_ERR("Failed to read status (%d)", ret);
                return ret;
        }

        if (chan == SENSOR_CHAN_LIGHT || chan == SENSOR_CHAN_ALL) {
                if ((status & LTR553_ALS_STATUS_ALS_DATA) == 0U) {
                        return -EBUSY;
                }

                ret = ltr553_read_als(config, data);
                if (ret < 0) {
                        return ret;
                }
        }

        if (chan == SENSOR_CHAN_PROX || chan == SENSOR_CHAN_ALL) {
                if ((status & LTR553_ALS_STATUS_PS_DATA) == 0U) {
                        return -EBUSY;
                }

                ret = ltr553_read_prox(config, data);
                if (ret < 0) {
                        return ret;
                }
        }

        return 0;
}

static int ltr553_channel_get(const struct device *dev, enum sensor_channel chan, struct sensor_value *val)
{
        struct ltr553_data *data = dev->data;

        switch (chan) {
        case SENSOR_CHAN_LIGHT: {
                uint32_t sum = data->als_ch0 + data->als_ch1;
                int ratio_index;

                if (sum == 0U) {
                        val->val1 = 0;
                        val->val2 = 0;
                        return 0;
                }

                uint32_t ratio = (uint32_t)data->als_ch1 * 100U / sum;

                if (ratio < 45U) {
                        ratio_index = 0;
                } else if (ratio < 64U) {
                        ratio_index = 1;
                } else if (ratio < 85U) {
                        ratio_index = 2;
                } else {
                        ratio_index = 3;
                }

                int64_t lux_num = (int64_t)data->als_ch0 * ltr553_ch0_coeff[ratio_index] -
                                  (int64_t)data->als_ch1 * ltr553_ch1_coeff[ratio_index];

                if (lux_num <= 0) {
                        val->val1 = 0;
                        val->val2 = 0;
                        return 0;
                }

                int64_t scaled = lux_num;

                scaled *= 1000000LL;
                scaled *= 100LL;
                scaled /= LTR553_ALS_INT_TIME_MS;
                scaled /= LTR553_ALS_GAIN;
                scaled /= 10000LL;

                val->val1 = scaled / 1000000LL;
                val->val2 = scaled % 1000000LL;
                return 0;
        }
        case SENSOR_CHAN_PROX:
                val->val1 = data->ps_data;
                val->val2 = 0;
                return 0;
        default:
                return -ENOTSUP;
        }
}

static const struct sensor_driver_api ltr553_driver_api = {
        .sample_fetch = ltr553_sample_fetch,
        .channel_get = ltr553_channel_get,
};

static int ltr553_chip_init(const struct device *dev)
{
        const struct ltr553_config *config = dev->config;
        uint8_t value;
        int ret;

        if (!i2c_is_ready_dt(&config->bus)) {
                LOG_ERR("I2C bus not ready");
                return -ENODEV;
        }

        ret = i2c_reg_read_byte_dt(&config->bus, LTR553_REG_PART_ID, &value);
        if (ret < 0) {
                LOG_ERR("Failed to read PART_ID (%d)", ret);
                return ret;
        }

        if ((value >> 4) != 0x09) {
                LOG_ERR("Unexpected part id 0x%02x", value);
                return -ENODEV;
        }

        ret = i2c_reg_read_byte_dt(&config->bus, LTR553_REG_MANUFACTURER_ID, &value);
        if (ret < 0) {
                LOG_ERR("Failed to read MANUFACTURER_ID (%d)", ret);
                return ret;
        }

        if (value != 0x05U) {
                LOG_ERR("Unexpected manufacturer id 0x%02x", value);
                return -ENODEV;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_ALS_CONTROL, LTR553_ALS_CONTROL_SW_RESET);
        if (ret < 0) {
                LOG_ERR("Failed to trigger reset (%d)", ret);
                return ret;
        }

        for (int i = 0; i < 10; i++) {
                ret = i2c_reg_read_byte_dt(&config->bus, LTR553_REG_ALS_CONTROL, &value);
                if (ret < 0) {
                        LOG_ERR("Failed to poll reset (%d)", ret);
                        return ret;
                }

                if ((value & LTR553_ALS_CONTROL_SW_RESET) == 0U) {
                        break;
                }

                k_msleep(10);
        }

        if ((value & LTR553_ALS_CONTROL_SW_RESET) != 0U) {
                LOG_ERR("Reset did not complete");
                return -EIO;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_ALS_CONTROL,
                                    LTR553_ALS_CONTROL_MODE_ACTIVE | (LTR553_ALS_GAIN_CODE << LTR553_ALS_CONTROL_GAIN_SHIFT));
        if (ret < 0) {
                LOG_ERR("Failed to configure ALS control (%d)", ret);
                return ret;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_ALS_MEAS_RATE,
                                    LTR553_ALS_INT_CODE | LTR553_ALS_REPEAT_CODE);
        if (ret < 0) {
                LOG_ERR("Failed to configure ALS measurement rate (%d)", ret);
                return ret;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_PS_LED,
                                    LTR553_PS_LED_FREQ_CODE | LTR553_PS_LED_DUTY_CODE | LTR553_PS_LED_CURRENT_CODE);
        if (ret < 0) {
                LOG_ERR("Failed to configure LED (%d)", ret);
                return ret;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_PS_N_PULSES, LTR553_PS_NUM_PULSES);
        if (ret < 0) {
                LOG_ERR("Failed to configure pulses (%d)", ret);
                return ret;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_PS_MEAS_RATE, LTR553_PS_MEAS_RATE_CODE);
        if (ret < 0) {
                LOG_ERR("Failed to configure proximity measurement rate (%d)", ret);
                return ret;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_INTERRUPT, 0x00);
        if (ret < 0) {
                LOG_ERR("Failed to configure interrupt (%d)", ret);
                return ret;
        }

        ret = i2c_reg_write_byte_dt(&config->bus, LTR553_REG_PS_CONTROL,
                                    LTR553_PS_CONTROL_SAT_EN | LTR553_PS_CONTROL_ACTIVE_MASK);
        if (ret < 0) {
                LOG_ERR("Failed to enable proximity (%d)", ret);
                return ret;
        }

        return 0;
}

#define LTR553_DEFINE(inst)                                                                   \
        static struct ltr553_data ltr553_data_##inst;                                         \
                                                                                                \
        static const struct ltr553_config ltr553_config_##inst = {                             \
                .bus = I2C_DT_SPEC_INST_GET(inst),                                             \
        };                                                                                      \
                                                                                                \
        SENSOR_DEVICE_DT_INST_DEFINE(inst,                                                     \
                                      ltr553_chip_init,                                        \
                                      NULL,                                                    \
                                      &ltr553_data_##inst,                                     \
                                      &ltr553_config_##inst,                                   \
                                      POST_KERNEL,                                             \
                                      CONFIG_SENSOR_INIT_PRIORITY,                             \
                                      &ltr553_driver_api);

DT_INST_FOREACH_STATUS_OKAY(LTR553_DEFINE)
