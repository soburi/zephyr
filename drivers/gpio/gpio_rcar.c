/*
 * Copyright (c) 2020-2023 IoT.bzh
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT renesas_rcar_gpio

#include <errno.h>

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/clock_control.h>
#include <zephyr/drivers/clock_control/renesas_cpg_mssr.h>
#include <zephyr/irq.h>

#include <zephyr/drivers/gpio/gpio_utils.h>

#define LOG_LEVEL CONFIG_GPIO_LOG_LEVEL
#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(gpio_rcar);

typedef void (*init_func_t)(const struct device *dev);

/* Required by DEVICE_MMIO_NAMED_* macros */
#define DEV_CFG(_dev) \
	((const struct gpio_rcar_cfg *)(_dev)->config)
#define DEV_DATA(_dev) ((struct gpio_rcar_data *)(_dev)->data)

struct gpio_rcar_cfg {
	struct gpio_driver_config common;
	DEVICE_MMIO_NAMED_ROM(reg_base);
	init_func_t init_func;
	const struct device *clock_dev;
	struct rcar_cpg_clk mod_clk;
};

struct gpio_rcar_data {
	struct gpio_driver_data common;
	DEVICE_MMIO_NAMED_RAM(reg_base);
	sys_slist_t cb;
};

#define IOINTSEL 0x00   /* General IO/Interrupt Switching Register */
#define INOUTSEL 0x04   /* General Input/Output Switching Register */
#define OUTDT    0x08   /* General Output Register */
#define INDT     0x0c   /* General Input Register */
#define INTDT    0x10   /* Interrupt Display Register */
#define INTCLR   0x14   /* Interrupt Clear Register */
#define INTMSK   0x18   /* Interrupt Mask Register */
#define MSKCLR   0x1c   /* Interrupt Mask Clear Register */
#define POSNEG   0x20   /* Positive/Negative Logic Select Register */
#define EDGLEVEL 0x24   /* Edge/level Select Register */
#define FILONOFF 0x28   /* Chattering Prevention On/Off Register */
#define OUTDTSEL 0x40   /* Output Data Select Register */
#define BOTHEDGE 0x4c   /* One Edge/Both Edge Select Register */
#define INEN     0x50	/* General Input Enable Register */

static inline uint32_t gpio_rcar_read(const struct device *dev, uint32_t offs)
{
	uint32_t value =
	sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + offs);

	printk("gpio_rcar_read  %lx %lx: %d\n", DEV_CFG(dev)->reg_base.phys_addr, DEVICE_MMIO_NAMED_GET(dev, reg_base) + offs, value);
	return value;
}

static int wcount;


int arch_page_phys_get(void *virt, uintptr_t *phys);

static inline void gpio_rcar_write(const struct device *dev, uint32_t offs, uint32_t value)
{
    volatile uintptr_t base = DEVICE_MMIO_NAMED_GET(dev, reg_base);
    uintptr_t page = base & (~0xFFFUL);

    uintptr_t pa;

    int i;

    int rc = arch_page_phys_get((void *)base, &pa);
    printk("arch_page_phys_get(va=%p) -> rc=%d, pa=%p\n", (void *)base, rc, (void *)pa);
    if (wcount == 0) {
    for (i=0; i<0x1000; i+= 0x100) {
	    char msg[16];
	    sprintf(msg, "0x%04x", i);
	    LOG_HEXDUMP_ERR((const unsigned int*)(page+i), 0x100, msg);
    }
    }

    if ((wcount % 20) == 0 && wcount != 0) {
    printk("base %p:%p\n", (void*)base, (void*)page);
    //LOG_HEXDUMP_ERR((const unsigned int*)page, 0x100, "0x000");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x100), 0x100, "0x100");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x200), 0x100, "0x200");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x300), 0x100, "0x300");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x400), 0x100, "0x400");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x500), 0x100, "0x500");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x600), 0x100, "0x600");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x700), 0x100, "0x700");
    LOG_HEXDUMP_ERR((const unsigned int*)(page+0x800), 0x100, "0x800");
    LOG_HEXDUMP_ERR((const unsigned int*)(page+0x900), 0x100, "0x900");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xa00), 0x100, "0xa00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xb00), 0x100, "0xb00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xc00), 0x100, "0xc00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xd00), 0x100, "0xd00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xe00), 0x100, "0xe00");
    }

	printk("gpio_rcar_write %lx: %d\n", DEVICE_MMIO_NAMED_GET(dev, reg_base) + offs, value);
	sys_write32(value, DEVICE_MMIO_NAMED_GET(dev, reg_base) + offs);
	value =
	sys_read32(DEVICE_MMIO_NAMED_GET(dev, reg_base) + offs);
	printk("readback: %lx %lx: %d\n", DEV_CFG(dev)->reg_base.phys_addr, DEVICE_MMIO_NAMED_GET(dev, reg_base) + offs, value);

    if ((wcount % 20) == 0) {
    printk("BASE %p:%p\n", (void*)base, (void*)page);
    //LOG_HEXDUMP_ERR((const unsigned int*)page, 0x1000, "0x000");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x100), 0x100, "0x100");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x200), 0x100, "0x200");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x300), 0x100, "0x300");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x400), 0x100, "0x400");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x500), 0x100, "0x500");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x600), 0x100, "0x600");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0x700), 0x100, "0x700");
    LOG_HEXDUMP_ERR((const unsigned int*)(page+0x800), 0x100, "0x800");
    LOG_HEXDUMP_ERR((const unsigned int*)(page+0x900), 0x100, "0x900");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xa00), 0x100, "0xa00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xb00), 0x100, "0xb00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xc00), 0x100, "0xc00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xd00), 0x100, "0xd00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xe00), 0x100, "0xe00");
    //LOG_HEXDUMP_ERR((const unsigned int*)(page+0xf00), 0x100, "0xf00");
    }

    if (wcount == 0) {
    for (i=0; i<0x1000; i+= 0x100) {
	    for (int j=0; j<0x100; j++) {
		sys_write8(0xFF, page + i + j);
	    }
	    char msg[16];
	    sprintf(msg, "0x%04x", i);
	    LOG_HEXDUMP_ERR((const unsigned int*)(page+i), 0x100, msg);
    }
    }
    wcount++;
}

static void gpio_rcar_modify_bit(const struct device *dev,
				 uint32_t offs, int bit, bool value)
{
	uint32_t tmp = gpio_rcar_read(dev, offs);

	if (value) {
		tmp |= BIT(bit);
	} else {
		tmp &= ~BIT(bit);
	}

	gpio_rcar_write(dev, offs, tmp);
}

static void gpio_rcar_port_isr(const struct device *dev)
{
	struct gpio_rcar_data *data = dev->data;
	uint32_t pending, fsb, mask;

	pending = gpio_rcar_read(dev, INTDT);
	mask = gpio_rcar_read(dev, INTMSK);

	while ((pending = gpio_rcar_read(dev, INTDT) &
			  gpio_rcar_read(dev, INTMSK))) {
		fsb = find_lsb_set(pending) - 1;
		gpio_fire_callbacks(&data->cb, dev, BIT(fsb));
		gpio_rcar_write(dev, INTCLR, BIT(fsb));
	}
}

static void gpio_rcar_config_general_input_output_mode(
	const struct device *dev,
	uint32_t gpio,
	bool output)
{
	/* follow steps in the GPIO documentation for
	 * "Setting General Output Mode" and
	 * "Setting General Input Mode"
	 */

	/* Configure positive logic in POSNEG */
	gpio_rcar_modify_bit(dev, POSNEG, gpio, false);

	/* Select "Input Enable/Disable" in INEN for Gen4 SoCs */
#ifdef CONFIG_SOC_SERIES_RCAR_GEN4
	gpio_rcar_modify_bit(dev, INEN, gpio, !output);
#endif

	/* Select "General Input/Output Mode" in IOINTSEL */
	gpio_rcar_modify_bit(dev, IOINTSEL, gpio, false);

	/* Select Input Mode or Output Mode in INOUTSEL */
	printk("INOUTSEL - %d\n", output);
	gpio_rcar_modify_bit(dev, INOUTSEL, gpio, output);

	/* Select General Output Register to output data in OUTDTSEL */
	if (output) {
		gpio_rcar_modify_bit(dev, OUTDTSEL, gpio, false);
	}
}

static int gpio_rcar_configure(const struct device *dev,
			       gpio_pin_t pin, gpio_flags_t flags)
{
	printk("gpio_rcar_configure %lx: %d %x\n", DEV_CFG(dev)->reg_base.phys_addr, pin, flags);
	if ((flags & GPIO_OUTPUT) && (flags & GPIO_INPUT)) {
		/* Pin cannot be configured as input and output */
		return -ENOTSUP;
	} else if (!(flags & (GPIO_INPUT | GPIO_OUTPUT))) {
		/* Pin has to be configured as input or output */
		return -ENOTSUP;
	}

	if (flags & GPIO_OUTPUT) {
		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			printk("OUTDT - true\n");
			gpio_rcar_modify_bit(dev, OUTDT, pin, true);
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			printk("OUTDT - false\n");
			gpio_rcar_modify_bit(dev, OUTDT, pin, false);
		}
		gpio_rcar_config_general_input_output_mode(dev, pin, true);
	} else {
		gpio_rcar_config_general_input_output_mode(dev, pin, false);
	}

	return 0;
}

static int gpio_rcar_port_get_raw(const struct device *dev,
				  gpio_port_value_t *value)
{
	*value = gpio_rcar_read(dev, INDT);
	return 0;
}

static int gpio_rcar_port_set_masked_raw(const struct device *dev,
					 gpio_port_pins_t mask,
					 gpio_port_value_t value)
{
	uint32_t port_val;

	port_val = gpio_rcar_read(dev, OUTDT);
	port_val = (port_val & ~mask) | (value & mask);
	gpio_rcar_write(dev, OUTDT, port_val);

	return 0;
}

static int gpio_rcar_port_set_bits_raw(const struct device *dev,
				       gpio_port_pins_t pins)
{
	uint32_t port_val;

	port_val = gpio_rcar_read(dev, OUTDT);
	port_val |= pins;
	gpio_rcar_write(dev, OUTDT, port_val);

	return 0;
}

static int gpio_rcar_port_clear_bits_raw(const struct device *dev,
					 gpio_port_pins_t pins)
{
	uint32_t port_val;

	port_val = gpio_rcar_read(dev, OUTDT);
	port_val &= ~pins;
	gpio_rcar_write(dev, OUTDT, port_val);

	return 0;
}

static int gpio_rcar_port_toggle_bits(const struct device *dev,
				      gpio_port_pins_t pins)
{
	uint32_t port_val;

	port_val = gpio_rcar_read(dev, OUTDT);
	port_val ^= pins;
	gpio_rcar_write(dev, OUTDT, port_val);

	return 0;
}

static int gpio_rcar_pin_interrupt_configure(const struct device *dev,
					     gpio_pin_t pin,
					     enum gpio_int_mode mode,
					     enum gpio_int_trig trig)
{
	if (mode == GPIO_INT_MODE_DISABLED) {
		return -ENOTSUP;
	}

	/* Configure positive or negative logic in POSNEG */
	gpio_rcar_modify_bit(dev, POSNEG, pin,
			     (trig == GPIO_INT_TRIG_LOW));

	/* Configure edge or level trigger in EDGLEVEL */
	if (mode == GPIO_INT_MODE_EDGE) {
		gpio_rcar_modify_bit(dev, EDGLEVEL, pin, true);
	} else {
		gpio_rcar_modify_bit(dev, EDGLEVEL, pin, false);
	}

	if (trig == GPIO_INT_TRIG_BOTH) {
		gpio_rcar_modify_bit(dev, BOTHEDGE, pin, true);
	}

	/* Select "Input Enable" in INEN for Gen4 SoCs */
#ifdef CONFIG_SOC_SERIES_RCAR_GEN4
	gpio_rcar_modify_bit(dev, INEN, pin, true);
#endif

	gpio_rcar_modify_bit(dev, IOINTSEL, pin, true);

	if (mode == GPIO_INT_MODE_EDGE) {
		/* Write INTCLR in case of edge trigger */
		gpio_rcar_write(dev, INTCLR, BIT(pin));
	}

	gpio_rcar_write(dev, MSKCLR, BIT(pin));

	return 0;
}

static int gpio_rcar_init(const struct device *dev)
{
	const struct gpio_rcar_cfg *config = dev->config;
	int ret;

	if (!device_is_ready(config->clock_dev)) {
		return -ENODEV;
	}

	ret = clock_control_on(config->clock_dev,
			       (clock_control_subsys_t) &config->mod_clk);

	if (ret < 0) {
		return ret;
	}

	DEVICE_MMIO_NAMED_MAP(dev, reg_base, K_MEM_CACHE_NONE);
	config->init_func(dev);
	return 0;
}

static int gpio_rcar_manage_callback(const struct device *dev,
				     struct gpio_callback *callback,
				     bool set)
{
	struct gpio_rcar_data *data = dev->data;

	return gpio_manage_callback(&data->cb, callback, set);
}

static DEVICE_API(gpio, gpio_rcar_driver_api) = {
	.pin_configure = gpio_rcar_configure,
	.port_get_raw = gpio_rcar_port_get_raw,
	.port_set_masked_raw = gpio_rcar_port_set_masked_raw,
	.port_set_bits_raw = gpio_rcar_port_set_bits_raw,
	.port_clear_bits_raw = gpio_rcar_port_clear_bits_raw,
	.port_toggle_bits = gpio_rcar_port_toggle_bits,
	.pin_interrupt_configure = gpio_rcar_pin_interrupt_configure,
	.manage_callback = gpio_rcar_manage_callback,
};

/* Device Instantiation */
#define GPIO_RCAR_INIT(n)					      \
	static void gpio_rcar_##n##_init(const struct device *dev);   \
	static const struct gpio_rcar_cfg gpio_rcar_cfg_##n = {	      \
		DEVICE_MMIO_NAMED_ROM_INIT(reg_base, DT_DRV_INST(n)), \
		.common = {					      \
			.port_pin_mask =			      \
				GPIO_PORT_PIN_MASK_FROM_DT_INST(n),   \
		},						      \
		.init_func = gpio_rcar_##n##_init,		      \
		.clock_dev = DEVICE_DT_GET(DT_INST_CLOCKS_CTLR(n)),   \
		.mod_clk.module =				      \
			DT_INST_CLOCKS_CELL_BY_IDX(n, 0, module),     \
		.mod_clk.domain =				      \
			DT_INST_CLOCKS_CELL_BY_IDX(n, 0, domain),     \
	};							      \
	static struct gpio_rcar_data gpio_rcar_data_##n;	      \
								      \
	DEVICE_DT_INST_DEFINE(n,				      \
			      gpio_rcar_init,			      \
			      NULL,				      \
			      &gpio_rcar_data_##n,		      \
			      &gpio_rcar_cfg_##n,		      \
			      PRE_KERNEL_1,			      \
			      CONFIG_GPIO_INIT_PRIORITY,	      \
			      &gpio_rcar_driver_api		      \
			      );				      \
	static void gpio_rcar_##n##_init(const struct device *dev)    \
	{							      \
		IRQ_CONNECT(DT_INST_IRQN(n),			      \
			    0,					      \
			    gpio_rcar_port_isr,			      \
			    DEVICE_DT_INST_GET(n), 0);		      \
								      \
		irq_enable(DT_INST_IRQN(n));			      \
	}

DT_INST_FOREACH_STATUS_OKAY(GPIO_RCAR_INIT)
