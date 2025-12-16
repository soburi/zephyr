#define DT_DRV_COMPAT raspberrypi_rp1_pinctrl

#include <zephyr/drivers/pinctrl.h>

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(pinctrl_rp1, CONFIG_GPIO_LOG_LEVEL);

#define RP1_GPIO_CTRL_OFFSET(pin) (pin * 8 + 4)
#define RP1_PADS_CTRL_OFFSET(pin) (pin * 4)

#define GPIO_CTRL(base, n) ((uintptr_t)base + RP1_GPIO_CTRL_OFFSET(n))
#define PADS_CTRL(base, n) (((uintptr_t)base) + RP1_PADS_CTRL_OFFSET(n))

#define GPIO_CTRL_FUNCSEL_SHIFT               0
#define GPIO_CTRL_FUNCSEL_MASK                (BIT_MASK(5) << GPIO_CTRL_FUNCSEL_SHIFT)
#define GPIO_CTRL_F_M_SHIFT                   5
#define GPIO_CTRL_F_M_MASK                    (BIT_MASK(7) << GPIO_CTRL_F_M_SHIFT)
#define GPIO_CTRL_OUTOVER_SHIFT               12
#define GPIO_CTRL_OUTOVER_MASK                (BIT_MASK(2) << GPIO_CTRL_OUTOVER_SHIFT)
#define GPIO_CTRL_OEOVER_SHIFT                14
#define GPIO_CTRL_OEOVER_MASK                 (BIT_MASK(2) << GPIO_CTRL_OEOVER_SHIFT)
#define GPIO_CTRL_INOVER_SHIFT                16
#define GPIO_CTRL_INOVER_MASK                 (BIT_MASK(2) << GPIO_CTRL_INOVER_SHIFT)
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

#define PADS_SLEWFAST         0x1U
#define PADS_SCHMITT_ENABLE   0x2U
#define PADS_PULL_DOWN_ENABLE 0x4U
#define PADS_PULL_UP_ENABLE   0x8U
#define PADS_DRIVE_MASK       0x30U
#define PADS_DRIVE_SHIFT      4
#define PADS_INPUT_ENABLE     0x40U
#define PADS_OUTPUT_DISABLE   0x80U

#define DEV_CFG(dev)  ((const struct pinctrl_rp1_config *)(dev)->config)
#define DEV_DATA(dev) ((struct pinctrl_rp1_data *)(dev)->data)

struct pinctrl_rp1_config {
	DEVICE_MMIO_NAMED_ROM(gpio);
	DEVICE_MMIO_NAMED_ROM(pads);
};

struct pinctrl_rp1_data {
	DEVICE_MMIO_NAMED_RAM(gpio);
	DEVICE_MMIO_NAMED_RAM(pads);
};

int pinctrl_rp1_configure_pin(const struct rp1_pinctrl_soc_pin *pin)
{
	const struct device *dev = DEVICE_DT_GET(DT_DRV_INST(0));
	const uintptr_t ctrl_addr = GPIO_CTRL(DEVICE_MMIO_NAMED_RAM_PTR(dev, gpio), pin->pin_num);
	const uintptr_t pads_addr = PADS_CTRL(DEVICE_MMIO_NAMED_RAM_PTR(dev, pads), pin->pin_num);
	uint32_t reg_val;

	reg_val = sys_read32(pads_addr);
	reg_val &=
		~(PADS_SLEWFAST | PADS_SCHMITT_ENABLE | PADS_PULL_DOWN_ENABLE |
		  PADS_PULL_UP_ENABLE | PADS_DRIVE_MASK | PADS_INPUT_ENABLE | PADS_OUTPUT_DISABLE);

	reg_val |= pin->slew_rate ? PADS_SLEWFAST : 0;
	reg_val |= pin->schmitt_enable ? PADS_SCHMITT_ENABLE : 0;
	reg_val |= pin->pulldown ? PADS_PULL_DOWN_ENABLE : 0;
	reg_val |= pin->pullup ? PADS_PULL_UP_ENABLE : 0;
	reg_val |= (pin->drive_strength << PADS_DRIVE_SHIFT) & PADS_DRIVE_MASK;
	reg_val |= pin->input_enable ? PADS_INPUT_ENABLE : 0;
	reg_val |= pin->output_disable ? PADS_OUTPUT_DISABLE : 0;

	LOG_DBG("set pads %x %x", GPIO_CTRL(DEVICE_MMIO_NAMED_ROM_PTR(dev, pads), pin->pin_num),
		reg_val);

	sys_write32(reg_val, pads_addr);

	/* Configure pin function and overrides */
	reg_val = sys_read32(ctrl_addr);

	reg_val &= ~(GPIO_CTRL_FUNCSEL_MASK | GPIO_CTRL_OUTOVER_MASK | GPIO_CTRL_OEOVER_MASK |
		     GPIO_CTRL_INOVER_MASK | GPIO_CTRL_IRQOVER_MASK);
	reg_val |= (pin->alt_func << GPIO_CTRL_FUNCSEL_SHIFT) & GPIO_CTRL_FUNCSEL_MASK;
	reg_val |= (pin->f_m << GPIO_CTRL_F_M_SHIFT) & GPIO_CTRL_F_M_MASK;
	reg_val |= (pin->out_override << GPIO_CTRL_OUTOVER_SHIFT) & GPIO_CTRL_OUTOVER_MASK;
	reg_val |= (pin->oe_override << GPIO_CTRL_OEOVER_SHIFT) & GPIO_CTRL_OEOVER_MASK;
	reg_val |= (pin->in_override << GPIO_CTRL_INOVER_SHIFT) & GPIO_CTRL_INOVER_MASK;
	reg_val |= (pin->irqmask_edge_low << GPIO_CTRL_IRQMASK_EDGE_LOW_SHIFT) &
		   GPIO_CTRL_IRQMASK_EDGE_LOW_MASK;
	reg_val |= (pin->irqmask_edge_high << GPIO_CTRL_IRQMASK_EDGE_HIGH_SHIFT) &
		   GPIO_CTRL_IRQMASK_EDGE_HIGH_MASK;
	reg_val |= (pin->irqmask_level_low << GPIO_CTRL_IRQMASK_LEVEL_LOW_SHIFT) &
		   GPIO_CTRL_IRQMASK_LEVEL_LOW_MASK;
	reg_val |= (pin->irqmask_level_high << GPIO_CTRL_IRQMASK_LEVEL_HIGH_SHIFT) &
		   GPIO_CTRL_IRQMASK_LEVEL_HIGH_MASK;
	reg_val |= (pin->irqmask_f_edge_low << GPIO_CTRL_IRQMASK_F_EDGE_LOW_SHIFT) &
		   GPIO_CTRL_IRQMASK_F_EDGE_LOW_MASK;
	reg_val |= (pin->irqmask_f_edge_high << GPIO_CTRL_IRQMASK_F_EDGE_HIGH_SHIFT) &
		   GPIO_CTRL_IRQMASK_F_EDGE_HIGH_MASK;
	reg_val |= (pin->irqmask_db_level_low << GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_SHIFT) &
		   GPIO_CTRL_IRQMASK_DB_LEVEL_LOW_MASK;
	reg_val |= (pin->irqmask_db_level_high << GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_SHIFT) &
		   GPIO_CTRL_IRQMASK_DB_LEVEL_HIGH_MASK;
	reg_val |= (pin->irq_override << GPIO_CTRL_IRQOVER_SHIFT) & GPIO_CTRL_IRQOVER_MASK;

	sys_write32(reg_val, ctrl_addr);

	LOG_DBG("set gpio %x %x", GPIO_CTRL(DEVICE_MMIO_NAMED_ROM_PTR(dev, gpio), pin->pin_num),
		reg_val);

	uint32_t rb = sys_read32(pads_addr);
	LOG_INF("PADS pin%u addr=%lx R=%08x", pin->pin_num, (unsigned long)pads_addr, rb);

	uint32_t rb2 = sys_read32(ctrl_addr);
	LOG_INF("GPIO_CTRL pin%u addr=%lx R=%08x", pin->pin_num, (unsigned long)ctrl_addr, rb2);

	return 0;
}

static int pinctrl_rp1_init(const struct device *dev)
{
	DEVICE_MMIO_NAMED_MAP(dev, gpio, K_MEM_CACHE_NONE);
	DEVICE_MMIO_NAMED_MAP(dev, pads, K_MEM_CACHE_NONE);

	return 0;
}

#define PINCTRL_RP1_INIT(n)                                                                        \
	static struct pinctrl_rp1_data pinctrl_rp1_data_##n;                                       \
                                                                                                   \
	static const struct pinctrl_rp1_config pinctrl_rp1_cfg_##n = {                             \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(gpio, DT_DRV_INST(n)),                          \
		DEVICE_MMIO_NAMED_ROM_INIT_BY_NAME(pads, DT_DRV_INST(n)),                          \
	};                                                                                         \
                                                                                                   \
	DEVICE_DT_INST_DEFINE(n, pinctrl_rp1_init, NULL, &pinctrl_rp1_data_##n,                    \
			      &pinctrl_rp1_cfg_##n, POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(PINCTRL_RP1_INIT)
