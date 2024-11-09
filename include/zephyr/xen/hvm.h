/*
 * Copyright (c) 2021 EPAM Systems
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_XEN_HVM_H_
#define ZEPHYR_INCLUDE_XEN_HVM_H_

#include <zephyr/xen/public/hvm/hvm_op.h>
#include <zephyr/xen/public/hvm/params.h>

#include <zephyr/kernel.h>

int hvm_set_parameter(int idx, int domid, uint64_t value);
int hvm_get_parameter(int idx, int domid, uint64_t *value);

#endif /* ZEPHYR_INCLUDE_XEN_HVM_H_ */
