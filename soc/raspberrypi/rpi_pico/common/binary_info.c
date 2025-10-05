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
#define RP2_BI_ENCODE_PIN(n, idx, off)  ((uint32_t)RP2_BI_PIN_NUMBER(n, idx) << (2 + (idx + off + 1) * 5))
#define RP2_BI_MAX_PIN_ENTRIES          4
#define RP2_BI_ENCODE_PINS_WITH_FUNC    __bi_encoded_pins_with_func
#else
#define RP2_BI_ENCODE_PIN(n, idx, off)  ((uint64_t)RP2_BI_PIN_NUMBER(n, idx) << ((idx + off + 1) * 8))
#define RP2_BI_MAX_PIN_ENTRIES          6
#define RP2_BI_ENCODE_PINS_WITH_FUNC    __bi_encoded_pins_64_with_func
#endif

#define RP2_BI_PIN_NUMBER(n, idx) ((DT_PROP_BY_IDX(n, pinmux, idx) >> RP2_PIN_NUM_POS) & RP2_PIN_NUM_MASK)
#define RP2_BI_PIN_FUNCTION(n, idx) (DT_PROP_BY_IDX(n, pinmux, idx) & RP2_ALT_FUNC_MASK)

/* Pin counts and offs for groups */

#define RP2_BI_PINCTRL_GROUP_PIN_COUNT(node_id)                                                    \
        COND_CODE_1(DT_NODE_HAS_PROP(node_id, pinmux), (DT_PROP_LEN(node_id, pinmux)), (0))

#define RP2_BI_PINCTRL_GROUP_PIN_COUNT_MATCH(child, idx)                                           \
        COND_CODE_1(IS_EQ(DT_NODE_CHILD_IDX(child), idx), (RP2_BI_PINCTRL_GROUP_PIN_COUNT(child)), (0))
#define RP2_BI_PINCTRL_GROUP_PIN_COUNT_BY_INDEX(node_id, idx)                                      \
        /*                                                                                         \
         * The raspberrypi,pico-pinctrl child binding does not permit a "status"                   \
         * property, so the STATUS_OK iterator still visits every group.                           \
         */                                                                                        \
        (DT_FOREACH_CHILD_STATUS_OKAY_SEP_VARGS(node_id, RP2_BI_PINCTRL_GROUP_PIN_COUNT_MATCH, (+), idx))

#define RP2_BI_PINCTRL_GROUP_OFFSET_TERM(i, node_id) +RP2_BI_PINCTRL_GROUP_PIN_COUNT_BY_INDEX(node_id, i)
#define RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, count)                                            \
        (0 LISTIFY(count, RP2_BI_PINCTRL_GROUP_OFFSET_TERM, (), node_id))
#define RP2_BI_PINCTRL_GROUP_OFFSET_0(node_id)   (0)
#define RP2_BI_PINCTRL_GROUP_OFFSET_1(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 1)
#define RP2_BI_PINCTRL_GROUP_OFFSET_2(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 2)
#define RP2_BI_PINCTRL_GROUP_OFFSET_3(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 3)
#define RP2_BI_PINCTRL_GROUP_OFFSET_4(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 4)
#define RP2_BI_PINCTRL_GROUP_OFFSET_5(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 5)
#define RP2_BI_PINCTRL_GROUP_OFFSET_6(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 6)
#define RP2_BI_PINCTRL_GROUP_OFFSET_7(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 7)
#define RP2_BI_PINCTRL_GROUP_OFFSET_8(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 8)
#define RP2_BI_PINCTRL_GROUP_OFFSET_9(node_id)   RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 9)
#define RP2_BI_PINCTRL_GROUP_OFFSET_10(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 10)
#define RP2_BI_PINCTRL_GROUP_OFFSET_11(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 11)
#define RP2_BI_PINCTRL_GROUP_OFFSET_12(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 12)
#define RP2_BI_PINCTRL_GROUP_OFFSET_13(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 13)
#define RP2_BI_PINCTRL_GROUP_OFFSET_14(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 14)
#define RP2_BI_PINCTRL_GROUP_OFFSET_15(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 15)
#define RP2_BI_PINCTRL_GROUP_OFFSET_16(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 16)
#define RP2_BI_PINCTRL_GROUP_OFFSET_17(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 17)
#define RP2_BI_PINCTRL_GROUP_OFFSET_18(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 18)
#define RP2_BI_PINCTRL_GROUP_OFFSET_19(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 19)
#define RP2_BI_PINCTRL_GROUP_OFFSET_20(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 20)
#define RP2_BI_PINCTRL_GROUP_OFFSET_21(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 21)
#define RP2_BI_PINCTRL_GROUP_OFFSET_22(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 22)
#define RP2_BI_PINCTRL_GROUP_OFFSET_23(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 23)
#define RP2_BI_PINCTRL_GROUP_OFFSET_24(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 24)
#define RP2_BI_PINCTRL_GROUP_OFFSET_25(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 25)
#define RP2_BI_PINCTRL_GROUP_OFFSET_26(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 26)
#define RP2_BI_PINCTRL_GROUP_OFFSET_27(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 27)
#define RP2_BI_PINCTRL_GROUP_OFFSET_28(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 28)
#define RP2_BI_PINCTRL_GROUP_OFFSET_29(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 29)
#define RP2_BI_PINCTRL_GROUP_OFFSET_30(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 30)
#define RP2_BI_PINCTRL_GROUP_OFFSET_31(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 31)
#define RP2_BI_PINCTRL_GROUP_OFFSET_32(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET_SUM(node_id, 32)

#define RP2_BI_PINCTRL_GROUP_OFFSET(node_id, idx) UTIL_CAT(RP2_BI_PINCTRL_GROUP_OFFSET_, idx)(node_id)
#define RP2_BI_PINCTRL_GROUP_PIN_TOTAL(node_id)  RP2_BI_PINCTRL_GROUP_OFFSET(node_id, DT_CHILD_NUM(node_id))

/* Iterate groups and subgroups */

#define RP2_BI_PINCTRL_FOREACH_GROUP_PIN(n, fn, sep, ...)                                          \
        DT_FOREACH_PROP_ELEM_SEP_VARGS(n, pinmux, fn, sep, __VA_ARGS__)
#define RP2_BI_PINCTRL_FOREACH_GROUP(n, sep, fn, ...)                                              \
        DT_FOREACH_CHILD_SEP_VARGS(n, RP2_BI_PINCTRL_FOREACH_GROUP_PIN, sep, fn, sep, __VA_ARGS__)

/* Encode the pins in a group */

#define RP2_BI_PINCTRL_IS_LAST_PIN(end, idx, off) (((idx) + (off) + 1) == (end))
#define RP2_BI_PINCTRL_PIN_TERMINATE(n, p, i, off, end)                                            \
        (RP2_BI_PINCTRL_IS_LAST_PIN(end, i, off) ? RP2_BI_ENCODE_PIN(n, i, (off) + 1) : 0)
#define RP2_BI_PINCTRL_PIN_ENTRY(n, p, i, off)                                                     \
        (DT_PROP_HAS_IDX(n, p, i) ? RP2_BI_ENCODE_PIN(n, i, off) : 0)
#define RP2_BI_PINCTRL_ENCODE_PIN(n, p, i, end)                                                    \
        RP2_BI_PINCTRL_PIN_ENTRY(n, p, i, RP2_BI_PINCTRL_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n))) | \
                RP2_BI_PINCTRL_PIN_TERMINATE(n, p, i,                                             \
                                             RP2_BI_PINCTRL_GROUP_OFFSET(DT_PARENT(n), DT_NODE_CHILD_IDX(n)), end)
#define RP2_BI_PINCTRL_ENCODE_GROUP_PINS(n)                                                        \
        (RP2_BI_PINCTRL_FOREACH_GROUP(n, (|), RP2_BI_PINCTRL_ENCODE_PIN,                           \
                                       RP2_BI_PINCTRL_GROUP_PIN_TOTAL(n)))

/* Get group-wide pin functions */

#define RP2_BI_PINCTRL_PIN_FUNC(n, p, i, _) RP2_BI_PIN_FUNCTION(n, i)
#define RP2_BI_PINCTRL_GROUP_FUNC(n)         (RP2_BI_PINCTRL_FOREACH_GROUP(n, (|), RP2_BI_PINCTRL_PIN_FUNC))
#define RP2_BI_PINCTRL_GROUP_HEADER(n)       (BI_PINS_ENCODING_MULTI | (RP2_BI_PINCTRL_GROUP_FUNC(n) << 3))

/* Check if pin functions are all equal within a group */

#define RP2_BI_PINCTRL_PIN_FUNC_IS(n, p, i, func) (RP2_BI_PIN_FUNCTION(n, i) == func)
#define RP2_BI_PINCTRL_ALL_PINS_FUNC_IS(n, pinfunc)                                                \
        (RP2_BI_PINCTRL_FOREACH_GROUP(n, (&&), RP2_BI_PINCTRL_PIN_FUNC_IS, pinfunc))

#define RP2_BI_DECLARE_PIN_GROUP(n)                                                                \
        BUILD_ASSERT(RP2_BI_PINCTRL_GROUP_PIN_TOTAL(n) > 0, "Group must contain at least one pin"); \
        BUILD_ASSERT(RP2_BI_PINCTRL_GROUP_PIN_TOTAL(n) <= RP2_BI_MAX_PIN_ENTRIES,                   \
                     "Too many pins in group");                                                    \
        BUILD_ASSERT(RP2_BI_PINCTRL_ALL_PINS_FUNC_IS(n, RP2_BI_PINCTRL_GROUP_FUNC(n)),             \
                     "Group pins must share identical function");                                 \
        bi_decl(RP2_BI_ENCODE_PINS_WITH_FUNC(RP2_BI_PINCTRL_GROUP_HEADER(n) |                      \
                                            RP2_BI_PINCTRL_ENCODE_GROUP_PINS(n)))

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
#define RP2_BI_DECLARE_PINCTRL_GROUP(child) RP2_BI_DECLARE_PIN_GROUP(child)
DT_FOREACH_CHILD(DT_NODELABEL(pinctrl), RP2_BI_DECLARE_PINCTRL_GROUP);
#undef RP2_BI_DECLARE_PINCTRL_GROUP
#endif /* DT_NODE_EXISTS(DT_NODELABEL(pinctrl)) */
#endif /* CONFIG_RPI_PICO_BINARY_INFO_PINS_WITH_FUNC_ENABLE */
