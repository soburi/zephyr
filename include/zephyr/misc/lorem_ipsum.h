/*
 * Copyright (c) 2015 Intel Corporation.
 * Copyright (c) 2024 Tenstorrent AI ULC
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_MISC_LOREM_IPSUM_H_
#define ZEPHYR_INCLUDE_MISC_LOREM_IPSUM_H_

#include <zephyr/toolchain.h>

/*
 * N.B. These strings are generally only used for tests and samples. They are not part of any
 * official Zephyr API, but were moved to reduce duplication.
 */

/* Generated by http://www.lipsum.com/
 * 1 paragraph, 69 words, 445 bytes of Lorem Ipsum
 */
#define LOREM_IPSUM_SHORT                                                                          \
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit, "                                \
	"sed do eiusmod tempor incididunt ut labore et dolore magna "                              \
	"aliqua. Ut enim ad minim veniam, quis nostrud exercitation "                              \
	"ullamco laboris nisi ut aliquip ex ea commodo consequat. Duis "                           \
	"aute irure dolor in reprehenderit in voluptate velit esse "                               \
	"cillum dolore eu fugiat nulla pariatur. Excepteur sint "                                  \
	"occaecat cupidatat non proident, sunt in culpa qui officia "                              \
	"deserunt mollit anim id est laborum."

#define LOREM_IPSUM_SHORT_STRLEN 445
BUILD_ASSERT(sizeof(LOREM_IPSUM_SHORT) == LOREM_IPSUM_SHORT_STRLEN + 1);

/* Generated by http://www.lipsum.com/
 * 2 paragraphs, 173 words, 1160 bytes of Lorem Ipsum
 */
#define LOREM_IPSUM                                                                                \
	"Lorem ipsum dolor sit amet, consectetur adipiscing elit. Vivamus a varius sapien. "       \
	"Suspendisse interdum nulla et enim elementum faucibus. Vestibulum nec est libero. Duis "  \
	"leo orci, tincidunt a interdum ut, porttitor ac arcu. Etiam porttitor pretium nibh, non " \
	"laoreet nisl vulputate vitae. Vestibulum sit amet odio sit amet nibh faucibus mattis "    \
	"eget at ex. Nam non arcu vitae nisi congue eleifend. Suspendisse sagittis, leo at "       \
	"blandit semper, arcu neque tristique dolor, eu consequat urna quam non tortor. "          \
	"Suspendisse ut ullamcorper lectus. Nullam ut accumsan lacus, sed iaculis leo. "           \
	"Suspendisse potenti. Duis ullamcorper velit tellus, ac dictum tellus ultricies quis."     \
	"\n"                                                                                       \
	"Curabitur tellus eros, congue a sem et, mattis fermentum velit. Donec sollicitudin "      \
	"faucibus enim eu vehicula. Aliquam pulvinar lectus et finibus laoreet. Praesent at "      \
	"tempor ex. Aenean blandit nunc viverra enim vulputate, et dictum turpis elementum. "      \
	"Donec porttitor in dolor a ultricies. Cras neque ipsum, blandit sed varius at, semper "   \
	"quis justo. Meanness ac metus ex. Pellentesque eu tortor eget nisl tempor pretium. "      \
	"Donec elit enim, ultrices sit amet est vitae, sollicitudin bibendum egestas."

#define LOREM_IPSUM_STRLEN 1160
BUILD_ASSERT(sizeof(LOREM_IPSUM) == LOREM_IPSUM_STRLEN + 1);

#endif /* ZEPHYR_INCLUDE_MISC_LOREM_IPSUM_H_ */
