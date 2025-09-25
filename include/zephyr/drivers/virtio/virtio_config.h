/*
 * Copyright (c) 2025 Antmicro <www.antmicro.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_VIRTIO_VIRTIO_CONFIG_H_
#define ZEPHYR_VIRTIO_VIRTIO_CONFIG_H_

/**
 * @name Virtio device status bits
 * @brief Bit positions in the @c device_status field defined in
 *        Virtio 1.3, §2.1 "Device Status Field".
 * @{ */
/** The guest has recognized the device presence (Virtio 1.3 §2.1). */
#define DEVICE_STATUS_ACKNOWLEDGE 0
/** The guest driver is ready to drive the device (Virtio 1.3 §2.1). */
#define DEVICE_STATUS_DRIVER      1
/** The driver has successfully set up the device (Virtio 1.3 §2.1). */
#define DEVICE_STATUS_DRIVER_OK   2
/**
 * The driver and device agreed on the negotiated feature set
 * (Virtio 1.3 §2.1).
 */
#define DEVICE_STATUS_FEATURES_OK 3
/** The device requests a reset to recover from an error (Virtio 1.3 §2.1). */
#define DEVICE_STATUS_NEEDS_RESET 6
/** The device has experienced a non-recoverable error (Virtio 1.3 §2.1). */
#define DEVICE_STATUS_FAILED      7
/** @} */

/**
 * @name Core feature bits shared by all Virtio devices
 * @brief Negotiable feature bit positions described in Virtio 1.3,
 *        §3 "Reserved Feature Bits".
 * @{ */
/** Virtqueue layout can deviate from the legacy format (Virtio 1.3 §3). */
#define VIRTIO_F_ANY_LAYOUT      27
/** Device complies with Virtio 1.0+ semantics (Virtio 1.3 §3). */
#define VIRTIO_F_VERSION_1       32
/** Device needs platform-specific DMA handling (Virtio 1.3 §3). */
#define VIRTIO_F_ACCESS_PLATFORM 33
/** @} */

/**
 * @name Split virtqueue feature bits
 * @brief Virtqueue extensions defined in Virtio 1.3, §3 "Reserved
 *        Feature Bits".
 * @{ */
/** Descriptors can reference descriptor tables (Virtio 1.3 §3). */
#define VIRTIO_RING_F_INDIRECT_DESC 28
/** Driver/device use event index for notifications (Virtio 1.3 §3). */
#define VIRTIO_RING_F_EVENT_IDX     29
/** @} */

/**
 * @brief Flag value for suppressing used-buffer notifications in the
 *        split available ring as described in Virtio 1.3,
 *        section "The Virtqueue Available Ring".
 */
/** Driver requests the device to skip interrupts (Virtio 1.3 §2.6.6). */
#define VIRTQ_AVAIL_F_NO_INTERRUPT 1

/**
 * @brief Flag value for suppressing available-buffer notifications in
 *        the split used ring as described in Virtio 1.3,
 *        section "The Virtqueue Used Ring".
 */
/** Device requests the driver to suppress notifications (Virtio 1.3 §2.6.7). */
#define VIRTQ_USED_F_NO_NOTIFY 1

/* Ranges of feature bits for specific device types (see spec 2.2)*/
/** Start of the first device-specific feature range (Virtio 1.3 §2.2). */
#define DEV_TYPE_FEAT_RANGE_0_BEGIN 0
/** End of the first device-specific feature range (Virtio 1.3 §2.2). */
#define DEV_TYPE_FEAT_RANGE_0_END   23
/** Start of the second device-specific feature range (Virtio 1.3 §2.2). */
#define DEV_TYPE_FEAT_RANGE_1_BEGIN 50
/** End of the second device-specific feature range (Virtio 1.3 §2.2). */
#define DEV_TYPE_FEAT_RANGE_1_END   127

/*
 * While defined separately in 4.1.4.5 for PCI and in 4.2.2 for MMIO
 * the same bits are responsible for the same interrupts, so defines
 * with them can be unified
 */
/**
 * @name Virtio transport interrupt bits
 * @brief Shared interrupt status bit positions defined in Virtio 1.3,
 *        §4.1.4.5 and §4.2.2 for PCI and MMIO transports.
 * @{ */
/** A virtqueue has pending used buffers (Virtio 1.3 §4.1.4.5/§4.2.2). */
#define VIRTIO_QUEUE_INTERRUPT                1
/** Device configuration space has changed (Virtio 1.3 §4.1.4.5/§4.2.2). */
#define VIRTIO_DEVICE_CONFIGURATION_INTERRUPT 2
/** @} */

/*
 * VIRTIO-MMIO register definitions.
 *
 * Based on Virtual I/O Device (VIRTIO) Version 1.3 specification:
 * https://docs.oasis-open.org/virtio/virtio/v1.3/csd01/virtio-v1.3-csd01.pdf
 */

/**
 * @name Virtio-MMIO register offsets
 * @brief Register layout defined in Virtio 1.3,
 *        §4.2.2 "MMIO Device Register Layout" (offsets in bytes).
 * @{ */
/** Magic value identifying the virtio MMIO device (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_MAGIC_VALUE         0x000
/** Virtio specification version exposed by the device (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_VERSION             0x004
/** Device type identifier register (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_DEVICE_ID           0x008
/** Vendor-specific identifier register (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_VENDOR_ID           0x00c
/** Lower 32 bits of the device feature bitmap (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_DEVICE_FEATURES     0x010
/** Selector choosing the device feature word (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_DEVICE_FEATURES_SEL 0x014
/** Lower 32 bits of the negotiated driver feature bitmap (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_DRIVER_FEATURES     0x020
/** Selector choosing the driver feature word (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_DRIVER_FEATURES_SEL 0x024
/** Virtqueue index selected for subsequent accesses (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_SEL           0x030
/** Maximum queue size supported by the selected virtqueue (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_SIZE_MAX      0x034
/** Queue size chosen by the driver for the selected virtqueue (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_SIZE          0x038
/** Ready flag indicating driver ownership of the queue (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_READY         0x044
/** Doorbell register for queue notifications (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_NOTIFY        0x050
/** Pending interrupt summary bits (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_INTERRUPT_STATUS    0x060
/** Interrupt acknowledgement register (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_INTERRUPT_ACK       0x064
/** Device status field mirror (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_STATUS              0x070
/** Lower 32 bits of the descriptor table address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_DESC_LOW      0x080
/** Upper 32 bits of the descriptor table address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_DESC_HIGH     0x084
/** Lower 32 bits of the available ring address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_AVAIL_LOW     0x090
/** Upper 32 bits of the available ring address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_AVAIL_HIGH    0x094
/** Lower 32 bits of the used ring address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_USED_LOW      0x0a0
/** Upper 32 bits of the used ring address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_USED_HIGH     0x0a4
/** Shared memory region selector (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_SHM_SEL             0x0ac
/** Lower 32 bits of the shared memory length (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_SHM_LEN_LOW         0x0b0
/** Upper 32 bits of the shared memory length (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_SHM_LEN_HIGH        0x0b4
/** Lower 32 bits of the shared memory base address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_SHM_BASE_LOW        0x0b8
/** Upper 32 bits of the shared memory base address (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_SHM_BASE_HIGH       0x0bc
/** Queue reset control register (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_QUEUE_RESET         0x0c0
/** Generation counter for configuration space (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_CONFIG_GENERATION   0x0fc
/** Base offset of the device configuration structure (Virtio 1.3 §4.2.2). */
#define VIRTIO_MMIO_CONFIG              0x100
/** @} */

#endif /* ZEPHYR_VIRTIO_VIRTIO_CONFIG_H_ */
