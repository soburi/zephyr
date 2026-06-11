/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Derived from NuttX include/nuttx/pci/pci_epf.h (Apache-2.0),
 * which in turn follows the design of the Linux PCI endpoint framework.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PCIE_EPF_PCI_EPF_H_
#define ZEPHYR_INCLUDE_DRIVERS_PCIE_EPF_PCI_EPF_H_

/**
 * @brief PCI Endpoint Function (EPF) framework
 *
 * The EPF framework lets Zephyr implement the function side of a PCI
 * endpoint device. An EPF device describes one PCI function
 * (configuration header, BARs, MSI requirements); an EPF driver
 * implements its behavior. EPF devices are bound to a PCI Endpoint
 * Controller (EPC) device that programs the actual endpoint hardware.
 *
 * @defgroup pci_epf_apis PCI EPF APIs
 * @ingroup io_interfaces
 * @{
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Number of standard BARs of a PCI function */
#define PCI_STD_NUM_BARS 6

/** @name BAR flags
 * @{
 */
#define PCI_EPF_BAR_SPACE_IO       BIT(0) /**< I/O space (otherwise memory) */
#define PCI_EPF_BAR_MEM_TYPE_64    BIT(2) /**< 64-bit memory BAR */
#define PCI_EPF_BAR_MEM_PREFETCH   BIT(3) /**< Prefetchable memory BAR */
/** @} */

/**
 * @brief Standard configuration space header of an endpoint function
 */
struct pci_epf_header {
	uint16_t vendorid;         /**< Device manufacturer */
	uint16_t deviceid;         /**< Particular device */
	uint8_t revid;             /**< Device-specific revision */
	uint8_t progif_code;       /**< Register-level programming interface */
	uint8_t subclass_code;     /**< Specific function of the device */
	uint8_t baseclass_code;    /**< Broad classification of the function */
	uint8_t cache_line_size;   /**< System cacheline size in DWORDs */
	uint8_t interrupt_pin;     /**< Interrupt pin used by the function */
	uint16_t subsys_vendor_id; /**< Vendor of the add-in card/subsystem */
	uint16_t subsys_id;        /**< Subsystem ID specific to the vendor */
};

/**
 * @brief BAR of an endpoint function
 */
struct pci_epf_bar {
	uintptr_t phys_addr; /**< Physical address mapped to the BAR */
	void *addr;          /**< Virtual address corresponding to phys_addr */
	size_t size;         /**< Size of the address space in the BAR */
	int barno;           /**< BAR number */
	int flags;           /**< PCI_EPF_BAR_* flags */
};

struct pci_epf_device;
struct pci_epc;

/**
 * @brief Set of function pointers for performing EPF operations
 */
struct pci_epf_ops {
	/** Called when an EPC device has been bound to the EPF device */
	int (*bind)(struct pci_epf_device *epf);
	/** Called when the binding between EPC and EPF device is lost */
	void (*unbind)(struct pci_epf_device *epf);
};

/**
 * @brief Callbacks for capturing EPC events
 */
struct pci_epc_event_ops {
	/** EPC core initialization complete */
	int (*core_init)(struct pci_epf_device *epf);
	/** PCI link came up */
	int (*link_up)(struct pci_epf_device *epf);
	/** PCI link went down */
	int (*link_down)(struct pci_epf_device *epf);
	/** Bus Master Enable was set by the host */
	int (*bme)(struct pci_epf_device *epf);
};

/**
 * @brief PCI EPF driver
 */
struct pci_epf_driver {
	/** Called when a new EPF device is bound to this driver */
	int (*probe)(struct pci_epf_device *epf);
	/** Called when the binding to the EPF device is broken */
	void (*remove)(struct pci_epf_device *epf);

	sys_snode_t node;
	const struct pci_epf_ops *ops;
	/** NULL-name-terminated array of names this driver matches */
	const char *const *names;
};

/**
 * @brief PCI EPF device
 *
 * Represents one PCI function to be exposed through an endpoint
 * controller.
 */
struct pci_epf_device {
	const char *name;     /**< Name used to match an EPF driver */
	const char *epc_name; /**< Name of the EPC device to bind to */
	struct pci_epf_header *header;
	struct pci_epf_bar bar[PCI_STD_NUM_BARS];
	uint8_t msi_interrupts;   /**< Number of required MSI interrupts */
	uint16_t msix_interrupts; /**< Number of required MSI-X interrupts */
	uint8_t funcno;           /**< Physical function number within the EPC */

	struct pci_epc *epc;           /**< Bound EPC */
	struct pci_epf_driver *driver; /**< Bound EPF driver */
	sys_snode_t node;
	sys_snode_t epc_node;

	struct k_mutex lock;
	bool is_bound;
	const struct pci_epc_event_ops *event_ops;
	void *priv;
};

/**
 * @brief Register a PCI EPF device
 *
 * The device is bound to a matching registered EPF driver, if any.
 *
 * @param epf EPF device to register
 *
 * @retval 0       Success
 * @retval -errno  Failure
 */
int pci_epf_device_register(struct pci_epf_device *epf);

/**
 * @brief Unregister a PCI EPF device
 *
 * @param epf EPF device to unregister
 *
 * @retval 0       Success
 * @retval -errno  Failure
 */
int pci_epf_device_unregister(struct pci_epf_device *epf);

/**
 * @brief Register a PCI EPF driver
 *
 * Already registered, unbound EPF devices with a matching name are
 * bound to the driver.
 *
 * @param drv EPF driver to register
 *
 * @retval 0       Success
 * @retval -errno  Failure
 */
int pci_epf_register_driver(struct pci_epf_driver *drv);

/**
 * @brief Unregister a PCI EPF driver
 *
 * @param drv EPF driver to unregister
 *
 * @retval 0       Success
 * @retval -errno  Failure
 */
int pci_epf_unregister_driver(struct pci_epf_driver *drv);

/**
 * @brief Allocate backing memory for an EPF BAR
 *
 * @param epf   EPF device
 * @param barno BAR number corresponding to the allocated space
 * @param size  Size of the memory to allocate
 * @param align Required alignment of the region (0 for page alignment)
 *
 * @return Virtual address of the space, or NULL on failure
 */
void *pci_epf_alloc_space(struct pci_epf_device *epf, int barno, size_t size, size_t align);

/**
 * @brief Free memory allocated with pci_epf_alloc_space()
 *
 * @param epf   EPF device
 * @param barno BAR number corresponding to the space
 */
void pci_epf_free_space(struct pci_epf_device *epf, int barno);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_PCIE_EPF_PCI_EPF_H_ */
