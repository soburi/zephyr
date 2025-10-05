/*
 * Copyright (c) 2025 TOKITA Hiroshi <tokita.hiroshi@gmail.com>
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
#define MAX_PIN_ENTRIES       4
#define ENCODE_BASE_IDX       1
#define ENCODE_ENTRY_WIDTH    5
#define ENCODE_PADDING        2
#define ENCODE_PINS_WITH_FUNC __bi_encoded_pins_with_func
#else
#define MAX_PIN_ENTRIES       6
#define ENCODE_BASE_IDX       1
#define ENCODE_ENTRY_WIDTH    8
#define ENCODE_PADDING        0
#define ENCODE_PINS_WITH_FUNC __bi_encoded_pins_64_with_func
#endif

#define PIN_FUNC(n, idx) (DT_PROP_BY_IDX(n, pinmux, idx) & RP2_ALT_FUNC_MASK)
#define PIN_NUM(n, idx)  ((DT_PROP_BY_IDX(n, pinmux, idx) >> RP2_PIN_NUM_POS) & RP2_PIN_NUM_MASK)

#define ENCODE_OFFSET(idx)      (((idx + ENCODE_BASE_IDX) * ENCODE_ENTRY_WIDTH) + ENCODE_PADDING)
#define ENCODE_PIN(n, idx, off) ((uint64_t)PIN_NUM(n, idx) << ENCODE_OFFSET(idx + off))

/* Pin amounts and offets for groups */

#define PIN_GROUP_AMOUNT(node_id)                                                                  \
	COND_CODE_1(DT_NODE_HAS_PROP(node_id, pinmux), (DT_PROP_LEN(node_id, pinmux)), (0))
#define PIN_GROUP_AMOUNT_IF_MATCH_IDX(child, idx)                                                  \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (PIN_GROUP_AMOUNT(child)), (0))
/*
 * The raspberrypi,pico-pinctrl child binding does not permit a "status"
 * property, so the STATUS_OK iterator still visits every group.
 * Use this to avoid nested calls to the same macro.
 */
#define PIN_GROUP_AMOUNT_BY_IDX(node_id, idx)                                                      \
	(DT_FOREACH_CHILD_STATUS_OKAY_SEP_VARGS(node_id, PIN_GROUP_AMOUNT_IF_MATCH_IDX, (+), idx))

#define PIN_GROUP_OFFSET_TERM(i, node_id) +PIN_GROUP_AMOUNT_BY_IDX(node_id, i)
#define PIN_GROUP_OFFSET(node_id, count)  (0 LISTIFY(count, PIN_GROUP_OFFSET_TERM, (), node_id))
#define PIN_AMOUNT(node_id)               PIN_GROUP_OFFSET(node_id, DT_CHILD_NUM(node_id))

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
#define ENCODE_EACH_PIN(n, p, i, end)                                                              \
	PIN_ENTRY(n, p, i, PIN_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n))) |                 \
		PIN_TERMINATE(n, p, i, PIN_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n)), end)

/* Get group-wide pin functions */

#define PIN_FUNC_OFFSET       3
#define PIN_FUNC_(n, p, i, _) PIN_FUNC(n, i)
#define PIN_GROUP_FUNC(n)     (FOREACH_PIN_GROUP(n, (|), PIN_FUNC_))
#define PIN_GROUP_HEADER(n)   (BI_PINS_ENCODING_MULTI | (PIN_GROUP_FUNC(n) << PIN_FUNC_OFFSET))
#define ENCODE_GROUP_PINS(n)  (FOREACH_PIN_GROUP(n, (|), ENCODE_EACH_PIN, PIN_GROUP_AMOUNT(n)))

/* Check if pin functions are all equal within a group */

#define PIN_FUNC_IS(n, p, i, func)   (PIN_FUNC(n, i) == func)
#define ALL_PINS_FUNC_IS(n, pinfunc) (FOREACH_PIN_GROUP(n, (&&), PIN_FUNC_IS, pinfunc))

#define DECLARE_PIN_GROUP(n)                                                                       \
	BUILD_ASSERT(PIN_AMOUNT(n) > 0, "Group must contain at least one pin");                    \
	BUILD_ASSERT(PIN_AMOUNT(n) <= MAX_PIN_ENTRIES, "Too many pins in group");                  \
	BUILD_ASSERT(ALL_PINS_FUNC_IS(n, PIN_GROUP_FUNC(n)),                                       \
		     "Group pins must share identical function");                                  \
	bi_decl(ENCODE_PINS_WITH_FUNC(PIN_GROUP_HEADER(n) | ENCODE_GROUP_PINS(n)))

#define DECLARE_PIN_GROUP_IF_MATCH_IDX(child, idx)                                                 \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (DECLARE_PIN_GROUP(child)), ())

#define BINARY_INFO_FROM_PINCFG(node_id, idx)                                                      \
	DT_FOREACH_CHILD_VARGS(node_id, DECLARE_PIN_GROUP_IF_MATCH_IDX, idx)

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
 * Extract pin info from pinctrl
 *
 * The Raspberry Pi Pico SDK ``bi_decl`` macro derives unique symbol names from
 * ``__LINE__``.  Keep each instantiation on a dedicated source line rather
 * than using DT_FOREACH_CHILD, which would expand every group on the same line
 * and violate the uniqueness requirement.
 */
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 0
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 0);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 1
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 1);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 2
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 2);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 3
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 3);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 4
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 4);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 5
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 5);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 6
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 6);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 7
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 7);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 8
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 8);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 9
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 9);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 10
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 10);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 11
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 11);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 12
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 12);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 13
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 13);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 14
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 14);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 15
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 15);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 16
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 16);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 17
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 17);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 18
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 18);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 19
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 19);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 20
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 20);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 21
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 21);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 22
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 22);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 23
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 23);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 24
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 24);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 25
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 25);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 26
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 26);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 27
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 27);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 28
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 28);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 29
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 29);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 30
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 30);
#endif
#if DT_CHILD_NUM(DT_NODELABEL(pinctrl)) > 31
BINARY_INFO_FROM_PINCFG(DT_NODELABEL(pinctrl), 31);
#endif
#endif /* DT_NODE_EXISTS(DT_NODELABEL(pinctrl)) */
#endif /* CONFIG_RPI_PICO_BINARY_INFO_PINS_WITH_FUNC_ENABLE */
