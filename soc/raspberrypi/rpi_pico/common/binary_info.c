/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/dt-bindings/pinctrl/rpi-pico-pinctrl-common.h>
#include <boot_stage2/config.h>

#include <version.h>
#ifdef HAS_APP_VERSION
#include <app_version.h>
#endif

#include <pico/binary_info.h>
#include "binary_info.h"

/*
 * The binary_info macros ensure uniqueness by line numbers,
 * which is improper to use in the DT macro so that
 * we will redefine it here.
 */
#define __bi_lineno_var_name_sym(sym)     __CONCAT(__bi_, sym)
#define __bi_ptr_lineno_var_name_sym(sym) __CONCAT(__bi_ptr, sym)
#define __bi_enclosure_check_lineno_var_name_sym(sym)                                              \
	__CONCAT(_error_bi_is_missing_enclosing_decl_, sym)
#define __bi_enclosure_check_sym(x, sym) (x + __bi_enclosure_check_lineno_var_name_sym(sym))
#define __bi_mark_enclosure_sym(sym)                                                               \
	static const __unused int __bi_enclosure_check_lineno_var_name_sym(sym) = 0;

#define bi_decl_sym(_decl, sym)                                                                    \
	__bi_mark_enclosure_sym(sym) _decl;                                                        \
	__bi_decl(__bi_ptr_lineno_var_name_sym(sym), &__bi_lineno_var_name_sym(sym).core,          \
		  ".binary_info.keep.", __used);

#define __bi_encoded_pins_with_func_sym(_encoding, _sym)                                           \
	static const struct _binary_info_pins_with_func __bi_lineno_var_name_sym(_sym) = {         \
		.core =                                                                            \
			{                                                                          \
				.type = __bi_enclosure_check_sym(BINARY_INFO_TYPE_PINS_WITH_FUNC,  \
								 _sym),                            \
				.tag = BINARY_INFO_TAG_RASPBERRY_PI,                               \
			},                                                                         \
		.pin_encoding = _encoding}

#define __bi_encoded_pins_64_with_func_sym(_encoding, _sym)                                        \
	static const struct _binary_info_pins64_with_func __bi_lineno_var_name_sym(_sym) = {       \
		.core =                                                                            \
			{                                                                          \
				.type = __bi_enclosure_check_sym(                                  \
					BINARY_INFO_TYPE_PINS64_WITH_FUNC, _sym),                  \
				.tag = BINARY_INFO_TAG_RASPBERRY_PI,                               \
			},                                                                         \
		.pin_encoding = _encoding}

/* utils for pin encoding calcluation */

#ifdef CONFIG_SOC_RP2040
#define MAX_PIN_ENTRY                4
#define PIN_ENCODE(n, idx, offset)   ((uint32_t)PIN_NUM(n, idx) << (2 + (idx + offset + 1) * 5))
#define BI_ENCODE_PINS_WITH_FUNC_SYM __bi_encoded_pins_with_func_sym
#else
#define MAX_PIN_ENTRY                6
#define PIN_ENCODE(n, idx, offset)   ((uint64_t)PIN_NUM(n, idx) << ((idx + offset + 1) * 8))
#define BI_ENCODE_PINS_WITH_FUNC_SYM __bi_encoded_pins_64_with_func_sym
#endif

#define PIN_NUM(n, idx)  ((DT_PROP_BY_IDX(n, pinmux, idx) >> RP2_PIN_NUM_POS) & RP2_PIN_NUM_MASK)
#define PIN_FUNC(n, idx) (DT_PROP_BY_IDX(n, pinmux, idx) & RP2_ALT_FUNC_MASK)

/* Recurrence formula for calculating pin offsets for each group */

#define OFFSET_TO_GROUP_IDX_N(n, i, x)                                                             \
	EXPR_ADD(COND_CODE_1(DT_NODE_HAS_PROP(DT_CHILD_BY_IDX(n, i), pinmux),                      \
			     (DT_PROP_LEN(DT_CHILD_BY_IDX(n, i), pinmux)), (0)), x)

#define OFFSET_TO_GROUP_IDX_0(n) 0
#define OFFSET_TO_GROUP_IDX_1(n) OFFSET_TO_GROUP_IDX_N(n, 0, OFFSET_TO_GROUP_IDX_0(n))
#define OFFSET_TO_GROUP_IDX_2(n) OFFSET_TO_GROUP_IDX_N(n, 1, OFFSET_TO_GROUP_IDX_1(n))
#define OFFSET_TO_GROUP_IDX_3(n) OFFSET_TO_GROUP_IDX_N(n, 2, OFFSET_TO_GROUP_IDX_2(n))
#define OFFSET_TO_GROUP_IDX_4(n) OFFSET_TO_GROUP_IDX_N(n, 3, OFFSET_TO_GROUP_IDX_3(n))
#define OFFSET_TO_GROUP_IDX_5(n) OFFSET_TO_GROUP_IDX_N(n, 5, OFFSET_TO_GROUP_IDX_4(n))
#define OFFSET_TO_GROUP_IDX_6(n) OFFSET_TO_GROUP_IDX_N(n, 6, OFFSET_TO_GROUP_IDX_5(n))
#define OFFSET_TO_GROUP_IDX_7(n) OFFSET_TO_GROUP_IDX_N(n, 7, OFFSET_TO_GROUP_IDX_6(n))
#define OFFSET_TO_GROUP_IDX_8(n) OFFSET_TO_GROUP_IDX_N(n, 8, OFFSET_TO_GROUP_IDX_7(n))

#define OFFSET_TO_GROUP_IDX(n, i) UTIL_CAT(OFFSET_TO_GROUP_IDX_, i)(n)
#define COUNT_ALL_PINS(n)         OFFSET_TO_GROUP_IDX_8(n)

/* Iterate all pins in group */

#define EACH_PINCTRL_GROUP_PINMUX(n, fn, sep, ...)                                                 \
	DT_FOREACH_PROP_ELEM_SEP_VARGS(n, pinmux, fn, sep, __VA_ARGS__)
#define EACH_PIN_IN_GROUP(n, sep, fn, ...)                                                         \
	DT_FOREACH_CHILD_SEP_VARGS(n, EACH_PINCTRL_GROUP_PINMUX, sep, fn, sep, __VA_ARGS__)

/* Encode the pins in a group */

#define IS_LAST_PIN(end, idx, off) EXPR_EQ(end, EXPR_ADD(1, EXPR_ADD(idx, off)))
#define PIN_TERMINATE(n, p, i, offset, end)                                                        \
	COND_CODE_1(IS_LAST_PIN(end, i, offset), (PIN_ENCODE(n, i, EXPR_INCR(offset))), (0))
#define PIN_ENTRY(n, p, i, off) COND_CODE_1(DT_PROP_HAS_IDX(n, p, i), (PIN_ENCODE(n, i, off)), (0))
#define ENCODE_PIN(n, p, i, end)                                                                   \
	PIN_ENTRY(n, p, i, OFFSET_TO_GROUP_IDX(DT_PARENT(n), DT_NODE_CHILD_IDX(n))) |              \
		PIN_TERMINATE(n, p, i, OFFSET_TO_GROUP_IDX(DT_PARENT(n), DT_NODE_CHILD_IDX(n)),    \
			      end)
#define ENCODE_GROUP_PINS(n) (EACH_PIN_IN_GROUP(n, (|), ENCODE_PIN, COUNT_ALL_PINS(n)))

/* Get group-wide pin functions */

#define PIN_FUNC_(n, p, i, _) PIN_FUNC(n, i)
#define GROUP_PIN_FUNC(n)     (EACH_PIN_IN_GROUP(n, (|), PIN_FUNC_))

/* Check if pin functions are all equal within a group */

#define PIN_FUNC_EQUALS(n, p, i, func) (PIN_FUNC(n, i) == func)
#define ALL_PIN_FUNC_IS(n, pinfunc)    (EACH_PIN_IN_GROUP(n, (&&), PIN_FUNC_EQUALS, pinfunc))

#define BI_PINS_FROM_PINCTRL(n)                                                                    \
	COND_CODE_1(EXPR_EQ(0, COUNT_ALL_PINS(n)), (),                                             \
		    (BUILD_ASSERT(COUNT_ALL_PINS(n) <= MAX_PIN_ENTRY,                              \
				  "Too many pin in group");                                        \
		     BUILD_ASSERT(ALL_PIN_FUNC_IS(n, GROUP_PIN_FUNC(n)),                           \
				  "Group must contain only single function type");                 \
		     bi_decl_sym(BI_ENCODE_PINS_WITH_FUNC_SYM(BI_PINS_ENCODING_MULTI |             \
			    (GROUP_PIN_FUNC(n) << 3) |  ENCODE_GROUP_PINS(n), n), n)));

#define BI_PINS_FROM_PINCTRL_DEVICE(n)                                                             \
	COND_CODE_1(DT_NODE_HAS_PROP(n, pinctrl_0),                                                \
		  (BI_PINS_FROM_PINCTRL(DT_PROP_BY_IDX(n, pinctrl_0, 0))), ())

#define BI_PINS_FROM_PINCTRL_DEVICE_CHILDREN(n) DT_FOREACH_CHILD(n, BI_PINS_FROM_PINCTRL_DEVICE)

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_NAME
#define BI_PROGRAM_NAME CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_NAME
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_DESCRIPTION
#define BI_PROGRAM_DESCRIPTION CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_DESCRIPTION
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_URL
#define BI_PROGRAM_URL CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_URL
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_BUILD_DATE
#define BI_PROGRAM_BUILD_DATE CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_BUILD_DATE
#else
#define BI_PROGRAM_BUILD_DATE __DATE__
#endif

extern uint32_t __rom_region_end;
bi_decl(bi_binary_end((intptr_t)&__rom_region_end));

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_NAME_ENABLE
bi_decl(bi_program_name((uint32_t)BI_PROGRAM_NAME));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PICO_BOARD_ENABLE
bi_decl(bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_PICO_BOARD,
		  (uint32_t)CONFIG_BOARD));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_SDK_VERSION_STRING_ENABLE
bi_decl(bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_SDK_VERSION,
		  (uint32_t) "zephyr-" STRINGIFY(BUILD_VERSION)));
#endif

#if defined(CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_VERSION_STRING_ENABLE) && defined(HAS_APP_VERSION)
bi_decl(bi_program_version_string((uint32_t)APP_VERSION_EXTENDED_STRING));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_DESCRIPTION_ENABLE
bi_decl(bi_program_description((uint32_t)BI_PROGRAM_DESCRIPTION));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_URL_ENABLE
bi_decl(bi_program_url((uint32_t)BI_PROGRAM_URL));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_BUILD_DATE_ENABLE
bi_decl(bi_program_build_date_string((uint32_t)BI_PROGRAM_BUILD_DATE));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_BOOT_STAGE2_NAME_ENABLE
bi_decl(bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_BOOT2_NAME,
		  (uint32_t)PICO_BOOT_STAGE2_NAME));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_ATTRIBUTE_BUILD_TYPE_ENABLE
#ifdef CONFIG_DEBUG
bi_decl(bi_program_build_attribute((uint32_t) "Debug"));
#else
bi_decl(bi_program_build_attribute((uint32_t) "Release"));
#endif
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PINS_WITH_FUNC_ENABLE
DT_FOREACH_STATUS_OKAY(arm_pl011, BI_PINS_FROM_PINCTRL_DEVICE);
DT_FOREACH_STATUS_OKAY(arm_pl022, BI_PINS_FROM_PINCTRL_DEVICE);
DT_FOREACH_STATUS_OKAY(snps_designware_i2c, BI_PINS_FROM_PINCTRL_DEVICE);
DT_FOREACH_STATUS_OKAY(raspberrypi_pico_adc, BI_PINS_FROM_PINCTRL_DEVICE);
DT_FOREACH_STATUS_OKAY(raspberrypi_pico_clock_controller, BI_PINS_FROM_PINCTRL_DEVICE);
DT_FOREACH_STATUS_OKAY(raspberrypi_pico_pwm, BI_PINS_FROM_PINCTRL_DEVICE);
DT_FOREACH_STATUS_OKAY(raspberrypi_pico_usbd, BI_PINS_FROM_PINCTRL_DEVICE);

DT_FOREACH_STATUS_OKAY(raspberrypi_pico_pio, BI_PINS_FROM_PINCTRL_DEVICE_CHILDREN);
#endif
