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
#define PIN_GROUP_AMOUNT_BY_IDX(node_id, idx)                                                      \
	(DT_FOREACH_CHILD_STATUS_OKAY_SEP_VARGS(node_id, PIN_GROUP_AMOUNT_IF_MATCH_IDX, (+), idx))

/* Iterate groups */

#define FOREACH_PIN_GROUP_ENTRY(n, fn, sep, ...)                                                   \
	DT_FOREACH_PROP_ELEM_SEP_VARGS(n, pinmux, fn, sep, __VA_ARGS__)

/* Encode the pins in a group */

#define IS_LAST_PIN(i, end, off)         (((i) + (off) + 1) == (end))
#define ENCODE_TERMINATE(n, i, end, off) (IS_LAST_PIN(i, end, off) ? ENCODE_PIN(n, i, off + 1) : 0)
#define ENCODE_EACH_PIN_(n, p, i, end)                                                             \
	(DT_PROP_HAS_IDX(n, p, i) ? ENCODE_PIN(n, i, 0) : 0) | ENCODE_TERMINATE(n, i, end, 0)
#define ENCODE_EACH_PIN(n, p, i, end) ENCODE_EACH_PIN_(n, p, i, end)
#define ENCODE_GROUP_PINS(n)          (FOREACH_PIN_GROUP_ENTRY(n, ENCODE_EACH_PIN, (|), PIN_GROUP_AMOUNT(n)))

/* Get group-wide pin functions */

#define PIN_FUNC_OFFSET     3
#define PIN_FUNC_(n, p, i)  PIN_FUNC(n, i)
#define PIN_GROUP_FUNC(n)   DT_FOREACH_PROP_ELEM(n, pinmux, PIN_FUNC_)
#define PIN_GROUP_HEADER(n) (BI_PINS_ENCODING_MULTI | (PIN_GROUP_FUNC(n) << PIN_FUNC_OFFSET))

/* Check if pin functions are all equal within a group */

#define PIN_FUNC_IS(n, p, i, func) (PIN_FUNC(n, i) == func)

#define DECLARE_PINCFG(n) bi_decl(ENCODE_PINS_WITH_FUNC(PIN_GROUP_HEADER(n) | ENCODE_GROUP_PINS(n)))

#define DECLARE_PINCFG_IF_MATCH_IDX(child, idx)                                                    \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (DECLARE_PINCFG(child)), ())

#define BINARY_INFO_FROM_PINCFG(node_id, idx)                                                      \
	DT_FOREACH_CHILD_VARGS(node_id, DECLARE_PINCFG_IF_MATCH_IDX, idx)

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_NAME
#define BI_PROGRAM_NAME CONFIG_RPI_PICO_BINARY_INFO_OVERRIDE_PROGRAM_NAME
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_DESCRIPTION
#define BI_PROGRAM_DESCRIPTION CONFIG_RPI_PICO_BINARY_INFO_OVERRIDE_PROGRAM_DESCRIPTION
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_URL
#define BI_PROGRAM_URL CONFIG_RPI_PICO_BINARY_INFO_OVERRIDE_PROGRAM_URL
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_BUILD_DATE
#define BI_PROGRAM_BUILD_DATE CONFIG_RPI_PICO_BINARY_INFO_OVERRIDE_PROGRAM_BUILD_DATE
#else
#define BI_PROGRAM_BUILD_DATE __DATE__
#endif

extern uint32_t __rom_region_end;
bi_decl(bi_binary_end((intptr_t)&__rom_region_end));

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_NAME
bi_decl(bi_program_name((uint32_t)BI_PROGRAM_NAME));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PICO_BOARD
bi_decl(bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_PICO_BOARD,
		  (uint32_t)CONFIG_BOARD));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_SDK_VERSION_STRING
bi_decl(bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_SDK_VERSION,
		  (uint32_t)"zephyr-" STRINGIFY(BUILD_VERSION)));
#endif

#if defined(CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_VERSION_STRING) && defined(HAS_APP_VERSION)
bi_decl(bi_program_version_string((uint32_t)APP_VERSION_EXTENDED_STRING));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_DESCRIPTION
bi_decl(bi_program_description((uint32_t)BI_PROGRAM_DESCRIPTION));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_URL
bi_decl(bi_program_url((uint32_t)BI_PROGRAM_URL));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PROGRAM_BUILD_DATE
bi_decl(bi_program_build_date_string((uint32_t)BI_PROGRAM_BUILD_DATE));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_BOOT_STAGE2_NAME
bi_decl(bi_string(BINARY_INFO_TAG_RASPBERRY_PI, BINARY_INFO_ID_RP_BOOT2_NAME,
		  (uint32_t)PICO_BOOT_STAGE2_NAME));
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_ATTRIBUTE_BUILD_TYPE
#ifdef CONFIG_DEBUG
bi_decl(bi_program_build_attribute((uint32_t)"Debug"));
#else
bi_decl(bi_program_build_attribute((uint32_t)"Release"));
#endif
#endif

#ifdef CONFIG_RPI_PICO_BINARY_INFO_PINS_WITH_FUNC
#if DT_NODE_EXISTS(DT_NODELABEL(pinctrl))
/*
 * Extract pin info from pinctrl
 *
 * The Raspberry Pi Pico SDK ``bi_decl`` macro derives unique symbol names from
 * ``__LINE__``.  Keep each instantiation on a dedicated source line rather
 * than using DT_FOREACH_CHILD, which would expand every group on the same line
 * and violate the uniqueness requirement.
 */

#define CHILD_NUM_OF_GROUP(node_id, group_idx)                                                     \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(node_id), group_idx), (DT_CHILD_NUM(node_id)), ())

#define CHILD_NUM_OF_PINCFG_GROUP_IF_MATCH_IDX(node_id, pincfg_idx, group_idx)                     \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(node_id), pincfg_idx), \
			(CHILD_NUM_OF_GROUP(node_id, group_idx)), ())

#define CHILD_NUM_OF_PINCFG_GROUP(node_id, pincfg_idx, group_idx)                                  \
	DT_FOREACH_CHILD_VARGS(node_id, CHILD_NUM_OF_PINCFG_GROUP_IF_MATCH_IDX, pincfg_idx,        \
			       group_idx)

#define BINARY_INFO_FROM_GROUP_IDX(node_id, group_idx)                                             \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(node_id), group_idx), (DECLARE_PINCFG(node_id)), ())

#define BINARY_INFO_FROM_GROUP_IF_MATCH_GROUP_IDX(node_id, group_idx)                              \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(node_id), group_idx), \
			(BINARY_INFO_FROM_GROUP_IDX(node_id, group_idx)), ())

#define BINARY_INFO_FROM_GROUP_IF_MATCH_IDX(node_id, pincfg_idx, group_idx)                        \
	COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(node_id), pincfg_idx), \
		(DT_FOREACH_CHILD_SEP_VARGS(node_id, BINARY_INFO_FROM_GROUP_IF_MATCH_GROUP_IDX, (), group_idx)), \
		())

#define BINARY_INFO_FROM_GROUP(node_id, pincfg_idx, group_idx)                                     \
	DT_FOREACH_CHILD_VARGS(node_id, BINARY_INFO_FROM_GROUP_IF_MATCH_IDX, pincfg_idx, group_idx)

int hoge;

BINARY_INFO_FROM_GROUP(DT_NODELABEL(pinctrl), 0, 0);

#endif
#endif
