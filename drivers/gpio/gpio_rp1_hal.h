#ifndef GPIO_RP1_H__
#define GPIO_RP1_H__

#include <errno.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/gpio/gpio_utils.h>
#include <zephyr/irq.h>

#define RP1_ATOMIC_RAW_OFF 0x0000
#define RP1_ATOMIC_XOR_OFF 0x1000
#define RP1_ATOMIC_SET_OFF 0x2000
#define RP1_ATOMIC_CLR_OFF 0x3000

#define GPIO_STATUS(port, n) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x8 * n)
#define GPIO_CTRL(port, n)   (GPIO_STATUS(port, n) + 0x4)
#define PADS_CTRL(port, n)   (DEVICE_MMIO_NAMED_GET(port, pads) + 0x4 * (n))

#define GPIO_INTR(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x100)
#define GPIO_INTE(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x104)
#define GPIO_INTF(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x108)
#define GPIO_INTS(port) (DEVICE_MMIO_NAMED_GET(port, gpio) + 0x10c)
#define RIO_OUT(port)   (DEVICE_MMIO_NAMED_GET(port, rio) + 0x0)
#define RIO_OE(port)    (DEVICE_MMIO_NAMED_GET(port, rio) + 0x4)
#define RIO_IN(port)    (DEVICE_MMIO_NAMED_GET(port, rio) + 0x8)

#define GPIO_CTRL_RAW(port, n, val) sys_write32(val, GPIO_CTRL(port, n) + RP1_ATOMIC_RAW_OFF)
#define GPIO_CTRL_XOR(port, n, val) sys_write32(val, GPIO_CTRL(port, n) + RP1_ATOMIC_XOR_OFF)
#define GPIO_CTRL_SET(port, n, val) sys_write32(val, GPIO_CTRL(port, n) + RP1_ATOMIC_SET_OFF)
#define GPIO_CTRL_CLR(port, n, val) sys_write32(val, GPIO_CTRL(port, n) + RP1_ATOMIC_CLR_OFF)
#define PADS_CTRL_RAW(port, n, val) sys_write32(val, PADS_CTRL(port, n) + RP1_ATOMIC_RAW_OFF)
#define PADS_CTRL_XOR(port, n, val) sys_write32(val, PADS_CTRL(port, n) + RP1_ATOMIC_XOR_OFF)
#define PADS_CTRL_SET(port, n, val) sys_write32(val, PADS_CTRL(port, n) + RP1_ATOMIC_SET_OFF)
#define PADS_CTRL_CLR(port, n, val) sys_write32(val, PADS_CTRL(port, n) + RP1_ATOMIC_CLR_OFF)

#define GPIO_INTR_RAW(port, val) sys_write32(val, GPIO_INTR(port) + RP1_ATOMIC_RAW_OFF)
#define GPIO_INTR_XOR(port, val) sys_write32(val, GPIO_INTR(port) + RP1_ATOMIC_XOR_OFF)
#define GPIO_INTR_SET(port, val) sys_write32(val, GPIO_INTR(port) + RP1_ATOMIC_SET_OFF)
#define GPIO_INTR_CLR(port, val) sys_write32(val, GPIO_INTR(port) + RP1_ATOMIC_CLR_OFF)
#define GPIO_INTE_RAW(port, val) sys_write32(val, GPIO_INTE(port) + RP1_ATOMIC_RAW_OFF)
#define GPIO_INTE_XOR(port, val) sys_write32(val, GPIO_INTE(port) + RP1_ATOMIC_XOR_OFF)
#define GPIO_INTE_SET(port, val) sys_write32(val, GPIO_INTE(port) + RP1_ATOMIC_SET_OFF)
#define GPIO_INTE_CLR(port, val) sys_write32(val, GPIO_INTE(port) + RP1_ATOMIC_CLR_OFF)
#define RIO_OUT_RAW(port, val)   sys_write32(val, RIO_OUT(port) + RP1_ATOMIC_RAW_OFF)
#define RIO_OUT_XOR(port, val)   sys_write32(val, RIO_OUT(port) + RP1_ATOMIC_XOR_OFF)
#define RIO_OUT_SET(port, val)   sys_write32(val, RIO_OUT(port) + RP1_ATOMIC_SET_OFF)
#define RIO_OUT_CLR(port, val)   sys_write32(val, RIO_OUT(port) + RP1_ATOMIC_CLR_OFF)
#define RIO_OE_RAW(port, val)    sys_write32(val, RIO_OE(port) + RP1_ATOMIC_RAW_OFF)
#define RIO_OE_XOR(port, val)    sys_write32(val, RIO_OE(port) + RP1_ATOMIC_XOR_OFF)
#define RIO_OE_SET(port, val)    sys_write32(val, RIO_OE(port) + RP1_ATOMIC_SET_OFF)
#define RIO_OE_CLR(port, val)    sys_write32(val, RIO_OE(port) + RP1_ATOMIC_CLR_OFF)

#define GPIO_CTRL_OUTOVER_MASK 0x3000
#define GPIO_CTRL_OEOVER_MASK  0xc000
#define GPIO_CTRL_FUNCSEL_MASK 0x001f
#define GPIO_CTRL_FUNCSEL_RIO  0x5

#define GPIO_FUNC_SIO 0x5
#define GPIO_IN       1
#define GPIO_OUT      0

#define PADS_OUTPUT_DISABLE 0x80
#define PADS_INPUT_ENABLE   0x40

#define PADS_PULL_UP_ENABLE   0x8
#define PADS_PULL_DOWN_ENABLE 0x4

#define GPIO_CTRL_IRQMASK_EDGE_LOW_SHIFT      20
#define GPIO_CTRL_IRQMASK_EDGE_LOW_MASK       (BIT_MASK(1) << GPIO_CTRL_IRQMASK_EDGE_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_EDGE_HIGH_SHIFT     21
#define GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK      (BIT_MASK(1) << GPIO_CTRL_IRQMASK_EDGE_HIGH_SHIFT)
#define GPIO_CTRL_IRQMASK_LEVEL_LOW_SHIFT     22
#define GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK      (BIT_MASK(1) << GPIO_CTRL_IRQMASK_LEVEL_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_LEVEL_HIGH_SHIFT    23
#define GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK     (BIT_MASK(1) << GPIO_CTRL_IRQMASK_LEVEL_HIGH_SHIFT)
#define GPIO_CTRL_IRQMASK_F_EDGE_LOW_SHIFT    24
#define GPIO_CTRL_IRQMASK_F_EDGE_LOW_MASK     (BIT_MASK(1) << GPIO_CTRL_IRQMASK_F_EDGE_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_F_EDGE_HIGH_SHIFT   25
#define GPIO_CTRL_IRQMASK_F_EDGE_HIGH_MASK    (BIT_MASK(1) << GPIO_CTRL_IRQMASK_F_EDGE_HIGH_SHIFT)
#define GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_SHIFT  26
#define GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_MASK   (BIT_MASK(1) << GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_SHIFT)
#define GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_SHIFT 27
#define GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_MASK  (BIT_MASK(1) << GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_SHIFT)
#define GPIO_CTRL_IRQRESET_SHIFT              28
#define GPIO_CTRL_IRQRESET_MASK               (BIT_MASK(1) << GPIO_CTRL_IRQRESET_SHIFT)
#define GPIO_CTRL_IRQOVER_SHIFT               30
#define GPIO_CTRL_IRQOVER_MASK                (BIT_MASK(2) << GPIO_CTRL_IRQOVER_SHIFT)

#define GPIO_PADS_SLEWFAST_SHIFT         0
#define GPIO_PADS_SLEWFAST_MASK          (BIT_MASK(1) << GPIO_PADS_SLEWFAST_SHIFT)
#define GPIO_PADS_SCHMITT_ENABLE_SHIFT   1
#define GPIO_PADS_SCHMITT_ENABLE_MASK    (BIT_MASK(1) << GPIO_PADS_SCHMITT_ENABLE_SHIFT)
#define GPIO_PADS_PULL_DOWN_ENABLE_SHIFT 2
#define GPIO_PADS_PULL_DOWN_ENABLE_MASK  (BIT_MASK(1) << GPIO_PADS_PULL_DOWN_ENABLE_SHIFT)
#define GPIO_PADS_PULL_UP_ENABLE_SHIFT   3
#define GPIO_PADS_PULL_UP_ENABLE_MASK    (BIT_MASK(1) << GPIO_PADS_PULL_UP_ENABLE_SHIFT)
#define GPIO_PADS_DRIVE_SHIFT            4
#define GPIO_PADS_DRIVE_MASK             (BIT_MASK(2) << GPIO_PADS_DRIVE_SHIFT)
#define GPIO_PADS_INPUT_ENABLE_SHIFT     6
#define GPIO_PADS_INPUT_ENABLE_MASK      (BIT_MASK(1) << GPIO_PADS_INPUT_ENABLE_SHIFT)
#define GPIO_PADS_OUTPUT_DISABLE_SHIFT   7
#define GPIO_PADS_OUTPUT_DISABLE_MASK    (BIT_MASK(1) << GPIO_PADS_OUTPUT_DISABLE_SHIFT)

#define REGNAME_SIO_RIO        rio
#define GPIO_RPI_LO_AVAILABLE  (DT_INST_FOREACH_STATUS_OKAY_VARGS(ADDR_IS_ZERO) 0)
#define GPIO_RPI_HI_AVAILABLE  0
#define NUM_BANK0_GPIOS        28
#define GPIO_RPI_PINS_PER_PORT 28
#define ALL_EVENTS                                                                                 \
	(GPIO_CTRL_IRQMASK_EDGE_LOW_MASK | GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK |                      \
	 GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK | GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK |                    \
	 GPIO_CTRL_IRQMASK_F_EDGE_LOW_MASK | GPIO_CTRL_IRQMASK_F_EDGE_HIGH_MASK |                  \
	 GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_MASK | GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_MASK)

enum {
	GPIO_IRQ_EDGE_FALL = GPIO_CTRL_IRQMASK_EDGE_LOW_MASK,
	GPIO_IRQ_EDGE_RISE = GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK,
	GPIO_IRQ_LEVEL_LOW = GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK,
	GPIO_IRQ_LEVEL_HIGH = GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK,
};

bool gpio_get_out_level(uint32_t pin);
bool gpio_get_dir(uint32_t pin);
void gpio_set_mask_n(uint32_t n, uint32_t mask);
void gpio_clr_mask_n(uint32_t n, uint32_t mask);
void gpio_xor_mask_n(uint32_t n, uint32_t mask);
void gpio_put_masked_n(uint32_t n, uint32_t mask, uint32_t value);
void gpio_put(uint32_t pin, bool value);
void gpio_set_dir(uint32_t pin, bool value);
int gpio_is_pulled_up(uint32_t pin);
int gpio_is_pulled_down(uint32_t pin);
int gpio_set_irq_enabled(uint32_t pin, uint32_t events, bool value);
void gpio_set_input_enabled(uint32_t pin, bool enabled);
void gpio_set_function(uint32_t gpio, uint32_t fn);
void gpio_set_pulls(uint32_t gpio, bool up, bool down);
void gpio_set_input_enabled_output_disabled(uint32_t pin, bool ie, bool od);

void gpio_acknowledge_irq(uint32_t gpio, uint32_t event_mask);
uint32_t gpio_get_irq_event_mask(uint32_t gpio);
bool gpio_has_pending_irq();

#endif /* GPIO_RP1_H__ */
