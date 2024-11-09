/*
 * Copyright (c) 2022 Espressif Systems (Shanghai) Co., Ltd.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_ESP32C3_GPIO_SIGMAP_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_ESP32C3_GPIO_SIGMAP_H_

#define ESP_NOSIG                       ESP_SIG_INVAL

#define ESP_SPICLK_OUT_MUX              ESP_SPICLK_OUT
#define ESP_SPIQ_IN                     0
#define ESP_SPIQ_OUT                    0
#define ESP_SPID_IN                     1
#define ESP_SPID_OUT                    1
#define ESP_SPIHD_IN                    2
#define ESP_SPIHD_OUT                   2
#define ESP_SPIWP_IN                    3
#define ESP_SPIWP_OUT                   3
#define ESP_SPICLK_OUT                  4
#define ESP_SPICS0_OUT                  5
#define ESP_U0RXD_IN                    6
#define ESP_U0TXD_OUT                   6
#define ESP_U0CTS_IN                    7
#define ESP_U0RTS_OUT                   7
#define ESP_U0DSR_IN                    8
#define ESP_U0DTR_OUT                   8
#define ESP_U1RXD_IN                    9
#define ESP_U1TXD_OUT                   9
#define ESP_U1CTS_IN                    10
#define ESP_U1RTS_OUT                   10
#define ESP_U1DSR_IN                    11
#define ESP_U1DTR_OUT                   11
#define ESP_I2S_MCLK_IN                 12
#define ESP_I2S_MCLK_OUT                12
#define ESP_I2SO_BCK_IN                 13
#define ESP_I2SO_BCK_OUT                13
#define ESP_I2SO_WS_IN                  14
#define ESP_I2SO_WS_OUT                 14
#define ESP_I2SI_SD_IN                  15
#define ESP_I2SO_SD_OUT                 15
#define ESP_I2SI_BCK_IN                 16
#define ESP_I2SI_BCK_OUT                16
#define ESP_I2SI_WS_IN                  17
#define ESP_I2SI_WS_OUT                 17
#define ESP_GPIO_BT_PRIORITY            18
#define ESP_GPIO_WLAN_PRIO              18
#define ESP_GPIO_BT_ACTIVE              19
#define ESP_GPIO_WLAN_ACTIVE            19
#define ESP_BB_DIAG0                    20
#define ESP_BB_DIAG1                    21
#define ESP_BB_DIAG2                    22
#define ESP_BB_DIAG3                    23
#define ESP_BB_DIAG4                    24
#define ESP_BB_DIAG5                    25
#define ESP_BB_DIAG6                    26
#define ESP_BB_DIAG7                    27
#define ESP_BB_DIAG8                    28
#define ESP_BB_DIAG9                    29
#define ESP_BB_DIAG10                   30
#define ESP_BB_DIAG11                   31
#define ESP_BB_DIAG12                   32
#define ESP_BB_DIAG13                   33
#define ESP_BB_DIAG14                   34
#define ESP_BB_DIAG15                   35
#define ESP_BB_DIAG16                   36
#define ESP_BB_DIAG17                   37
#define ESP_BB_DIAG18                   38
#define ESP_BB_DIAG19                   39
#define ESP_USB_EXTPHY_VP               40
#define ESP_USB_EXTPHY_OEN              40
#define ESP_USB_EXTPHY_VM               41
#define ESP_USB_EXTPHY_SPEED            41
#define ESP_USB_EXTPHY_RCV              42
#define ESP_USB_EXTPHY_VPO              42
#define ESP_USB_EXTPHY_VMO              43
#define ESP_USB_EXTPHY_SUSPND           44
#define ESP_EXT_ADC_START               45
#define ESP_LEDC_LS_SIG_OUT0            45
#define ESP_LEDC_LS_SIG_OUT1            46
#define ESP_LEDC_LS_SIG_OUT2            47
#define ESP_LEDC_LS_SIG_OUT3            48
#define ESP_LEDC_LS_SIG_OUT4            49
#define ESP_LEDC_LS_SIG_OUT5            50
#define ESP_RMT_SIG_IN0                 51
#define ESP_RMT_SIG_OUT0                51
#define ESP_RMT_SIG_IN1                 52
#define ESP_RMT_SIG_OUT1                52
#define ESP_I2CEXT0_SCL_IN              53
#define ESP_I2CEXT0_SCL_OUT             53
#define ESP_I2CEXT0_SDA_IN              54
#define ESP_I2CEXT0_SDA_OUT             54
#define ESP_GPIO_SD0_OUT                55
#define ESP_GPIO_SD1_OUT                56
#define ESP_GPIO_SD2_OUT                57
#define ESP_GPIO_SD3_OUT                58
#define ESP_FSPICLK_IN                  63
#define ESP_FSPICLK_OUT                 63
#define ESP_FSPIQ_IN                    64
#define ESP_FSPIQ_OUT                   64
#define ESP_FSPID_IN                    65
#define ESP_FSPID_OUT                   65
#define ESP_FSPIHD_IN                   66
#define ESP_FSPIHD_OUT                  66
#define ESP_FSPIWP_IN                   67
#define ESP_FSPIWP_OUT                  67
#define ESP_FSPICS0_IN                  68
#define ESP_FSPICS0_OUT                 68
#define ESP_FSPICS1_OUT                 69
#define ESP_FSPICS2_OUT                 70
#define ESP_FSPICS3_OUT                 71
#define ESP_FSPICS4_OUT                 72
#define ESP_FSPICS5_OUT                 73
#define ESP_TWAI_RX                     74
#define ESP_TWAI_TX                     74
#define ESP_TWAI_BUS_OFF_ON             75
#define ESP_TWAI_CLKOUT                 76
#define ESP_PCMFSYNC_IN                 77
#define ESP_BT_AUDIO0_IRQ               77
#define ESP_PCMCLK_IN                   78
#define ESP_BT_AUDIO1_IRQ               78
#define ESP_PCMDIN                      79
#define ESP_BT_AUDIO2_IRQ               79
#define ESP_RW_WAKEUP_REQ               80
#define ESP_BLE_AUDIO0_IRQ              80
#define ESP_BLE_AUDIO1_IRQ              81
#define ESP_BLE_AUDIO2_IRQ              82
#define ESP_PCMFSYNC_OUT                83
#define ESP_PCMCLK_OUT                  84
#define ESP_PCMDOUT                     85
#define ESP_BLE_AUDIO_SYNC0_P           86
#define ESP_BLE_AUDIO_SYNC1_P           87
#define ESP_BLE_AUDIO_SYNC2_P           88
#define ESP_ANT_SEL0                    89
#define ESP_ANT_SEL1                    90
#define ESP_ANT_SEL2                    91
#define ESP_ANT_SEL3                    92
#define ESP_ANT_SEL4                    93
#define ESP_ANT_SEL5                    94
#define ESP_ANT_SEL6                    95
#define ESP_ANT_SEL7                    96
#define ESP_SIG_IN_FUNC_97              97
#define ESP_SIG_IN_FUNC97               97
#define ESP_SIG_IN_FUNC_98              98
#define ESP_SIG_IN_FUNC98               98
#define ESP_SIG_IN_FUNC_99              99
#define ESP_SIG_IN_FUNC99               99
#define ESP_SIG_IN_FUNC_100             100
#define ESP_SIG_IN_FUNC100              100
#define ESP_SYNCERR                     101
#define ESP_SYNCFOUND_FLAG              102
#define ESP_EVT_CNTL_IMMEDIATE_ABORT    103
#define ESP_LINKLBL                     104
#define ESP_DATA_EN                     105
#define ESP_DATA                        106
#define ESP_PKT_TX_ON                   107
#define ESP_PKT_RX_ON                   108
#define ESP_RW_TX_ON                    109
#define ESP_RW_RX_ON                    110
#define ESP_EVT_REQ_P                   111
#define ESP_EVT_STOP_P                  112
#define ESP_BT_MODE_ON                  113
#define ESP_GPIO_LC_DIAG0               114
#define ESP_GPIO_LC_DIAG1               115
#define ESP_GPIO_LC_DIAG2               116
#define ESP_CH                          117
#define ESP_RX_WINDOW                   118
#define ESP_UPDATE_RX                   119
#define ESP_RX_STATUS                   120
#define ESP_CLK_GPIO                    121
#define ESP_NBT_BLE                     122
#define ESP_CLK_OUT_OUT1                123
#define ESP_CLK_OUT_OUT2                124
#define ESP_CLK_OUT_OUT3                125
#define ESP_SPICS1_OUT                  126
#define ESP_SIG_GPIO_OUT                128
#define ESP_GPIO_MAP_DATE               0x2006130

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_PINCTRL_ESP32C3_GPIO_SIGMAP_H_ */
