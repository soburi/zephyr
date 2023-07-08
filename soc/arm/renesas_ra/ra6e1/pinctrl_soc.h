/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_SOC_ARM_RENESAS_RA_RA6E1_PINCTRL_SOC_H_
#define ZEPHYR_SOC_ARM_RENESAS_RA_RA6E1_PINCTRL_SOC_H_

#define PODR_POS  0
#define PIDR_POS  1
#define PDR_POS   2
#define PCR_POS   4
#define NCODR_POS 6
#define ISEL_POS  14
#define ASEL_POS  15
#define PMR_POS   16

struct ra_pinctrl_soc_pin {
	union {
		uint32_t config;
		struct {
			uint8_t PODR: 1;
			uint8_t PIDR: 1;
			uint8_t PDR: 1;
			uint8_t RESERVED2: 1;
			uint8_t PCR: 1;
			uint8_t RESERVED3: 1;
			uint8_t NCODR: 1;
			uint8_t RESERVED4: 1;
			uint8_t RESERVED41: 1;
			uint8_t RESERVED42: 1;
			uint8_t DSCR: 2;
			uint8_t EOFR: 2;
			uint8_t ISEL: 1;
			uint8_t ASEL: 1;
			uint8_t PMR: 1;
			uint8_t RESERVED5: 7;
			uint8_t PSEL: 5;
			uint8_t RESERVED6: 3;
		};
		struct {
			uint32_t RESERVED7: 17;
			uint8_t pin: 4;
			uint8_t port: 3;
			uint32_t RESERVED8: 5;
			uint8_t port4: 3;
		};
	};
};

typedef struct ra_pinctrl_soc_pin pinctrl_soc_pin_t;

/**
 * @brief Utility macro to initialize each pin.
 *
 * @param node_id Node identifier.
 * @param prop Property name.
 * @param idx Property entry index.
 */
#define Z_PINCTRL_STATE_PIN_INIT(node_id, prop, idx)                                               \
	{                                                                                          \
		.config = DT_PROP_BY_IDX(node_id, prop, idx),                                      \
	},

/**
 * @brief Utility macro to initialize state pins contained in a given property.
 *
 * @param node_id Node identifier.
 * @param prop Property name describing state pins.
 */
#define Z_PINCTRL_STATE_PINS_INIT(node_id, prop)                                                   \
	{                                                                                          \
		DT_FOREACH_CHILD_VARGS(DT_PHANDLE(node_id, prop), DT_FOREACH_PROP_ELEM, pinmux,    \
				       Z_PINCTRL_STATE_PIN_INIT)                                   \
	}

#endif /* ZEPHYR_SOC_ARM_RENESAS_RA_RA6E1_PINCTRL_SOC_H_ */
