/*
 * Copyright (c) 2024 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <zephyr/kernel.h>
#include <zephyr/devicetree.h>

#include <zephyr/dt-bindings/pinctrl/rpi-pico-pinctrl-common.h>

#include <soc.h>

#include <boot_stage2/config.h>
#include <pico/binary_info.h>

#include <version.h>
#ifdef HAS_APP_VERSION
#include <app_version.h>
#endif

#include <stdio.h>
#include <stdint.h>
#include <pico/binary_info.h>

extern uint32_t __binary_info_start;
extern uint32_t __binary_info_end;

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wint-conversion"
bi_decl(bi_program_feature_group(0x1111, 0, (uint32_t)"LED Configuration"));
bi_decl(bi_ptr_int32(0x1111, 0, LED_PIN, 25));
bi_decl(bi_ptr_string(0x1234, 0, hoge, "hoge", 10));
bi_decl(bi_block_device(0x2222, (uint32_t)"blk", 0x3210, 0xFFFF, 0, 0))
#pragma GCC diagnostic pop

int main()
{
	void **core_ptr = (void *)&__binary_info_start;

	printk("hoge");

	for(; core_ptr < (void **)&__binary_info_end; core_ptr++) {
		binary_info_core_t *core = *core_ptr;

		printk("type:%2u, tag:0x%4x ", core->type, core->tag);

		switch (core->type) {
		case BINARY_INFO_TYPE_RAW_DATA:
		case BINARY_INFO_TYPE_SIZED_DATA:
		case BINARY_INFO_TYPE_BINARY_INFO_LIST_ZERO_TERMINATED:
		case BINARY_INFO_TYPE_BSON:
			printk("Not suppported type: %d", core->type);
			break;
		case BINARY_INFO_TYPE_ID_AND_INT: {
			binary_info_id_and_int_t *id_and_int = *core_ptr;
			printk("ID:0x%08x 0x%x", id_and_int->id, id_and_int->value);
			break;
		}
		case BINARY_INFO_TYPE_ID_AND_STRING: {
			binary_info_id_and_string_t *id_and_string = *core_ptr;
			printk("ID:0x%08x %s", id_and_string->id, (uint8_t*)id_and_string->value);
			break;
		}
		case BINARY_INFO_TYPE_BLOCK_DEVICE: {
			binary_info_block_device_t *block_device = *core_ptr;
			printk("NAME: %s, ADDR:0x%08x, SIZE:%u, FLAGS:0x%04x",
			       (uint8_t*)block_device->name, block_device->address, block_device->size,
			       block_device->flags);
			break;
		}
		case BINARY_INFO_TYPE_PINS_WITH_FUNC: {
			binary_info_pins_with_func_t *pins_w_func = *core_ptr;
			printk("pin_w_func: 0x%08x", pins_w_func->pin_encoding);
			break;
		}
		case BINARY_INFO_TYPE_PINS_WITH_NAME: {
			binary_info_pins_with_name_t *pins_w_name = *core_ptr;
			printk("pin_w_name: 0x%08x label: %s", pins_w_name->pin_mask,
			       (uint8_t*)pins_w_name->label);
			break;
		}
		case BINARY_INFO_TYPE_NAMED_GROUP: {
			binary_info_named_group_t *named_group = *core_ptr;
			printk("ID:0x%08x PARENT:0x%08x TAG:0x%04x FLAGS:0x%04x LABEL:%s",
			       named_group->group_id, named_group->parent_id,
			       named_group->group_tag, named_group->flags, (uint8_t*)named_group->label);
			break;
		}
		case BINARY_INFO_TYPE_PTR_INT32_WITH_NAME: {
			binary_info_ptr_int32_with_name_t *ptr_int32_w_name = *core_ptr;
			printk("ID:0x%08x LABEL:%s 0x%08x", ptr_int32_w_name->id,
			       (uint8_t*)ptr_int32_w_name->label, *(uint32_t*)ptr_int32_w_name->value);
			break;
		}
		case BINARY_INFO_TYPE_PTR_STRING_WITH_NAME: {
			binary_info_ptr_string_with_name_t *ptr_str_w_name = *core_ptr;
			printk("ID:0x%08x LABEL:%s %s", ptr_str_w_name->id,
			       (uint8_t*)ptr_str_w_name->label, (uint8_t*)ptr_str_w_name->value);
			break;
		}
		case BINARY_INFO_TYPE_PINS64_WITH_FUNC: {
			binary_info_pins64_with_func_t *pins64_w_func = *core_ptr;
			printk("pin_w_func: 0x%016llx", pins64_w_func->pin_encoding);
			break;
		}
		case BINARY_INFO_TYPE_PINS64_WITH_NAME: {
			binary_info_pins64_with_name_t *pins64_w_name = *core_ptr;
			printk("pin_w_name: 0x%016llx, label:%s", pins64_w_name->pin_mask,
			       (uint8_t*)pins64_w_name->label);
			break;
		}
		default:
			printk("Unknown type\n");
			return -1;
		}
		printk("\n");
	}
}
