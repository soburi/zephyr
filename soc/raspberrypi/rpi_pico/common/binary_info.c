/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@fujitsu.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>
#include <zephyr/sys/util_macro.h>

#include <zephyr/dt-bindings/pinctrl/rpi-pico-pinctrl-common.h>

#include <soc.h>

#include <boot_stage2/config.h>
#include <pico/binary_info.h>

#include <version.h>
#ifdef HAS_APP_VERSION
#include <app_version.h>
#endif

/* utils for pin encoding calcluation */

#ifdef CONFIG_SOC_RP2040
#define ENCODE_PIN(n, idx, off)  ((uint32_t)PIN_NUM(n, idx) << (2 + (idx + off + 1) * 5))
#define MAX_PIN_ENTRY            4
#define BI_ENCODE_PINS_WITH_FUNC __bi_encoded_pins_with_func
#else
#define ENCODE_PIN(n, idx, off)  ((uint64_t)PIN_NUM(n, idx) << ((idx + off + 1) * 8))
#define MAX_PIN_ENTRY            6
#define BI_ENCODE_PINS_WITH_FUNC __bi_encoded_pins_64_with_func
#endif

#define PIN_NUM(n, idx)  ((DT_PROP_BY_IDX(n, pinmux, idx) >> RP2_PIN_NUM_POS) & RP2_PIN_NUM_MASK)
#define PIN_FUNC(n, idx) (DT_PROP_BY_IDX(n, pinmux, idx) & RP2_ALT_FUNC_MASK)

/* Pin counts and offs for groups */

#define PIN_GROUP_PIN_COUNT(node_id)                                                               \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, pinmux), (DT_PROP_LEN(node_id, pinmux)), (0))

#define PIN_GROUP_PIN_COUNT_MATCH(child, idx)                                                      \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (PIN_GROUP_PIN_COUNT(child)), (0))
#define PIN_GROUP_PIN_COUNT_BY_IDX(node_id, idx)                                                   \
	/*                                                                                         \
	 * The raspberrypi,pico-pinctrl child binding does not permit a "status"                   \
	 * property, so the STATUS_OK iterator still visits every group.                           \
	 */                                                                                        \
	(DT_FOREACH_CHILD_STATUS_OKAY_SEP_VARGS(node_id, PIN_GROUP_PIN_COUNT_MATCH, (+), idx))

#define PIN_GROUP_OFFSET_STEP_TERM(i, node_id) +PIN_GROUP_PIN_COUNT_BY_IDX(node_id, i)
#define PIN_GROUP_OFFSET_STEP(node_id, count)                                                      \
	(0 LISTIFY(count, PIN_GROUP_OFFSET_STEP_TERM, (), node_id))
#define PIN_GROUP_OFFSET_0(node_id)  (0)
#define PIN_GROUP_OFFSET_1(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 1)
#define PIN_GROUP_OFFSET_2(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 2)
#define PIN_GROUP_OFFSET_3(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 3)
#define PIN_GROUP_OFFSET_4(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 4)
#define PIN_GROUP_OFFSET_5(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 5)
#define PIN_GROUP_OFFSET_6(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 6)
#define PIN_GROUP_OFFSET_7(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 7)
#define PIN_GROUP_OFFSET_8(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 8)
#define PIN_GROUP_OFFSET_9(node_id)  PIN_GROUP_OFFSET_STEP(node_id, 9)
#define PIN_GROUP_OFFSET_10(node_id) PIN_GROUP_OFFSET_STEP(node_id, 10)
#define PIN_GROUP_OFFSET_11(node_id) PIN_GROUP_OFFSET_STEP(node_id, 11)
#define PIN_GROUP_OFFSET_12(node_id) PIN_GROUP_OFFSET_STEP(node_id, 12)
#define PIN_GROUP_OFFSET_13(node_id) PIN_GROUP_OFFSET_STEP(node_id, 13)
#define PIN_GROUP_OFFSET_14(node_id) PIN_GROUP_OFFSET_STEP(node_id, 14)
#define PIN_GROUP_OFFSET_15(node_id) PIN_GROUP_OFFSET_STEP(node_id, 15)
#define PIN_GROUP_OFFSET_16(node_id) PIN_GROUP_OFFSET_STEP(node_id, 16)
#define PIN_GROUP_OFFSET_17(node_id) PIN_GROUP_OFFSET_STEP(node_id, 17)
#define PIN_GROUP_OFFSET_18(node_id) PIN_GROUP_OFFSET_STEP(node_id, 18)
#define PIN_GROUP_OFFSET_19(node_id) PIN_GROUP_OFFSET_STEP(node_id, 19)
#define PIN_GROUP_OFFSET_20(node_id) PIN_GROUP_OFFSET_STEP(node_id, 20)
#define PIN_GROUP_OFFSET_21(node_id) PIN_GROUP_OFFSET_STEP(node_id, 21)
#define PIN_GROUP_OFFSET_22(node_id) PIN_GROUP_OFFSET_STEP(node_id, 22)
#define PIN_GROUP_OFFSET_23(node_id) PIN_GROUP_OFFSET_STEP(node_id, 23)
#define PIN_GROUP_OFFSET_24(node_id) PIN_GROUP_OFFSET_STEP(node_id, 24)

#define PIN_GROUP_OFFSET(node_id, idx) UTIL_CAT(PIN_GROUP_OFFSET_, idx)(node_id)
#define PIN_GROUP_TOTAL_PINS(node_id)  PIN_GROUP_OFFSET(node_id, DT_CHILD_NUM(node_id))

/* Iterate groups and subgroups */

#define FOREACH_PIN_SUBGROUP(n, fn, sep, ...)                                                      \
	DT_FOREACH_PROP_ELEM_SEP_VARGS(n, pinmux, fn, sep, __VA_ARGS__)
#define FOREACH_PIN_GROUP(n, sep, fn, ...)                                                         \
	DT_FOREACH_CHILD_SEP_VARGS(n, FOREACH_PIN_SUBGROUP, sep, fn, sep, __VA_ARGS__)

/* Encode the pins in a group */

#define IS_LAST_PIN(end, idx, off) (((idx) + (off) + 1) == (end))
#define PIN_TERMINATE(n, p, i, off, end)                                                           \
	(IS_LAST_PIN(end, i, off) ? ENCODE_PIN(n, i, (off) + 1) : 0)
#define PIN_ENTRY(n, p, i, off) (DT_PROP_HAS_IDX(n, p, i) ? ENCODE_PIN(n, i, off) : 0)
#define ENCODE_EACH_PIN(n, p, i, end)                                                              \
	PIN_ENTRY(n, p, i, PIN_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n))) |                 \
		PIN_TERMINATE(n, p, i, PIN_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n)), end)
#define ENCODE_GROUP_PINS(n) (FOREACH_PIN_GROUP(n, (|), ENCODE_EACH_PIN, PIN_GROUP_TOTAL_PINS(n)))

/* Get group-wide pin functions */

#define EACH_PIN_FUNC(n, p, i, _) PIN_FUNC(n, i)
#define GROUP_PIN_FUNC(n)         (FOREACH_PIN_GROUP(n, (|), EACH_PIN_FUNC))
#define GROUP_PIN_HEADER(n)       (BI_PINS_ENCODING_MULTI | (GROUP_PIN_FUNC(n) << 3))

/* Check if pin functions are all equal within a group */

#define EACH_PIN_FUNC_IS(n, p, i, func) (PIN_FUNC(n, i) == func)
#define ALL_PINS_FUNC_IS(n, pinfunc)    (FOREACH_PIN_GROUP(n, (&&), EACH_PIN_FUNC_IS, pinfunc))

#define PIN_GROUP_(n)                                                                              \
	BUILD_ASSERT(PIN_GROUP_TOTAL_PINS(n) > 0, "Group must contain at least one pin");          \
	BUILD_ASSERT(PIN_GROUP_TOTAL_PINS(n) <= MAX_PIN_ENTRY, "Too many pin in group");           \
	BUILD_ASSERT(ALL_PINS_FUNC_IS(n, GROUP_PIN_FUNC(n)), "Group pins have identical func");    \
	bi_decl(BI_ENCODE_PINS_WITH_FUNC(GROUP_PIN_HEADER(n) | ENCODE_GROUP_PINS(n)))

#define PIN_GROUP_SELECT(child, idx)                                                               \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (PIN_GROUP_(child)), ())

#define PIN_GROUP(n, idx) DT_FOREACH_CHILD_VARGS(n, PIN_GROUP_SELECT, idx)

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
		  (uint32_t)"zephyr-" STRINGIFY(BUILD_VERSION)));
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
bi_decl(bi_program_build_attribute((uint32_t)"Debug"));
#else
bi_decl(bi_program_build_attribute((uint32_t)"Release"));
#endif
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PINS_WITH_FUNC_ENABLE
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 0
PIN_GROUP(DT_NODELABEL(pinctrl), 0);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 1
PIN_GROUP(DT_NODELABEL(pinctrl), 1);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 2
PIN_GROUP(DT_NODELABEL(pinctrl), 2);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 3
PIN_GROUP(DT_NODELABEL(pinctrl), 3);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 4
PIN_GROUP(DT_NODELABEL(pinctrl), 4);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 5
PIN_GROUP(DT_NODELABEL(pinctrl), 5);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 6
PIN_GROUP(DT_NODELABEL(pinctrl), 6);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 7
PIN_GROUP(DT_NODELABEL(pinctrl), 7);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 8
PIN_GROUP(DT_NODELABEL(pinctrl), 8);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 9
PIN_GROUP(DT_NODELABEL(pinctrl), 9);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 10
PIN_GROUP(DT_NODELABEL(pinctrl), 10);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 11
PIN_GROUP(DT_NODELABEL(pinctrl), 11);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 12
PIN_GROUP(DT_NODELABEL(pinctrl), 12);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 13
PIN_GROUP(DT_NODELABEL(pinctrl), 13);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 14
PIN_GROUP(DT_NODELABEL(pinctrl), 14);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 15
PIN_GROUP(DT_NODELABEL(pinctrl), 15);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 16
PIN_GROUP(DT_NODELABEL(pinctrl), 16);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 17
PIN_GROUP(DT_NODELABEL(pinctrl), 17);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 18
PIN_GROUP(DT_NODELABEL(pinctrl), 18);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 19
PIN_GROUP(DT_NODELABEL(pinctrl), 19);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 20
PIN_GROUP(DT_NODELABEL(pinctrl), 20);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 21
PIN_GROUP(DT_NODELABEL(pinctrl), 21);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 22
PIN_GROUP(DT_NODELABEL(pinctrl), 22);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 23
PIN_GROUP(DT_NODELABEL(pinctrl), 23);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 24
PIN_GROUP(DT_NODELABEL(pinctrl), 24);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 25
PIN_GROUP(DT_NODELABEL(pinctrl), 25);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 26
PIN_GROUP(DT_NODELABEL(pinctrl), 26);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 27
PIN_GROUP(DT_NODELABEL(pinctrl), 27);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 28
PIN_GROUP(DT_NODELABEL(pinctrl), 28);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 29
PIN_GROUP(DT_NODELABEL(pinctrl), 29);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 30
PIN_GROUP(DT_NODELABEL(pinctrl), 30);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 31
PIN_GROUP(DT_NODELABEL(pinctrl), 31);
#endif
#endif
