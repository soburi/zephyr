/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * Copyright (c) 2025
 */

#ifndef ZEPHYR_DRIVERS_AUDIO_ES7210_H_
#define ZEPHYR_DRIVERS_AUDIO_ES7210_H_

#define ES7210_REG_RESET_CTL        0x00
#define ES7210_REG_CLK_ON_OFF       0x01
#define ES7210_REG_DIGITAL_PDN      0x06
#define ES7210_REG_ADC_OSR          0x07
#define ES7210_REG_MODE_CFG         0x08
#define ES7210_REG_TCT0_CHPINI      0x09
#define ES7210_REG_TCT1_CHPINI      0x0A

#define ES7210_REG_ADC34_HPF2       0x20
#define ES7210_REG_ADC34_HPF1       0x21
#define ES7210_REG_ADC12_HPF2       0x22
#define ES7210_REG_ADC12_HPF1       0x23

#define ES7210_REG_ANALOG_SYS       0x40
#define ES7210_REG_MICBIAS12        0x41
#define ES7210_REG_MICBIAS34        0x42
#define ES7210_REG_MIC1_GAIN        0x43
#define ES7210_REG_MIC2_GAIN        0x44
#define ES7210_REG_MIC3_GAIN        0x45
#define ES7210_REG_MIC4_GAIN        0x46
#define ES7210_REG_MIC1_LP          0x47
#define ES7210_REG_MIC2_LP          0x48
#define ES7210_REG_MIC3_LP          0x49
#define ES7210_REG_MIC4_LP          0x4A
#define ES7210_REG_MIC12_PDN        0x4B
#define ES7210_REG_MIC34_PDN        0x4C

#endif /* ZEPHYR_DRIVERS_AUDIO_ES7210_H_ */
