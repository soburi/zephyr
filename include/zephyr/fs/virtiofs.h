/*
 * Copyright (c) 2025 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_FS_VIRTIOFS_H_
#define ZEPHYR_INCLUDE_FS_VIRTIOFS_H_
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Runtime metadata gathered from the virtio-fs handshake.
 *
 * Virtio 1.3, Chapter 5 "Device Types", §"File System Device"
 * requires the driver to initiate a FUSE session and honour the
 * limits advertised during @c FUSE_INIT. This structure keeps track
 * of those negotiated parameters for a mounted instance.
 */
struct virtiofs_fs_data {
        /**
         * Maximum payload size accepted by FUSE_WRITE as reported in the
         * FUSE_INIT reply, see Virtio 1.3, "File System Device" — Device
         * Operation.
         */
        uint32_t max_write;
};

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_FS_VIRTIOFS_H_ */
