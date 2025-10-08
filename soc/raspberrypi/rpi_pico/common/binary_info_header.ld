/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

PROVIDE(BINARY_INFO_MARKER_START = 0x7188ebf2);
PROVIDE(BINARY_INFO_MARKER_END = 0xe71aa390);

LONG(BINARY_INFO_MARKER_START);
LONG(__binary_info_start);
LONG(__binary_info_end);
LONG(data_cpy_table);
LONG(BINARY_INFO_MARKER_END);

PROVIDE(__binary_info_data_cpy_table_default = .);
PROVIDE(data_cpy_table = __binary_info_data_cpy_table_default);

. = __binary_info_data_cpy_table_default;
LONG(0);
