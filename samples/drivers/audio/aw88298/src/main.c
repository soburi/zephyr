/*
 * Copyright (c) 2025 The Zephyr Authors
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/drivers/i2s.h>
#include <zephyr/audio/codec.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/printk.h>

#define SAMPLE_FREQUENCY 48000U
#define SAMPLE_WORD_SIZE 16U
#define CHANNEL_COUNT    2U

static const int16_t sine_wave[] = {
 3211,   6392,   9511,  12539,  15446,  18204,  20787,  23169,
25329,  27244,  28897,  30272,  31356,  32137,  32609,  32767,
32609,  32137,  31356,  30272,  28897,  27244,  25329,  23169,
20787,  18204,  15446,  12539,   9511,   6392,   3211,      0,
-3212,  -6393,  -9512, -12540, -15447, -18205, -20788, -23170,
-25330, -27245, -28898, -30273, -31357, -32138, -32610, -32767,
-32610, -32138, -31357, -30273, -28898, -27245, -25330, -23170,
-20788, -18205, -15447, -12540,  -9512,  -6393,  -3212,     -1,
};

#define SAMPLES_PER_BLOCK ARRAY_SIZE(sine_wave)
#define BLOCK_SIZE        (SAMPLES_PER_BLOCK * CHANNEL_COUNT * sizeof(int16_t))
#define INITIAL_BLOCKS    4U
#define BLOCK_COUNT       (INITIAL_BLOCKS + 4U)
#define PLAYBACK_SECONDS  4U
#define TOTAL_BLOCKS      ((PLAYBACK_SECONDS * SAMPLE_FREQUENCY) / SAMPLES_PER_BLOCK)

K_MEM_SLAB_DEFINE(tx_mem_slab, BLOCK_SIZE, BLOCK_COUNT, 4);

static void fill_block(int16_t *buffer, uint32_t block_idx)
{
    const size_t count = ARRAY_SIZE(sine_wave);
    const uint32_t attenuation = block_idx % 3U;

    for (size_t i = 0; i < count; i++) {
        int16_t sample_l = sine_wave[i] >> attenuation;
        size_t right_index = (i + count / 4U) % count;
        int16_t sample_r = sine_wave[right_index] >> attenuation;

        buffer[CHANNEL_COUNT * i] = sample_l;
        buffer[CHANNEL_COUNT * i + 1] = sample_r;
    }
}

int main(void)
{
    const struct device *const i2s_dev = DEVICE_DT_GET(DT_ALIAS(i2s_tx));
    const struct device *const codec_dev = DEVICE_DT_GET(DT_ALIAS(audio_codec));
    struct i2s_config i2s_cfg = {0};
    struct audio_codec_cfg codec_cfg = {0};
    audio_property_value_t volume = {.vol = 160};
    bool codec_started = false;
    bool i2s_started = false;
    int ret = 0;

    printk("AW88298 playback sample\n");

    if (!device_is_ready(i2s_dev)) {
        printk("I2S device %s is not ready\n", i2s_dev->name);
        return -ENODEV;
    }

    if (!device_is_ready(codec_dev)) {
        printk("Codec device %s is not ready\n", codec_dev->name);
        return -ENODEV;
    }

    i2s_cfg.word_size = SAMPLE_WORD_SIZE;
    i2s_cfg.channels = CHANNEL_COUNT;
    i2s_cfg.format = I2S_FMT_DATA_FORMAT_I2S;
    i2s_cfg.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
    i2s_cfg.frame_clk_freq = SAMPLE_FREQUENCY;
    i2s_cfg.mem_slab = &tx_mem_slab;
    i2s_cfg.block_size = BLOCK_SIZE;
    i2s_cfg.timeout = 2000U;

    ret = i2s_configure(i2s_dev, I2S_DIR_TX, &i2s_cfg);
    if (ret < 0) {
        printk("Failed to configure I2S: %d\n", ret);
        return ret;
    }

    codec_cfg.dai_type = AUDIO_DAI_TYPE_I2S;
    codec_cfg.dai_route = AUDIO_ROUTE_PLAYBACK;
    codec_cfg.dai_cfg.i2s.word_size = SAMPLE_WORD_SIZE;
    codec_cfg.dai_cfg.i2s.channels = CHANNEL_COUNT;
    codec_cfg.dai_cfg.i2s.format = I2S_FMT_DATA_FORMAT_I2S;
    codec_cfg.dai_cfg.i2s.options = I2S_OPT_FRAME_CLK_MASTER | I2S_OPT_BIT_CLK_MASTER;
    codec_cfg.dai_cfg.i2s.frame_clk_freq = SAMPLE_FREQUENCY;

    ret = audio_codec_configure(codec_dev, &codec_cfg);
    if (ret < 0) {
        printk("Failed to configure codec: %d\n", ret);
        return ret;
    }

    ret = audio_codec_set_property(codec_dev, AUDIO_PROPERTY_OUTPUT_VOLUME,
                                   AUDIO_CHANNEL_ALL, volume);
    if (ret < 0) {
        printk("Failed to set codec volume: %d\n", ret);
    }

    ret = audio_codec_apply_properties(codec_dev);
    if (ret < 0) {
        printk("Failed to apply codec properties: %d\n", ret);
    }

    for (uint32_t i = 0U; i < INITIAL_BLOCKS; i++) {
        void *block;

        ret = k_mem_slab_alloc(&tx_mem_slab, &block, K_FOREVER);
        if (ret < 0) {
            printk("Failed to allocate TX block: %d\n", ret);
            return ret;
        }

        fill_block((int16_t *)block, i);

        ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
        if (ret < 0) {
            printk("Failed to queue TX block: %d\n", ret);
            return ret;
        }
    }

    audio_codec_start_output(codec_dev);
    codec_started = true;

    ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_START);
    if (ret < 0) {
        printk("Failed to start I2S stream: %d\n", ret);
        goto cleanup;
    }
    i2s_started = true;

    for (uint32_t block_idx = INITIAL_BLOCKS; block_idx < TOTAL_BLOCKS; block_idx++) {
        void *block;

        ret = k_mem_slab_alloc(&tx_mem_slab, &block, K_FOREVER);
        if (ret < 0) {
            printk("Failed to allocate TX block: %d\n", ret);
            goto cleanup;
        }

        fill_block((int16_t *)block, block_idx);

        ret = i2s_write(i2s_dev, block, BLOCK_SIZE);
        if (ret < 0) {
            printk("Failed to queue TX block: %d\n", ret);
            goto cleanup;
        }
    }

    ret = i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_DRAIN);
    if (ret < 0) {
        printk("Failed to drain I2S stream: %d\n", ret);
        goto cleanup;
    }

cleanup:
    if (i2s_started) {
        (void)i2s_trigger(i2s_dev, I2S_DIR_TX, I2S_TRIGGER_STOP);
    }

    if (codec_started) {
        audio_codec_stop_output(codec_dev);
    }

    if (ret < 0) {
        printk("Playback finished with error: %d\n", ret);
        return ret;
    }

    printk("AW88298 playback complete\n");

    return 0;
}
