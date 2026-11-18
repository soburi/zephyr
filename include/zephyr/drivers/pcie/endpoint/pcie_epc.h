/*
 * Copyright The Zephyr Project Contributors
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Ported from NuttX include/nuttx/pci/pci_epc.h
 * (commit 43d9de6f97055eec11234a69cf5641a811f7847c).
 */

/**
 * @file
 * @brief PCIe Endpoint Controller (EPC) framework API
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_PCIE_ENDPOINT_PCIE_EPC_H_
#define ZEPHYR_INCLUDE_DRIVERS_PCIE_ENDPOINT_PCIE_EPC_H_

#include <stdbool.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/bitarray.h>
#include <zephyr/sys/dlist.h>
#include <zephyr/drivers/pcie/endpoint/pcie_epf.h>
#include <zephyr/drivers/pcie/endpoint/pci_regs.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup pcie_epc_interface PCIe Endpoint Controller Interface
 * @ingroup io_interfaces
 * @{
 */

/** @brief Type of interrupt an endpoint function raises to the host */
enum pcie_epc_irq_type {
	PCIE_EPC_IRQ_UNKNOWN,
	PCIE_EPC_IRQ_LEGACY,
	PCIE_EPC_IRQ_MSI,
	PCIE_EPC_IRQ_MSIX,
};

/**
 * @brief Features supported by a EPC device per function
 */
struct pcie_epc_features_s {
	/** Indicate if the EPC device can notify EPF driver on link up */
	unsigned int linkup_notifier : 1;
	/** Indicate cores that can notify about their availability for
	 *  initialization
	 */
	unsigned int core_init_notifier : 1;
	/** Indicate if the endpoint function has MSI capability */
	unsigned int msi_capable : 1;
	/** Indicate if the endpoint function has MSI-X capability */
	unsigned int msix_capable : 1;
	/** Indicate if pcie_epc_add_capability() is supported */
	unsigned int capability_writable : 1;
	/** Supports ordered BAR requests. A controller with legacy interrupts
	 *  also controls their level with pcie_epc_set_legacy_irq().
	 */
	unsigned int bar_access : 1;
	/** Indicate if the EPC device notifies the EPF driver of a host access
	 *  to a software-serviced BAR. Without it the EPF driver polls
	 *  pcie_epc_bar_access_get().
	 */
	unsigned int bar_access_notifier : 1;
	/** Bitmap to indicate reserved BAR unavailable to function driver */
	uint8_t bar_reserved;
	/** Bitmap to indicate fixed 64bit BARs */
	uint8_t bar_fixed_64bit;
	/** Array specifying the size supported by each BAR */
	uint64_t bar_fixed_size[PCI_STD_NUM_BARS];
	/** Alignment size required for BAR buffer allocation */
	size_t align;
};

/** @brief One host access to a software-serviced BAR. */
struct pcie_epc_bar_access {
	/** Controller-assigned request identifier */
	uint32_t id;
	/** Byte offset within the BAR */
	uint32_t offset;
	/** Write value or read response, in CPU byte order */
	uint64_t value;
	/** BAR number */
	uint8_t bar;
	/** Access width in bytes: 1, 2, 4 or 8 */
	uint8_t size;
	/** True for a host write */
	bool write;
};

/**
 * @brief Set of function pointers for performing EPC operations
 *
 * Each operation receives the EPC device. The driver defines the table with
 * DEVICE_API(pcie_epc, ...) and passes it as the API of its device; the
 * driver state is reached through the device data, which starts with
 * struct pcie_epc_ctrl_s.
 */
__subsystem struct pcie_epc_driver_api {
	/** Ops to populate configuration space header */
	int (*write_header)(const struct device *dev, uint8_t funcno,
			    struct pcie_epf_header_s *hdr);
	/** Ops to append a capability to the capability list of a function */
	int (*add_capability)(const struct device *dev, uint8_t funcno,
			      uint8_t id, const void *body, size_t len);
	/** Ops to remove the capabilities added with add_capability */
	void (*clear_capabilities)(const struct device *dev, uint8_t funcno);
	/** Ops to configure the BAR */
	int (*set_bar)(const struct device *dev, uint8_t funcno,
		       struct pcie_epf_bar_s *bar);
	/** Ops to reset the BAR */
	void (*clear_bar)(const struct device *dev, uint8_t funcno,
			  struct pcie_epf_bar_s *bar);
	/** Ops to map CPU address to PCI address */
	int (*map_addr)(const struct device *dev, uint8_t funcno,
			uintptr_t addr, uint64_t pci_addr, size_t size);
	/** Ops to unmap CPU address and PCI address */
	void (*unmap_addr)(const struct device *dev, uint8_t funcno,
			   uintptr_t addr);
	/** Ops to set the requested number of MSI interrupts in the MSI
	 *  capability register
	 */
	int (*set_msi)(const struct device *dev, uint8_t funcno,
		       uint8_t interrupts);
	/** Ops to get the number of MSI interrupts allocated by the RC from
	 *  the MSI capability register
	 */
	int (*get_msi)(const struct device *dev, uint8_t funcno);
	/** Ops to set the requested number of MSI-X interrupts in the MSI-X
	 *  capability register
	 */
	int (*set_msix)(const struct device *dev, uint8_t funcno,
			uint16_t interrupts, int barno, uint32_t offset);
	/** Ops to get the number of MSI-X interrupts allocated by the RC
	 *  from the MSI-X capability register
	 */
	int (*get_msix)(const struct device *dev, uint8_t funcno);
	/** Ops to raise a legacy, MSI or MSI-X interrupt */
	int (*raise_irq)(const struct device *dev, uint8_t funcno,
			 enum pcie_epc_irq_type type,
			 uint16_t interrupt_num);
	/** Ops to map physical address to MSI address and return MSI data */
	int (*map_msi_irq)(const struct device *dev, uint8_t funcno,
			   uintptr_t phys_addr, uint8_t interrupt_num,
			   uint32_t entry_size, uint32_t *msi_data,
			   uint32_t *msi_addr_offset);
	/** Ops to start the PCI link */
	int (*start)(const struct device *dev);
	/** Ops to stop the PCI link */
	int (*stop)(const struct device *dev);
	/** Ops to get the features supported by the EPC */
	const struct pcie_epc_features_s *
	(*get_features)(const struct device *dev, uint8_t funcno);
	/** Enable software handling of host accesses to a BAR */
	int (*bar_access_enable)(const struct device *dev, uint8_t funcno, uint8_t bar,
				 bool enable);
	/** Get the next outstanding host access */
	int (*bar_access_get)(const struct device *dev, uint8_t funcno,
			     struct pcie_epc_bar_access *access);
	/** Complete the outstanding access */
	int (*bar_access_complete)(const struct device *dev, uint8_t funcno,
				  const struct pcie_epc_bar_access *access, int status);
	/** Set the level of the legacy interrupt */
	int (*set_legacy_irq)(const struct device *dev, uint8_t funcno, bool asserted);

	/** Ops to transfer data between mapped Host memory and device memory
	 *  with "System DMA"
	 */
	int (*dma_xfer)(const struct device *dev, uintptr_t mapped_addr,
			uintptr_t local_addr, uint32_t size, bool fromdev);
};

/**
 * @brief Enable or disable software servicing of a BAR
 *
 * Call before starting the controller. Accesses to enabled BARs are delivered
 * in host order and reads wait for completion. Other BARs retain direct memory
 * access. One thread per function must retrieve and complete requests. The
 * controller must stop delivering requests and abort outstanding requests on
 * stop. All functions below require thread context.
 *
 * @param dev EPC device
 * @param funcno Function number
 * @param bar BAR number
 * @param enable Enable software servicing
 * @retval 0 Success
 * @retval -EINVAL Invalid function or BAR
 * @retval -EBUSY Controller is started
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_bar_access_enable(const struct device *dev, uint8_t funcno, uint8_t bar,
			       bool enable);

/**
 * @brief Retrieve a host BAR access without waiting
 *
 * @param dev EPC device
 * @param funcno Function number
 * @param access Request destination
 * @retval 0 Request available
 * @retval -EAGAIN No request available
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_bar_access_get(const struct device *dev, uint8_t funcno,
			  struct pcie_epc_bar_access *access);

/**
 * @brief Complete a host BAR access
 *
 * @param dev EPC device
 * @param funcno Function number
 * @param access Request with the read response in value
 * @param status Zero on success, negative errno on failure
 * @retval 0 Response submitted
 * @retval -ESTALE Request expired or was aborted
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_bar_access_complete(const struct device *dev, uint8_t funcno,
			       const struct pcie_epc_bar_access *access, int status);

/**
 * @brief Assert or deassert the function's legacy interrupt
 *
 * @param dev EPC device
 * @param funcno Function number
 * @param asserted Interrupt level
 * @retval 0 Success
 * @retval -EINVAL Invalid function
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_set_legacy_irq(const struct device *dev, uint8_t funcno, bool asserted);

/**
 * @brief Address window of the endpoint controller
 */
struct pcie_epc_mem_window_s {
	/** Virtual base address of the PCI address window */
	void *virt_base;
	/** Physical base address of the PCI address window */
	uintptr_t phys_base;
	/** The size of the PCI address window */
	size_t size;
	/** Size of each page */
	size_t page_size;
};

/**
 * @brief Address space of the endpoint controller
 */
struct pcie_epc_mem_s {
	/** Virtual base address of the PCI address window */
	void *virt_base;
	/** Physical base address of the PCI address window */
	uintptr_t phys_base;
	/** The size of the PCI address window */
	size_t size;
	/** Size of each page */
	size_t page_size;

	/** Bitmap to manage the PCI address space, provided by the EPC
	 *  driver with a capacity of at least size / page_size bits
	 */
	sys_bitarray_t *bitmap;
	/** Number of bits representing the address region */
	int pages;
};

/**
 * @brief Static configuration of an EPC device
 *
 * The EPC driver places this structure at the beginning of the config of
 * its device and initializes it from the devicetree node with
 * PCIE_EPC_DT_INST_CONFIG_INIT().
 */
struct pcie_epc_config {
	/** Number of functions that can be configured in this EPC */
	uint8_t max_functions;
};

/**
 * @brief Initializer of struct pcie_epc_config from a devicetree instance
 *
 * The node must include pcie-epc.yaml.
 *
 * @param inst Devicetree instance number
 */
#define PCIE_EPC_DT_INST_CONFIG_INIT(inst)				\
	{								\
		.max_functions = DT_INST_PROP(inst, max_functions),	\
	}

/**
 * @brief Represents the PCI EPC device
 *
 * The EPC driver places this structure at the beginning of the data of
 * its device, so the framework reaches it through the device pointer.
 */
struct pcie_epc_ctrl_s {
	/** The device this EPC belongs to */
	const struct device *dev;
	/** List of endpoint functions present in this EPC device */
	sys_dlist_t epf;
	/** Array of address spaces of the endpoint controller */
	struct pcie_epc_mem_s *mem;
	/** Number of address spaces supported by device */
	unsigned int num_windows;
	/** Heap for EPF BAR buffer allocation, may be NULL */
	struct k_heap *dmaheap;

	/** Mutex to protect against concurrent access of EP controller */
	struct k_mutex lock;
	/** Protects the list of endpoint functions against ISR context */
	struct k_spinlock epf_lock;
	/** Bitmap to manage physical function number */
	unsigned long funcno_map;
	/** Indicates if the PCI link has been started */
	bool started;
};

/**
 * @brief Get unreserved BAR starting from @p barno
 *
 * Invoke to get the next unreserved BAR starting from @p barno that can be
 * used for endpoint function.
 *
 * @param epc_features pcie_epc_features_s structure that holds the reserved
 *                     bar bitmap
 * @param barno        The starting BAR number from where unreserved BAR
 *                     should be searched
 *
 * @return BAR number on success, negative errno on failure
 */
int pcie_epc_get_next_free_bar(const struct pcie_epc_features_s *epc_features,
			       int barno);

/**
 * @brief Get the first unreserved BAR
 *
 * Invoke to get the first unreserved BAR that can be used by the endpoint
 * function.
 *
 * @param f pcie_epc_features_s structure that holds the reserved bar bitmap
 *
 * @return BAR number on success, negative errno on failure
 */
#define pcie_epc_get_first_free_bar(f) pcie_epc_get_next_free_bar(f, 0)

/**
 * @brief Get the features supported by EPC
 *
 * Invoke to get the features provided by the EPC which may be specific to
 * an endpoint function.
 *
 * @param dev    The EPC device whose features are returned
 * @param funcno The features supported by the EPC device specific to the
 *               endpoint function with funcno will be returned
 *
 * @return EPC features on success, NULL on failure
 */
const struct pcie_epc_features_s *
pcie_epc_get_features(const struct device *dev, uint8_t funcno);

/**
 * @brief Stop the PCI link
 *
 * Successful return guarantees that inbound BAR accesses have stopped.
 * A failed stop leaves the controller started and prevents EPF removal.
 *
 * @param dev The EPC device whose link has to be stopped
 *
 * @retval 0 Controller stopped
 * @retval -EINVAL Invalid device
 * @retval -ENOTSUP Stopping is not supported
 * @return Other negative errno from the controller
 */
int pcie_epc_stop(const struct device *dev);

/**
 * @brief Start the PCI link
 *
 * Invoke once all endpoint functions have been added with
 * pcie_epc_add_epf(). The host can enumerate the functions from this point
 * on, so no function can be added until the link is stopped again.
 *
 * @param dev The EPC device whose link has to be started
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid device
 * @retval -ENOTSUP Starting is not supported
 */
int pcie_epc_start(const struct device *dev);

/**
 * @brief Interrupt the host system
 *
 * Invoke to raise a legacy, MSI or MSI-X interrupt.
 *
 * @param dev           The EPC device which has to interrupt the host
 * @param funcno        The physical endpoint function number in the EPC
 *                      device
 * @param type          Specify the type of interrupt; legacy, MSI or MSI-X
 * @param interrupt_num The MSI or MSI-X interrupt number with range (1-N)
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_raise_irq(const struct device *dev, uint8_t funcno,
		       enum pcie_epc_irq_type type, uint16_t interrupt_num);

/**
 * @brief Map physical address to MSI address and return MSI data
 *
 * The physical address should be an address in the outbound region. This is
 * required to implement doorbell functionality of NTB wherein EPC on either
 * side of the interface (primary and secondary) can directly write to the
 * physical address (in outbound region) of the other interface to ring
 * doorbell.
 *
 * @param dev             The EPC device which has to interrupt the host
 * @param funcno          The physical endpoint function number in the EPC
 *                        device
 * @param phys_addr       The physical address of the outbound region
 * @param interrupt_num   The MSI or MSI-X interrupt number with range (1-N)
 * @param entry_size      Size of Outbound address region for each interrupt
 * @param msi_data        The data that should be written in order to raise
 *                        MSI interrupt with interrupt number as 'interrupt
 *                        num'
 * @param msi_addr_offset Offset of MSI address from the aligned outbound
 *                        address to which the MSI address is mapped
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_map_msi_irq(const struct device *dev, uint8_t funcno,
			 uintptr_t phys_addr, uint8_t interrupt_num,
			 uint32_t entry_size, uint32_t *msi_data,
			 uint32_t *msi_addr_offset);

/**
 * @brief Get the number of MSI interrupt numbers allocated
 *
 * Invoke to get the number of MSI interrupts allocated by the RC.
 *
 * @param dev    The EPC device to which MSI interrupts was requested
 * @param funcno The physical endpoint function number in the EPC device
 *
 * @return Number of interrupts on success, negative errno on failure
 */
int pcie_epc_get_msi(const struct device *dev, uint8_t funcno);

/**
 * @brief Set the number of MSI interrupt numbers required
 *
 * Invoke to set the required number of MSI interrupts.
 *
 * @param dev        The EPC device on which MSI has to be configured
 * @param funcno     The physical endpoint function number in the EPC device
 * @param interrupts Number of MSI interrupts required by the EPF
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_set_msi(const struct device *dev, uint8_t funcno,
		     uint8_t interrupts);

/**
 * @brief Get the number of MSI-X interrupt numbers allocated
 *
 * Invoke to get the number of MSI-X interrupts allocated by the RC.
 *
 * @param dev    The EPC device to which MSI-X interrupts was requested
 * @param funcno The physical endpoint function number in the EPC device
 *
 * @return Number of interrupts on success, negative errno on failure
 */
int pcie_epc_get_msix(const struct device *dev, uint8_t funcno);

/**
 * @brief Set the number of MSI-X interrupt numbers required
 *
 * Invoke to set the required number of MSI-X interrupts.
 *
 * @param dev        The EPC device on which MSI-X has to be configured
 * @param funcno     The physical endpoint function number in the EPC device
 * @param interrupts Number of MSI-X interrupts required by the EPF
 * @param barno      BAR where the MSI-X table resides
 * @param offset     Offset pointing to the start of MSI-X table
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_set_msix(const struct device *dev, uint8_t funcno,
		      uint16_t interrupts, int barno, uint32_t offset);

/**
 * @brief Unmap CPU address from PCI address
 *
 * Invoke to unmap the CPU address from PCI address.
 *
 * @param dev       The EPC device on which address is allocated
 * @param funcno    The physical endpoint function number in the EPC device
 * @param phys_addr Physical address of the local system
 */
void pcie_epc_unmap_addr(const struct device *dev, uint8_t funcno,
			 uintptr_t phys_addr);

/**
 * @brief Map CPU address to PCI address
 *
 * Invoke to map CPU address with PCI address.
 *
 * @param dev       The EPC device on which address is allocated
 * @param funcno    The physical endpoint function number in the EPC device
 * @param phys_addr Physical address of the local system
 * @param pci_addr  PCI address to which the physical address should be
 *                  mapped
 * @param size      The size of the allocation
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_map_addr(const struct device *dev, uint8_t funcno,
		      uintptr_t phys_addr, uint64_t pci_addr, size_t size);

/**
 * @brief Reset the BAR
 *
 * Invoke to reset the BAR of the endpoint device.
 *
 * @param dev    The EPC device for which the BAR has to be cleared
 * @param funcno The physical endpoint function number in the EPC device
 * @param bar    The struct bar that contains the BAR information
 */
void pcie_epc_clear_bar(const struct device *dev, uint8_t funcno,
			struct pcie_epf_bar_s *bar);

/**
 * @brief Configure BAR in order for host to assign PCI addr space
 *
 * Invoke to configure the BAR of the endpoint device.
 *
 * @param dev    The EPC device on which BAR has to be configured
 * @param funcno The physical endpoint function number in the EPC device
 * @param bar    The struct pcie_epf_bar_s that contains the BAR information
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -ENOTSUP Operation is unsupported
 */
int pcie_epc_set_bar(const struct device *dev, uint8_t funcno,
		     struct pcie_epf_bar_s *bar);

/**
 * @brief Write standard configuration header
 *
 * Invoke to write the configuration header to the endpoint controller. Every
 * endpoint controller will have a dedicated location to which the standard
 * configuration header would be written. The callback function should write
 * the header fields to this dedicated location.
 *
 * An EPC device whose configuration header is fixed in the hardware does not
 * implement the operation, and the call succeeds without writing anything.
 * The integration then makes the hardware match the header the endpoint
 * function expects.
 *
 * @param dev    The EPC device to which the configuration header should be
 *               written
 * @param funcno The physical endpoint function number in the EPC device
 * @param header Standard configuration header fields
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 */
int pcie_epc_write_header(const struct device *dev, uint8_t funcno,
			  struct pcie_epf_header_s *header);

/**
 * @brief Bind PCI endpoint function to an endpoint controller
 *
 * A PCI endpoint device can have one or more functions. In the case of PCIe,
 * the specification allows up to 8 PCIe endpoint functions. Invoke
 * pcie_epc_add_epf() to add a PCI endpoint function to an endpoint
 * controller.
 *
 * The endpoint function is a caller provided object with its ops, header
 * and interrupt counts filled in. It is assigned the first free function
 * number of the EPC device and its bind operation is invoked. All functions
 * must be added before the link is started with pcie_epc_start().
 *
 * @param dev The EPC device to which the endpoint function should be added
 * @param epf The endpoint function to be added
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument or the EPF device has no bind operation
 * @retval -EBUSY The EPF device is already bound to an EPC device
 * @retval -EBUSY The link of the EPC device is already started
 * @retval -ENOENT All function numbers of the EPC device are in use
 * @retval -errno Other negative errno returned by the bind operation
 */
int pcie_epc_add_epf(const struct device *dev,
		     struct pcie_epf_device_s *epf);

/**
 * @brief Remove PCI endpoint function from endpoint controller
 *
 * Invoke to remove PCI endpoint function from the endpoint controller. The
 * unbind operation of the endpoint function is invoked. The link should be
 * stopped with pcie_epc_stop() beforehand. Removal is rejected while the
 * controller is started, even if the physical link is down.
 *
 * The caller must serialize add, remove, start and stop operations for the
 * controller. Call from thread context, outside EPF callbacks and workers
 * that unbind waits for. The EPF object must remain valid until this returns.
 * The unbind callback must stop all function work before releasing resources.
 *
 * @param dev The EPC device from which the endpoint function should be
 *            removed
 * @param epf The endpoint function to be removed
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid device or function binding
 * @retval -EBUSY The controller is started
 */
int pcie_epc_remove_epf(const struct device *dev,
			struct pcie_epf_device_s *epf);

/**
 * @brief Notify the EPF devices that the EPC device has established a
 *        connection with the Root Complex
 *
 * Invokes the link_up callback of every EPF device, in the context of the
 * caller with the EPF list spinlock held. Can be called from an ISR.
 *
 * @param dev The EPC device which has established link with the host
 */
void pcie_epc_linkup(const struct device *dev);

/**
 * @brief Notify the EPF devices that the EPC device has dropped the
 *        connection with the Root Complex
 *
 * Invokes the link_down callback of every EPF device, in the same context
 * and under the same rules as pcie_epc_linkup(). Can be called from an ISR.
 *
 * @param dev The EPC device which has dropped the link with the host
 */
void pcie_epc_linkdown(const struct device *dev);

/**
 * @brief Notify the EPF devices that the EPC device's core initialization
 *        is completed
 *
 * Invokes the core_init callback of every EPF device, in the same context
 * and under the same rules as pcie_epc_linkup(). Can be called from an ISR.
 *
 * @param dev The EPC device whose core initialization is completed
 */
void pcie_epc_init_notify(const struct device *dev);

/**
 * @brief Notify the EPF devices that the EPC device has received the BME
 *        event from the Root complex
 *
 * Invoke to notify the EPF devices that the EPC device has received the Bus
 * Master Enable (BME) event from the Root complex, in the same context and
 * under the same rules as pcie_epc_linkup(). Can be called from an ISR.
 *
 * @param dev The EPC device that received the BME event
 */
void pcie_epc_bme_notify(const struct device *dev);

/**
 * @brief Append a capability to the capability list of a function
 *
 * The EPC device places the capability and links it into the list, after
 * the capabilities it manages itself, such as MSI and MSI-X. The
 * capabilities added to a function are removed when its EPF device is
 * removed. Not every EPC device can describe capabilities, see
 * @ref pcie_epc_features_s::capability_writable.
 *
 * @param dev    The EPC device
 * @param funcno The function the capability belongs to
 * @param id     The capability ID, for example PCI_CAP_ID_VNDR
 * @param body   The capability without its first two bytes, the ID and the
 *               offset of the next capability, which the EPC device fills
 * @param len    The length of body in bytes
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 * @retval -EBUSY The link has been started
 * @retval -ENOSPC The capability does not fit in the configuration space
 * @retval -ENOTSUP The EPC device cannot add capabilities
 */
int pcie_epc_add_capability(const struct device *dev, uint8_t funcno,
			    uint8_t id, const void *body, size_t len);

/**
 * @brief Notify an EPF device that the host rang one of its doorbells
 *
 * Invokes the doorbell callback of the EPF device bound to the function
 * in the context of the caller, with the EPF list spinlock held. Can be
 * called from an ISR. The callback must not block or call EPC APIs; defer
 * processing to a thread. Removal waits for this callback to return.
 *
 * A doorbell is an interrupt the EPC device raises when the host writes a
 * register of a BAR. Where that register lies, and what the write means, is
 * an agreement between the endpoint function and its host driver that the
 * EPC device knows nothing about. The integration, a devicetree or the
 * hardware design, wires a register to a doorbell number. Likewise, the
 * mapping of interrupts to functions and doorbell numbers is specific to the
 * EPC driver. Doorbells can coalesce: after one, the function must look at
 * everything the host may have written.
 *
 * @param dev    The EPC device that received the doorbell
 * @param funcno The function the doorbell belongs to
 * @param index  The doorbell number
 */
void pcie_epc_doorbell_notify(const struct device *dev, uint8_t funcno,
			      uint8_t index);

/**
 * @brief Notify the EPF device of a host access to a software-serviced BAR
 *
 * Invokes the bar_access callback of the EPF device bound to the function,
 * in the same context and under the same rules as
 * pcie_epc_doorbell_notify(). Can be called from an ISR. Notifications can
 * coalesce: after one, the function retrieves accesses with
 * pcie_epc_bar_access_get() until none is left.
 *
 * @param dev    The EPC device that received the access
 * @param funcno The function the BAR belongs to
 */
void pcie_epc_bar_access_notify(const struct device *dev, uint8_t funcno);

/**
 * @brief Initialize the EPC control structure of an EPC device
 *
 * Invoke from the init function of the EPC driver to initialize the control
 * structure placed at the beginning of the device data.
 *
 * @param dev     The EPC device
 * @param dmaheap Heap used for EPF BAR buffer allocation, may be NULL
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid argument
 */
int pcie_epc_init(const struct device *dev, struct k_heap *dmaheap);

/**
 * @brief Initialize the address spaces of the endpoint controller
 *
 * Invoke to initialize the address spaces used by the endpoint functions to
 * allocate mapped PCI addresses. The @p mem entries are provided by the EPC
 * driver with the window description and the backing bitmap filled in.
 *
 * @param dev         The EPC device
 * @param mem         Array of address spaces, statically allocated by the
 *                    EPC driver
 * @param num_windows Number of address spaces the device supports
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid window geometry or insufficient bitmap capacity
 */
int pcie_epc_mem_multi_init(const struct device *dev,
			    struct pcie_epc_mem_s *mem,
			    unsigned int num_windows);

/**
 * @brief Initialize a single address space of the endpoint controller
 *
 * @param dev       The EPC device
 * @param mem       Address space state, statically allocated by the EPC
 *                  driver with the backing bitmap filled in
 * @param virt      The virtual base address of the PCI address window
 * @param phys      The physical base address of the PCI address window
 * @param size      The PCI address window size
 * @param page_size Size of each window page
 *
 * @retval 0 Success
 * @retval -EINVAL Invalid window geometry or insufficient bitmap capacity
 */
int pcie_epc_mem_init(const struct device *dev, struct pcie_epc_mem_s *mem,
		      void *virt, uintptr_t phys, size_t size,
		      size_t page_size);

/**
 * @brief Cleanup the address spaces of the endpoint controller
 *
 * @param dev The EPC device whose address spaces are released
 */
void pcie_epc_mem_exit(const struct device *dev);

/**
 * @brief Allocate memory address from EPC addr space
 *
 * Invoke to allocate a memory address from the EPC address space. This is
 * usually done to map the remote RC address into the local system.
 *
 * @param dev  The EPC device on which memory has to be allocated
 * @param phys The physical address of the allocated space
 * @param size The size of the address space that has to be allocated
 *
 * @return The virtual address of the allocated space, NULL on failure
 */
void *pcie_epc_mem_alloc_addr(const struct device *dev, uintptr_t *phys,
			      size_t size);

/**
 * @brief Free the allocated memory address
 *
 * Invoke to free the memory allocated using pcie_epc_mem_alloc_addr().
 *
 * @param dev       The EPC device on which memory was allocated
 * @param phys_addr The allocated physical address
 * @param size      The size of the allocated address space
 */
void pcie_epc_mem_free_addr(const struct device *dev, uintptr_t phys_addr,
			    size_t size);

/**
 * @brief Allocate memory from the EPC DMA heap
 *
 * @param dev  The EPC device whose DMA heap is used
 * @param size The size of the allocation
 *
 * @return Pointer to the allocated memory, NULL on failure
 */
void *pcie_epc_dma_alloc(const struct device *dev, size_t size);

/**
 * @brief Allocate aligned memory from the EPC DMA heap
 *
 * @param dev       The EPC device whose DMA heap is used
 * @param alignment Alignment size
 * @param size      The size of the allocation
 *
 * @return Pointer to the allocated memory, NULL on failure
 */
void *pcie_epc_dma_memalign(const struct device *dev, size_t alignment,
			    size_t size);

/**
 * @brief Free memory allocated from the EPC DMA heap
 *
 * @param dev The EPC device whose DMA heap is used
 * @param mem The memory to be freed
 */
void pcie_epc_dma_free(const struct device *dev, void *mem);

/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_PCIE_ENDPOINT_PCIE_EPC_H_ */
