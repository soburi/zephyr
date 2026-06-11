/*
 * Copyright (c) 2026 TOKITA Hiroshi
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Derived from NuttX include/nuttx/pci/pci_epc.h (Apache-2.0),
 * which in turn follows the design of the Linux PCI endpoint framework.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PCIE_EPF_PCI_EPC_H_
#define ZEPHYR_INCLUDE_DRIVERS_PCIE_EPF_PCI_EPC_H_

/**
 * @brief PCI Endpoint Controller (EPC) API
 *
 * The EPC API abstracts PCI endpoint controller hardware. EPC drivers
 * implement `struct pci_epc_ops`; PCI Endpoint Function (EPF) drivers
 * use this API to program the configuration header, BARs and
 * interrupts of the endpoint, and to map host (RC) memory windows.
 *
 * @defgroup pci_epc_apis PCI EPC APIs
 * @ingroup io_interfaces
 * @{
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/slist.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/drivers/pcie/epf/pci_epf.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Interrupt types an endpoint can raise towards the host */
enum pci_epc_irq_type {
	PCI_EPC_IRQ_UNKNOWN,
	PCI_EPC_IRQ_LEGACY,
	PCI_EPC_IRQ_MSI,
	PCI_EPC_IRQ_MSIX,
};

/**
 * @brief Features supported by an EPC device, per function
 */
struct pci_epc_features {
	/** EPC can notify EPF drivers about link up */
	bool linkup_notifier;
	/** EPC notifies about core availability for initialization */
	bool core_init_notifier;
	/** Endpoint function supports MSI */
	bool msi_capable;
	/** Endpoint function supports MSI-X */
	bool msix_capable;
	/** Bitmap of BARs unavailable to the function driver */
	uint8_t bar_reserved;
	/** Bitmap of BARs that are fixed 64-bit */
	uint8_t bar_fixed_64bit;
	/** Fixed size required for each BAR (0: any size) */
	uint64_t bar_fixed_size[PCI_STD_NUM_BARS];
	/** Alignment required for BAR buffers */
	size_t align;
};

/**
 * @brief Address window of the endpoint controller
 *
 * Outbound window through which the local CPU accesses host (RC)
 * memory.
 */
struct pci_epc_mem_window {
	void *virt_base;     /**< Virtual base address of the window */
	uintptr_t phys_base; /**< Physical base address of the window */
	size_t size;         /**< Size of the window */
	size_t page_size;    /**< Allocation granularity */
};

/**
 * @brief Set of function pointers for performing EPC operations
 *
 * Implemented by endpoint controller drivers.
 */
__subsystem struct pci_epc_ops {
	/** Populate the configuration space header */
	int (*write_header)(const struct device *dev, uint8_t funcno,
			    const struct pci_epf_header *hdr);
	/** Configure a BAR */
	int (*set_bar)(const struct device *dev, uint8_t funcno, const struct pci_epf_bar *bar);
	/** Reset a BAR */
	void (*clear_bar)(const struct device *dev, uint8_t funcno, const struct pci_epf_bar *bar);
	/** Map a CPU address to a PCI (host) address */
	int (*map_addr)(const struct device *dev, uint8_t funcno, uintptr_t addr,
			uint64_t pci_addr, size_t size);
	/** Unmap a CPU address */
	void (*unmap_addr)(const struct device *dev, uint8_t funcno, uintptr_t addr);
	/** Set the number of MSI vectors requested in the MSI capability */
	int (*set_msi)(const struct device *dev, uint8_t funcno, uint8_t interrupts);
	/** Get the number of MSI vectors allocated by the host */
	int (*get_msi)(const struct device *dev, uint8_t funcno);
	/** Set the number of MSI-X vectors requested in the MSI-X capability */
	int (*set_msix)(const struct device *dev, uint8_t funcno, uint16_t interrupts, int barno,
			uint32_t offset);
	/** Get the number of MSI-X vectors allocated by the host */
	int (*get_msix)(const struct device *dev, uint8_t funcno);
	/** Raise a legacy, MSI or MSI-X interrupt towards the host */
	int (*raise_irq)(const struct device *dev, uint8_t funcno, enum pci_epc_irq_type type,
			 uint16_t interrupt_num);
	/** Map a physical address to an MSI address and return MSI data */
	int (*map_msi_irq)(const struct device *dev, uint8_t funcno, uintptr_t phys_addr,
			   uint8_t interrupt_num, uint32_t entry_size, uint32_t *msi_data,
			   uint32_t *msi_addr_offset);
	/** Start the PCI link */
	int (*start)(const struct device *dev);
	/** Stop the PCI link */
	void (*stop)(const struct device *dev);
	/** Get the features supported by the EPC for a function */
	const struct pci_epc_features *(*get_features)(const struct device *dev, uint8_t funcno);
};

/**
 * @brief Runtime data of an EPC device
 *
 * Embedded in the EPC driver's data and registered through
 * pci_epc_register(). The EPF core uses it to track bound functions
 * and the outbound memory windows.
 */
struct pci_epc {
	const struct device *dev;  /**< EPC device backpointer */
	sys_slist_t epf;           /**< Bound EPF devices */
	sys_snode_t node;          /**< Node in the global EPC list */
	struct k_mutex lock;       /**< Protects ops invocation and lists */
	uint32_t funcno_map;       /**< Bitmap of used physical function numbers */
	uint8_t max_functions;     /**< Functions configurable on this EPC */

	/** Outbound memory windows */
	const struct pci_epc_mem_window *windows;
	unsigned int num_windows;
	/** Per-window allocation bitmaps, sized by the EPC driver */
	sys_bitarray_t **window_maps;
};

/**
 * @brief Register an endpoint controller with the EPF core
 *
 * @param epc EPC runtime structure (fields dev, max_functions, windows,
 *            num_windows, window_maps must be initialized)
 *
 * @retval 0       Success
 * @retval -errno  Failure
 */
int pci_epc_register(struct pci_epc *epc);

/**
 * @brief Find a registered EPC by device name
 *
 * @param epc_name Device name of the endpoint controller
 *
 * @return EPC structure or NULL
 */
struct pci_epc *pci_epc_get(const char *epc_name);

/**
 * @brief Get the features supported by the EPC for a function
 */
const struct pci_epc_features *pci_epc_get_features(struct pci_epc *epc, uint8_t funcno);

/**
 * @brief Get the first unreserved BAR
 *
 * @return BAR number, or negative on error
 */
int pci_epc_get_first_free_bar(const struct pci_epc_features *epc_features);

/**
 * @brief Get the next unreserved BAR starting from @a barno
 *
 * @return BAR number, or negative on error
 */
int pci_epc_get_next_free_bar(const struct pci_epc_features *epc_features, int barno);

/**
 * @brief Populate the configuration space header of a function
 */
int pci_epc_write_header(struct pci_epc *epc, uint8_t funcno, const struct pci_epf_header *hdr);

/**
 * @brief Configure a BAR of a function
 */
int pci_epc_set_bar(struct pci_epc *epc, uint8_t funcno, const struct pci_epf_bar *bar);

/**
 * @brief Reset a BAR of a function
 */
void pci_epc_clear_bar(struct pci_epc *epc, uint8_t funcno, const struct pci_epf_bar *bar);

/**
 * @brief Map a local CPU address to a PCI (host) address
 */
int pci_epc_map_addr(struct pci_epc *epc, uint8_t funcno, uintptr_t addr, uint64_t pci_addr,
		     size_t size);

/**
 * @brief Unmap a local CPU address
 */
void pci_epc_unmap_addr(struct pci_epc *epc, uint8_t funcno, uintptr_t addr);

/**
 * @brief Set the number of MSI vectors required by the function
 */
int pci_epc_set_msi(struct pci_epc *epc, uint8_t funcno, uint8_t interrupts);

/**
 * @brief Get the number of MSI vectors allocated by the host
 */
int pci_epc_get_msi(struct pci_epc *epc, uint8_t funcno);

/**
 * @brief Set the number of MSI-X vectors required by the function
 */
int pci_epc_set_msix(struct pci_epc *epc, uint8_t funcno, uint16_t interrupts, int barno,
		     uint32_t offset);

/**
 * @brief Get the number of MSI-X vectors allocated by the host
 */
int pci_epc_get_msix(struct pci_epc *epc, uint8_t funcno);

/**
 * @brief Raise an interrupt towards the host
 */
int pci_epc_raise_irq(struct pci_epc *epc, uint8_t funcno, enum pci_epc_irq_type type,
		      uint16_t interrupt_num);

/**
 * @brief Map a physical address to an MSI address
 */
int pci_epc_map_msi_irq(struct pci_epc *epc, uint8_t funcno, uintptr_t phys_addr,
			uint8_t interrupt_num, uint32_t entry_size, uint32_t *msi_data,
			uint32_t *msi_addr_offset);

/**
 * @brief Start the PCI link
 */
int pci_epc_start(struct pci_epc *epc);

/**
 * @brief Stop the PCI link
 */
void pci_epc_stop(struct pci_epc *epc);

/**
 * @brief Allocate space from the EPC outbound windows
 *
 * @param epc       EPC structure
 * @param size      Size to allocate
 * @param phys_addr Output for the physical address of the allocation
 *
 * @return Virtual address or NULL
 */
void *pci_epc_mem_alloc_addr(struct pci_epc *epc, size_t size, uintptr_t *phys_addr);

/**
 * @brief Free space allocated with pci_epc_mem_alloc_addr()
 */
void pci_epc_mem_free_addr(struct pci_epc *epc, void *virt_addr, size_t size);

/**
 * @brief Notify bound EPF drivers about link up
 */
void pci_epc_linkup(struct pci_epc *epc);

/**
 * @brief Notify bound EPF drivers about link down
 */
void pci_epc_linkdown(struct pci_epc *epc);

/**
 * @brief Notify bound EPF drivers about core initialization completion
 */
void pci_epc_init_notify(struct pci_epc *epc);

/**
 * @brief Notify bound EPF drivers about Bus Master Enable
 */
void pci_epc_bme_notify(struct pci_epc *epc);

#ifdef __cplusplus
}
#endif

/**
 * @}
 */

#endif /* ZEPHYR_INCLUDE_DRIVERS_PCIE_EPF_PCI_EPC_H_ */
