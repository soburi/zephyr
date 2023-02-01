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
#define ENCODE_PIN(n, idx, off) ((uint32_t)PIN_NUMBER(n, idx) << (2 + (idx + off + 1) * 5))
#define MAX_PIN_ENTRIES         4
#define ENCODE_PINS_WITH_FUNC   __bi_encoded_pins_with_func
#else
#define ENCODE_PIN(n, idx, off) ((uint64_t)PIN_NUMBER(n, idx) << ((idx + off + 1) * 8))
#define MAX_PIN_ENTRIES         6
#define ENCODE_PINS_WITH_FUNC   __bi_encoded_pins_64_with_func
#endif

#define PIN_NUMBER(n, idx)   ((DT_PROP_BY_IDX(n, pinmux, idx) >> RP2_PIN_NUM_POS) & RP2_PIN_NUM_MASK)
#define PIN_FUNCTION(n, idx) (DT_PROP_BY_IDX(n, pinmux, idx) & RP2_ALT_FUNC_MASK)

/* Pin counts and offs for groups */

#define PIN_GROUP_COUNT(node_id)                                                                   \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, pinmux), (DT_PROP_LEN(node_id, pinmux)), (0))

#define PIN_GROUP_COUNT_MATCH(child, idx)                                                          \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (PIN_GROUP_COUNT(child)), (0))
#define PIN_GROUP_COUNT_BY_INDEX(node_id, idx)                                                     \
	/*                                                                                         \
	 * The raspberrypi,pico-pinctrl child binding does not permit a "status"                   \
	 * property, so the STATUS_OK iterator still visits every group.                           \
	 */                                                                                        \
	(DT_FOREACH_CHILD_STATUS_OKAY_SEP_VARGS(node_id, PIN_GROUP_COUNT_MATCH, (+), idx))

#define PIN_GROUP_OFFSET_TERM(i, node_id) +PIN_GROUP_COUNT_BY_INDEX(node_id, i)
#define PIN_GROUP_OFFSET(node_id, count)  (0 LISTIFY(count, PIN_GROUP_OFFSET_TERM, (), node_id))
#define PIN_GROUP_AMOUNT(node_id)         PIN_GROUP_OFFSET(node_id, DT_CHILD_NUM(node_id))

/* Iterate groups and subgroups */

#define FOREACH_PIN_GROUP_ENTRY(n, fn, sep, ...)                                                   \
	DT_FOREACH_PROP_ELEM_SEP_VARGS(n, pinmux, fn, sep, __VA_ARGS__)
#define FOREACH_PIN_GROUP(n, sep, fn, ...)                                                         \
	DT_FOREACH_CHILD_SEP_VARGS(n, FOREACH_PIN_GROUP_ENTRY, sep, fn, sep, __VA_ARGS__)

/* Encode the pins in a group */

#define IS_LAST_PIN(end, idx, off) (((idx) + (off) + 1) == (end))
#define PIN_TERMINATE(n, p, i, off, end)                                                           \
	(IS_LAST_PIN(end, i, off) ? ENCODE_PIN(n, i, (off) + 1) : 0)
#define PIN_ENTRY(n, p, i, off) (DT_PROP_HAS_IDX(n, p, i) ? ENCODE_PIN(n, i, off) : 0)
#define PIN_ENCODE_PIN(n, p, i, end)                                                               \
	PIN_ENTRY(n, p, i, PIN_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n))) |                 \
		PIN_TERMINATE(n, p, i, PIN_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n)), end)
#define ENCODE_GROUP_PINS(n) (FOREACH_PIN_GROUP(n, (|), PIN_ENCODE_PIN, PIN_GROUP_AMOUNT(n)))

/* Get group-wide pin functions */

#define PIN_FUNC(n, p, i, _)  PIN_FUNCTION(n, i)
#define PIN_GROUP_FUNC(n)     (FOREACH_PIN_GROUP(n, (|), PIN_FUNC))
#define PIN_GROUP_FUNC_OFFSET 3
#define PIN_GROUP_HEADER(n)   (BI_PINS_ENCODING_MULTI | (PIN_GROUP_FUNC(n) << PIN_GROUP_FUNC_OFFSET))

/* Check if pin functions are all equal within a group */

#define PIN_FUNC_IS(n, p, i, func)   (PIN_FUNCTION(n, i) == func)
#define ALL_PINS_FUNC_IS(n, pinfunc) (FOREACH_PIN_GROUP(n, (&&), PIN_FUNC_IS, pinfunc))

#define DECLARE_PIN_GROUP(n)                                                                       \
	BUILD_ASSERT(PIN_GROUP_AMOUNT(n) > 0, "Group must contain at least one pin");              \
	BUILD_ASSERT(PIN_GROUP_AMOUNT(n) <= MAX_PIN_ENTRIES, "Too many pins in group");            \
	BUILD_ASSERT(ALL_PINS_FUNC_IS(n, PIN_GROUP_FUNC(n)),                                       \
		     "Group pins must share identical function");                                  \
	bi_decl(ENCODE_PINS_WITH_FUNC(PIN_GROUP_HEADER(n) | ENCODE_GROUP_PINS(n)))

#define PIN_GROUP_MATCH(child, idx)                                                                \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (DECLARE_PIN_GROUP(child)), ())

#define PIN_DECLARE_GROUP_BY_INDEX(node_id, idx)                                                   \
	DT_FOREACH_CHILD_VARGS(node_id, PIN_GROUP_MATCH, idx)

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
#if DT_NODE_EXISTS(DT_NODELABEL(pinctrl))
/*
 * The Raspberry Pi Pico SDK ``bi_decl`` macro derives unique symbol names from
 * ``__LINE__``.  Keep each instantiation on a dedicated source line rather
 * than using DT_FOREACH_CHILD, which would expand every group on the same line
 * and violate the uniqueness requirement.
 */
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 0
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 0);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 1
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 1);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 2
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 2);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 3
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 3);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 4
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 4);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 5
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 5);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 6
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 6);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 7
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 7);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 8
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 8);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 9
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 9);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 10
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 10);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 11
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 11);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 12
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 12);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 13
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 13);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 14
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 14);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 15
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 15);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 16
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 16);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 17
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 17);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 18
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 18);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 19
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 19);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 20
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 20);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 21
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 21);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 22
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 22);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 23
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 23);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 24
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 24);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 25
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 25);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 26
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 26);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 27
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 27);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 28
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 28);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 29
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 29);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 30
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 30);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 31
PIN_DECLARE_GROUP_BY_INDEX(DT_NODELABEL(pinctrl), 31);
#endif
#endif /* DT_NODE_EXISTS(DT_NODELABEL(pinctrl)) */
#endif /* CONFIG_RPI_PICO_BINARY_INFO_PINS_WITH_FUNC_ENABLE */
