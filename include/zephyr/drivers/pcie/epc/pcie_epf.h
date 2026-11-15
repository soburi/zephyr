/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX include/nuttx/pci/pci_epf.h
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

/**
 * @file
 * @brief PCI Endpoint Function (EPF) framework API
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PCIE_EPC_PCIE_EPF_H_
#define ZEPHYR_INCLUDE_DRIVERS_PCIE_EPC_PCIE_EPF_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/drivers/pcie/epc/pci_regs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup pcie_epf_interface PCI Endpoint Function Interface
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Represents standard configuration header
 */
struct pcie_epf_header_s {
	/** Identifies device manufacturer */
	uint16_t vendorid;
	/** Identifies a particular device */
	uint16_t deviceid;
	/** Specifies a device-specific revision identifier */
	uint8_t revid;
	/** Identifies a specific register-level programming interface */
	uint8_t progif_code;
	/** Identifies more specifically the function of the device */
	uint8_t subclass_code;
	/** Broadly classifies the type of function the device performs */
	uint8_t baseclass_code;
	/** Specifies the system cacheline size in units of DWORDs */
	uint8_t cache_line_size;
	/** Interrupt pin the device (or device function) uses */
	uint8_t interrupt_pin;
	/** Vendor of the add-in card or subsystem */
	uint16_t subsys_vendor_id;
	/** Id specific to vendor */
	uint16_t subsys_id;
};

/**
 * @brief Represents the BAR of EPF device
 */
struct pcie_epf_bar_s {
	/** Physical address that should be mapped to the BAR */
	uintptr_t phys_addr;
	/** Virtual address corresponding to the phys_addr */
	void *addr;
	/** The size of the address space present in BAR */
	size_t size;
	/** BAR number */
	int barno;
	/** Flags that are set for the BAR */
	int flags;
};

/**
 * @brief Identifies EPF devices for probing
 */
struct pcie_epf_device_id_s {
	/** Name of the EPF device */
	char name[32];
	/** Data private to the driver */
	unsigned long driver_data;
};

struct pcie_epf_driver_s;
struct pcie_epc_event_ops_s;

/**
 * @brief Represents the PCI EPF device
 */
struct pcie_epf_device_s {
	/** The name of the PCI EPF device */
	const char *name;
	/** The name of the EPC device this EPF device should bind to */
	const char *epc_name;
	/** Represents standard configuration header */
	struct pcie_epf_header_s *header;
	/** Represents the BAR of EPF device */
	struct pcie_epf_bar_s bar[PCI_STD_NUM_BARS];
	/** Number of MSI interrupts required by this function */
	uint8_t msi_interrupts;
	/** Number of MSI-X interrupts required by this function */
	uint16_t msix_interrupts;
	/** Unique (physical) function number within this endpoint device */
	uint8_t funcno;

	/** The EPC device to which this EPF device is bound */
	const struct device *epc;
	/** The EPF driver to which this EPF device is bound */
	struct pcie_epf_driver_s *driver;
	/** Pointer to the EPF device ID */
	const struct pcie_epf_device_id_s *id;
	/** Node in the global list of EPF devices */
	sys_dnode_t node;
	/** Node in the list of EPF devices bound to a pcie_epc_ctrl_s */
	sys_dnode_t epc_node;

	/** Mutex to protect against concurrent access of pcie_epf_ops_s */
	struct k_mutex lock;
	/** Indicates if bind notification to function driver has been invoked */
	bool is_bound;
	/** Callbacks for capturing the EPC events */
	const struct pcie_epc_event_ops_s *event_ops;
	/** The private data */
	void *priv;
};

/**
 * @brief Set of function pointers for performing EPF operations
 */
struct pcie_epf_ops_s {
	/** Ops to perform when a EPC device has been bound to EPF device */
	int (*bind)(struct pcie_epf_device_s *epf);
	/** Ops to perform when a binding has been lost between a EPC device
	 *  and EPF device
	 */
	void (*unbind)(struct pcie_epf_device_s *epf);
};

/**
 * @brief Callbacks for capturing the EPC events
 */
struct pcie_epc_event_ops_s {
	/** Callback for the EPC initialization complete event */
	int (*core_init)(struct pcie_epf_device_s *epf);
	/** Callback for the EPC link up event */
	int (*link_up)(struct pcie_epf_device_s *epf);
	/** Callback for the EPC link down event */
	int (*link_down)(struct pcie_epf_device_s *epf);
	/** Callback for the EPC BME (Bus Master Enable) event */
	int (*bme)(struct pcie_epf_device_s *epf);
};

/**
 * @brief Represents the PCI EPF driver
 */
struct pcie_epf_driver_s {
	/** Ops to perform when a new EPF device has been bound to the EPF
	 *  driver
	 */
	int (*probe)(struct pcie_epf_device_s *epf);
	/** Ops to perform when the binding between the EPF device and EPF
	 *  driver is broken
	 */
	void (*remove)(struct pcie_epf_device_s *epf);

	/** Node in the global list of EPF drivers */
	sys_dnode_t node;
	/** Set of function pointers for performing EPF operations */
	const struct pcie_epf_ops_s *ops;
	/** Identifies EPF devices for probing */
	const struct pcie_epf_device_id_s *id_table;
};

/**
 * @brief Represents the MSIX table entry structure
 */
struct pcie_epf_msix_tbl_s {
	/** Writes to this address will trigger MSIX interrupt in host */
	uint64_t msg_addr;
	/** Data that should be written to msg_addr to trigger MSIX interrupt */
	uint32_t msg_data;
	/** Identifies if the function is prohibited from sending a message
	 *  using this MSIX table entry
	 */
	uint32_t vector_ctrl;
};

/**
 * @brief Register a new PCI EPF device
 *
 * Invoke to register a statically allocated PCI EPF device. The EPF device
 * is bound to a matching EPF driver, if one is already registered.
 *
 * @param epf The PCI EPF device to be registered
 *
 * @retval 0 Success
 * @retval -ENODEV No EPC device matches the epc_name of the EPF device
 * @retval -errno Other negative errno on driver probe failure
 */
int pcie_epf_device_register(struct pcie_epf_device_s *epf);

/**
 * @brief Unregister a PCI EPF device
 *
 * @param epf The PCI EPF device to be unregistered
 *
 * @retval 0 Success
 */
int pcie_epf_device_unregister(struct pcie_epf_device_s *epf);

/**
 * @brief Register a new PCI EPF driver
 *
 * Registered EPF devices without a driver are bound to the new driver when
 * their name matches an entry of the driver's id_table.
 *
 * @param drv EPF driver
 *
 * @retval 0 Success
 * @retval -EINVAL Required driver callbacks are missing
 * @retval -ENODEV No EPC device matches a bound EPF device
 */
int pcie_epf_register_driver(struct pcie_epf_driver_s *drv);

/**
 * @brief Unregister the PCI EPF driver
 *
 * @param drv The PCI EPF driver that has to be unregistered
 *
 * @retval 0 Success
 */
int pcie_epf_unregister_driver(struct pcie_epf_driver_s *drv);

/**
 * @brief Allocate memory for the PCI EPF register space
 *
 * The memory is allocated from the DMA heap of the EPC device the EPF
 * device is bound to.
 *
 * @param epf   The EPF device to whom allocate the memory
 * @param barno The BAR number corresponding to the allocated register space
 * @param size  The size of the memory that has to be allocated
 * @param align Alignment size for the allocation region
 *
 * @return Address of the allocated space, NULL on failure
 */
void *pcie_epf_alloc_space(struct pcie_epf_device_s *epf, int barno,
			  size_t size, size_t align);

/**
 * @brief Free the allocated PCI EPF register space
 *
 * @param epf   The EPF device from whom to free the memory
 * @param barno The BAR number corresponding to the register space
 * @param addr  The virtual address of the PCI EPF register space
 */
void pcie_epf_free_space(struct pcie_epf_device_s *epf, int barno, void *addr);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_PCIE_EPC_PCIE_EPF_H_ */
