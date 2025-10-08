/*
 * Copyright (c) 2023 TOKITA Hiroshi <tokita.hiroshi@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

PROVIDE(binary_info_header = .);
PROVIDE(__binary_info_data_cpy_table_default = . + (5 * 4));
PROVIDE(data_cpy_table = __binary_info_data_cpy_table_default);

LONG(0x7188ebf2); /* BINARY_INFO_MARKER_START */
LONG(__binary_info_start);
LONG(__binary_info_end);
LONG(data_cpy_table);
LONG(0xe71aa390); /* BINARY_INFO_MARKER_END */

. = __binary_info_data_cpy_table_default;
LONG(0); /* sentinel */
